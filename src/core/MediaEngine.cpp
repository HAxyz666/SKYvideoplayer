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
#include "NetworkStreamManager.h"

#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QBuffer>
#include <QtConcurrent>
#include <cstring>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
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
    , m_networkManager(new NetworkStreamManager(this))
    , m_interruptCtx(new InterruptContext())
{
    m_positionTimer->setInterval(250);
    connect(m_positionTimer, &QTimer::timeout, this, &MediaEngine::updatePosition);

    m_bufferCheckTimer = new QTimer(this);
    m_bufferCheckTimer->setInterval(500);
    connect(m_bufferCheckTimer, &QTimer::timeout, this, &MediaEngine::checkBufferState);

    connect(m_audioOutput, &AudioOutput::speedTransitionFinished,
            this, &MediaEngine::onSpeedTransitionFinished);
}

MediaEngine::~MediaEngine()
{
    stop();
    delete m_interruptCtx;
}

// 中断回调函数 - 用于中断 av_read_frame 的网络阻塞
int MediaEngine::interruptCallback(void *ctx)
{
    auto *ictx = static_cast<InterruptContext*>(ctx);
    // 如果被标记为中断，返回 1 中断 av_read_frame
    if (ictx->interrupted.load()) {
        return 1;
    }
    return 0;
}

// 设置中断回调
void MediaEngine::setupInterruptCallback()
{
    if (m_fmtCtx && m_interruptCtx) {
        m_interruptCtx->interrupted.store(false);
        m_interruptCtx->lastReadTime.start();
        m_fmtCtx->interrupt_callback.callback = interruptCallback;
        m_fmtCtx->interrupt_callback.opaque = m_interruptCtx;
    }
}

// 取指定流的 start_time 偏移（微秒）。仅取正值且不超过 1 小时（滤掉直播流/异常值）。
qint64 MediaEngine::streamStartUs(int streamIdx) const
{
    if (streamIdx < 0 || !m_fmtCtx)
        return 0;
    AVStream *st = m_fmtCtx->streams[streamIdx];
    if (!st || st->start_time == AV_NOPTS_VALUE || st->start_time <= 0)
        return 0;
    qint64 startUs = av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q);
    return (startUs > 0 && startUs < 3600LL * AV_TIME_BASE) ? startUs : 0;
}

bool MediaEngine::initFFmpeg(const QString &filename)
{
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_subtitleStreamIndex = -1;
    m_subtitleStreamsInfo.clear();
    m_currentSubtitle = QString();
    m_coverArtUrl = QString();
    m_lyrics.clear();
    m_currentLyric = QString();

    m_networkManager->reset();

    AVDictionary *options = nullptr;
    if (NetworkStreamManager::isNetworkUrl(filename)) {
        m_networkManager->buildOpenOptions(&options, filename);
    }

    int ret = avformat_open_input(&m_fmtCtx, filename.toUtf8().constData(), nullptr, &options);
    av_dict_free(&options);
    if (ret != 0) {
        char errbuf[128] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        qCritical() << "Could not open input:" << filename << errbuf;
        emit errorOccurred(QString("无法打开输入: %1").arg(errbuf), NetworkStreamManager::isNetworkUrl(filename));
        return false;
    }

    // 为网络流设置中断回调
    if (NetworkStreamManager::isNetworkUrl(filename)) {
        setupInterruptCallback();
        m_fmtCtx->max_analyze_duration = 5 * AV_TIME_BASE;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        qCritical() << "Could not find stream info";
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    if (NetworkStreamManager::isNetworkUrl(filename)) {
        m_networkManager->detectLiveStream(m_fmtCtx);
    }

    for (unsigned int i = 0; i < m_fmtCtx->nb_streams; i++) {
        AVStream *st = m_fmtCtx->streams[i];
        // 处理封面/专辑封面（attached picture）
        if (st->disposition & AV_DISPOSITION_ATTACHED_PIC) {
            AVPacket &pic = st->attached_pic;
            if (pic.size > 0 && pic.data) {
                QImage img;
                if (img.loadFromData(pic.data, pic.size)) {
                    QByteArray ba;
                    QBuffer buffer(&ba);
                    buffer.open(QIODevice::WriteOnly);
                    img.save(&buffer, "JPEG");
                    m_coverArtUrl = "data:image/jpeg;base64," + ba.toBase64();
                }
            }
            continue;
        }
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

    // 音频流 start_time 偏移（微秒）：主时钟（音频时钟）为流内原始时间戳，
    // 外挂字幕/歌词条目加载时加上该偏移对齐（与内置字幕条目同基），
    // 查询侧零换算。仅取正值且不超过 1 小时（滤掉直播流/异常值）。
    // 须在 detectExternalSubtitles/detectLyrics 之前计算。
    m_audioStartUs = streamStartUs(m_audioStreamIndex);
#ifdef QT_DEBUG
    if (m_audioStartUs > 0)
        qDebug().noquote() << "[sub] audio stream start offset (raw pts base):" << m_audioStartUs << "us";
#endif

    detectExternalSubtitles(m_filename);
    detectLyrics(m_filename);

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
            // 无内嵌字幕，仅有外挂：仅预载条目（含音频流偏移对齐），
            // 被动查询线程由 startThreads 统一创建
            m_externalMode = true;
            m_externalSubtitles = loadExternalSubtitleFile(m_subtitleStreamsInfo[0].filePath);
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
    emit isNetworkStreamChanged(m_networkManager->isNetworkStream());
    emit isLiveStreamChanged(m_networkManager->isLiveStream());

    return true;
}

void MediaEngine::start()
{
    if (!initFFmpeg(m_filename))
        return;
    startPlayback();
}

void MediaEngine::startPlayback()
{
    emit hasVideoChanged();
    emit coverArtChanged();
    emit currentLyricChanged(m_currentLyric);

    m_paused = false;
    m_audioOutputReady = false;
    m_syncController->reset();
    m_syncController->setSpeed(m_speed);

    m_position = 0.0;
    m_startTimeUs = av_gettime();
    m_pausedDurationUs = 0;
    m_pauseStartUs = 0;
    m_displaySpeed = m_speed;   // 界面显示速度跟随当前倍速

    startThreads();
    initAudioOutput();

    m_positionTimer->start();
    if (m_videoFrameQueue)
        startDisplayThread();

    // 网络流启用缓冲检测（启动后临时抑制，等待初始队列填充）
    if (m_networkManager->isNetworkStream()) {
        m_bufferState = BufferPlaying;
        m_bufferSuppressed = true;
        m_bufferCheckTimer->start();
        QTimer::singleShot(3000, this, [this]() { m_bufferSuppressed = false; });
    }
}

// 网络流初始化完成后调用，跳过 initFFmpeg
void MediaEngine::initAudioOutput()
{
    if (!m_audioCodecCtx || m_audioOutputReady)
        return;

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

void MediaEngine::stop()
{
    m_positionTimer->stop();
    stopDisplayThread();
    m_bufferCheckTimer->stop();
    m_bufferState = BufferPlaying;
    m_bufferSuppressed = false;

    // 取消进行中的网络初始化
    if (m_networkInitWatcher) {
        m_networkInitWatcher->cancel();
        m_networkInitWatcher->deleteLater();
        m_networkInitWatcher = nullptr;
    }

    // 重置加载状态，防止切换文件后遮罩残留
    if (m_isLoading) {
        m_isLoading = false;
        m_loadingText.clear();
        emit isLoadingChanged(false);
        emit loadingTextChanged(m_loadingText);
    }

    // 仅在实际播放时才报告"未播放"状态，避免首次 open() 时触发虚假的 pausedChanged。
    const bool wasPlaying = !m_paused && m_fmtCtx != nullptr;
    m_paused = true;

    stopThreads();

    // 等待后台 seek 完成，避免 use-after-free
    int waitMs = 0;
    while (m_seekInProgress.load() && waitMs < 5000) {
        QThread::msleep(10);
        waitMs += 10;
    }

    cleanup();
    m_position = 0.0;
    m_duration = 0.0;
    emit positionChanged(0.0);
    emit durationChanged(0.0);
    if (wasPlaying)
        emit pausedChanged(true);
}

bool MediaEngine::open(const QString &url, double initialSeekPos)
{
    stop();
    QString path = url;
    if (path.startsWith("file://"))
        path = path.mid(7);
    m_filename = path;

    // 网络流：异步初始化，避免阻塞主线程
    if (NetworkStreamManager::isNetworkUrl(path)) {
        // 取消旧的初始化（如果正在进行）
        if (m_networkInitWatcher) {
            m_networkInitWatcher->cancel();
            m_networkInitWatcher->deleteLater();
            m_networkInitWatcher = nullptr;
        }

        m_isLoading = true;
        m_loadingText = QStringLiteral("加载中...");
        emit isLoadingChanged(true);
        emit loadingTextChanged(m_loadingText);

        m_networkInitWatcher = new QFutureWatcher<bool>(this);
        connect(m_networkInitWatcher, &QFutureWatcher<bool>::finished,
                this, &MediaEngine::onNetworkInitFinished);
        m_networkInitWatcher->setFuture(
            QtConcurrent::run([this, path]() { return initFFmpeg(path); }));

        return true;
    }

    // 本地文件：同步初始化
    // 若有初始 seek 位置，在 startPlayback() 之前完成 seek + flush，
    // 避免先从 position 0 播放再 seek 导致的音画不同步。
    if (initialSeekPos > 0.0) {
        if (!initFFmpeg(m_filename))
            return m_fmtCtx != nullptr;

        // 有效范围检查：距离头尾太近则跳过 seek
        double dur = m_duration;
        if (dur <= 0 || initialSeekPos >= dur * 0.95) {
            startPlayback();
            return m_fmtCtx != nullptr;
        }

        int64_t targetUs = static_cast<int64_t>(initialSeekPos * AV_TIME_BASE);
        if (targetUs < 0) targetUs = 0;
        if (targetUs > static_cast<int64_t>(dur * AV_TIME_BASE))
            targetUs = static_cast<int64_t>(dur * AV_TIME_BASE);

        int ret = avformat_seek_file(m_fmtCtx, -1,
                                     INT64_MIN, targetUs, targetUs,
                                     AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
            qWarning() << "Initial seek failed:" << ret;

        if (m_videoCodecCtx)
            avcodec_flush_buffers(m_videoCodecCtx);
        if (m_audioCodecCtx)
            avcodec_flush_buffers(m_audioCodecCtx);
        if (m_subtitleCodecCtx)
            avcodec_flush_buffers(m_subtitleCodecCtx);

        startPlayback();

        // 调整时间基准，使 positionTimer 计算出正确的已 seek 位置
        m_syncController->updateAudioClock(initialSeekPos);
        qint64 now = av_gettime();
        m_startTimeUs = now - static_cast<qint64>(initialSeekPos * 1000000 / qMax(m_speed, 0.1));
        m_position = initialSeekPos;
        emit positionChanged(m_position);

        return true;
    }

    start();
    return m_fmtCtx != nullptr;
}

void MediaEngine::onNetworkInitFinished()
{
    if (!m_networkInitWatcher)
        return;

    bool success = m_networkInitWatcher->result();
    m_networkInitWatcher->deleteLater();
    m_networkInitWatcher = nullptr;

    m_isLoading = false;
    m_loadingText.clear();
    emit isLoadingChanged(false);
    emit loadingTextChanged(m_loadingText);

    if (!success) {
        emit errorOccurred("无法打开网络流", true);
        return;
    }

    emit networkStreamReady(m_filename);
    startPlayback();
}

void MediaEngine::pause()
{
    if (m_paused.exchange(true))
        return;
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
    emit pausedChanged(false);
}

void MediaEngine::togglePause()
{
    if (m_paused)
        resume();
    else
        pause();
}

void MediaEngine::seek(double seconds)
{
    if (!m_fmtCtx)
        return;

    if (m_networkManager->isLiveStream())
        return;

    m_positionTimer->stop();
    stopDisplayThread();

    // 网络流：后台 seek，避免阻塞主线程
    if (m_networkManager->isNetworkStream()) {
        m_isLoading = true;
        m_loadingText = QStringLiteral("缓冲中...");
        emit isLoadingChanged(true);
        emit loadingTextChanged(m_loadingText);

        // 更新位置显示
        m_position = seconds;
        emit positionChanged(m_position);

        // 保存需要的上下文
        bool wasPaused = m_paused;
        double speed = m_speed;

        m_seekInProgress.store(true);
        (void)QtConcurrent::run([this, seconds, wasPaused, speed]() {
            // 停止线程
            if (m_interruptCtx)
                m_interruptCtx->interrupted.store(true);
            if (m_demuxThread) {
                m_demuxThread->stopRead();
                m_demuxThread->wait(3000);
            }
            if (m_videoThread) {
                m_videoThread->stopDecode();
                m_videoThread->wait(1000);
            }
            if (m_audioThread) {
                m_audioThread->stopDecode();
                m_audioThread->wait(1000);
            }

            m_audioOutput->pause();
            m_audioOutput->reset();

            // 执行 seek
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
            if (m_subtitleCodecCtx)
                avcodec_flush_buffers(m_subtitleCodecCtx);

            // 后台工作完成，允许 stop() 继续
            m_seekInProgress.store(false);

            // 回到主线程完成后续操作
            QMetaObject::invokeMethod(this, [this, seconds, wasPaused, speed]() {
                m_syncController->reset();
                m_syncController->setSpeed(speed);
                m_syncController->updateAudioClock(seconds);

                qint64 now = av_gettime();
                m_startTimeUs = now - static_cast<qint64>(seconds * 1000000 / qMax(speed, 0.1));
                m_pausedDurationUs = 0;
                m_pauseStartUs = wasPaused ? now : 0;

                // 清理线程指针
                delete m_demuxThread; m_demuxThread = nullptr;
                delete m_videoThread; m_videoThread = nullptr;
                delete m_audioThread; m_audioThread = nullptr;
                if (m_subtitleThread) {
                    // 先唤醒退出（stopDecode 会 flush 队列，pop 立即返回）再销毁，
                    // 避免销毁仍在运行中的线程。
                    m_subtitleThread->stopDecode();
                    m_subtitleThread->wait();
                    delete m_subtitleThread;
                    m_subtitleThread = nullptr;
                }
                if (m_subtitlePacketQueue) {
                    // 旧队列退役：demux 已停止，在途 push 不再产生，
                    // 由下一次 stopThreads/cleanup 统一回收。
                    m_subtitlePacketQueue->requestQuit();
                    m_subtitlePacketQueue->flush();
                    m_subtitlePacketQueue->clear();
                    m_retiredSubtitleQueues.append(m_subtitlePacketQueue);
                    m_subtitlePacketQueue = nullptr;
                }

                // 重新启动
                startThreads();
                if (m_audioThread && !qFuzzyCompare(speed, 1.0))
                    m_audioThread->setSpeed(speed);
                if (!wasPaused)
                    m_audioOutput->resume();

    m_positionTimer->start();
    if (m_videoFrameQueue)
        startDisplayThread();

                // seek 后临时抑制缓冲检测，等待队列填充
                if (m_networkManager->isNetworkStream()) {
                    m_bufferState = BufferPlaying;
                    m_bufferSuppressed = true;
                    m_bufferCheckTimer->start();
                    // 3 秒后解除抑制
                    QTimer::singleShot(3000, this, [this]() { m_bufferSuppressed = false; });
                }

                m_isLoading = false;
                m_loadingText.clear();
                emit isLoadingChanged(false);
                emit loadingTextChanged(m_loadingText);
            });
        });
        return;
    }

    // 本地文件：同步 seek
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
    m_currentLyric = QString();
    emit currentLyricChanged(m_currentLyric);

    m_syncController->reset();
    m_syncController->setSpeed(m_speed);
    m_syncController->updateAudioClock(seconds);

    qint64 now = av_gettime();
    m_startTimeUs = now - static_cast<qint64>(seconds * 1000000 / qMax(m_speed, 0.1));
    m_pausedDurationUs = 0;
    m_pauseStartUs = m_paused ? now : 0;
    m_position = seconds;
    emit positionChanged(m_position);

    startThreads();

    if (m_audioThread && !qFuzzyCompare(m_speed, 1.0))
        m_audioThread->setSpeed(m_speed);

    if (!m_paused)
        m_audioOutput->resume();

    m_positionTimer->start();
    if (m_videoFrameQueue)
        startDisplayThread();
}

double MediaEngine::position() const
{
    return m_position;
}

double MediaEngine::duration() const
{
    return m_duration;
}

bool MediaEngine::isNetworkStream() const
{
    return m_networkManager && m_networkManager->isNetworkStream();
}

bool MediaEngine::isLiveStream() const
{
    return m_networkManager && m_networkManager->isLiveStream();
}

// --- 速度控制 ---

void MediaEngine::setSpeed(double speed)
{
    // 匹配音频 sonic/atempo 的工作范围，保持音画同步。
    speed = qBound(0.5, speed, 2.0);
    if (qFuzzyCompare(m_speed, speed))
        return;
    double oldSpeed = m_speed;
    m_speed = speed;

    // 不断音变速：不 seek、不清任何缓冲。
    // 1) 音频解码线程先切换 sonic 速度（新解码帧按新速度变速）；
    // 2) AudioOutput 快照"旧速度预缓冲"（FIFO+帧队列），时钟折算按
    //    旧速度消耗旧数据、耗尽后按新速度，时钟全程连续不冻结；
    // 3) AVSyncController 从旧速率平滑收敛到新速率，视频 delay 折算
    //    跟随音频实际推进节奏，画面既不冻结也不快进跳变。
    if (m_audioThread)
        m_audioThread->setSpeed(speed);
    if (m_audioOutput)
        m_audioOutput->setSpeed(speed);
    if (m_syncController)
        m_syncController->setSpeed(speed);

    // 界面位置：内容在旧缓冲耗尽前仍按旧节奏前进，显示速度先保持旧值，
    // 重锚定到当前听感位置；AudioOutput 检测到旧数据耗尽时切为新速度。
    if (m_startTimeUs != 0) {
        double contentPos = m_syncController ? m_syncController->audioClock() : 0.0;
        if (!(contentPos > 0.0))   // 音频时钟不可用（刚开始播放）时退回界面位置
            contentPos = m_position;
        m_displaySpeed = oldSpeed;
        m_startTimeUs = av_gettime()
            - static_cast<qint64>(contentPos * 1000000.0 / qMax(oldSpeed, 0.1));
        m_pausedDurationUs = 0;
        if (m_paused)
            m_pauseStartUs = av_gettime();
    }

    emit speedChanged(m_speed);
}

void MediaEngine::onSpeedTransitionFinished()
{
    // 旧速度预缓冲已全部消费：管线内只剩新速度数据，播放节奏已完全切换。
    if (m_displaySpeed == m_speed || m_startTimeUs == 0)
        return;
    m_displaySpeed = m_speed;

    // 重新锚定到听感位置（内容已按新速度推进），界面继续以新速度前进。
    double contentPos = m_syncController ? m_syncController->audioClock() : 0.0;
    if (!(contentPos > 0.0))
        contentPos = m_position;
    m_startTimeUs = av_gettime()
        - static_cast<qint64>(contentPos * 1000000.0 / qMax(m_speed, 0.1));
    m_pausedDurationUs = 0;
    if (m_paused)
        m_pauseStartUs = av_gettime();
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

void MediaEngine::updatePosition()
{
    if (m_paused || m_startTimeUs == 0)
        return;

    double pos = (av_gettime() - m_startTimeUs - m_pausedDurationUs) * m_displaySpeed / 1000000.0;
    if (pos < 0) pos = 0;
    if (m_duration > 0 && pos > m_duration) pos = m_duration;

    if (qAbs(pos - m_position) > 0.01) {
        m_position = pos;
        emit positionChanged(m_position);
    }

    // 字幕/歌词同步：以主时钟（音频时钟）驱动，字幕与听感内容严格一致。
    // 音频时钟为流内原始时间戳，内置字幕条目同基；外挂/歌词条目加载时已按
    // 音频流 start_time 偏移对齐，因此查询侧零换算。
    // 无音频流（或时钟尚未启动）时退回 0 基墙钟位置 m_position。
    double mainClock = m_syncController->audioClock();
    if (m_audioStreamIndex == -1 || mainClock <= 0.0)
        mainClock = pos;
    updateSubtitle(mainClock);
    updateLyric(mainClock);
}

void MediaEngine::checkBufferState()
{
    if (m_paused.load() || m_startTimeUs == 0)
        return;

    if (m_bufferSuppressed)
        return;

    bool videoFrameLow = (m_videoFrameQueue != nullptr && m_videoFrameQueue->size() < 3);
    bool audioFrameLow = (m_audioFrameQueue != nullptr && m_audioFrameQueue->size() < 5);
    bool hasVideo = (m_videoFrameQueue != nullptr);
    bool hasAudio = (m_audioFrameQueue != nullptr);

    bool low = hasVideo ? videoFrameLow : (hasAudio && audioFrameLow);

    if (low) {
        if (m_bufferState != BufferBuffering) {
            m_bufferState = BufferBuffering;
            emit bufferStateChanged(static_cast<int>(m_bufferState));
        }
    } else {
        if (m_bufferState != BufferPlaying) {
            m_bufferState = BufferPlaying;
            emit bufferStateChanged(static_cast<int>(m_bufferState));
        }
    }
}

void MediaEngine::updateSubtitle(double clockSeconds)
{
    if (!m_subtitleThread)
        return;

    // clockSeconds 已由调用方统一换算为 0 基内容时间：
    // 内嵌/外挂字幕条目同为 0 基，查询无需区分模式。
    qint64 posUs = static_cast<qint64>(clockSeconds * 1000000);
    QString sub = m_subtitleThread->getSubtitleAt(posUs);
    if (sub != m_currentSubtitle) {
        m_currentSubtitle = sub;
#ifdef QT_DEBUG
        qDebug().noquote() << QStringLiteral("[sub] change clock=%1s posUs=%2us -> \"%3\"")
            .arg(clockSeconds, 0, 'f', 3)
            .arg(posUs)
            .arg(sub.left(24));
#endif
        emit currentSubtitleChanged(m_currentSubtitle);
    }
}

void MediaEngine::updateLyric(double clockSeconds)
{
    if (m_lyrics.isEmpty())
        return;

    qint64 posUs = static_cast<qint64>(clockSeconds * 1000000);
    QString lyric;
    // 找到最后一个 startUs <= posUs 的条目
    for (const auto &entry : m_lyrics) {
        if (posUs >= entry.startUs && posUs < entry.endUs) {
            lyric = entry.text;
            break;
        }
    }
    if (lyric != m_currentLyric) {
        m_currentLyric = lyric;
        emit currentLyricChanged(m_currentLyric);
    }
}

// 加载外挂字幕文件，并将条目偏移对齐到音频流时间轴：
// 主时钟（音频时钟）为流内原始时间戳，外挂文件为 0 基文件时间，
// 加载时加上音频流 start_time 偏移，使条目与内置字幕（原始流时间）同基。
QList<SubtitleEntry> MediaEngine::loadExternalSubtitleFile(const QString &path) const
{
    QList<SubtitleEntry> subs = SubtitleDecodeThread::loadFromFile(path);
    if (m_audioStartUs > 0) {
        for (SubtitleEntry &e : subs) {
            e.startUs += m_audioStartUs;
            if (e.endUs != INT64_MAX)
                e.endUs += m_audioStartUs;
        }
    }
    return subs;
}

void MediaEngine::detectLyrics(const QString &audioPath)
{
    if (audioPath.isEmpty())
        return;

    QFileInfo fi(audioPath);
    QDir dir = fi.dir();
    QString base = fi.completeBaseName();

    // 查找同名 .lrc 文件：base.lrc
    QString lrcPath = dir.absoluteFilePath(base + u".lrc");
    if (QFileInfo::exists(lrcPath)) {
        QList<SubtitleEntry> lyrics = SubtitleDecodeThread::loadLrc(lrcPath);
        // 与主时钟同基：加上音频流 start_time 偏移
        if (m_audioStartUs > 0) {
            for (SubtitleEntry &e : lyrics) {
                e.startUs += m_audioStartUs;
                if (e.endUs != INT64_MAX)
                    e.endUs += m_audioStartUs;
            }
        }
        if (!lyrics.isEmpty()) {
            m_lyrics = lyrics;
            qDebug() << "[lyric] loaded:" << lrcPath;
        }
    }
}

void MediaEngine::startThreads()
{
    // 仅在有音频流时才分配音频包队列。
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
        emit errorOccurred(msg, true);
    });

    // 重置中断标志
    if (m_interruptCtx) {
        m_interruptCtx->interrupted.store(false);
        m_interruptCtx->lastReadTime.start();
    }

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
    // 先停止显示线程，它在读取 FrameQueue
    stopDisplayThread();

    if (m_demuxThread) {
        // 设置中断标志，让 av_read_frame 立即返回
        if (m_interruptCtx) {
            m_interruptCtx->interrupted.store(true);
        }
        m_demuxThread->stopRead();
        // 等待线程自然退出，不再使用 terminate()
        if (!m_demuxThread->wait(5000)) {
            qWarning() << "DemuxThread did not exit in time after interrupt";
        }
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

    // 请求所有队列退出，唤醒阻塞的线程
    if (m_videoPacketQueue) m_videoPacketQueue->requestQuit();
    if (m_audioPacketQueue) m_audioPacketQueue->requestQuit();
    if (m_subtitlePacketQueue) m_subtitlePacketQueue->requestQuit();
    if (m_videoFrameQueue) m_videoFrameQueue->requestQuit();
    if (m_audioFrameQueue) m_audioFrameQueue->requestQuit();

    delete m_videoPacketQueue; m_videoPacketQueue = nullptr;
    delete m_audioPacketQueue; m_audioPacketQueue = nullptr;
    delete m_subtitlePacketQueue; m_subtitlePacketQueue = nullptr;
    delete m_videoFrameQueue; m_videoFrameQueue = nullptr;
    delete m_audioFrameQueue; m_audioFrameQueue = nullptr;

    // demux 已退出，可安全回收播放中切换字幕流时退役的队列
    qDeleteAll(m_retiredSubtitleQueues);
    m_retiredSubtitleQueues.clear();
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
    m_useHardwareDecode = false;
#endif

    if (!m_coverArtUrl.isEmpty()) {
        m_coverArtUrl = QString();
        emit coverArtChanged();
    }

    if (!m_currentLyric.isEmpty()) {
        m_currentLyric = QString();
        emit currentLyricChanged(m_currentLyric);
    }
    m_lyrics.clear();

    m_currentSubtitle = QString();
    emit currentSubtitleChanged(m_currentSubtitle);
    m_currentSubtitleStreamIndex = -1;
    m_subtitleStreamIndex = -1;
    m_subtitleStreamsInfo.clear();
    m_externalMode = false;
    m_externalSubtitles.clear();
    qDeleteAll(m_retiredSubtitleQueues);
    m_retiredSubtitleQueues.clear();
    emit subtitleStreamsChanged();

    if (m_networkManager->isNetworkStream() || m_networkManager->isLiveStream()) {
        m_networkManager->reset();
        emit isNetworkStreamChanged(false);
        emit isLiveStreamChanged(false);
    }
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

void MediaEngine::stopSubtitleThread()
{
    // 先切断 demux 线程的字幕推送（索引与队列指针），
    // 避免其把字幕包 push 到正在销毁的队列（use-after-free）。
    if (m_demuxThread) {
        m_demuxThread->setSubtitleStreamIndex(-1);
        m_demuxThread->clearSubtitleQueue();
    }

    if (m_subtitleThread) {
        m_subtitleThread->stopDecode();
        m_subtitleThread->wait();
        delete m_subtitleThread;
        m_subtitleThread = nullptr;
    }

    // 队列延迟销毁：demux 线程可能仍有在途 push，先退役，
    // 待 demux 退出（stopThreads/cleanup）后再统一回收。
    if (m_subtitlePacketQueue) {
        m_subtitlePacketQueue->requestQuit();
        m_subtitlePacketQueue->flush();
        m_subtitlePacketQueue->clear();
        m_retiredSubtitleQueues.append(m_subtitlePacketQueue);
        m_subtitlePacketQueue = nullptr;
    }
    if (m_subtitleCodecCtx) {
        avcodec_free_context(&m_subtitleCodecCtx);
        m_subtitleCodecCtx = nullptr;
    }
    m_subtitleStreamIndex = -1;
    m_externalMode = false;
    m_externalSubtitles.clear();
}

void MediaEngine::setCurrentSubtitleStream(int index)
{
    if (index == m_currentSubtitleStreamIndex)
        return;

    // -1 = 关闭字幕
    if (index < 0 || index >= m_subtitleStreamsInfo.size()) {
        m_currentSubtitle = QString();
        emit currentSubtitleChanged(m_currentSubtitle);
        stopSubtitleThread();
        m_currentSubtitleStreamIndex = -1;
        if (m_demuxThread)
            m_demuxThread->setSubtitleStreamIndex(-1);
        emit currentSubtitleStreamChanged(-1);
        return;
    }

    // 停止旧字幕线程
    m_currentSubtitle = QString();
    emit currentSubtitleChanged(m_currentSubtitle);
    stopSubtitleThread();

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

    // 启动新字幕线程
    m_subtitlePacketQueue = new PacketQueue(16);
    m_subtitleThread = new SubtitleDecodeThread(this);
    m_subtitleThread->setCodecContext(m_subtitleCodecCtx);
    m_subtitleThread->setPacketQueue(m_subtitlePacketQueue);
    m_subtitleThread->setTimeBase(m_fmtCtx->streams[m_subtitleStreamIndex]->time_base);
    m_subtitleThread->setPausedRef(m_paused);

    // 先让 demux 指向新队列，再切换字幕流索引，
    // 确保字幕包进入新队列而非已退役的旧队列。
    if (m_demuxThread) {
        m_demuxThread->setSubtitleQueue(m_subtitlePacketQueue);
        m_demuxThread->setSubtitleStreamIndex(m_subtitleStreamIndex);
    }
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

    QList<SubtitleEntry> subs = loadExternalSubtitleFile(info.filePath);
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

// --- 视频显示线程：精确休眠，替代 QTimer 轮询 ---

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

static AVFrame *convertToYUV420P(AVFrame *src)
{
    AVFrame *dst = av_frame_alloc();
    dst->format = AV_PIX_FMT_YUV420P;
    dst->width = src->width;
    dst->height = src->height;
    if (av_frame_get_buffer(dst, 32) < 0) {
        av_frame_free(&dst);
        return nullptr;
    }
    struct SwsContext *sws = sws_getContext(
        src->width, src->height, static_cast<AVPixelFormat>(src->format),
        dst->width, dst->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        av_frame_free(&dst);
        return nullptr;
    }
    sws_scale(sws, src->data, src->linesize, 0, src->height, dst->data, dst->linesize);
    sws_freeContext(sws);
    return dst;
}

void MediaEngine::startDisplayThread()
{
    m_displayStopRequested.store(false, std::memory_order_release);
    delete m_displayThread;
    m_displayThread = QThread::create([this]() { displayLoop(); });
    connect(m_displayThread, &QThread::finished, m_displayThread, &QObject::deleteLater);
    m_displayThread->start();
}

void MediaEngine::stopDisplayThread()
{
    m_displayStopRequested.store(true, std::memory_order_release);
    if (m_displayThread) {
        m_displayThread->quit();
        m_displayThread->wait(2000);
        m_displayThread = nullptr;
    }
}

void MediaEngine::displayLoop()
{
    while (!m_displayStopRequested.load(std::memory_order_acquire)) {
        if (m_paused.load(std::memory_order_acquire)) {
            QThread::msleep(10);
            continue;
        }

        if (!m_videoFrameQueue || !m_syncController) {
            QThread::msleep(10);
            continue;
        }

        AVFrame *frame = m_videoFrameQueue->peek();
        if (!frame) {
            QThread::msleep(1);
            continue;
        }

        double pts = 0.0;
        if (frame->pts != AV_NOPTS_VALUE)
            pts = frame->pts * av_q2d(m_videoTimeBase);

        // 使用新的 computeFrameSync 接口获取同步动作
        SyncResult sync = m_syncController->computeFrameSync(pts);

        // 计算 A-V diff 用于统计
        double audioClock = m_syncController->audioClock();
        double avDiff = pts - audioClock;
        m_syncController->recordSyncEvent(sync.action, avDiff);

        switch (sync.action) {
        case SyncAction::SkipFrame:
            // 丢弃当前帧
            frame = m_videoFrameQueue->tryPop(0);
            if (frame) {
                m_syncController->onFrameDisplayed(pts);
                av_frame_free(&frame);
            }
            continue;  // 不显示，继续下一帧

        case SyncAction::RepeatFrame:
            // 等待后重复显示上一帧（不取新帧）
            if (sync.delay > 0.0)
                av_usleep(static_cast<qint64>(sync.delay * 1000000.0));
            // 继续循环，重新 peek 同一帧
            continue;

        case SyncAction::DelayFrame:
        case SyncAction::ShowNormal:
        default:
            if (sync.delay > 0.0)
                av_usleep(static_cast<qint64>(sync.delay * 1000000.0));
            break;
        }

        if (m_displayStopRequested.load(std::memory_order_acquire))
            break;
        if (m_paused.load(std::memory_order_acquire))
            continue;

        frame = m_videoFrameQueue->tryPop(0);
        if (!frame)
            continue;

        m_syncController->onFrameDisplayed(pts);

        // 帧率检测：每2秒更新一次
        if (m_lastDisplayedPts >= 0.0 && pts > m_lastDisplayedPts) {
            m_fpsFrameCount++;
            if (m_fpsTimer.isValid()) {
                double elapsed = m_fpsTimer.elapsed() / 1000.0;
                if (elapsed >= 2.0) {
                    double fps = m_fpsFrameCount / elapsed;
                    m_syncController->updateFrameRate(fps);
                    m_fpsFrameCount = 0;
                    m_fpsTimer.restart();
                }
            } else {
                m_fpsTimer.start();
            }
        }
        m_lastDisplayedPts = pts;

        // 字幕/歌词不在此驱动：统一由 updatePosition（主时钟/音频时钟）
        // 以 250ms 粒度更新，保证字幕与听感内容一致，
        // 避免视频轨容器级延迟导致字幕提前出现。
        YUVFrame yuv;
        AVFrame *convFrame = nullptr;
        AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
        if (fmt == AV_PIX_FMT_YUV420P) {
            yuv = extractYUV420P(frame, frame->width, frame->height);
        } else if (fmt == AV_PIX_FMT_NV12) {
            yuv = extractNV12(frame, frame->width, frame->height);
        } else {
            convFrame = convertToYUV420P(frame);
            if (!convFrame) {
                qWarning("displayLoop: sws_scale conversion failed for: %s",
                         av_get_pix_fmt_name(fmt));
                av_frame_free(&frame);
                continue;
            }
            yuv = extractYUV420P(convFrame, convFrame->width, convFrame->height);
        }

        emit frameReady(yuv);
        av_frame_free(&frame);
        av_frame_free(&convFrame);
    }
}
