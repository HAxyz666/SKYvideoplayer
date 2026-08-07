#include "VideoDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include <QDebug>

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
    // 请求所有队列退出，唤醒阻塞的 pop()/push() 调用
    if (m_packetQueue) {
        m_packetQueue->requestQuit();
        m_packetQueue->flush();
        m_packetQueue->setFinished(true);
    }
    if (m_frameQueue) {
        m_frameQueue->requestQuit();
        m_frameQueue->flush();
        m_frameQueue->setFinished(true);
    }
}

void VideoDecodeThread::setPausedRef(const std::atomic<bool> &paused)
{
    m_paused = &paused;
}

void VideoDecodeThread::setTimeBase(AVRational tb)
{
    m_timeBase = tb;
}

void VideoDecodeThread::setPtsDropBefore(double sec)
{
    m_ptsDropBefore.store(sec, std::memory_order_release);
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

            // 丢弃 seek/切换锚点之前的旧画面帧：backward seek 落在上一关键帧，
            // 整段 GOP 会先被解码出来，若进入显示队列会以正常节奏走完（画面卡
            // 旧内容），且视频队列积压阻塞 demux、饿死音频队列。
            // 此处直接丢弃，帧队列只保留锚点之后的帧，显示端即播即放。
            double dropBefore = m_ptsDropBefore.load(std::memory_order_acquire);
            if (dropBefore >= 0.0 && frame->pts != AV_NOPTS_VALUE) {
                double ptsSec = static_cast<double>(frame->pts) * av_q2d(m_timeBase);
                if (ptsSec + 1e-6 < dropBefore) {
                    av_frame_free(&frame);
                    continue;
                }
            }

#ifdef ENABLE_HWACCEL
            // 将硬件帧转换为系统内存中的 NV12 格式，以便显示端提取 YUV 平面而无需调用硬件 API。
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
            // FrameQueue::push 会克隆帧，因此可以立即释放本地引用。
            m_frameQueue->push(frame);
            av_frame_free(&frame);
        }
    }

    m_frameQueue->setFinished(true);
}
