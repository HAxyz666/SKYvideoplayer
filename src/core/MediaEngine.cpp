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
#include <QImage>
#include <QtConcurrent>
#include <cstring>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
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

    // EOF 排空门控：demux 报 EOF 时解码线程已消费完所有包，但解码帧队列与
    // 音频 FIFO 中仍缓冲着最后约 1 秒内容，须等其全部播完再发 playbackFinished，
    // 否则短视频结尾被直接跳过（切下一集/循环时丢尾）。
    m_eofDrainTimer = new QTimer(this);
    m_eofDrainTimer->setInterval(50);
    connect(m_eofDrainTimer, &QTimer::timeout, this, [this]() {
        // 视频：帧队列空且解码线程已退出（退出前至多再推 1 帧）；
        // 音频：帧队列空、FIFO 空且解码线程已退出。
        bool videoDone = !m_videoThread
            || (m_videoFrameQueue->size() == 0 && !m_videoThread->isRunning());
        bool audioDone = !m_audioThread
            || (m_audioFrameQueue->size() == 0 && m_audioOutput->fifoEmpty()
                && !m_audioThread->isRunning());
        if (videoDone && audioDone) {
            m_eofDrainTimer->stop();
            // A-B 循环激活：到达文件末尾不结束播放，跳回 A 点继续循环。
            // seek 保持暂停态；播放中则 seek 内部已恢复音频输出。
            if (m_abLoopState == Looping) {
                seek(m_abLoopA);
                return;
            }
            emit playbackFinished();
        }
    });

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

// 设置中断回调（split 模式须对视频/音频两个 context 分别设置）
void MediaEngine::setupInterruptCallback(AVFormatContext *ctx)
{
    if (ctx && m_interruptCtx) {
        m_interruptCtx->interrupted.store(false);
        m_interruptCtx->lastReadTime.start();
        ctx->interrupt_callback.callback = interruptCallback;
        ctx->interrupt_callback.opaque = m_interruptCtx;
    }
}

// 取指定 context 中流的 start_time 偏移（微秒）。仅取正值且不超过 1 小时（滤掉直播流/异常值）。
qint64 MediaEngine::streamStartUs(AVFormatContext *ctx, int streamIdx) const
{
    if (streamIdx < 0 || !ctx)
        return 0;
    AVStream *st = ctx->streams[streamIdx];
    if (!st || st->start_time == AV_NOPTS_VALUE || st->start_time <= 0)
        return 0;
    qint64 startUs = av_rescale_q(st->start_time, st->time_base, AV_TIME_BASE_Q);
    return (startUs > 0 && startUs < 3600LL * AV_TIME_BASE) ? startUs : 0;
}

// 取指定 context 中流的 start_time 偏移（秒），同 streamStartUs 的过滤规则。
double MediaEngine::streamStartSeconds(AVFormatContext *ctx, int streamIdx) const
{
    return streamStartUs(ctx, streamIdx) / (double)AV_TIME_BASE;
}

// 取流的语言/标题元数据（缺失时为空串）
static std::pair<QString, QString> streamLangTitle(AVStream *st)
{
    AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", nullptr, 0);
    AVDictionaryEntry *title = av_dict_get(st->metadata, "title", nullptr, 0);
    return {lang ? QString::fromUtf8(lang->value) : QString(),
            title ? QString::fromUtf8(title->value) : QString()};
}

bool MediaEngine::initFFmpeg(const MediaSource &src)
{
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_subtitleStreamIndex = -1;
    m_subtitleStreamsInfo.clear();
    m_currentSubtitle = QString();
    m_coverArtUrl = QString();
    m_lyrics.clear();
    m_currentLyric = QString();

    // 新文件：重置 A-B 循环状态
    resetABLoop();

    m_networkManager->reset();
    m_splitMode = src.isSplit();
    m_splitEofCount = 0;

    const QString filename = src.url;
    const bool isNetwork = NetworkStreamManager::isNetworkUrl(filename)
        || (!src.audioUrl.isEmpty() && NetworkStreamManager::isNetworkUrl(src.audioUrl));

    // ---- 打开主（视频/合并）context ----
    AVDictionary *options = nullptr;
    if (NetworkStreamManager::isNetworkUrl(filename))
        m_networkManager->buildOpenOptions(&options, filename, src.headers);
    int ret = avformat_open_input(&m_fmtCtx, filename.toUtf8().constData(), nullptr, &options);
    av_dict_free(&options);
    if (ret != 0) {
        char errbuf[128] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        qCritical() << "Could not open input:" << filename << errbuf;
        emit errorOccurred(QString("无法打开输入: %1").arg(errbuf), isNetwork);
        return false;
    }

    // 为网络流设置中断回调
    if (NetworkStreamManager::isNetworkUrl(filename)) {
        setupInterruptCallback(m_fmtCtx);
        m_fmtCtx->max_analyze_duration = 5 * AV_TIME_BASE;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        qCritical() << "Could not find stream info";
        avformat_close_input(&m_fmtCtx);
        m_fmtCtx = nullptr;
        return false;
    }

    if (NetworkStreamManager::isNetworkUrl(filename))
        m_networkManager->detectLiveStream(m_fmtCtx);

    // ---- split 模式：打开音频独立 context ----
    if (m_splitMode) {
        AVDictionary *aopts = nullptr;
        m_networkManager->buildOpenOptions(&aopts, src.audioUrl, src.audioHeaders);
        ret = avformat_open_input(&m_audioFmtCtx, src.audioUrl.toUtf8().constData(), nullptr, &aopts);
        av_dict_free(&aopts);
        if (ret != 0) {
            char errbuf[128] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            qCritical() << "Could not open audio input:" << src.audioUrl << errbuf;
            avformat_close_input(&m_fmtCtx);
            m_fmtCtx = nullptr;
            emit errorOccurred(QString("无法打开音频流: %1").arg(errbuf), true);
            return false;
        }
        setupInterruptCallback(m_audioFmtCtx);
        m_audioFmtCtx->max_analyze_duration = 5 * AV_TIME_BASE;
        if (avformat_find_stream_info(m_audioFmtCtx, nullptr) < 0) {
            qCritical() << "Could not find audio stream info";
            avformat_close_input(&m_fmtCtx);
            m_fmtCtx = nullptr;
            avformat_close_input(&m_audioFmtCtx);
            m_audioFmtCtx = nullptr;
            return false;
        }
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
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // split 模式下音频取自独立 context，主 context 的音频流忽略
            if (!m_splitMode) {
                const auto [lang, title] = streamLangTitle(st);
                m_audioStreamsInfo.append({
                    static_cast<int>(i),
                    lang,
                    title,
                    st->codecpar->ch_layout.nb_channels,
                    st->codecpar->sample_rate
                });
            }
        } else if (st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            const auto [lang, title] = streamLangTitle(st);
            m_subtitleStreamsInfo.append({
                static_cast<int>(i),
                lang,
                title
            });
        }
    }

    // split 模式：音频流从独立音频 context 枚举
    if (m_splitMode) {
        AVFormatContext *actx = audioFmtCtx();
        for (unsigned int i = 0; i < actx->nb_streams; i++) {
            AVStream *st = actx->streams[i];
            if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                continue;
            const auto [lang, title] = streamLangTitle(st);
            m_audioStreamsInfo.append({
                static_cast<int>(i),
                lang,
                title,
                st->codecpar->ch_layout.nb_channels,
                st->codecpar->sample_rate
            });
        }
    }

    // 选择默认音轨：优先带 DEFAULT 标志的，否则取第一条。
    m_currentAudioStreamIndex = -1;
    m_audioStreamIndex = -1;

    if (!m_audioStreamsInfo.isEmpty()) {
        AVFormatContext *actx = audioFmtCtx();
        for (int i = 0; i < m_audioStreamsInfo.size(); ++i) {
            if (actx->streams[m_audioStreamsInfo[i].streamIndex]->disposition & AV_DISPOSITION_DEFAULT) {
                m_currentAudioStreamIndex = i;
                break;
            }
        }
        if (m_currentAudioStreamIndex < 0)
            m_currentAudioStreamIndex = 0;
        m_audioStreamIndex = m_audioStreamsInfo[m_currentAudioStreamIndex].streamIndex;
        qDebug() << "[audio] default track:" << m_currentAudioStreamIndex
                 << "stream" << m_audioStreamIndex
                 << "count" << m_audioStreamsInfo.size();
    }

    // 音频流 start_time 偏移（微秒）：主时钟（音频时钟）为流内原始时间戳，
    // 外挂字幕/歌词条目加载时加上该偏移对齐（与内置字幕条目同基），
    // 查询侧零换算。仅取正值且不超过 1 小时（滤掉直播流/异常值）。
    // 须在 detectExternalSubtitles/detectLyrics 之前计算。
    m_audioStartUs = streamStartUs(audioFmtCtx(), m_audioStreamIndex);
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
        m_audioCodecCtx = openAudioCodecForStream(m_audioStreamIndex);
        if (!m_audioCodecCtx)
            m_audioStreamIndex = -1;
    }

    if (!m_audioCodecCtx)
        m_currentAudioStreamIndex = -1;

    // 初始化字幕解码器（选择默认或第一个字幕流）。
    // split 模式（DASH 分离流）v1 不支持内嵌字幕解码，保持关闭。
    m_currentSubtitleStreamIndex = -1;
    m_subtitleStreamIndex = -1;
    if (!m_splitMode && !m_subtitleStreamsInfo.isEmpty()) {
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
    emit audioStreamsChanged();

    av_dump_format(m_fmtCtx, 0, filename.toUtf8().constData(), 0);

    m_duration = m_fmtCtx->duration != AV_NOPTS_VALUE
        ? m_fmtCtx->duration / (double)AV_TIME_BASE
        : 0.0;

    emit durationChanged(m_duration);
    emit isNetworkStreamChanged(m_networkManager->isNetworkStream());
    emit isLiveStreamChanged(m_networkManager->isLiveStream());

    return true;
}

void MediaEngine::start(const MediaSource &src)
{
    if (!initFFmpeg(src))
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
    // 新文件倍速重置为 1x：界面显示与实际播放保持一致
    // （新建的音频解码线程默认 1x，若沿用旧 m_speed 会出现"显示旧倍速、
    // 实际 1x 播放"的不一致）。
    if (!qFuzzyCompare(m_speed, 1.0)) {
        m_speed = 1.0;
        m_displaySpeed = 1.0;
        emit speedChanged(m_speed);
    }
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
    // 使在途网络 seek 的重启回调作废：它若在 stop 之后执行会重新创建管线
    // （旧 fmtCtx/队列已清理），造成状态错乱。
    ++m_seekGeneration;

    // 先等待后台 seek 完成再拆卸线程：后台任务期间仍持有线程对象指针
    // （stopRead/stopDecode），并发删除会造成 use-after-free。
    int waitMs = 0;
    while (m_seekInProgress.load() && waitMs < 5000) {
        QThread::msleep(10);
        waitMs += 10;
    }

    m_positionTimer->stop();
    m_eofDrainTimer->stop();
    stopDisplayThread();
    m_bufferCheckTimer->stop();
    m_bufferState = BufferPlaying;
    m_bufferSuppressed = false;
    m_liveStalling = false;
    m_liveReopenPending = false;

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

    cleanup();
    m_position = 0.0;
    m_duration = 0.0;
    emit positionChanged(0.0);
    emit durationChanged(0.0);
    if (wasPlaying)
        emit pausedChanged(true);
}

void MediaEngine::setLoadingState(bool loading, const QString &text)
{
    if (m_isLoading == loading && m_loadingText == text)
        return;
    m_isLoading = loading;
    m_loadingText = text;
    emit isLoadingChanged(m_isLoading);
    emit loadingTextChanged(m_loadingText);
}

bool MediaEngine::open(const QString &url, double initialSeekPos)
{
    return openSource({url, {}, {}, {}}, initialSeekPos);
}

bool MediaEngine::openWithHeaders(const QString &url, const QVariantMap &headers,
                                  double initialSeekPos)
{
    return openSource({url, headers, {}, {}}, initialSeekPos);
}

bool MediaEngine::openSplit(const QString &videoUrl, const QString &audioUrl,
                            const QVariantMap &videoHeaders, const QVariantMap &audioHeaders,
                            double initialSeekPos)
{
    return openSource({videoUrl, videoHeaders, audioUrl, audioHeaders}, initialSeekPos);
}

bool MediaEngine::openSource(const MediaSource &src, double initialSeekPos)
{
    stop();
    m_activeSource = src;
    QString path = src.url;
    if (path.startsWith("file://"))
        path = path.mid(7);
    m_filename = path;

    const bool isNetwork = NetworkStreamManager::isNetworkUrl(path)
        || (!src.audioUrl.isEmpty() && NetworkStreamManager::isNetworkUrl(src.audioUrl));

    // 网络流：异步初始化，避免阻塞主线程
    if (isNetwork) {
        m_isLoading = true;
        m_loadingText = QStringLiteral("加载中...");
        emit isLoadingChanged(true);
        emit loadingTextChanged(m_loadingText);

        m_networkInitWatcher = new QFutureWatcher<bool>(this);
        connect(m_networkInitWatcher, &QFutureWatcher<bool>::finished,
                this, &MediaEngine::onNetworkInitFinished);
        m_networkInitWatcher->setFuture(
            QtConcurrent::run([this, src]() { return initFFmpeg(src); }));

        return true;
    }

    // 本地文件：同步初始化
    // 若有初始 seek 位置，在 startPlayback() 之前完成 seek + flush，
    // 避免先从 position 0 播放再 seek 导致的音画不同步。
    if (initialSeekPos > 0.0) {
        if (!initFFmpeg(src))
            return m_fmtCtx != nullptr;

        // 有效范围检查：距离头尾太近则跳过 seek
        double dur = m_duration;
        if (dur <= 0 || initialSeekPos >= dur * 0.95) {
            startPlayback();
            return m_fmtCtx != nullptr;
        }

        int64_t targetUs = clampSeekTarget(initialSeekPos);
        performSeekAndFlush(targetUs);

        // 初始 seek 同样丢弃目标位置之前的旧画面帧（backward seek 落在上一关键帧）。
        // 须在 startPlayback（内部 startThreads 创建视频线程）之前设置。
        prepareSeekDropBefore(initialSeekPos);
        // 音频同丢弃目标位置之前的帧（backward seek 从上一关键帧起读，音频包
        // 也从更早位置开始）：不重基（锚点 -1，音频按自身原始 PTS 起播），
        // 只丢弃 initialSeekPos 之前的音频内容。阈值直接用 initialSeekPos：
        // 保存位置 = 音频时钟域 = 音频流时间，无需再加流起始偏移
        // （视频侧阈值经 prepareSeekDropBefore 换算，两者落在同一内容点）。
        m_pendingAudioPtsAnchor = -1.0;
        m_pendingAudioPtsDropBefore = initialSeekPos;

        startPlayback();

        // 调整时间基准，使 positionTimer 计算出正确的已 seek 位置
        m_syncController->setClock(initialSeekPos);
        qint64 now = av_gettime();
        m_startTimeUs = now - static_cast<qint64>(initialSeekPos * 1000000 / qMax(m_speed, 0.1));
        m_position = initialSeekPos;
        emit positionChanged(m_position);

        return true;
    }

    start(src);
    return m_fmtCtx != nullptr;
}

// 致命错误统一出口：先停整个管线（音频/线程），再发 fatalErrorOccurred，
// 避免 UI 已回主菜单但音频设备/解码线程仍在运行（直播断流后残留播放）。
void MediaEngine::handleFatalError(const QString &message)
{
    if (m_fmtCtx || m_demuxThread || m_audioThread || m_videoThread)
        stop();
    emit fatalErrorOccurred(message, true);
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
        m_liveReopenPending = false;
        handleFatalError("无法打开网络流");
        return;
    }

    m_liveReopenPending = false;
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
    const qint64 pauseDurationUs = av_gettime() - m_pauseStartUs;
    m_audioOutput->resume();
    m_pausedDurationUs += pauseDurationUs;
    m_pauseStartUs = 0;
    emit pausedChanged(false);

    // 直播暂停恢复：暂停期间直播持续推进，从旧位置续播会永久滞后。
    // 暂停超过阈值则跳到直播边缘（reopen 新连接从直播边缘拉流）。
    // 短暂停（< 阈值）仍从当前点续播，残留延迟可忽略。
    if (m_networkManager->isLiveStream() && pauseDurationUs > kLivePauseReopenUs)
        reopenLiveStream();
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

    // 本 seek 启动后，任何更早排队的网络 seek 重启回调都应作废
    // （它们捕获的代数已过期）。
    ++m_seekGeneration;

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
        const int gen = m_seekGeneration;

        m_seekInProgress.store(true);
        (void)QtConcurrent::run([this, seconds, wasPaused, speed, gen]() {
            // 停止线程
            if (m_interruptCtx)
                m_interruptCtx->interrupted.store(true);
            if (m_demuxThread) {
                m_demuxThread->stopRead();
                m_demuxThread->wait(3000);
            }
            if (m_audioDemuxThread) {
                m_audioDemuxThread->stopRead();
                m_audioDemuxThread->wait(3000);
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
            int64_t targetUs = clampSeekTarget(seconds);
            performSeekAndFlush(targetUs);

            // 后台工作完成，允许 stop() 继续
            m_seekInProgress.store(false);

            // 回到主线程完成后续操作
            QMetaObject::invokeMethod(this, [this, seconds, wasPaused, speed, gen]() {
                // 期间若已发生更新的 seek / 音轨切换 / stop，本回调作废：
                // 线程已由后续操作重建或销毁，继续执行会重复删除/重启管线。
                if (gen != m_seekGeneration)
                    return;

                m_syncController->reset();
                m_syncController->setSpeed(speed);
                m_syncController->setClock(seconds);

                // 视频 PTS 丢弃阈值：同本地 seek（音轨切换时由 switchAudioToStream 预置）
                prepareSeekDropBefore(seconds);
                // 音频同丢弃目标之前的帧（seek 从上一关键帧起读）
                m_pendingAudioPtsDropBefore = seconds;

                qint64 now = av_gettime();
                m_startTimeUs = now - static_cast<qint64>(seconds * 1000000 / qMax(speed, 0.1));
                m_pausedDurationUs = 0;
                m_pauseStartUs = wasPaused ? now : 0;

                // 清理线程指针
                delete m_demuxThread; m_demuxThread = nullptr;
                delete m_audioDemuxThread; m_audioDemuxThread = nullptr;
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
                    retireSubtitleQueue();
                }
                // 音频/视频队列同线程一并回收：后台任务已停止 demux 与解码线程，
                // 无在途 push/pop，可直接删除（否则每次网络 seek 泄漏上一代队列
                // 及其中的帧缓存）。
                if (m_videoPacketQueue) m_videoPacketQueue->requestQuit();
                if (m_audioPacketQueue) m_audioPacketQueue->requestQuit();
                if (m_videoFrameQueue) m_videoFrameQueue->requestQuit();
                if (m_audioFrameQueue) m_audioFrameQueue->requestQuit();
                delete m_videoPacketQueue; m_videoPacketQueue = nullptr;
                delete m_audioPacketQueue; m_audioPacketQueue = nullptr;
                delete m_videoFrameQueue; m_videoFrameQueue = nullptr;
                delete m_audioFrameQueue; m_audioFrameQueue = nullptr;

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

    // 本地文件：解码管线线程全部存活时走线程常驻的轻量 seek（demux 线程
    // 执行容器 seek + 队列冲刷标记，解码线程就地冲刷，不拆线程/队列）；
    // 线程已退出（EOF 排空等）时回退全量重建的重路径。
    // 外挂字幕被动线程不启动 run()（isRunning 恒 false），不参与判定。
    bool threadsAlive = m_demuxThread && m_demuxThread->isRunning()
        && (m_videoThread == nullptr || m_videoThread->isRunning())
        && (m_audioThread == nullptr || m_audioThread->isRunning())
        && (m_subtitleThread == nullptr || m_externalMode || m_subtitleThread->isRunning());
    if (threadsAlive) {
        seekLight(seconds);
        return;
    }
    seekHeavy(seconds);
}

// 本地文件 seek 重路径：拆卸全部线程与队列，主线程执行容器 seek 与编解码器
// 冲刷，再整体重建。音轨切换（switchAudioToStream）与 EOF 后 seek 专用。
void MediaEngine::seekHeavy(double seconds)
{
    stopThreads();

    prepareSeekDropBefore(seconds);
    // 音频同丢弃目标之前的帧（seek 从上一关键帧起读，音频会早于目标起播）。
    // 由 startThreads 消费注入新音频线程；音轨切换（switchAudioToStream）已
    // 预置换算到新音轨时间基的阈值时（以 anchor 调用本函数），不得覆盖。
    if (m_pendingAudioPtsDropBefore < 0.0)
        m_pendingAudioPtsDropBefore = seconds;

    int64_t targetUs = clampSeekTarget(seconds);
    performSeekAndFlush(targetUs);

    clearSeekOverlayText();
    anchorClockTo(seconds);

    startThreads();

    if (m_audioThread && !qFuzzyCompare(m_speed, 1.0))
        m_audioThread->setSpeed(m_speed);

    restartAfterSeek();
}

// 本地文件 seek 轻路径（线程常驻）：仅当解码管线线程全部存活时调用。
// 与重路径的差异：demux 线程执行 avformat_seek_file 并清空包队列、投入
// 冲刷标记；解码线程消费标记后就地冲刷编解码器与帧队列，管线不拆卸重建，
// seek 代价为毫秒级（拖动进度条预览/连续 seek 不再卡顿）。
void MediaEngine::seekLight(double seconds)
{
    // EOF 排空中的 demux 不会响应 seek 请求（排空循环不检查请求），且排空
    // 期间解码线程可能已退出（队列清空即 pop 返回退出），轻量 seek 无法完成，
    // 直接回退全量重建（seekHeavy 内部 stopThreads 会中断排空循环）。
    if (m_demuxThread->isDraining()) {
        qWarning() << "seekLight: demux is draining, falling back to heavy seek";
        seekHeavy(seconds);
        return;
    }

    // 位置定时器与显示线程已由 seek() 停止（与重路径同一契约）；
    // EOF 排空定时器只有此处停（轻量 seek 不拆线程，stopThreads 不会执行）。
    m_eofDrainTimer->stop();

    m_audioOutput->pause();
    m_audioOutput->reset();

    prepareSeekDropBefore(seconds);
    if (m_videoThread)
        m_videoThread->setPtsDropBefore(m_videoPtsDropBefore);
    // 音频同丢弃目标之前的帧（backward seek 从上一关键帧起读，音频包
    // 也从更早位置开始）：若不丢弃，音频时钟从目标之前起步，画面领先，
    // 显示循环会判定视频超前而冻结画面等待音频追赶（seek 后卡顿）。
    // 阈值直接用 seconds：音频时钟域 = 音频流时间。
    if (m_audioThread)
        m_audioThread->setPtsDropBefore(seconds);

    int64_t targetUs = clampSeekTarget(seconds);

    clearSeekOverlayText();
    anchorClockTo(seconds);

    // 由 demux 线程执行容器 seek + 清空包队列 + 投入冲刷标记
    // （avformat_seek_file 毫秒级；请求可能因线程阻塞于满队列 push 而延迟，
    // requestSeek 内的 notifySeekWaiters 已将其唤醒）。
    m_demuxThread->requestSeek(targetUs);
    int waited = 0;
    while (!m_demuxThread->seekDone() && waited < 2000) {
        QThread::msleep(1);
        ++waited;
    }
    if (!m_demuxThread->seekDone()) {
        // 竞态：demux 在 EOF 排空与退出之间收到请求且未处理。
        // 线程已死、或仍在排空（即将退出）都无法再消费标记，回退全量重建。
        if (!m_demuxThread->isRunning() || m_demuxThread->isDraining()) {
            qWarning() << "seekLight: demux exited or draining before processing seek, falling back to heavy seek";
            seekHeavy(seconds);
            return;
        }
        // 极端情况（读取阻塞）：请求仍驻留，demux 空闲后会处理，
        // 时钟已重锚定，按轻量流程继续恢复即可。
        qWarning() << "seekLight: demux busy, seek will be applied when it frees up";
    }

    // 冲刷标记已入队：唤醒暂停中的解码线程就地冲刷（播放中的线程自然消费）。
    // 每线程独立标志，避免共享原子被一线程清除后其余线程仍在门控睡眠。
    if (m_videoThread)
        m_videoThread->wakeFlush();
    if (m_audioThread)
        m_audioThread->wakeFlush();
    if (m_subtitleThread && !m_externalMode)
        m_subtitleThread->wakeFlush();

    restartAfterSeek();
}

// 两条 seek 路径共用的片段。

// seek 目标钳制到合法区间（秒 → 微秒）。
qint64 MediaEngine::clampSeekTarget(double seconds) const
{
    int64_t targetUs = static_cast<int64_t>(seconds * AV_TIME_BASE);
    if (targetUs < 0) targetUs = 0;
    if (m_duration > 0 && targetUs > static_cast<int64_t>(m_duration * AV_TIME_BASE))
        targetUs = static_cast<int64_t>(m_duration * AV_TIME_BASE);
    return targetUs;
}

// 视频 PTS 丢弃阈值：backward seek 落在目标位置之前的上一关键帧，解码出的
// 锚点前旧画面直接丢弃（普通 seek 按目标位置计算；音轨切换由
// switchAudioToStream 预置 m_pendingVideoPtsDropBefore）。
// 重路径在 startThreads 中消费该阈值，轻路径直接注入视频线程。
void MediaEngine::prepareSeekDropBefore(double seconds)
{
    if (m_pendingVideoPtsDropBefore >= 0.0) {
        m_videoPtsDropBefore = m_pendingVideoPtsDropBefore;
        m_pendingVideoPtsDropBefore = -1.0;
    } else {
        m_videoPtsDropBefore = seconds
            + streamStartSeconds(m_fmtCtx, m_videoStreamIndex)
            - streamStartSeconds(audioFmtCtx(), m_audioStreamIndex);
    }
}

// 清字幕/歌词显示（新位置的条目由 updateSubtitle/updateLyric 重新加载）
void MediaEngine::clearSeekOverlayText()
{
    m_currentSubtitle = QString();
    emit currentSubtitleChanged(m_currentSubtitle);
    m_currentLyric = QString();
    emit currentLyricChanged(m_currentLyric);
}

// 主时钟与时间锚点先就位（音频输出已暂停，无并发时钟更新）。
void MediaEngine::anchorClockTo(double seconds)
{
    m_syncController->reset();
    m_syncController->setSpeed(m_speed);
    m_syncController->setClock(seconds);

    qint64 now = av_gettime();
    m_startTimeUs = now - static_cast<qint64>(seconds * 1000000 / qMax(m_speed, 0.1));
    m_pausedDurationUs = 0;
    m_pauseStartUs = m_paused ? now : 0;
    m_position = seconds;
    emit positionChanged(m_position);
}

// seek 后恢复播放：音频输出、位置定时器与显示线程（轻/重路径共用尾部）。
void MediaEngine::restartAfterSeek()
{
    if (!m_paused)
        m_audioOutput->resume();

    m_positionTimer->start();
    if (m_videoFrameQueue)
        startDisplayThread();
}

// 容器 seek + 三个解码器冲刷（open 初始 seek/网络回调/重路径共用）。
// split 模式（DASH 分离流）须对视频/音频两个 context 分别 seek。
// 调用方保证此时无解码线程在运行（初始 seek 无线程；网络回调与重路径已停线程）。
void MediaEngine::performSeekAndFlush(int64_t targetUs)
{
    int ret = avformat_seek_file(m_fmtCtx, -1,
                                 INT64_MIN, targetUs, targetUs,
                                 AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        qWarning() << "Seek failed:" << ret;

    if (m_audioFmtCtx) {
        ret = avformat_seek_file(m_audioFmtCtx, -1,
                                 INT64_MIN, targetUs, targetUs,
                                 AVSEEK_FLAG_BACKWARD);
        if (ret < 0)
            qWarning() << "Audio seek failed:" << ret;
    }

    if (m_videoCodecCtx)
        avcodec_flush_buffers(m_videoCodecCtx);
    if (m_audioCodecCtx)
        avcodec_flush_buffers(m_audioCodecCtx);
    if (m_subtitleCodecCtx)
        avcodec_flush_buffers(m_subtitleCodecCtx);
}

// 字幕包队列退役：demux 可能仍有在途 push，先退役（quit+flush+clear），
// 待 demux 退出（stopThreads/cleanup）后再统一回收。
void MediaEngine::retireSubtitleQueue()
{
    if (!m_subtitlePacketQueue)
        return;
    m_subtitlePacketQueue->requestQuit();
    m_subtitlePacketQueue->flush();
    m_subtitlePacketQueue->clear();
    m_retiredSubtitleQueues.append(m_subtitlePacketQueue);
    m_subtitlePacketQueue = nullptr;
}

// 主时钟（音频时钟）；无音频流或时钟尚未启动时退回 0 基墙钟位置。
double MediaEngine::mainClockOrPosition(double pos) const
{
    double mainClock = m_syncController->audioClock();
    if (m_audioStreamIndex == -1 || mainClock <= 0.0)
        return pos;
    return mainClock;
}

// 以当前听感位置重锚定时间基准（变速过渡与结束后共用）。
void MediaEngine::reanchorToContentPosition(double speed)
{
    double contentPos = m_syncController->audioClock();
    if (!(contentPos > 0.0))   // 音频时钟不可用（刚开始播放）时退回界面位置
        contentPos = m_position;
    m_startTimeUs = av_gettime()
        - static_cast<qint64>(contentPos * 1000000.0 / qMax(speed, 0.1));
    m_pausedDurationUs = 0;
    if (m_paused)
        m_pauseStartUs = av_gettime();
}

double MediaEngine::position() const
{
    return m_position;
}

double MediaEngine::duration() const
{
    return m_duration;
}

// --- A-B 区间循环 ---

void MediaEngine::toggleABLoop()
{
    if (!m_fmtCtx || m_networkManager->isLiveStream())
        return;

    if (m_abLoopState == Looping) {
        // 循环中再按：清除循环，播放位置不动
        resetABLoop();
        return;
    }

    if (m_abLoopState == APending) {
        // 设置 B 点。B 在 A 之前时自动交换，避免反向设置造成困惑。
        m_abLoopB = position();
        if (m_abLoopB < m_abLoopA)
            qSwap(m_abLoopA, m_abLoopB);
        if (m_abLoopB - m_abLoopA < 0.01) {
            // 区间过短视为误触：回到未激活
            resetABLoop();
            return;
        }
        setABLoopState(Looping);
        // 立即从 A 点开始循环播放
        seek(m_abLoopA);
        if (m_paused)
            resume();
        return;
    }

    // 未激活：记录当前播放位置为 A 点
    m_abLoopA = position();
    setABLoopState(APending);
}

// 统一状态变更出口：置状态并成对发出状态/坐标信号。
// QML 侧依赖两信号同批到达刷新提示文案（ApplicationController 中同批取回）。
void MediaEngine::setABLoopState(int state)
{
    m_abLoopState = state;
    emit abLoopStateChanged(m_abLoopState);
    emit abLoopChanged();
}

// 清除循环并复位 A/B 点坐标（新文件加载/关闭/误触清除共用）。
void MediaEngine::resetABLoop()
{
    if (m_abLoopState == NoLoop)
        return;
    m_abLoopState = NoLoop;
    m_abLoopA = m_abLoopB = 0.0;
    emit abLoopStateChanged(m_abLoopState);
    emit abLoopChanged();
}

// --- 逐帧步进 ---

void MediaEngine::stepFrameForward()
{
    if (!m_fmtCtx || m_videoStreamIndex < 0 || m_networkManager->isLiveStream())
        return;
    if (m_stepOneFrame.load(std::memory_order_acquire) || m_seekInProgress.load())
        return;

    if (!m_paused)
        pause();

    // 暂停态下解码并显示恰好一帧后冻结。帧队列里已有解码线程预取的帧时
    // 直接消费；队列为空才让 demux/视频解码线程各推进一个步进单位补产。
    m_stepOneFrame.store(true, std::memory_order_release);
    const bool queueHasFrame = m_videoFrameQueue && m_videoFrameQueue->size() > 0;
    if (!queueHasFrame) {
        if (m_demuxThread)
            m_demuxThread->requestStep();
        if (m_videoThread)
            m_videoThread->requestStep();
    }
}

// --- 进度条拖动预览 ---

void MediaEngine::seekAndShowFrame(double seconds)
{
    if (!m_fmtCtx || m_videoStreamIndex < 0 || m_networkManager->isLiveStream())
        return;

    if (!m_paused)
        pause();

    // 完整 seek（重建管线）后置步进标志：seek 内部的 stopDisplayThread
    // 会清除该标志，必须在其返回后再设置。显示线程重启后处于暂停态，
    // 检测到步进标志即解码并显示目标位置的一帧后冻结。
    seek(seconds);
    m_stepOneFrame.store(true, std::memory_order_release);
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
    m_audioOutput->setSpeed(speed);
    m_syncController->setSpeed(speed);

    // 界面位置：内容在旧缓冲耗尽前仍按旧节奏前进，显示速度先保持旧值，
    // 重锚定到当前听感位置；AudioOutput 检测到旧数据耗尽时切为新速度。
    if (m_startTimeUs != 0) {
        m_displaySpeed = oldSpeed;
        reanchorToContentPosition(oldSpeed);
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
    reanchorToContentPosition(m_speed);
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

    // A-B 循环：越过 B 点即跳回 A 点。定时器 250ms 粒度下最多过冲
    // 0.25s × 2x 变速 ≈ 0.5s，跳回后自然对齐，无需精确截断。
    if (m_abLoopState == Looping && m_abLoopB > m_abLoopA && pos >= m_abLoopB) {
        seek(m_abLoopA);
        return;
    }

    // 字幕/歌词同步：以主时钟（音频时钟）驱动，字幕与听感内容严格一致。
    // 音频时钟为流内原始时间戳，内置字幕条目同基；外挂/歌词条目加载时已按
    // 音频流 start_time 偏移对齐，因此查询侧零换算。
    // 无音频流（或时钟尚未启动）时退回 0 基墙钟位置 m_position。
    double mainClock = mainClockOrPosition(pos);
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

    // 直播卡顿看门狗：持续缓冲超过阈值判定为"真卡死"（网络波动/连接断），
    // 自动 reopen 直播源——新连接从直播边缘重新拉流，既打破卡死，
    // 也消除"一次卡顿永久滞后"。冷却期防止网络差时频繁重连。
    if (m_networkManager->isLiveStream()) {
        if (low) {
            if (!m_liveStalling) {
                m_liveStalling = true;
                m_liveStallTimer.restart();
            } else if (!m_liveReopenPending
                       && m_liveStallTimer.elapsed() > kLiveReopenStallMs
                       && av_gettime() >= m_liveReopenCooldownUntilUs) {
                qWarning() << "[live] buffering too long, reopening to catch live edge";
                reopenLiveStream();
            }
        } else {
            m_liveStalling = false;
        }
    }
}

// 直播卡顿恢复：重建管线并重新连接直播源。
// 新连接从直播边缘拉流（FLV 新连接=直播边缘；HLS 重读 manifest=直播边缘），
// 位置/时钟回到直播边缘，累积延迟清零。复用 openSource 的异步初始化路径。
void MediaEngine::reopenLiveStream()
{
    if (m_liveReopenPending || !m_networkManager->isLiveStream() || m_activeSource.isSplit())
        return;

    m_liveReopenPending = true;
    m_liveStalling = false;
    m_liveReopenCooldownUntilUs = av_gettime() + kLiveReopenCooldownUs;
    openSource(m_activeSource, -1.0);
}

void MediaEngine::updateSubtitle(double clockSeconds)
{
    if (!m_subtitleThread)
        return;

    // clockSeconds 已由调用方统一换算为 0 基内容时间：
    // 内嵌/外挂字幕条目同为 0 基，查询无需区分模式。
    qint64 posUs = static_cast<qint64>(clockSeconds * 1000000);
    // 字幕延迟：正值 = 字幕推迟出现。推迟 N 毫秒等于按 (当前时间 - N) 查询，
    // 查询位置早于实际播放位置，落在前一条字幕的显示区间内。
    if (m_subtitleDelayMs != 0)
        posUs -= m_subtitleDelayMs * 1000;
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

    // ---- demux 线程 ----
    // split 模式：视频/音频各一个 demux 线程，分别读取两个 context；
    // 普通模式：单个 demux 线程分路全部流。
    m_demuxThread = new DemuxThread(this);
    const int expectedEof = m_splitMode ? 2 : 1;
    m_splitEofCount = 0;
    auto onDemuxEof = [this, expectedEof]() {
        // split 模式需两个 demux 都 EOF 才进入排空门控（避免视频/音频
        // 时长差异导致一先 EOF 就提前结束播放）
        if (expectedEof > 1 && ++m_splitEofCount < expectedEof)
            return;
        // 等帧队列/FIFO 排空后再发 playbackFinished（见 m_eofDrainTimer 注释）
        m_eofDrainTimer->start();
    };
    auto onDemuxFatalError = [this](const QString &msg) {
        qWarning() << "Demux fatal error:" << msg;
        handleFatalError(msg);
    };

    if (m_splitMode) {
        m_demuxThread->setFormatContext(m_fmtCtx);
        m_demuxThread->setStreamIndices(m_videoStreamIndex, -1, -1);
        m_demuxThread->setPacketQueues(m_videoPacketQueue, nullptr, nullptr);

        m_audioDemuxThread = new DemuxThread(this);
        m_audioDemuxThread->setFormatContext(m_audioFmtCtx);
        m_audioDemuxThread->setStreamIndices(-1, m_audioStreamIndex, -1);
        m_audioDemuxThread->setPacketQueues(nullptr, m_audioPacketQueue, nullptr);
        m_audioDemuxThread->setPausedRef(m_paused);
    } else {
        m_demuxThread->setFormatContext(m_fmtCtx);
        m_demuxThread->setStreamIndices(m_videoStreamIndex, m_audioStreamIndex, m_subtitleStreamIndex);
        m_demuxThread->setPacketQueues(m_videoPacketQueue, m_audioPacketQueue, m_subtitlePacketQueue);
    }
    m_demuxThread->setPausedRef(m_paused);
    connect(m_demuxThread, &DemuxThread::eofReached, this, onDemuxEof);
    if (m_audioDemuxThread)
        connect(m_audioDemuxThread, &DemuxThread::eofReached, this, onDemuxEof);
    connect(m_demuxThread, &DemuxThread::fatalErrorOccurred, this, onDemuxFatalError);
    if (m_audioDemuxThread)
        connect(m_audioDemuxThread, &DemuxThread::fatalErrorOccurred, this, onDemuxFatalError);

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
        m_videoThread->setTimeBase(m_videoTimeBase);
        // 丢弃 seek/切换锚点之前的旧画面帧（-1 = 不丢弃）
        m_videoThread->setPtsDropBefore(m_videoPtsDropBefore);

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
        m_audioThread->setTimeBase(audioFmtCtx()->streams[m_audioStreamIndex]->time_base);
        m_audioThread->setOutputSampleRate(44100);
        // 音轨切换：新音轨首帧 PTS 重基到锚点，并丢弃锚点之前的帧
        // （一次性消费，见 m_pendingAudioPtsAnchor / m_pendingAudioPtsDropBefore）
        if (m_pendingAudioPtsAnchor >= 0.0) {
            m_audioThread->setPtsAnchor(m_pendingAudioPtsAnchor);
            m_pendingAudioPtsAnchor = -1.0;
        }
        if (m_pendingAudioPtsDropBefore >= 0.0) {
            m_audioThread->setPtsDropBefore(m_pendingAudioPtsDropBefore);
            m_pendingAudioPtsDropBefore = -1.0;
        }
    }

    m_demuxThread->start();
    if (m_audioDemuxThread)
        m_audioDemuxThread->start();
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
    // 管线拆卸期间队列会被清空、线程全部退出，若 EOF 排空门控仍在轮询，
    // 会误判"内容已播完"而提前发 playbackFinished（seek/切轨/重开时）。
    m_eofDrainTimer->stop();

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
    if (m_audioDemuxThread) {
        if (m_interruptCtx)
            m_interruptCtx->interrupted.store(true);
        m_audioDemuxThread->stopRead();
        if (!m_audioDemuxThread->wait(5000)) {
            qWarning() << "AudioDemuxThread did not exit in time after interrupt";
        }
        delete m_audioDemuxThread;
        m_audioDemuxThread = nullptr;
    }
    auto stopDecodeAndDelete = [](auto *&thread) {
        if (!thread) return;
        thread->stopDecode();
        thread->wait();
        delete thread;
        thread = nullptr;
    };
    stopDecodeAndDelete(m_videoThread);
    stopDecodeAndDelete(m_audioThread);
    stopDecodeAndDelete(m_subtitleThread);

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
    // PTS 丢弃阈值只对产生它的那次 seek/切换生效；cleanup 后若不重置，
    // 重开文件时 startThreads 会把上一次会话的阈值注入新线程（短视频重开
    // 循环播放时表现为画面长时间冻结在上一帧）。
    m_videoPtsDropBefore = -1.0;
    m_pendingVideoPtsDropBefore = -1.0;
    m_pendingAudioPtsAnchor = -1.0;
    m_pendingAudioPtsDropBefore = -1.0;

    // 关闭/切换文件时清空 A-B 循环状态，避免 UI 残留
    resetABLoop();

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
    if (m_audioFmtCtx) {
        avformat_close_input(&m_audioFmtCtx);
        m_audioFmtCtx = nullptr;
    }
    m_splitMode = false;
    m_splitEofCount = 0;

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
    m_audioStreamsInfo.clear();
    m_currentAudioStreamIndex = -1;
    emit audioStreamsChanged();

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

QVariantList MediaEngine::audioStreams() const
{
    QVariantList list;
    int idx = 1;
    for (const auto &info : m_audioStreamsInfo) {
        QString label;
        if (!info.language.isEmpty())
            label = info.language;
        else if (!info.title.isEmpty())
            label = info.title;
        else
            label = QStringLiteral("Audio #%1").arg(idx);
        if (info.channels > 0)
            label += QStringLiteral(" (%1ch)").arg(info.channels);
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
    retireSubtitleQueue();
    if (m_subtitleCodecCtx) {
        avcodec_free_context(&m_subtitleCodecCtx);
        m_subtitleCodecCtx = nullptr;
    }
    m_subtitleStreamIndex = -1;
    m_externalMode = false;
    m_externalSubtitles.clear();
}

void MediaEngine::setSubtitleDelayMs(qint64 delayMs)
{
    // 限制在 ±60s，超出范围的输入直接截断
    delayMs = qBound<qint64>(-60000LL, delayMs, 60000LL);
    if (m_subtitleDelayMs == delayMs)
        return;
    m_subtitleDelayMs = delayMs;
    // 立即用新偏移重查当前位置（主时钟 = 音频时钟，同 updatePosition），
    // 无需等下一个位置定时器 tick。
    double mainClock = mainClockOrPosition(m_position);
    if (m_subtitleThread)
        updateSubtitle(mainClock);
    emit subtitleDelayMsChanged(m_subtitleDelayMs);
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

AVCodecContext *MediaEngine::openAudioCodecForStream(int streamIdx)
{
    if (streamIdx < 0 || !audioFmtCtx())
        return nullptr;

    AVCodecParameters *audioPar = audioFmtCtx()->streams[streamIdx]->codecpar;
    const AVCodec *audioCodec = avcodec_find_decoder(audioPar->codec_id);
    qDebug() << "[audio] codec_id:" << audioPar->codec_id << "codec found:" << !!audioCodec
             << "channels:" << audioPar->ch_layout.nb_channels
             << "rate:" << audioPar->sample_rate;

    if (!audioCodec) {
        qWarning() << "[audio] decoder not found for codec_id:" << audioPar->codec_id;
        return nullptr;
    }

    AVCodecContext *ctx = avcodec_alloc_context3(audioCodec);
    if (!ctx)
        return nullptr;

    avcodec_parameters_to_context(ctx, audioPar);
    int ret = avcodec_open2(ctx, audioCodec, nullptr);
    if (ret < 0) {
        char errbuf[128] = {0};
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning() << "[audio] could not open codec:" << errbuf;
        avcodec_free_context(&ctx);
        return nullptr;
    }
    return ctx;
}

void MediaEngine::setCurrentAudioStream(int index)
{
    if (index == m_currentAudioStreamIndex)
        return;
    if (index < 0 || index >= m_audioStreamsInfo.size()) {
        qWarning() << "[audio] invalid track index:" << index;
        return;
    }

    // 网络流 seek 为异步：在途时把切换推迟到事件循环重试。在途 seek 的重启
    // 回调比本次重试更早排队，必然先执行，故重试时管线已稳定（不会与后台
    // 任务交错；也不阻塞主线程）。此时代数已更新，旧回调作废。
    if (m_seekInProgress.load()) {
        if (m_switchRetryStartUs < 0) {
            m_switchRetryStartUs = av_gettime();
        } else if (av_gettime() - m_switchRetryStartUs > 10000000) {
            m_switchRetryStartUs = -1;
            qWarning() << "[audio] switch aborted: async seek stuck";
            return;
        }
        QMetaObject::invokeMethod(this, [this, index]() {
            setCurrentAudioStream(index);
        }, Qt::QueuedConnection);
        return;
    }
    m_switchRetryStartUs = -1;

    const AudioStreamInfo &info = m_audioStreamsInfo[index];
    qDebug().noquote() << "[audio] switch to track" << index
                       << "stream" << info.streamIndex
                       << (info.language.isEmpty() ? info.title : info.language);
    switchAudioToStream(info.streamIndex);

    if (m_audioStreamIndex != info.streamIndex)
        return; // 切换失败（codec 打不开等），保持原状态

    m_currentAudioStreamIndex = index;
    emit currentAudioStreamChanged(index);
}

void MediaEngine::switchAudioToStream(int newStreamIdx)
{
    // split 模式（DASH 分离流）音频为单一独立 URL，不支持播放中切轨
    if (m_splitMode) {
        qWarning() << "[audio] track switch unsupported in split (DASH) mode";
        return;
    }
    if (newStreamIdx < 0 || !m_fmtCtx || newStreamIdx == m_audioStreamIndex)
        return;

    // 1) 先打开新解码器：失败则放弃切换，保持原音轨不动。
    AVCodecContext *newCtx = openAudioCodecForStream(newStreamIdx);
    if (!newCtx) {
        qWarning() << "[audio] failed to open codec for stream" << newStreamIdx;
        return;
    }

    // 2) 锚点 = 切换前的主时钟（旧音轨原始时间基）。新音轨首帧 PTS 将重基到
    //    该值，音频时钟轴切换前后保持不变，A-V diff 连续，画面不跳变/不冻结
    //    （不同音轨在容器内的时间戳基可能不同）。
    double anchor = m_syncController->audioClock();
    if (anchor <= 0.0)
        anchor = m_position;

    // 旧音轨 start_time（秒）：丢弃阈值与 seek 目标换算需要（锚点在旧音轨
    // 原始时间基上，须先换算回容器位置再对齐到新流的时间基）。
    double oldStartSec = streamStartSeconds(audioFmtCtx(), m_audioStreamIndex);

    // 3) 先停止旧音频解码线程，再释放旧解码器：旧线程仍持有旧 ctx 指针，
    //    反过来会 use-after-free。
    if (m_audioThread) {
        m_audioThread->stopDecode();
        m_audioThread->wait();
        delete m_audioThread;
        m_audioThread = nullptr;
    }
    m_audioOutput->pause();

    // 4) 换入新解码器与新流索引（此刻旧解码器已无线程引用，可安全释放）。
    if (m_audioCodecCtx)
        avcodec_free_context(&m_audioCodecCtx);
    m_audioCodecCtx = newCtx;
    m_audioStreamIndex = newStreamIdx;

    // 5) 待注入新音频线程的参数：
    //    - PTS 重基锚点 = 锚点；
    //    - 丢弃阈值（新音轨时间基）= 锚点换算到新音轨时间基 = 锚点 - 旧 start + 新 start。
    //      seek 落点在视频关键帧前时，新音轨从锚点之前的内容起播，
    //      这些帧须丢弃，否则重基后音频内容整体落后于画面（音画不同步）。
    m_pendingAudioPtsAnchor = anchor;
    m_pendingAudioPtsDropBefore = anchor + streamStartSeconds(audioFmtCtx(), newStreamIdx) - oldStartSec;

    // 视频同丢弃锚点前的旧画面帧：backward seek 落在 anchor 之前的关键帧，
    // 解码出的整段 GOP 若进显示队列会按正常节奏走完（画面卡旧内容），且视频
    // 队列积压阻塞 demux、饿死音频队列（声音卡住数秒后画面跳变）。
    // 阈值 = 锚点换算到视频流原始时间基。
    m_pendingVideoPtsDropBefore = anchor + streamStartSeconds(m_fmtCtx, m_videoStreamIndex) - oldStartSec;

    // 6) 直播流不可 seek：原地重建全部线程与队列。
    //    demux 从当前读位继续，新音轨起点与画面存在约一个队列深度的内容偏移，
    //    直播无绝对时间轴，可接受；重建后按锚点重设主时钟与位置基准。
    if (m_networkManager->isLiveStream()) {
        m_positionTimer->stop();
        // 直播流 demux 连续推进，无 backward seek 旧画面问题，不丢弃任何帧。
        m_pendingVideoPtsDropBefore = -1.0;
        m_videoPtsDropBefore = -1.0;
        stopThreads();
        m_syncController->reset();
        m_syncController->setSpeed(m_speed);
        m_syncController->setClock(anchor);

        qint64 now = av_gettime();
        m_startTimeUs = now - static_cast<qint64>(m_position * 1e6 / qMax(m_speed, 0.1));
        m_pausedDurationUs = 0;
        m_pauseStartUs = m_paused ? now : 0;

        startThreads();
        m_positionTimer->start();
        if (m_videoFrameQueue)
            startDisplayThread();
        if (!m_paused)
            m_audioOutput->resume();
        return;
    }

    // 7) 常规文件：以锚点为目标做一次迷你 seek，重建全部线程与队列，
    //    使 demux 从当前内容位置重读新音轨，新音轨与画面在同一内容点起播。
    //    新音频线程在 startThreads 中按锚点重基 PTS 并丢弃锚点之前的帧。
    //    必须走重路径：旧音频线程已在此函数中销毁，轻路径不会重建它，
    //    且 PTS 重基锚点需要新的音频线程承载。
    seekHeavy(anchor);
}

void MediaEngine::detectExternalSubtitles(const QString &videoPath)
{
    if (videoPath.isEmpty())
        return;

    QFileInfo vfi(videoPath);
    QDir dir = vfi.dir();
    QString base = vfi.completeBaseName();

    // 支持的扩展名
    static const QStringList exts = { QStringLiteral("srt"), QStringLiteral("vtt"), QStringLiteral("ass"), QStringLiteral("ssa") };

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

// Y 平面逐行拷贝（420P 与 NV12 的 Y 平面布局相同）
static void copyYPlane(YUVFrame &out, AVFrame *frame, int width, int height)
{
    out.yPlane.resize(width * height);
    for (int i = 0; i < height; i++)
        memcpy(out.yPlane.data() + i * width, frame->data[0] + i * frame->linesize[0], width);
}

static YUVFrame extractYUV420P(AVFrame *frame, int width, int height)
{
    YUVFrame out;
    out.frameSize = QSize(width, height);
    int halfW = (width + 1) / 2;
    int halfH = (height + 1) / 2;

    copyYPlane(out, frame, width, height);
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

    copyYPlane(out, frame, width, height);
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
    m_stepOneFrame.store(false, std::memory_order_release);
    m_displayStopRequested.store(true, std::memory_order_release);
    if (m_displayThread) {
        m_displayThread->quit();
        m_displayThread->wait(2000);
        m_displayThread = nullptr;
    }
}

void MediaEngine::displayLoop()
{
    // YUV 提取/转换：步进分支与普通分支共用（失败时已释放 frame，返回 false）
    auto extractFrame = [this](AVFrame *frame, YUVFrame &yuv, AVFrame *&convFrame) -> bool {
        AVPixelFormat fmt = static_cast<AVPixelFormat>(frame->format);
        if (fmt == AV_PIX_FMT_YUV420P) {
            yuv = extractYUV420P(frame, frame->width, frame->height);
        } else if (fmt == AV_PIX_FMT_NV12) {
            yuv = extractNV12(frame, frame->width, frame->height);
        } else {
            convFrame = convertToYUV420P(frame);
            if (!convFrame) {
                qWarning("displayLoop: sws_scale conversion failed: %s",
                         av_get_pix_fmt_name(fmt));
                av_frame_free(&frame);
                return false;
            }
            yuv = extractYUV420P(convFrame, convFrame->width, convFrame->height);
        }
        return true;
    };

    while (!m_displayStopRequested.load(std::memory_order_acquire)) {
        // 逐帧步进：暂停态下解码并显示恰好一帧后冻结（不 seek、不重排管线）。
        // 暂停时解码线程已预取若干帧，直接消费队头帧；队列为空则请求
        // demux/视频解码线程各推进一个步进单位补产，最多等待 1 秒。
        if (m_stepOneFrame.load(std::memory_order_acquire)) {
            if (!m_videoFrameQueue || !m_syncController) {
                QThread::msleep(5);
                continue;
            }

            AVFrame *frame = m_videoFrameQueue->peek();
            if (!frame) {
                if (m_demuxThread)
                    m_demuxThread->requestStep();
                if (m_videoThread)
                    m_videoThread->requestStep();
                const qint64 deadlineUs = av_gettime() + 1000000;
                while (!m_displayStopRequested.load(std::memory_order_acquire)) {
                    frame = m_videoFrameQueue->peek();
                    if (frame)
                        break;
                    if (av_gettime() > deadlineUs)
                        break;
                    QThread::msleep(2);
                }
                if (!frame) {
                    // 超时（文件末尾 / 无法继续解码）：放弃本次步进
                    m_stepOneFrame.store(false, std::memory_order_release);
                    if (m_demuxThread)
                        m_demuxThread->clearStepRequest();
                    if (m_videoThread)
                        m_videoThread->clearStepRequest();
                    continue;
                }
            }

            double pts = 0.0;
            if (frame->pts != AV_NOPTS_VALUE)
                pts = frame->pts * av_q2d(m_videoTimeBase);
            frame = m_videoFrameQueue->tryPop(0);
            if (!frame)
                continue;

            m_syncController->onFrameDisplayed(pts);
            m_syncController->setClock(pts);
            m_lastDisplayedPts = pts;

            // 恢复冻结：解码线程回到暂停等待（主线程位置定时器在暂停态不运行，
            // 此处写 m_position 无并发读写）
            if (m_videoThread)
                m_videoThread->clearStepRequest();
            if (m_demuxThread)
                m_demuxThread->clearStepRequest();
            m_stepOneFrame.store(false, std::memory_order_release);
            m_position = pts;
            emit positionChanged(m_position);

            YUVFrame yuv;
            AVFrame *convFrame = nullptr;
            if (!extractFrame(frame, yuv, convFrame))
                continue;

            emit frameReady(yuv);
            av_frame_free(&frame);
            av_frame_free(&convFrame);
            continue;
        }

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
        if (!extractFrame(frame, yuv, convFrame))
            continue;

        emit frameReady(yuv);
        av_frame_free(&frame);
        av_frame_free(&convFrame);
    }
}
