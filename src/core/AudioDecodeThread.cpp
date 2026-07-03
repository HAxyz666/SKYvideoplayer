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
    , m_timeBase{1, 44100}
{
}

AudioDecodeThread::~AudioDecodeThread()
{
    stopDecode();
    wait();
    destroySonic();
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

void AudioDecodeThread::setSpeed(double speed)
{
    speed = qBound(0.5, speed, 2.0);
    m_currentSpeed.store(speed, std::memory_order_release);
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

void AudioDecodeThread::flushSonic()
{
    if (!m_sonicStream)
        return;
    sonicFlushStream(m_sonicStream);
    int rate = m_outputSampleRate.load(std::memory_order_acquire);
    while (true) {
        int avail = sonicSamplesAvailable(m_sonicStream);
        if (avail <= 0)
            break;
        AVFrame *outFrame = av_frame_alloc();
        if (!outFrame)
            break;
        outFrame->format = AV_SAMPLE_FMT_S16;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&outFrame->ch_layout, &stereo);
        outFrame->sample_rate = rate;
        outFrame->nb_samples = qMin(avail + 64, 4096);
        if (av_frame_get_buffer(outFrame, 0) < 0) {
            av_frame_free(&outFrame);
            break;
        }
        int read = sonicReadShortFromStream(m_sonicStream,
                                            (int16_t *)outFrame->data[0],
                                            outFrame->nb_samples);
        if (read <= 0) {
            av_frame_free(&outFrame);
            break;
        }
        outFrame->nb_samples = read;
        outFrame->pts = static_cast<qint64>(m_sonicOutputPts * rate);
        outFrame->time_base = (AVRational){1, rate};
        m_sonicOutputPts += static_cast<double>(read) / rate;

        m_frameQueue->push(outFrame);
    }
}

void AudioDecodeThread::destroySonic()
{
    if (m_sonicStream) {
        sonicDestroyStream(m_sonicStream);
        m_sonicStream = nullptr;
    }
}

void AudioDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue || !m_frameQueue)
        return;

    while (m_outputSampleRate.load(std::memory_order_acquire) == 0
           && !m_quit) {
        msleep(1);
    }
    if (m_quit) return;

    if (!initSwrContext()) {
        qWarning() << "AudioDecodeThread: failed to init swr context";
        return;
    }

    int rate = m_outputSampleRate.load(std::memory_order_acquire);
    m_sonicStream = sonicCreateStream(rate, 2);
    if (!m_sonicStream) {
        qWarning() << "AudioDecodeThread: failed to create sonic stream";
        return;
    }
    m_lastSpeed = 1.0;
    m_sonicOutputPts = 0.0;

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

        double speed = m_currentSpeed.load(std::memory_order_acquire);
        if (!qFuzzyCompare(speed, m_lastSpeed)) {
            if (m_lastSpeed != 1.0) {
                sonicFlushStream(m_sonicStream);
                short discard[4096];
                while (sonicSamplesAvailable(m_sonicStream) > 0)
                    sonicReadShortFromStream(m_sonicStream, discard, 2048);
                m_sonicOutputPts = 0.0;
            }
            if (!qFuzzyCompare(speed, 1.0))
                sonicSetSpeed(m_sonicStream, static_cast<float>(speed));
            m_lastSpeed = speed;
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
            if (!resampled) {
                av_frame_unref(frame);
                continue;
            }

            speed = m_currentSpeed.load(std::memory_order_acquire);

            if (qFuzzyCompare(speed, 1.0)) {
                m_frameQueue->push(resampled);
                resampled = nullptr;
            } else {
                double inputPtsSec = resampled->pts != AV_NOPTS_VALUE
                    ? (double)resampled->pts / rate : m_sonicOutputPts;
                if (m_sonicOutputPts < 1.0 && inputPtsSec > m_sonicOutputPts)
                    m_sonicOutputPts = inputPtsSec;

                double inputDuration = (double)resampled->nb_samples / rate;
                double writePts = m_sonicOutputPts;

                sonicWriteShortToStream(m_sonicStream,
                                        (int16_t *)resampled->data[0],
                                        resampled->nb_samples);

                int totalRead = 0;
                QVector<AVFrame *> outFrames;
                while (true) {
                    int avail = sonicSamplesAvailable(m_sonicStream);
                    if (avail <= 0)
                        break;

                    AVFrame *outFrame = av_frame_alloc();
                    if (!outFrame)
                        break;
                    outFrame->format = AV_SAMPLE_FMT_S16;
                    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
                    av_channel_layout_copy(&outFrame->ch_layout, &stereo);
                    outFrame->sample_rate = rate;
                    outFrame->nb_samples = qMin(avail + 64, 4096);

                    if (av_frame_get_buffer(outFrame, 0) < 0) {
                        av_frame_free(&outFrame);
                        break;
                    }

                    int read = sonicReadShortFromStream(
                        m_sonicStream,
                        (int16_t *)outFrame->data[0],
                        outFrame->nb_samples);

                    if (read <= 0) {
                        av_frame_free(&outFrame);
                        break;
                    }

                    outFrame->nb_samples = read;
                    totalRead += read;
                    outFrames.append(outFrame);
                }

                int soFar = 0;
                for (auto *f : outFrames) {
                    double frac = totalRead > 0
                        ? (double)soFar / totalRead : 0.0;
                    double pts = writePts + frac * inputDuration;
                    f->pts = static_cast<qint64>(pts * rate);
                    f->time_base = (AVRational){1, rate};
                    m_frameQueue->push(f);
                    soFar += f->nb_samples;
                }

                m_sonicOutputPts = writePts + inputDuration;
            }

            av_frame_free(&resampled);
            av_frame_unref(frame);
        }
    }

    if (!qFuzzyCompare(m_lastSpeed, 1.0))
        flushSonic();

    av_frame_free(&frame);
}
