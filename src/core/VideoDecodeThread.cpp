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
    m_startTime.store(m_startTime.load(std::memory_order_relaxed) + offset,
                      std::memory_order_relaxed);
}

void VideoDecodeThread::setSpeed(double speed)
{
    speed = qBound(0.1, speed, 5.0);
    double oldSpeed = m_speed.load(std::memory_order_relaxed);
    if (qFuzzyCompare(oldSpeed, speed))
        return;
    m_speed.store(speed, std::memory_order_relaxed);

    if (!m_firstFrame.load(std::memory_order_relaxed) && !m_paused->load() && oldSpeed > 0.0) {
        int64_t now = av_gettime();
        int64_t elapsed = now - m_startTime.load(std::memory_order_relaxed);
        int64_t newElapsed = static_cast<int64_t>(elapsed * oldSpeed / speed);
        m_startTime.store(now - newElapsed, std::memory_order_relaxed);
    }
}

void VideoDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue)
        return;

    int videoWidth = m_codecCtx->width;
    int videoHeight = m_codecCtx->height;

    SwsContext *swsCtx = sws_getContext(
        videoWidth, videoHeight, m_codecCtx->pix_fmt,
        videoWidth, videoHeight, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsCtx) {
        emit frameReady(QImage());
        return;
    }

    AVFrame *rgbFrame = av_frame_alloc();
    int rgbWidth = (videoWidth + 15) & ~15;
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, rgbWidth, videoHeight, 32);
    if (numBytes <= 0) {
        sws_freeContext(swsCtx);
        return;
    }
    uint8_t *buffer = (uint8_t *)av_malloc(numBytes);
    if (!buffer) {
        av_frame_free(&rgbFrame);
        sws_freeContext(swsCtx);
        return;
    }
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24,
                         rgbWidth, videoHeight, 32);

    m_startTime.store(av_gettime(), std::memory_order_relaxed);

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

            sws_scale(swsCtx, frame->data, frame->linesize, 0,
                      frame->height, rgbFrame->data, rgbFrame->linesize);

            if (frame->pts != AV_NOPTS_VALUE) {
                double pts = frame->pts * av_q2d(m_timeBase);
                int64_t ptsUs = static_cast<int64_t>(pts * 1000000);

                if (m_firstFrame.load(std::memory_order_relaxed)) {
                    m_startTime.store(av_gettime() - ptsUs, std::memory_order_relaxed);
                    m_firstFrame.store(false, std::memory_order_relaxed);
                }

                {
                    int64_t now = av_gettime();
                    double speed = m_speed.load(std::memory_order_relaxed);
                    double effectiveSpeed = (speed > 0.0) ? speed : 1.0;
                    int64_t baseStart = m_startTime.load(std::memory_order_relaxed);
                    int64_t targetDisplayUs = baseStart + static_cast<int64_t>(ptsUs / effectiveSpeed);
                    int64_t delay = targetDisplayUs - now;
                    while (delay > 0 && !m_quit && !(m_paused && m_paused->load())) {
                        int64_t sleepUs = qMin(delay, (int64_t)10000);
                        av_usleep(sleepUs);
                        speed = m_speed.load(std::memory_order_relaxed);
                        effectiveSpeed = (speed > 0.0) ? speed : 1.0;
                        baseStart = m_startTime.load(std::memory_order_relaxed);
                        targetDisplayUs = baseStart + static_cast<int64_t>(ptsUs / effectiveSpeed);
                        delay = targetDisplayUs - av_gettime();
                    }
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
