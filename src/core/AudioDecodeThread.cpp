#include "AudioDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <QDebug>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

AudioDecodeThread::AudioDecodeThread(QObject *parent)
    : QThread(parent)
    , m_codecCtx(nullptr)
    , m_packetQueue(nullptr)
    , m_frameQueue(nullptr)
    , m_swrCtx(nullptr)
    , m_quit(false)
    , m_paused(nullptr)
{
}

AudioDecodeThread::~AudioDecodeThread()
{
    stopDecode();
    wait();
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }
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
    return outFrame;
}

void AudioDecodeThread::setPausedRef(const std::atomic<bool> &paused)
{
    m_paused = &paused;
}

void AudioDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue || !m_frameQueue)
        return;

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
            if (resampled) {
                // Non-blocking push: if queue is full, drop the frame to
                // prevent blocking the pipeline (which would stall video too).
                if (!m_frameQueue->isFull()) {
                    m_frameQueue->push(resampled);
                }
                av_frame_free(&resampled);
            }

            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
}
