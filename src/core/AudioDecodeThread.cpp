#include "AudioDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <QDebug>


static AVFrame *convertFrameToS16(AVFrame *frame)
{
    const int nb_samples = frame->nb_samples;
    const int channels = frame->ch_layout.nb_channels > 0
                             ? frame->ch_layout.nb_channels : 2;

    // ── S16 交错（回退 / 非倍速路径）──
    if (frame->format == AV_SAMPLE_FMT_S16) {
        int16_t *s = (int16_t*)frame->data[0];
        const int total = nb_samples * channels;

        float peak = 0.0f;
        for (int i = 0; i < total; i++) {
            float v = qAbs((float)s[i]);
            if (v > peak) peak = v;
        }

        // 仅在接近削波时缩小；0.99 余量
        const float scale = (peak > 32767.0f * 0.99f)
                                ? (32767.0f * 0.99f / peak) : 1.0f;
        if (scale < 1.0f)
            for (int i = 0; i < total; i++)
                s[i] = (int16_t)(s[i] * scale);

        return frame;
    }

    // ── FLTP 平面格式——找全帧峰值，然后转换 + 缩放──
    if (frame->format == AV_SAMPLE_FMT_FLTP) {
        float *src[8];
        for (int c = 0; c < channels && c < 8; c++)
            src[c] = (float*)frame->data[c];

        float peak = 0.0f;
        for (int i = 0; i < nb_samples; i++)
            for (int c = 0; c < channels; c++) {
                float v = qAbs(src[c][i]);
                if (v > peak) peak = v;
            }

        // 仅在 > 1.0 时缩小（atempo OLA 过冲）；保持 0.99 余量
        const float scale = (peak > 1.0f) ? (0.99f / peak) : 1.0f;

        // 3. 分配 S16 帧并转换
        AVFrame *out = av_frame_alloc();
        if (!out) { av_frame_free(&frame); return nullptr; }

        out->format      = AV_SAMPLE_FMT_S16;
        out->sample_rate = frame->sample_rate;
        out->nb_samples  = nb_samples;
        out->pts         = frame->pts;
        out->time_base   = frame->time_base;
        av_channel_layout_copy(&out->ch_layout, &frame->ch_layout);

        if (av_frame_get_buffer(out, 0) < 0) {
            av_frame_free(&out);
            av_frame_free(&frame);
            return nullptr;
        }

        int16_t *dst = (int16_t*)out->data[0];
        for (int i = 0; i < nb_samples; i++)
            for (int c = 0; c < channels; c++)
                *dst++ = (int16_t)(src[c][i] * scale * 32767.0f);

        av_frame_free(&frame);
        return out;
    }

    qWarning("AudioDecodeThread: unexpected frame format %d", frame->format);
    return frame;
}

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

AudioDecodeThread::AudioDecodeThread(QObject *parent)
    : QThread(parent)
    , m_codecCtx(nullptr)
    , m_packetQueue(nullptr)
    , m_frameQueue(nullptr)
    , m_swrCtx(nullptr)
    , m_quit(false)
    , m_paused(nullptr)
    , m_timeBase{1, 44100}
    , m_filterGraph(nullptr)
    , m_abufferCtx(nullptr)
    , m_aresampleInCtx(nullptr)
    , m_atempoCtx(nullptr)
    , m_aresampleOutCtx(nullptr)
    , m_abuffersinkCtx(nullptr)
    , m_speedDirty(false)
    , m_pendingSpeed(1.0)
{
}

AudioDecodeThread::~AudioDecodeThread()
{
    stopDecode();
    wait();
    destroyFilterGraph();
    if (m_swrCtx)
        swr_free(&m_swrCtx);
}

void AudioDecodeThread::setCodecContext(AVCodecContext *ctx)
{
    m_codecCtx = ctx;
}

void AudioDecodeThread::setPacketQueue(PacketQueue *queue)
{
    m_packetQueue = queue;
}

void AudioDecodeThread::setFrameQueue(FrameQueue *queue)
{
    m_frameQueue = queue;
}

void AudioDecodeThread::setTimeBase(AVRational tb)
{
    m_timeBase = tb;
}

void AudioDecodeThread::stopDecode()
{
    m_quit = true;
    if (m_packetQueue) {
        m_packetQueue->flush();
        m_packetQueue->setFinished(true);
    }
    if (m_frameQueue) {
        m_frameQueue->flush();
        m_frameQueue->setFinished(true);
    }
}

void AudioDecodeThread::setPausedRef(const std::atomic<bool> &paused)
{
    m_paused = &paused;
}

void AudioDecodeThread::setOutputSampleRate(int rate)
{
    m_outputSampleRate.store(rate, std::memory_order_release);
}

// ── 重采样（解码格式 → S16 44100Hz 立体声）──

bool AudioDecodeThread::initSwrContext()
{
    if (!m_codecCtx)
        return false;

    m_swrCtx = swr_alloc();
    if (!m_swrCtx)
        return false;

    AVChannelLayout inChLayout = m_codecCtx->ch_layout;
    av_opt_set_chlayout(m_swrCtx, "in_chlayout", &inChLayout, 0);
    av_opt_set_int(m_swrCtx, "in_sample_rate", m_codecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_codecCtx->sample_fmt, 0);

    AVChannelLayout outChLayout = AV_CHANNEL_LAYOUT_STEREO;
    av_opt_set_chlayout(m_swrCtx, "out_chlayout", &outChLayout, 0);
    int rate = m_outputSampleRate.load(std::memory_order_acquire);
    av_opt_set_int(m_swrCtx, "out_sample_rate", rate, 0);
    av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    return swr_init(m_swrCtx) >= 0;
}

AVFrame *AudioDecodeThread::resampleFrame(AVFrame *frame)
{
    if (!m_swrCtx)
        return nullptr;

    int outSamples = swr_get_out_samples(m_swrCtx, frame->nb_samples);
    if (outSamples <= 0)
        return nullptr;

    outSamples += 256;

    AVFrame *outFrame = av_frame_alloc();
    if (!outFrame)
        return nullptr;

    outFrame->format = AV_SAMPLE_FMT_S16;
    AVChannelLayout outChLayout = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&outFrame->ch_layout, &outChLayout);
    outFrame->sample_rate = m_outputSampleRate.load(std::memory_order_acquire);
    outFrame->nb_samples = outSamples;

    if (av_frame_get_buffer(outFrame, 0) < 0) {
        av_frame_free(&outFrame);
        return nullptr;
    }

    int converted = swr_convert(m_swrCtx,
                                outFrame->data, outSamples,
                                (const uint8_t **)frame->data, frame->nb_samples);

    if (converted <= 0 || converted > outSamples) {
        qWarning() << "AudioDecodeThread: swr_convert returned" << converted
                   << "outSamples was" << outSamples
                   << "in_samples was" << frame->nb_samples;
        av_frame_free(&outFrame);
        return nullptr;
    }

    outFrame->nb_samples = converted;
    outFrame->linesize[0] = av_samples_get_buffer_size(nullptr, outFrame->ch_layout.nb_channels,
                                                        converted, AV_SAMPLE_FMT_S16, 1);

    if (frame->pts != AV_NOPTS_VALUE) {
        int rate = m_outputSampleRate.load(std::memory_order_acquire);
        outFrame->pts = av_rescale_q(frame->pts, m_timeBase,
                                     (AVRational){1, rate});
    } else {
        outFrame->pts = AV_NOPTS_VALUE;
    }
    int rate = m_outputSampleRate.load(std::memory_order_acquire);
    outFrame->time_base = (AVRational){1, rate};

    return outFrame;
}

// ── 滤镜图：abuffer(S16) → aresample(→FLTP) → atempo → aresample(→FLTP) → abuffersink → 峰值归一(→S16) ──

void AudioDecodeThread::destroyFilterGraph()
{
    if (m_filterGraph) {
        avfilter_graph_free(&m_filterGraph);
        m_abufferCtx = nullptr;
        m_aresampleInCtx = nullptr;
        m_atempoCtx = nullptr;
        m_aresampleOutCtx = nullptr;
        m_abuffersinkCtx = nullptr;
    }
}

// ── 速度控制 ──

void AudioDecodeThread::drainFilterGraph()
{
    if (!m_filterGraph)
        return;
    (void)av_buffersrc_add_frame_flags(m_abufferCtx, nullptr, 0);
    while (!m_quit) {
        AVFrame *filtered = av_frame_alloc();
        int r = av_buffersink_get_frame(m_abuffersinkCtx, filtered);
        if (r < 0) { av_frame_free(&filtered); break; }
        filtered = convertFrameToS16(filtered);
        if (filtered) {
            int rate = m_outputSampleRate.load(std::memory_order_acquire);
            filtered->time_base = (AVRational){1, rate};
            m_frameQueue->push(filtered);
            av_frame_free(&filtered);
        }
    }
}

void AudioDecodeThread::setSpeed(double speed)
{
    speed = qBound(0.5, speed, 2.0);
    {
        std::lock_guard lock(m_speedMutex);
        m_pendingSpeed = speed;
    }
    m_speedDirty.store(true, std::memory_order_release);
}

void AudioDecodeThread::applySpeed()
{
    double speed;
    {
        std::lock_guard lock(m_speedMutex);
        speed = m_pendingSpeed;
    }
    m_speedDirty.store(false, std::memory_order_relaxed);

    double current = m_currentSpeed.load(std::memory_order_acquire);
    if (qFuzzyCompare(current, speed))
        return;

    // 在拆除旧滤镜图之前先构建新滤镜图。
    // 如果构建失败则继续以旧速度运行，而不是
    // 静默回退到 1.0× 直通而管线其他部分
    // 以为在 2×（→ 永久音画偏移）。
    bool ok = qFuzzyCompare(speed, 1.0);
    AVFilterGraph *tmpGraph = nullptr;
    AVFilterContext *buf = nullptr, *arsIn = nullptr,
                    *atm = nullptr, *arsOut = nullptr, *sink = nullptr;
    if (!ok) {
        int rate = m_outputSampleRate.load(std::memory_order_acquire);
        tmpGraph = avfilter_graph_alloc();
        if (tmpGraph) {
            const AVFilter *f_abuffer = avfilter_get_by_name("abuffer");
            const AVFilter *f_aresample = avfilter_get_by_name("aresample");
            const AVFilter *f_atempo = avfilter_get_by_name("atempo");
            const AVFilter *f_abuffersink = avfilter_get_by_name("abuffersink");
            if (f_abuffer && f_aresample && f_atempo && f_abuffersink) {
                const QString args = QStringLiteral(
                    "time_base=1/%1:sample_rate=%1:sample_fmt=s16:channel_layout=stereo")
                    .arg(rate);
                const QByteArray argsBuf = args.toUtf8();
                const QString ars = QStringLiteral(
                    "osr=%1:out_chlayout=stereo:osf=fltp").arg(rate);
                const QByteArray arsBuf = ars.toUtf8();
                const QString t = QStringLiteral("tempo=%1").arg(speed, 0, 'f', 4);
                const QByteArray tBuf = t.toUtf8();
                if (avfilter_graph_create_filter(&buf, f_abuffer, "in",
                        argsBuf.constData(), nullptr, tmpGraph) >= 0 &&
                    avfilter_graph_create_filter(&arsIn, f_aresample, "arsmp_in",
                        arsBuf.constData(), nullptr, tmpGraph) >= 0 &&
                    avfilter_graph_create_filter(&atm, f_atempo, "atempo",
                        tBuf.constData(), nullptr, tmpGraph) >= 0 &&
                    avfilter_graph_create_filter(&arsOut, f_aresample, "arsmp_out",
                        arsBuf.constData(), nullptr, tmpGraph) >= 0 &&
                    avfilter_graph_create_filter(&sink, f_abuffersink, "out",
                        nullptr, nullptr, tmpGraph) >= 0 &&
                    avfilter_link(buf, 0, arsIn, 0) >= 0 &&
                    avfilter_link(arsIn, 0, atm, 0) >= 0 &&
                    avfilter_link(atm, 0, arsOut, 0) >= 0 &&
                    avfilter_link(arsOut, 0, sink, 0) >= 0 &&
                    avfilter_graph_config(tmpGraph, nullptr) >= 0) {
                    // 成功——排空旧滤镜，然后替换。
                    ok = true;
                } else {
                    avfilter_graph_free(&tmpGraph);
                }
            } else {
                avfilter_graph_free(&tmpGraph);
            }
        }
        if (!ok) {
            qWarning() << "AudioDecodeThread: cannot build filter for tempo"
                       << speed << "- staying at" << current;
            return;
        }
        // 排空并销毁旧滤镜图，然后安装新滤镜图。
        drainFilterGraph();
        destroyFilterGraph();
        m_filterGraph = tmpGraph;
        m_abufferCtx = buf;
        m_aresampleInCtx = arsIn;
        m_atempoCtx = atm;
        m_aresampleOutCtx = arsOut;
        m_abuffersinkCtx = sink;
        m_currentSpeed.store(speed, std::memory_order_release);
    } else {
        // 1.0× 直通——无需滤镜。
        drainFilterGraph();
        destroyFilterGraph();
        m_currentSpeed.store(1.0, std::memory_order_release);
    }
}

// ── 主循环 ──

void AudioDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue || !m_frameQueue)
        return;

    // 等待 MediaEngine 告诉我们实际的输出采样率。
    while (m_outputSampleRate.load(std::memory_order_acquire) == 0
           && !m_quit) {
        msleep(1);
    }
    if (m_quit) return;

    if (!initSwrContext()) {
        qWarning() << "AudioDecodeThread: failed to init swr context";
        return;
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return;

    while (!m_quit) {
        if (m_paused && m_paused->load()) {
            while (!m_quit && m_paused->load())
                msleep(10);
            if (m_quit) break;
            continue;
        }

        if (m_speedDirty.load(std::memory_order_acquire))
            applySpeed();

        AVPacket *pkt = m_packetQueue->pop();
        if (!pkt)
            break;

        int ret = avcodec_send_packet(m_codecCtx, pkt);
        av_packet_free(&pkt);

        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            continue;

        while (!m_quit) {
            ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0)
                break;

            AVFrame *resampled = resampleFrame(frame);
            if (!resampled) {
                av_frame_unref(frame);
                continue;
            }

            if (m_filterGraph && !qFuzzyCompare(m_currentSpeed.load(std::memory_order_acquire), 1.0)) {
                int addRet = av_buffersrc_add_frame_flags(m_abufferCtx, resampled, 0);
                if (addRet >= 0) {
                    while (true) {
                        AVFrame *filtered = av_frame_alloc();
                        int r = av_buffersink_get_frame(m_abuffersinkCtx, filtered);
                        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                            av_frame_free(&filtered);
                            break;
                        }
                        if (r < 0) {
                            av_frame_free(&filtered);
                            break;
                        }
                        filtered = convertFrameToS16(filtered);
                        if (filtered) {
                            // 确保滤镜链后的 time_base 一致。
                            // atempo/aresample/abuffersink 组合可能留下
                            // 意外的 time_base 值，这会破坏 fillAudioFifo
                            // 中的 pts→秒 转换。
                            int rate = m_outputSampleRate.load(std::memory_order_acquire);
                            filtered->time_base = (AVRational){1, rate};
                            m_frameQueue->push(filtered);
                            av_frame_free(&filtered);
                        }
                    }
                } else if (addRet < 0) {
                    qWarning() << "AudioDecodeThread: av_buffersrc_add_frame_flags failed:" << addRet;
                }
            } else {
                m_frameQueue->push(resampled);
                resampled = nullptr;
            }

            av_frame_free(&resampled);
            av_frame_unref(frame);
        }
    }

    drainFilterGraph();

    av_frame_free(&frame);
}
