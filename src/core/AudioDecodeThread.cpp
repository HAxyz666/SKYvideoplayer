#include "AudioDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <QDebug>
#include <cmath>

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
    , m_atempoCtx(nullptr)
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
    outFrame->linesize[0] = converted * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * outFrame->ch_layout.nb_channels;

    if (frame->pts != AV_NOPTS_VALUE) {
        outFrame->pts = av_rescale_q(frame->pts, m_timeBase, (AVRational){1, 44100});
    } else {
        outFrame->pts = AV_NOPTS_VALUE;
    }
    outFrame->time_base = (AVRational){1, 44100};

    return outFrame;
}

// ── Filter graph (S16 format, single atempo) ──

bool AudioDecodeThread::initFilterGraph(double tempo)
{
    m_filterGraph = avfilter_graph_alloc();
    if (!m_filterGraph)
        return false;

    const AVFilter *abuffer = avfilter_get_by_name("abuffer");
    const AVFilter *atempo = avfilter_get_by_name("atempo");
    const AVFilter *abuffersink = avfilter_get_by_name("abuffersink");

    if (!abuffer || !atempo || !abuffersink) {
        qWarning() << "AudioDecodeThread: required filters not found";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    char args[256];
    snprintf(args, sizeof(args),
        "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s:channels=%d",
        1, 44100, 44100,
        av_get_sample_fmt_name(AV_SAMPLE_FMT_S16),
        "stereo", 2);

    if (avfilter_graph_create_filter(&m_abufferCtx, abuffer, "in", args, nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecodeThread: failed to create abuffer filter";
        avfilter_graph_free(&m_filterGraph);
        m_filterGraph = nullptr;
        return false;
    }

    char tempoStr[32];
    snprintf(tempoStr, sizeof(tempoStr), "tempo=%.4f", tempo);
    if (avfilter_graph_create_filter(&m_atempoCtx, atempo, "atempo", tempoStr, nullptr, m_filterGraph) < 0) {
        qWarning() << "AudioDecodeThread: failed to create atempo filter";
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

    if (avfilter_link(m_abufferCtx, 0, m_atempoCtx, 0) < 0 ||
        avfilter_link(m_atempoCtx, 0, m_abuffersinkCtx, 0) < 0) {
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
        m_atempoCtx = nullptr;
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

            AVFrame *s16 = resampleFrame(frame);
            if (!s16) {
                av_frame_unref(frame);
                continue;
            }

            if (m_filterGraph && !qFuzzyCompare(m_currentSpeed, 1.0)) {
                if (av_buffersrc_add_frame_flags(m_abufferCtx, s16, 0) >= 0) {
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

                        if (!m_frameQueue->isFull())
                            m_frameQueue->push(filtered);
                        else
                            av_frame_free(&filtered);
                    }
                }
            } else {
                if (!m_frameQueue->isFull())
                    m_frameQueue->push(s16);
            }

            av_frame_free(&s16);
            av_frame_unref(frame);
        }
    }

    if (m_filterGraph) {
        (void)av_buffersrc_add_frame_flags(m_abufferCtx, nullptr, AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT);
        while (true) {
            AVFrame *filtered = av_frame_alloc();
            int ret = av_buffersink_get_frame(m_abuffersinkCtx, filtered);
            if (ret < 0) {
                av_frame_free(&filtered);
                break;
            }
            if (!m_frameQueue->isFull() && !m_quit) {
                m_frameQueue->push(filtered);
            } else {
                av_frame_free(&filtered);
            }
        }
    }

    av_frame_free(&frame);
}
