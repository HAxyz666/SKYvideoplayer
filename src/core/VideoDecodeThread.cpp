#include "VideoDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "VideoRenderItem.h"
#include <QDebug>

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

void VideoDecodeThread::setCodecContext(AVCodecContext *ctx) { m_codecCtx = ctx; }
void VideoDecodeThread::setPacketQueue(PacketQueue *queue) { m_packetQueue = queue; }
void VideoDecodeThread::setFrameQueue(FrameQueue *queue) { m_frameQueue = queue; }
void VideoDecodeThread::setTimeBase(AVRational tb) { m_timeBase = tb; }

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

static YUVFrame extractYUV420P(AVFrame *frame, int width, int height)
{
    YUVFrame out;
    out.frameSize = QSize(width, height);

    int halfW = (width + 1) / 2;
    int halfH = (height + 1) / 2;

    out.yPlane.resize(width * height);
    for (int i = 0; i < height; i++)
        memcpy(out.yPlane.data() + i * width, frame->data[0] + i * frame->linesize[0], width);

    out.uPlane.resize(halfW * halfH);
    for (int i = 0; i < halfH; i++)
        memcpy(out.uPlane.data() + i * halfW, frame->data[1] + i * frame->linesize[1], halfW);

    out.vPlane.resize(halfW * halfH);
    for (int i = 0; i < halfH; i++)
        memcpy(out.vPlane.data() + i * halfW, frame->data[2] + i * frame->linesize[2], halfW);

    return out;
}

static YUVFrame extractNV12(AVFrame *frame, int width, int height)
{
    YUVFrame out;
    out.frameSize = QSize(width, height);

    int halfW = (width + 1) / 2;
    int halfH = (height + 1) / 2;

    out.yPlane.resize(width * height);
    for (int i = 0; i < height; i++)
        memcpy(out.yPlane.data() + i * width, frame->data[0] + i * frame->linesize[0], width);

    out.uPlane.resize(halfW * halfH);
    out.vPlane.resize(halfW * halfH);
    const uint8_t *uv = frame->data[1];
    for (int i = 0; i < halfH; i++) {
        for (int j = 0; j < halfW; j++) {
            out.uPlane[i * halfW + j] = uv[i * frame->linesize[1] + j * 2];
            out.vPlane[i * halfW + j] = uv[i * frame->linesize[1] + j * 2 + 1];
        }
    }

    return out;
}

void VideoDecodeThread::run()
{
    if (!m_codecCtx || !m_packetQueue)
        return;

    int videoWidth = m_codecCtx->width;
    int videoHeight = m_codecCtx->height;

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

            AVFrame *yuvFrame = frame;

#ifdef ENABLE_HWACCEL
            AVFrame *nv12Frame = nullptr;
            if (m_hwPixFmt != AV_PIX_FMT_NONE && frame->format == m_hwPixFmt) {
                nv12Frame = av_frame_alloc();
                nv12Frame->format = AV_PIX_FMT_NV12;
                if (av_hwframe_transfer_data(nv12Frame, frame, 0) != 0) {
                    qWarning() << "av_hwframe_transfer_data failed, dropping frame";
                    av_frame_free(&nv12Frame);
                    av_frame_unref(frame);
                    av_frame_free(&frame);
                    continue;
                }
                yuvFrame = nv12Frame;
            }
#endif

            if (frame->pts != AV_NOPTS_VALUE) {
                double pts = frame->pts * av_q2d(m_timeBase);
                int64_t ptsUs = static_cast<int64_t>(pts * 1000000);

                if (m_firstFrame.load(std::memory_order_relaxed)) {
                    double initSpeed = m_speed.load(std::memory_order_acquire);
                    double effSpeed = (initSpeed > 0.0) ? initSpeed : 1.0;
                    m_startTime.store(av_gettime() - static_cast<int64_t>(ptsUs / effSpeed),
                                      std::memory_order_release);
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
                    if (delay < 0) delay = 0;
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
#ifdef ENABLE_HWACCEL
                if (nv12Frame)
                    av_frame_free(&nv12Frame);
#endif
                av_frame_unref(frame);
                av_frame_free(&frame);
                break;
            }

            YUVFrame yuv;
            AVPixelFormat fmt = static_cast<AVPixelFormat>(yuvFrame->format);

            if (fmt == AV_PIX_FMT_NV12 || fmt == AV_PIX_FMT_NV21) {
                yuv = extractNV12(yuvFrame, videoWidth, videoHeight);
            } else {
                yuv = extractYUV420P(yuvFrame, videoWidth, videoHeight);
            }

            emit frameReady(yuv);

#ifdef ENABLE_HWACCEL
            if (nv12Frame)
                av_frame_free(&nv12Frame);
#endif
            av_frame_unref(frame);
            av_frame_free(&frame);
        }
    }

    if (m_frameQueue)
        m_frameQueue->setFinished(true);
}
