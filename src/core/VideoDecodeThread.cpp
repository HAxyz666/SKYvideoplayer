#include "VideoDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <QDebug>
#include <QImage>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

VideoDecodeThread::VideoDecodeThread(QObject *parent)
    : QThread(parent)
    , m_codecCtx(nullptr)
    , m_packetQueue(nullptr)
    , m_frameQueue(nullptr)
    , m_timeBase{1, 90000}
    , m_startTime(0)
    , m_quit(false)
    , m_paused(nullptr)
    , m_firstFrame(true)
{
}

VideoDecodeThread::~VideoDecodeThread()
{
    stopDecode();
    wait();
#ifdef ENABLE_HWACCEL
    if (m_hwDeviceCtx)
        av_buffer_unref(&m_hwDeviceCtx);
#endif
}

void VideoDecodeThread::setCodecContext(AVCodecContext *ctx)
{
    m_codecCtx = ctx;
}

void VideoDecodeThread::setPacketQueue(PacketQueue *queue)
{
    m_packetQueue = queue;
}

void VideoDecodeThread::setFrameQueue(FrameQueue *queue)
{
    m_frameQueue = queue;
}

void VideoDecodeThread::setTimeBase(AVRational tb)
{
    m_timeBase = tb;
}

void VideoDecodeThread::stopDecode()
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

void VideoDecodeThread::setPausedRef(const std::atomic<bool> &paused)
{
    m_paused = &paused;
}

void VideoDecodeThread::adjustStartTime(int64_t offset)
{
    if (offset < 0)
        offset = av_gettime() - m_pauseStartTime;
    m_startTime.store(m_startTime.load(std::memory_order_acquire) + offset,
                      std::memory_order_release);
    m_driftCompensation = 0;
}

void VideoDecodeThread::setSpeed(double speed)
{
    speed = qBound(0.1, speed, 5.0);
    double oldSpeed = m_speed.load(std::memory_order_acquire);
    if (qFuzzyCompare(oldSpeed, speed))
        return;
    m_speed.store(speed, std::memory_order_release);

    if (!m_firstFrame.load(std::memory_order_acquire) && !m_paused->load() && oldSpeed > 0.0) {
        int64_t now = av_gettime();
        int64_t elapsed = now - m_startTime.load(std::memory_order_acquire);
        int64_t newElapsed = static_cast<int64_t>(elapsed * oldSpeed / speed);
        m_startTime.store(now - newElapsed, std::memory_order_release);
        m_driftCompensation = 0;
    }
}

#ifdef ENABLE_HWACCEL
void VideoDecodeThread::setHwContext(AVBufferRef *ctx, AVPixelFormat pixFmt)
{
    if (ctx)
        m_hwDeviceCtx = av_buffer_ref(ctx);
    m_hwPixFmt = pixFmt;
}
#endif

void VideoDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue)
        return;

    int videoWidth = m_codecCtx->width;
    int videoHeight = m_codecCtx->height;

    SwsContext *swsCtx = nullptr;
    AVFrame *rgbFrame = nullptr;
    uint8_t *buffer = nullptr;
    int rgbWidth = (videoWidth + 15) & ~15;

    if (m_hwPixFmt == AV_PIX_FMT_NONE) {
        swsCtx = sws_getContext(
            videoWidth, videoHeight, m_codecCtx->pix_fmt,
            videoWidth, videoHeight, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    } else {
        swsCtx = sws_getContext(
            videoWidth, videoHeight, AV_PIX_FMT_NV12,
            videoWidth, videoHeight, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    }

    if (!swsCtx) {
        emit frameReady(QImage());
        return;
    }

    rgbFrame = av_frame_alloc();
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, rgbWidth, videoHeight, 32);
    if (numBytes <= 0) {
        sws_freeContext(swsCtx);
        return;
    }
    buffer = (uint8_t *)av_malloc(numBytes);
    if (!buffer) {
        av_frame_free(&rgbFrame);
        sws_freeContext(swsCtx);
        return;
    }
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24,
                         rgbWidth, videoHeight, 32);

    int64_t t_start = av_gettime();
    m_startTime.store(t_start, std::memory_order_release);

    while (!m_quit) {
        if (m_paused && m_paused->load()) {
            if (m_pauseStartTime == 0)
                m_pauseStartTime = av_gettime();
            while (!m_quit && m_paused->load())
                msleep(10);
            if (m_quit) break;
            adjustStartTime();
            m_pauseStartTime = 0;
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
            AVFrame *frame = av_frame_alloc();
            ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_free(&frame);
                break;
            }
            if (ret < 0) {
                av_frame_free(&frame);
                break;
            }

            if (m_frameQueue)
                m_frameQueue->push(frame);

            if (m_hwPixFmt != AV_PIX_FMT_NONE && frame->format == m_hwPixFmt) {
                AVFrame *nv12Frame = av_frame_alloc();
                nv12Frame->format = AV_PIX_FMT_NV12;
                if (av_hwframe_transfer_data(nv12Frame, frame, 0) == 0) {
                    if (frame->pts != AV_NOPTS_VALUE) {
                        double pts = frame->pts * av_q2d(m_timeBase);
                        int64_t ptsUs = static_cast<int64_t>(pts * 1000000);

                if (m_firstFrame.load(std::memory_order_relaxed)) {
                    double initSpeed = m_speed.load(std::memory_order_acquire);
                    double effSpeed = (initSpeed > 0.0) ? initSpeed : 1.0;
                    int64_t t_now = av_gettime();
                    m_startTime.store(t_now - static_cast<int64_t>(ptsUs / effSpeed), std::memory_order_release);
                    m_firstFrame.store(false, std::memory_order_relaxed);
                    m_driftCompensation = 0;
                }

                {
                    double speed = m_speed.load(std::memory_order_acquire);
                            double effectiveSpeed = (speed > 0.0) ? speed : 1.0;
                            int64_t baseStart = m_startTime.load(std::memory_order_acquire);
                            int64_t targetDisplayUs = baseStart + static_cast<int64_t>(ptsUs / effectiveSpeed);
                            int64_t delay = targetDisplayUs - av_gettime();
                            delay -= m_driftCompensation;
                            if (delay < 0)
                                delay = 0;
                            while (delay > 0 && !m_quit && !(m_paused && m_paused->load())) {
                                int64_t sleepUs = qMin(delay, (int64_t)10000);
                                av_usleep(sleepUs);
                                speed = m_speed.load(std::memory_order_acquire);
                                effectiveSpeed = (speed > 0.0) ? speed : 1.0;
                                baseStart = m_startTime.load(std::memory_order_acquire);
                                targetDisplayUs = baseStart + static_cast<int64_t>(ptsUs / effectiveSpeed);
                                delay = targetDisplayUs - av_gettime();
                            }
                            int64_t overrun = av_gettime() - targetDisplayUs;
                            if (overrun > 0)
                                m_driftCompensation = overrun;
                            else
                                m_driftCompensation = 0;
                        }
                    }
                    if (m_quit) {
                        av_frame_free(&nv12Frame);
                        av_frame_unref(frame);
                        av_frame_free(&frame);
                        break;
                    }

                    sws_scale(swsCtx, nv12Frame->data, nv12Frame->linesize, 0,
                              nv12Frame->height, rgbFrame->data, rgbFrame->linesize);

                    QImage image(rgbFrame->data[0], rgbWidth, videoHeight,
                                 rgbFrame->linesize[0], QImage::Format_RGB888);
                    QImage cropped = image.copy(0, 0, videoWidth, videoHeight);
                    emit frameReady(cropped);
                } else {
                    qWarning() << "av_hwframe_transfer_data failed, dropping frame";
                }
                av_frame_free(&nv12Frame);
            } else {
                sws_scale(swsCtx, frame->data, frame->linesize, 0,
                          frame->height, rgbFrame->data, rgbFrame->linesize);

                int64_t ptsUs = AV_NOPTS_VALUE;
                int64_t delay = 0;
                if (frame->pts != AV_NOPTS_VALUE) {
                    double pts = frame->pts * av_q2d(m_timeBase);
                    ptsUs = static_cast<int64_t>(pts * 1000000);

                    if (m_firstFrame.load(std::memory_order_relaxed)) {
                        double initSpeed = m_speed.load(std::memory_order_acquire);
                        double effSpeed = (initSpeed > 0.0) ? initSpeed : 1.0;
                        m_startTime.store(av_gettime() - static_cast<int64_t>(ptsUs / effSpeed), std::memory_order_release);
                        m_firstFrame.store(false, std::memory_order_relaxed);
                        m_driftCompensation = 0;
                    }

                    {
                        double speed = m_speed.load(std::memory_order_acquire);
                        double effectiveSpeed = (speed > 0.0) ? speed : 1.0;
                        int64_t baseStart = m_startTime.load(std::memory_order_acquire);
                        int64_t targetDisplayUs = baseStart + static_cast<int64_t>(ptsUs / effectiveSpeed);
                        delay = targetDisplayUs - av_gettime();
                        delay -= m_driftCompensation;
                        if (delay < 0)
                            delay = 0;
                        while (delay > 0 && !m_quit && !(m_paused && m_paused->load())) {
                            int64_t sleepUs = qMin(delay, (int64_t)10000);
                            av_usleep(sleepUs);
                            speed = m_speed.load(std::memory_order_acquire);
                            effectiveSpeed = (speed > 0.0) ? speed : 1.0;
                            baseStart = m_startTime.load(std::memory_order_acquire);
                            targetDisplayUs = baseStart + static_cast<int64_t>(ptsUs / effectiveSpeed);
                            delay = targetDisplayUs - av_gettime();
                        }
                        int64_t overrun = av_gettime() - targetDisplayUs;
                        if (overrun > 0)
                            m_driftCompensation = overrun;
                        else
                            m_driftCompensation = 0;
                    }
                }
                if (m_quit) {
                    av_frame_unref(frame);
                    av_frame_free(&frame);
                    break;
                }

                QImage image(rgbFrame->data[0], rgbWidth, videoHeight,
                             rgbFrame->linesize[0], QImage::Format_RGB888);
                QImage cropped = image.copy(0, 0, videoWidth, videoHeight);
                emit frameReady(cropped);
            }

            av_frame_unref(frame);
            av_frame_free(&frame);
        }
    }

    av_free(buffer);
    av_frame_free(&rgbFrame);
    sws_freeContext(swsCtx);

    if (m_frameQueue)
        m_frameQueue->setFinished(true);
}
