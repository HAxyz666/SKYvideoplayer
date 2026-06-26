#include "VideoDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <QDebug>

extern "C" {
#include <libavutil/imgutils.h>
}

VideoDecodeThread::VideoDecodeThread(QObject *parent)
    : QThread(parent)
    , m_codecCtx(nullptr)
    , m_packetQueue(nullptr)
    , m_frameQueue(nullptr)
    , m_quit(false)
    , m_paused(nullptr)
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
    if (!m_codecCtx || !m_packetQueue || !m_frameQueue)
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

#ifdef ENABLE_HWACCEL
            // Transfer HW frame to NV12 in system memory so the display
            // side can extract YUV planes without touching HW APIs.
            if (m_hwPixFmt != AV_PIX_FMT_NONE && frame->format == m_hwPixFmt) {
                AVFrame *swFrame = av_frame_alloc();
                if (!swFrame) {
                    av_frame_free(&frame);
                    continue;
                }
                swFrame->format = AV_PIX_FMT_NV12;
                if (av_hwframe_transfer_data(swFrame, frame, 0) != 0) {
                    qWarning() << "av_hwframe_transfer_data failed, dropping frame";
                    av_frame_free(&swFrame);
                    av_frame_free(&frame);
                    continue;
                }
                av_frame_copy_props(swFrame, frame);
                av_frame_free(&frame);
                frame = swFrame;
            }
#endif
            // FrameQueue::push clones the frame, so we can release our local
            // reference immediately.
            m_frameQueue->push(frame);
            av_frame_free(&frame);
        }
    }

    m_frameQueue->setFinished(true);
}
