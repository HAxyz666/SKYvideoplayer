#include "MediaEngine.h"
#include "DemuxThread.h"
#include "VideoDecodeThread.h"
#include "AudioDecodeThread.h"
#include "SubtitleDecodeThread.h"
#include "PacketQueue.h"
#include "FrameQueue.h"
#include "AVSyncController.h"
#include "AudioOutput.h"
#include "VideoRenderItem.h"

#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <cstring>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

// --- YUV plane extraction (runs on GUI thread inside onVideoRefresh) ---

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

#ifdef ENABLE_HWACCEL
bool MediaEngine::createHwDeviceContext(const AVCodec *codec)
{
    AVHWDeviceType hwTypes[] = {
#if defined(Q_OS_LINUX)
        AV_HWDEVICE_TYPE_VAAPI,
#elif defined(Q_OS_WIN)
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_DXVA2,
#elif defined(Q_OS_MACOS)
        AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
#endif
    };

    for (AVHWDeviceType type : hwTypes) {
        // 查找该 codec 是否支持此硬件类型的像素格式
        for (int i = 0; ; i++) {
            const AVCodecHWConfig *hwcfg = avcodec_get_hw_config(codec, i);
            if (!hwcfg) break;

            bool typeMatch = false;
            switch (type) {
            case AV_HWDEVICE_TYPE_VAAPI:     typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_VAAPI); break;
            case AV_HWDEVICE_TYPE_D3D11VA:   typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_D3D11); break;
            case AV_HWDEVICE_TYPE_DXVA2:     typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_DXVA2_VLD); break;
            case AV_HWDEVICE_TYPE_VIDEOTOOLBOX: typeMatch = (hwcfg->pix_fmt == AV_PIX_FMT_VIDEOTOOLBOX); break;
            default: break;
            }
            if (!typeMatch) continue;

            AVBufferRef *deviceCtx = nullptr;
            int ret = av_hwdevice_ctx_create(&deviceCtx, type, nullptr, nullptr, 0);
            if (ret < 0) {
                qDebug() << "Failed to create hw device" << av_hwdevice_get_type_name(type)
                         << ":" << av_err2str(ret);
                continue;
            }
            m_hwDeviceCtx = deviceCtx;
            m_hwPixFmt = hwcfg->pix_fmt;
            m_hwDeviceType = type;
            m_useHardwareDecode = true;
            qDebug() << "HW decode available:" << av_hwdevice_get_type_name(type)
                     << "fmt:" << av_get_pix_fmt_name(hwcfg->pix_fmt);
            return true;
        }
    }

    qDebug() << "No HW decoder available, using software decoding";
    return false;
}
#endif

MediaEngine::MediaEngine(QObject *parent)
    : QObject(parent)
    , m_fmtCtx(nullptr)
    , m_videoStreamIndex(-1)
    , m_audioStreamIndex(-1)
    , m_subtitleStreamIndex(-1)
    , m_videoCodecCtx(nullptr)
    , m_audioCodecCtx(nullptr)
    , m_subtitleCodecCtx(nullptr)
    , m_demuxThread(nullptr)
    , m_videoThread(nullptr)
    , m_audioThread(nullptr)
    , m_subtitleThread(nullptr)
    , m_videoPacketQueue(nullptr)
    , m_audioPacketQueue(nullptr)
    , m_subtitlePacketQueue(nullptr)
    , m_videoFrameQueue(nullptr)
    , m_syncController(new AVSyncController(this))
    , m_audioOutput(new AudioOutput(this))
    , m_videoRefreshTimer(new QTimer(this))
    , m_paused(false)
    , m_position(0.0)
    , m_duration(0.0)
    , m_startTimeUs(0)
    , m_pausedDurationUs(0)
    , m_pauseStartUs(0)
    , m_positionTimer(new QTimer(this))
    , m_volume(100.0)       // 默认音量 100%
    , m_muted(false)        // 默认非静音
    , m_audioOutputReady(false)
    , m_speed(1.0)
{
    // Video display refresh: peeks m_videoFrameQueue and times frame delivery
    // to VideoRenderItem via AVSyncController. 10ms granularity is fine for
    // <=60fps content; the actual delay is driven by computeFrameDelay().
    m_videoRefreshTimer->setInterval(10);
    connect(m_videoRefreshTimer, &QTimer::timeout, this, &MediaEngine::onVideoRefresh);

    m_positionTimer->setInterval(250);
    connect(m_positionTimer, &QTimer::timeout, this, &MediaEngine::updatePosition);
}

MediaEngine::~MediaEngine()
{
    stop();
}

bool MediaEngine::initFFmpeg(const QString &filename)
{
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_subtitleStreamIndex = -1;
    m_subtitleStreamsInfo.clear();
    m_currentSubtitle = QString();

    if (avformat_open_input(&m_fmtCtx, filename.toUtf8().constData(), nullptr, nullptr) != 0) {
        qCritical() << "Could not open file:" << filename;
        return false;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        qCritical() << "Could not find stream info";
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    for (unsigned int i = 0; i < m_fmtCtx->nb_streams; i++) {
        AVStream *st = m_fmtCtx->streams[i];
        // 跳过封面/专辑封面（attached picture），防止误识别为视频流
        if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
            continue;
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m_videoStreamIndex == -1) {
            m_videoStreamIndex = i;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && m_audioStreamIndex == -1) {
            m_audioStreamIndex = i;
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
            AVDictionaryEntry *title = av_dict_get(st->metadata, "title", nullptr, 0);
            m_subtitleStreamsInfo.append({
                static_cast<int>(i),
                lang ? QString::fromUtf8(lang->value) : QString(),
                title ? QString::fromUtf8(title->value) : QString()
            });
        }
    }

    detectExternalSubtitles(m_filename);

    if (m_videoStreamIndex != -1) {
        AVCodecParameters *videoPar = m_fmtCtx->streams[m_videoStreamIndex]->codecpar;
        const AVCodec *videoCodec = avcodec_find_decoder(videoPar->codec_id);
        if (!videoCodec) {
            qWarning() << "Video codec not found, playing without video";
            m_videoStreamIndex = -1;
        } else {
            m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
            if (!m_videoCodecCtx) {
                qWarning() << "Could not allocate video codec context, playing without video";
                m_videoStreamIndex = -1;
            } else {
                avcodec_parameters_to_context(m_videoCodecCtx, videoPar);

#ifdef ENABLE_HWACCEL
                if (createHwDeviceContext(videoCodec)) {
                    m_videoCodecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
                    m_videoCodecCtx->pix_fmt = m_hwPixFmt;
                }
#endif

                if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) {
#ifdef ENABLE_HWACCEL
                    if (m_hwDeviceCtx) {
                        qWarning() << "HW decode failed, falling back to software";
                        avcodec_free_context(&m_videoCodecCtx);
                        m_videoCodecCtx = avcodec_alloc_context3(videoCodec);
                        avcodec_parameters_to_context(m_videoCodecCtx, videoPar);
                        m_videoCodecCtx->hw_device_ctx = nullptr;
                        m_hwPixFmt = AV_PIX_FMT_NONE;

                        if (avcodec_open2(m_videoCodecCtx, videoCodec, nullptr) < 0) {
                            qWarning() << "Could not open video codec (both HW and SW failed), playing without video";
                            avcodec_free_context(&m_videoCodecCtx);
                            m_videoCodecCtx = nullptr;
                            m_videoStreamIndex = -1;
                        }
                    } else
#endif
                    {
                        qWarning() << "Could not open video codec, playing without video";
                        avcodec_free_context(&m_videoCodecCtx);
                        m_videoCodecCtx = nullptr;
                        m_videoStreamIndex = -1;
                    }
                }
            }
        }
    }

#ifdef ENABLE_HWACCEL
    if (m_videoCodecCtx && m_hwPixFmt != AV_PIX_FMT_NONE) {
        m_useHardwareDecode = true;
        qDebug() << "Hardware decoding active:" << av_get_pix_fmt_name(m_hwPixFmt);
    } else {
        m_useHardwareDecode = false;
        qDebug() << "Software decoding (hw device not used by codec)";
    }
#endif

    if (m_audioStreamIndex != -1) {
        AVCodecParameters *audioPar = m_fmtCtx->streams[m_audioStreamIndex]->codecpar;
        const AVCodec *audioCodec = avcodec_find_decoder(audioPar->codec_id);
        qDebug() << "Audio codec_id:" << audioPar->codec_id << "codec found:" << !!audioCodec;

        if (audioCodec) {
            m_audioCodecCtx = avcodec_alloc_context3(audioCodec);
            if (m_audioCodecCtx) {
                avcodec_parameters_to_context(m_audioCodecCtx, audioPar);

                int ret = avcodec_open2(m_audioCodecCtx, audioCodec, nullptr);
                if (ret < 0) {
                    char errbuf[128] = {0};
                    av_strerror(ret, errbuf, sizeof(errbuf));
                    qWarning() << "Could not open audio codec:" << errbuf;
                    avcodec_free_context(&m_audioCodecCtx);
                    m_audioCodecCtx = nullptr;
                }
            }
        } else {
            qWarning() << "Audio decoder not found for codec_id:" << audioPar->codec_id;
        }
    }

    if (!m_audioCodecCtx)
        m_audioStreamIndex = -1;

    // 初始化字幕解码器（选择默认或第一个字幕流）
    m_currentSubtitleStreamIndex = -1;
    m_subtitleStreamIndex = -1;
    if (!m_subtitleStreamsInfo.isEmpty()) {
        m_currentSubtitleStreamIndex = 0;
        if (m_subtitleStreamsInfo[0].isExternal) {
            // 无内嵌字幕，仅有外挂：直接激活外挂字幕
            activateExternalSubtitle(0);
        } else {
            int selIdx = m_subtitleStreamsInfo[0].streamIndex;
            AVCodecParameters *subPar = m_fmtCtx->streams[selIdx]->codecpar;
            const AVCodec *subCodec = avcodec_find_decoder(subPar->codec_id);
            if (subCodec) {
                m_subtitleCodecCtx = avcodec_alloc_context3(subCodec);
                if (m_subtitleCodecCtx) {
                    avcodec_parameters_to_context(m_subtitleCodecCtx, subPar);
                    if (avcodec_open2(m_subtitleCodecCtx, subCodec, nullptr) == 0) {
                        m_subtitleStreamIndex = selIdx;
                    } else {
                        avcodec_free_context(&m_subtitleCodecCtx);
                    }
                }
            }
            if (m_subtitleStreamIndex < 0)
                qWarning() << "Could not open subtitle codec";
        }
    }

    emit subtitleStreamsChanged();

    av_dump_format(m_fmtCtx, 0, filename.toUtf8().constData(), 0);

    m_duration = m_fmtCtx->duration != AV_NOPTS_VALUE
        ? m_fmtCtx->duration / (double)AV_TIME_BASE
        : 0.0;
    emit durationChanged(m_duration);

    return true;
}

void MediaEngine::start()
{
    if (!initFFmpeg(m_filename))
        return;

    emit hasVideoChanged();

    m_paused = false;
    m_audioOutputReady = false;
    m_syncController->reset();
    m_syncController->setSpeed(m_speed);

    m_position = 0.0;
    m_startTimeUs = av_gettime();
    m_pausedDurationUs = 0;
    m_pauseStartUs = 0;
    m_lastFrameDisplayTimeUs = 0;

    // Start threads first so audio decoder fills the FrameQueue
    startThreads();

    // Initialize SDL audio AFTER decoder has started, so FIFO can pre-fill
    if (m_audioCodecCtx) {
        SDL_AudioSpec spec;
        std::memset(&spec, 0, sizeof(spec));
        spec.freq = 44100;
        spec.format = AUDIO_S16SYS;
        spec.channels = 2;
        spec.samples = 1024;

        m_audioOutputReady = m_audioOutput->initialize(spec);
        if (m_audioOutputReady) {
            m_audioOutput->setSyncController(m_syncController);
        }
    }

    m_positionTimer->start();
    if (m_videoFrameQueue)
        m_videoRefreshTimer->start();
}

void MediaEngine::stop()
{
    m_positionTimer->stop();
    m_videoRefreshTimer->stop();

    // Report "not playing" only if something was actually playing; avoids
    // spurious pausedChanged on the very first open() (no prior fmtCtx).
    const bool wasPlaying = !m_paused && m_fmtCtx != nullptr;
    m_paused = true;

    stopThreads();
    cleanup();
    m_position = 0.0;
    m_duration = 0.0;
    emit positionChanged(0.0);
    emit durationChanged(0.0);
    if (wasPlaying)
        emit pausedChanged(true);
}

bool MediaEngine::open(const QString &url)
{
    stop();
    QString path = url;
    if (path.startsWith("file://"))
        path = path.mid(7);
    m_filename = path;
    start();
    return m_fmtCtx != nullptr;
}

void MediaEngine::pause()
{
    if (m_paused.exchange(true))
        return;
    m_videoRefreshTimer->stop();
    m_audioOutput->pause();
    m_pauseStartUs = av_gettime();
    emit pausedChanged(true);
}

void MediaEngine::resume()
{
    if (!m_paused.exchange(false))
        return;
    m_audioOutput->resume();
    m_pausedDurationUs += av_gettime() - m_pauseStartUs;
    m_pauseStartUs = 0;
    if (m_videoFrameQueue)
        m_videoRefreshTimer->start();
    emit pausedChanged(false);
}

void MediaEngine::togglePause()
{
    if (m_paused)
        resume();
    else
        pause();
}

bool MediaEngine::isPaused() const
{
    return m_paused;
}

void MediaEngine::seek(double seconds)
{
    if (!m_fmtCtx)
        return;

    m_positionTimer->stop();
    m_videoRefreshTimer->stop();

    // Stop audio directly (no pausedChanged signal) to avoid UI flicker.
    m_audioOutput->pause();
    m_audioOutput->reset();
    stopThreads();

    int64_t targetUs = static_cast<int64_t>(seconds * AV_TIME_BASE);
    if (targetUs < 0) targetUs = 0;
    if (m_duration > 0 && targetUs > static_cast<int64_t>(m_duration * AV_TIME_BASE))
        targetUs = static_cast<int64_t>(m_duration * AV_TIME_BASE);

    int ret = avformat_seek_file(m_fmtCtx, -1,
                                 INT64_MIN, targetUs, targetUs,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        qWarning() << "Seek failed:" << ret;

    if (m_videoCodecCtx)
        avcodec_flush_buffers(m_videoCodecCtx);
    if (m_audioCodecCtx)
        avcodec_flush_buffers(m_audioCodecCtx);
    if (m_subtitleCodecCtx) {
        avcodec_flush_buffers(m_subtitleCodecCtx);
        if (m_subtitleThread)
            m_subtitleThread->clearSubtitles();
    }
    m_currentSubtitle = QString();
    emit currentSubtitleChanged(m_currentSubtitle);

    m_syncController->reset();
    m_syncController->setSpeed(m_speed);
    m_syncController->updateAudioClock(seconds);

    qint64 now = av_gettime();
    m_startTimeUs = now - static_cast<qint64>(seconds * 1000000 / qMax(m_speed, 0.1));
    m_pausedDurationUs = 0;
    m_pauseStartUs = m_paused ? now : 0;
    m_lastFrameDisplayTimeUs = 0;
    m_position = seconds;
    emit positionChanged(m_position);

    startThreads();

    if (m_audioThread && !qFuzzyCompare(m_speed, 1.0))
        m_audioThread->setSpeed(m_speed);

    if (!m_paused)
        m_audioOutput->resume();

    m_positionTimer->start();
    if (m_videoFrameQueue)
        m_videoRefreshTimer->start();
}

double MediaEngine::position() const
{
    return m_position;
}

double MediaEngine::duration() const
{
    return m_duration;
}

// --- 速度控制 ---

void MediaEngine::setSpeed(double speed)
{
    // Match the audio atempo filter's working range so A/V stay in sync.
    speed = qBound(0.5, speed, 2.0);
    if (qFuzzyCompare(m_speed, speed))
        return;
    double oldSpeed = m_speed;
    m_speed = speed;

    // Re-anchor the wall-clock start time so the displayed position is
    // continuous across the speed change. When paused, anchor against the
    // pause start moment so resume doesn't jump.
    if (m_startTimeUs != 0) {
        qint64 refTime = m_paused ? m_pauseStartUs : av_gettime();
        double currentPos = (refTime - m_startTimeUs - m_pausedDurationUs) * oldSpeed / 1000000.0;
        m_startTimeUs = refTime - m_pausedDurationUs
                        - static_cast<qint64>(currentPos * 1000000.0 / speed);
    }

    // Do NOT clear audio queue or reset FIFO — old-speed frames have their
    // PTS corrected in AudioDecodeThread, so they blend seamlessly into the
    // new-speed stream.  No audio gap, no clock domain mismatch.

    m_syncController->setSpeed(speed);
    if (m_audioThread)
        m_audioThread->setSpeed(speed);
    m_audioOutput->setSpeed(speed);
    emit speedChanged(m_speed);
}

double MediaEngine::speed() const
{
    return m_speed;
}

// --- 音量控制实现，转发到 AudioOutput ---

void MediaEngine::setVolume(double vol)
{
    vol = qBound(0.0, vol, 100.0);
    if (qFuzzyCompare(m_volume, vol))
        return;
    m_volume = vol;
    m_audioOutput->setVolume(vol);          // 应用到 SDL 混音
    emit volumeChanged(m_volume);
}

void MediaEngine::setMuted(bool muted)
{
    if (m_muted == muted)
        return;
    m_muted = muted;
    m_audioOutput->setMuted(muted);         // 应用到 SDL 回调
    emit mutedChanged(m_muted);
}

double MediaEngine::volume() const
{
    return m_volume;
}

bool MediaEngine::muted() const
{
    return m_muted;
}

void MediaEngine::onVideoRefresh()
{
    // 字幕查询：10ms 粒度，与视频帧同源主时钟。放在所有早期返回之前，
    // 保证即使帧队列空或未到显示时间，字幕仍按主时钟及时更新。
    if (m_subtitleThread) {
        double subTime = m_syncController->audioClock();
        if (m_audioStreamIndex == -1 || subTime <= 0.0) {
            double pos = (av_gettime() - m_startTimeUs - m_pausedDurationUs)
                         * m_speed / 1000000.0;
            if (pos < 0) pos = 0;
            subTime = pos;
        }
        updateSubtitle(subTime);
    }

    if (!m_videoFrameQueue)
        return;

    AVFrame *frame = m_videoFrameQueue->peek();
    if (!frame)
        return;

    double pts = 0.0;
    if (frame->pts != AV_NOPTS_VALUE)
        pts = frame->pts * av_q2d(m_videoTimeBase);

    double delay = m_syncController->computeFrameDelay(pts);

    qint64 now = av_gettime();
    double elapsed = (now - m_lastFrameDisplayTimeUs) / 1000000.0;

    if (elapsed + 0.001 < delay)
        return;

    frame = m_videoFrameQueue->tryPop(0);
    if (!frame)
        return;

    // Update sync state only after we've committed to displaying this frame.
    m_syncController->onFrameDisplayed(pts);
    m_lastFrameDisplayTimeUs = now;

    YUVFrame yuv;
    AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
    if (fmt == AV_PIX_FMT_NV12 || fmt == AV_PIX_FMT_NV21)
        yuv = extractNV12(frame, frame->width, frame->height);
    else if (fmt == AV_PIX_FMT_YUV420P)
        yuv = extractYUV420P(frame, frame->width, frame->height);
    else {
        qWarning("Unsupported pixel format: %s, skipping frame", av_get_pix_fmt_name(fmt));
        av_frame_free(&frame);
        return;
    }

    emit frameReady(yuv);
    av_frame_free(&frame);
}

void MediaEngine::updatePosition()
{
    if (m_paused || m_startTimeUs == 0)
        return;

    double pos = (av_gettime() - m_startTimeUs - m_pausedDurationUs) * m_speed / 1000000.0;
    if (pos < 0) pos = 0;
    if (m_duration > 0 && pos > m_duration) pos = m_duration;

    if (qAbs(pos - m_position) > 0.01) {
        m_position = pos;
        emit positionChanged(m_position);
    }

    // 字幕同步：有视频流时由 onVideoRefresh() 以 10ms 粒度驱动；
    // 无视频流（纯音频+字幕）时在此以 250ms 粒度兜底。
    if (m_subtitleThread && !m_videoFrameQueue) {
        double subTime = m_syncController->audioClock();
        if (m_audioStreamIndex == -1 || subTime <= 0.0)
            subTime = pos;
        updateSubtitle(subTime);
    }
}

void MediaEngine::updateSubtitle(double clockSeconds)
{
    if (!m_subtitleThread)
        return;

    qint64 posUs = static_cast<qint64>(clockSeconds * 1000000);
    QString sub = m_subtitleThread->getSubtitleAt(posUs);
    if (sub != m_currentSubtitle) {
        m_currentSubtitle = sub;
        emit currentSubtitleChanged(m_currentSubtitle);
    }
}

void MediaEngine::startThreads()
{
    // Only allocate the audio packet queue when there is an audio stream.
    if (m_audioCodecCtx) {
        m_audioPacketQueue = new PacketQueue(64);
        m_audioFrameQueue = new FrameQueue(kAudioFrameQueueSize);
        m_audioOutput->setFrameQueue(m_audioFrameQueue);
    }

    if (m_videoStreamIndex != -1) {
        m_videoPacketQueue = new PacketQueue(64);
        m_videoFrameQueue = new FrameQueue(24);
        m_videoTimeBase = m_fmtCtx->streams[m_videoStreamIndex]->time_base;
    }

    if (m_subtitleCodecCtx) {
        m_subtitlePacketQueue = new PacketQueue(16);
        m_subtitleThread = new SubtitleDecodeThread(this);
        m_subtitleThread->setCodecContext(m_subtitleCodecCtx);
        m_subtitleThread->setPacketQueue(m_subtitlePacketQueue);
        m_subtitleThread->setTimeBase(m_fmtCtx->streams[m_subtitleStreamIndex]->time_base);
        m_subtitleThread->setPausedRef(m_paused);
    } else if (m_externalMode && !m_externalSubtitles.isEmpty()) {
        // seek 后重建：外挂字幕无 codec，需重建被动线程
        m_subtitleThread = new SubtitleDecodeThread(this);
        m_subtitleThread->setExternalSubtitles(m_externalSubtitles);
        m_subtitleThread->setPausedRef(m_paused);
    }

    m_demuxThread = new DemuxThread(this);
    m_demuxThread->setFormatContext(m_fmtCtx);
    m_demuxThread->setStreamIndices(m_videoStreamIndex, m_audioStreamIndex, m_subtitleStreamIndex);
    m_demuxThread->setPacketQueues(m_videoPacketQueue, m_audioPacketQueue, m_subtitlePacketQueue);
    m_demuxThread->setPausedRef(m_paused);
    connect(m_demuxThread, &DemuxThread::eofReached, this, [this]() {
        emit playbackFinished();
    });
    connect(m_demuxThread, &DemuxThread::errorOccurred, this, [this](const QString &msg) {
        qWarning() << "Demux error:" << msg;
    });

    if (m_videoCodecCtx) {
        m_videoThread = new VideoDecodeThread(this);
        m_videoThread->setCodecContext(m_videoCodecCtx);
        m_videoThread->setPacketQueue(m_videoPacketQueue);
        m_videoThread->setFrameQueue(m_videoFrameQueue);
        m_videoThread->setPausedRef(m_paused);

#ifdef ENABLE_HWACCEL
        if (m_useHardwareDecode && m_hwDeviceCtx) {
            m_videoThread->setHwContext(m_hwDeviceCtx, m_hwPixFmt);
        }
#endif
    }

    if (m_audioCodecCtx) {
        m_audioThread = new AudioDecodeThread(this);
        m_audioThread->setCodecContext(m_audioCodecCtx);
        m_audioThread->setPacketQueue(m_audioPacketQueue);
        m_audioThread->setFrameQueue(m_audioFrameQueue);
        m_audioThread->setPausedRef(m_paused);
        m_audioThread->setTimeBase(m_fmtCtx->streams[m_audioStreamIndex]->time_base);
        m_audioThread->setOutputSampleRate(44100);
    }

    m_demuxThread->start();
    if (m_videoThread)
        m_videoThread->start();
    if (m_audioThread)
        m_audioThread->start();
    // 外挂字幕的被动线程不启动 run()（无 codec/queue，run 会立即返回）
    if (m_subtitleThread && !m_externalMode)
        m_subtitleThread->start();
}

void MediaEngine::stopThreads()
{
    if (m_demuxThread) {
        m_demuxThread->stopRead();
        m_demuxThread->wait();
        delete m_demuxThread;
        m_demuxThread = nullptr;
    }
    if (m_videoThread) {
        m_videoThread->stopDecode();
        m_videoThread->wait();
        delete m_videoThread;
        m_videoThread = nullptr;
    }
    if (m_audioThread) {
        m_audioThread->stopDecode();
        m_audioThread->wait();
        delete m_audioThread;
        m_audioThread = nullptr;
    }
    if (m_subtitleThread) {
        m_subtitleThread->stopDecode();
        m_subtitleThread->wait();
        delete m_subtitleThread;
        m_subtitleThread = nullptr;
    }

    m_audioOutput->pause();
    m_audioOutput->reset();
    m_audioOutput->setFrameQueue(nullptr);

    delete m_videoPacketQueue; m_videoPacketQueue = nullptr;
    delete m_audioPacketQueue; m_audioPacketQueue = nullptr;
    delete m_subtitlePacketQueue; m_subtitlePacketQueue = nullptr;
    delete m_videoFrameQueue; m_videoFrameQueue = nullptr;
    delete m_audioFrameQueue; m_audioFrameQueue = nullptr;
}

void MediaEngine::cleanup()
{
    if (m_videoCodecCtx) {
        avcodec_free_context(&m_videoCodecCtx);
        m_videoCodecCtx = nullptr;
    }
    if (m_audioCodecCtx) {
        avcodec_free_context(&m_audioCodecCtx);
        m_audioCodecCtx = nullptr;
    }
    if (m_subtitleCodecCtx) {
        avcodec_free_context(&m_subtitleCodecCtx);
        m_subtitleCodecCtx = nullptr;
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
        m_fmtCtx = nullptr;
    }

#ifdef ENABLE_HWACCEL
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_hwDeviceType = AV_HWDEVICE_TYPE_NONE;
    m_useHardwareDecode = false;
#endif

    m_currentSubtitle = QString();
    emit currentSubtitleChanged(m_currentSubtitle);
    m_currentSubtitleStreamIndex = -1;
    m_subtitleStreamIndex = -1;
    m_subtitleStreamsInfo.clear();
    m_externalMode = false;
    m_externalSubtitles.clear();
    emit subtitleStreamsChanged();
}

QVariantList MediaEngine::subtitleStreams() const
{
    QVariantList list;
    int idx = 1;
    for (const auto &info : m_subtitleStreamsInfo) {
        QString label;
        if (info.isExternal) {
            label = info.label.isEmpty() ? QStringLiteral("外挂 #%1").arg(idx) : info.label;
        } else if (!info.language.isEmpty()) {
            label = info.language;
        } else if (!info.title.isEmpty()) {
            label = info.title;
        } else {
            label = QStringLiteral("Subtitle #%1").arg(idx);
        }
        list << label;
        idx++;
    }
    return list;
}

void MediaEngine::setCurrentSubtitleStream(int index)
{
    if (index == m_currentSubtitleStreamIndex)
        return;

    // -1 = 关闭字幕
    if (index < 0 || index >= m_subtitleStreamsInfo.size()) {
        m_currentSubtitle = QString();
        emit currentSubtitleChanged(m_currentSubtitle);
        if (m_subtitleThread) {
            m_subtitleThread->stopDecode();
            m_subtitleThread->wait();
            delete m_subtitleThread;
            m_subtitleThread = nullptr;
        }
        delete m_subtitlePacketQueue;
        m_subtitlePacketQueue = nullptr;
        if (m_subtitleCodecCtx) {
            avcodec_free_context(&m_subtitleCodecCtx);
            m_subtitleCodecCtx = nullptr;
        }
        m_subtitleStreamIndex = -1;
        m_currentSubtitleStreamIndex = -1;
        m_externalMode = false;
        m_externalSubtitles.clear();
        if (m_demuxThread)
            m_demuxThread->setSubtitleStreamIndex(-1);
        emit currentSubtitleStreamChanged(-1);
        return;
    }

    // 停止旧字幕线程
    m_currentSubtitle = QString();
    emit currentSubtitleChanged(m_currentSubtitle);

    if (m_subtitleThread) {
        m_subtitleThread->stopDecode();
        m_subtitleThread->wait();
        delete m_subtitleThread;
        m_subtitleThread = nullptr;
    }
    delete m_subtitlePacketQueue;
    m_subtitlePacketQueue = nullptr;

    if (m_subtitleCodecCtx) {
        avcodec_free_context(&m_subtitleCodecCtx);
        m_subtitleCodecCtx = nullptr;
    }
    m_subtitleStreamIndex = -1;
    m_externalMode = false;
    m_externalSubtitles.clear();

    // 分支：外挂字幕 vs 内嵌字幕
    if (m_subtitleStreamsInfo[index].isExternal) {
        activateExternalSubtitle(index);
        emit currentSubtitleStreamChanged(index);
        return;
    }

    // 打开新字幕流解码器
    int newStreamIdx = m_subtitleStreamsInfo[index].streamIndex;
    AVCodecParameters *subPar = m_fmtCtx->streams[newStreamIdx]->codecpar;
    const AVCodec *subCodec = avcodec_find_decoder(subPar->codec_id);
    if (!subCodec) {
        m_currentSubtitleStreamIndex = -1;
        emit currentSubtitleStreamChanged(-1);
        return;
    }

    m_subtitleCodecCtx = avcodec_alloc_context3(subCodec);
    if (!m_subtitleCodecCtx) {
        m_currentSubtitleStreamIndex = -1;
        emit currentSubtitleStreamChanged(-1);
        return;
    }

    avcodec_parameters_to_context(m_subtitleCodecCtx, subPar);
    if (avcodec_open2(m_subtitleCodecCtx, subCodec, nullptr) < 0) {
        avcodec_free_context(&m_subtitleCodecCtx);
        m_currentSubtitleStreamIndex = -1;
        emit currentSubtitleStreamChanged(-1);
        return;
    }

    m_subtitleStreamIndex = newStreamIdx;
    m_currentSubtitleStreamIndex = index;

    // 更新解复用线程的字幕流索引
    if (m_demuxThread)
        m_demuxThread->setSubtitleStreamIndex(m_subtitleStreamIndex);

    // 启动新字幕线程
    m_subtitlePacketQueue = new PacketQueue(16);
    m_subtitleThread = new SubtitleDecodeThread(this);
    m_subtitleThread->setCodecContext(m_subtitleCodecCtx);
    m_subtitleThread->setPacketQueue(m_subtitlePacketQueue);
    m_subtitleThread->setTimeBase(m_fmtCtx->streams[m_subtitleStreamIndex]->time_base);
    m_subtitleThread->setPausedRef(m_paused);
    m_subtitleThread->start();

    emit currentSubtitleStreamChanged(index);
}

void MediaEngine::detectExternalSubtitles(const QString &videoPath)
{
    if (videoPath.isEmpty())
        return;

    QFileInfo vfi(videoPath);
    QDir dir = vfi.dir();
    QString base = vfi.completeBaseName();

    // 支持的扩展名
    static const QStringList exts = { QStringLiteral("srt"), QStringLiteral("ass"), QStringLiteral("ssa") };

    // 匹配 base.ext 和 base.*.ext
    for (const QString &ext : exts) {
        QStringList filters;
        filters << (base + u'.' + ext)                  // movie.srt
                << (base + u".*." + ext);               // movie.zh.srt
        QStringList files = dir.entryList(filters, QDir::Files);
        for (const QString &fname : files) {
            QString absPath = dir.absoluteFilePath(fname);

            // 跳过重复（同一文件被多个 filter 命中）
            bool dup = false;
            for (const auto &existing : m_subtitleStreamsInfo) {
                if (existing.isExternal && existing.filePath == absPath) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;

            // 从 movie.zh.srt 提取 "zh" 作为语言标签；否则用文件名
            QString label;
            QFileInfo ffi(fname);
            QString fbase = ffi.completeBaseName();
            if (fbase.size() > base.size() && fbase.startsWith(base + u'.'))
                label = QStringLiteral("外挂: ") + fbase.mid(base.size() + 1);
            else
                label = QStringLiteral("外挂: ") + fname;

            m_subtitleStreamsInfo.append({
                -1,                 // streamIndex
                QString(),          // language
                QString(),          // title
                true,               // isExternal
                absPath,            // filePath
                label               // label
            });
            qDebug() << "[extsub] detected:" << absPath;
        }
    }
}

void MediaEngine::activateExternalSubtitle(int infoIndex)
{
    if (infoIndex < 0 || infoIndex >= m_subtitleStreamsInfo.size())
        return;
    const SubtitleStreamInfo &info = m_subtitleStreamsInfo[infoIndex];
    if (!info.isExternal)
        return;

    QList<SubtitleEntry> subs = SubtitleDecodeThread::loadFromFile(info.filePath);
    m_externalSubtitles = subs;
    m_externalMode = true;
    m_subtitleStreamIndex = -1;
    m_currentSubtitleStreamIndex = infoIndex;
    if (m_demuxThread)
        m_demuxThread->setSubtitleStreamIndex(-1);

    // 创建被动 SubtitleDecodeThread（不启动线程，仅作查询容器）
    m_subtitleThread = new SubtitleDecodeThread(this);
    m_subtitleThread->setExternalSubtitles(m_externalSubtitles);
    m_subtitleThread->setPausedRef(m_paused);

    qDebug() << "[extsub] activated:" << info.filePath
             << "entries:" << subs.size();
}

// --- 画面旋转 (UC-07) ---

void MediaEngine::setRotation(int angle)
{
    int normalized = angle % 360;
    if (normalized < 0) normalized += 360;
    if (m_rotation == normalized) return;
    m_rotation = normalized;
    emit rotationChanged(m_rotation);
}

void MediaEngine::rotateLeft()
{
    // 逆时针 90°
    setRotation(m_rotation + 90);
}

void MediaEngine::rotateRight()
{
    // 顺时针 90° = 逆时针 -90°
    setRotation(m_rotation - 90);
}

void MediaEngine::setFlipVertical(bool flip)
{
    if (m_flipVertical == flip) return;
    m_flipVertical = flip;
    emit flipVerticalChanged(m_flipVertical);
}

void MediaEngine::resetRotation()
{
    setRotation(0);
    setFlipVertical(false);
}
