#include "AudioDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <QDebug>
#include <cmath>
#include <cstring>


static AVFrame *convertFrameToS16(AVFrame *frame)
{
    const int nb_samples = frame->nb_samples;
    const int channels = frame->ch_layout.nb_channels > 0
                             ? frame->ch_layout.nb_channels : 2;

    // ── S16 interleaved (fallback / non-speed path) ──
    if (frame->format == AV_SAMPLE_FMT_S16) {
        int16_t *s = (int16_t*)frame->data[0];
        const int total = nb_samples * channels;

        float peak = 0.0f;
        for (int i = 0; i < total; i++) {
            float v = fabsf((float)s[i]);
            if (v > peak) peak = v;
        }

        // Only scale down when near clipping; 0.99 headroom
        const float scale = (peak > 32767.0f * 0.99f)
                                ? (32767.0f * 0.99f / peak) : 1.0f;
        if (scale < 1.0f)
            for (int i = 0; i < total; i++)
                s[i] = (int16_t)(s[i] * scale);

        return frame;
    }

    // ── FLTP planar ── find full-frame peak, then convert + scale ──
    if (frame->format == AV_SAMPLE_FMT_FLTP) {
        float *src[8];
        for (int c = 0; c < channels && c < 8; c++)
            src[c] = (float*)frame->data[c];

        float peak = 0.0f;
        for (int i = 0; i < nb_samples; i++)
            for (int c = 0; c < channels; c++) {
                float v = fabsf(src[c][i]);
                if (v > peak) peak = v;
            }

        // Only scale down when > 1.0 (atempo OLA overshoot); keep 0.99 headroom
        const float scale = (peak > 1.0f) ? (0.99f / peak) : 1.0f;

        // 3. Allocate S16 frame and convert
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

// ── Resample (decode format ─> S16 44100Hz stereo) ──

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
    av_opt_set_int(m_swrCtx, "out_sample_rate", 44100, 0);
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
    outFrame->sample_rate = 44100;
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
        outFrame->pts = av_rescale_q(frame->pts, m_timeBase, (AVRational){1, 44100});
    } else {
        outFrame->pts = AV_NOPTS_VALUE;
    }
    outFrame->time_base = (AVRational){1, 44100};

    return outFrame;
}

// ── Filter graph: abuffer(S16) → aresample(→FLTP) → atempo → aresample(→FLTP) → abuffersink → peak-norm(→S16) ──

bool AudioDecodeThread::initFilterGraph(double tempo)
{
    m_filterGraph = avfilter_graph_alloc();
    if (!m_filterGraph)
        return false;

    const AVFilter *abuffer = avfilter_get_by_name("abuffer");
    const AVFilter *aresample = avfilter_get_by_name("aresample");
    const AVFilter *atempo = avfilter_get_by_name("atempo");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");

    if (!abuffer || !aresample || !atempo || !abuffersink) {
        qWarning() << "AudioDecodeThread: required filters not found";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    const QString args = QStringLiteral("time_base=1/44100:sample_rate=44100:sample_fmt=s16:channel_layout=stereo");
    const QByteArray argsBuf = args.toUtf8();

    if (avfilter_graph_create_filter(&m_abufferCtx, abuffer, "in", argsBuf.constData(), nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecodeThread: failed to create abuffer filter";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    if (avfilter_graph_create_filter(&m_aresampleInCtx, aresample, "aresample_in",
                                     "osr=44100:out_chlayout=stereo:osf=fltp", nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecodeThread: failed to create input aresample filter";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    const QString tempoArg = QStringLiteral("tempo=%1").arg(tempo, 0, 'f', 4);
    const QByteArray tempoArgBuf = tempoArg.toUtf8();
    if (avfilter_graph_create_filter(&m_atempoCtx, atempo, "atempo", tempoArgBuf.constData(), nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecodeThread: failed to create atempo filter";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    if (avfilter_graph_create_filter(&m_aresampleOutCtx, aresample, "aresample_out",
                                     "osr=44100:out_chlayout=stereo:osf=fltp", nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecodeThread: failed to create output aresample filter";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    if (avfilter_graph_create_filter(&m_abuffersinkCtx, abuffersink, "out", nullptr, nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecodeThread: failed to create abuffersink filter";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    if (avfilter_link(m_abufferCtx, 0, m_aresampleInCtx, 0) < 0 ||
        avfilter_link(m_aresampleInCtx, 0, m_atempoCtx, 0) < 0 ||
        avfilter_link(m_atempoCtx, 0, m_aresampleOutCtx, 0) < 0 ||
        avfilter_link(m_aresampleOutCtx, 0, m_abuffersinkCtx, 0) < 0) {
        qWarning() << "AudioDecodeThread: failed to link filter graph";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    if (avfilter_graph_config(m_filterGraph, nullptr) < 0) {
        qWarning() << "AudioDecodeThread: failed to configure filter graph";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    return true;
}

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

// ── Speed ──

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

    if (m_currentSpeed == speed)
        return;
    m_currentSpeed = speed;

    if (m_filterGraph) {
        av_buffersrc_add_frame_flags(m_abufferCtx, nullptr, 0);
        while (!m_quit) {
            AVFrame *filtered = av_frame_alloc();
            int r = av_buffersink_get_frame(m_abuffersinkCtx, filtered);
            if (r < 0) {
                av_frame_free(&filtered);
                break;
            }
            filtered = convertFrameToS16(filtered);
            m_frameQueue->push(filtered);
            av_frame_free(&filtered);
        }
    }

    destroyFilterGraph();
    if (qFuzzyCompare(speed, 1.0))
        return;
    if (!initFilterGraph(speed))
        qWarning() << "AudioDecodeThread: failed to recreate filter graph at tempo" << speed;
}

// ── Main loop ──

void AudioDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue || !m_frameQueue)
        return;

    if (!initSwrContext()) {
        qWarning() << "AudioDecodeThread: failed to init swr context";
        return;
    }

    if (!qFuzzyCompare(m_currentSpeed, 1.0))
        initFilterGraph(m_currentSpeed);

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

            if (m_filterGraph && !qFuzzyCompare(m_currentSpeed, 1.0)) {
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

                        m_frameQueue->push(filtered);
                        av_frame_free(&filtered);
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

    if (m_filterGraph) {
        (void)av_buffersrc_add_frame_flags(m_abufferCtx, nullptr, 0);
        while (true) {
            AVFrame *filtered = av_frame_alloc();
            int ret = av_buffersink_get_frame(m_abuffersinkCtx, filtered);
            if (ret < 0) {
                av_frame_free(&filtered);
                break;
            }
            filtered = convertFrameToS16(filtered);
            if (!m_quit)
                m_frameQueue->push(filtered);
            av_frame_free(&filtered);
        }
    }

    av_frame_free(&frame);
}
