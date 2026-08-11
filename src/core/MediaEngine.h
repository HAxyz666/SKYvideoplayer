#pragma once

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <QList>
#include <QImage>
#include <QFutureWatcher>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
#ifdef ENABLE_HWACCEL
#include <libavutil/hwcontext.h>
#endif
}

#include "SubtitleDecodeThread.h"   // SubtitleEntry（m_externalSubtitles 成员需要完整类型）

struct YUVFrame;

class DemuxThread;
class VideoDecodeThread;
class AudioDecodeThread;
class PacketQueue;
class FrameQueue;
class AVSyncController;
class AudioOutput;
class NetworkStreamManager;

class MediaEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(QString currentSubtitle READ currentSubtitle NOTIFY currentSubtitleChanged)
    Q_PROPERTY(QVariantList subtitleStreams READ subtitleStreams NOTIFY subtitleStreamsChanged)
    Q_PROPERTY(int currentSubtitleStream READ currentSubtitleStream WRITE setCurrentSubtitleStream NOTIFY currentSubtitleStreamChanged)
    Q_PROPERTY(qint64 subtitleDelayMs READ subtitleDelayMs WRITE setSubtitleDelayMs NOTIFY subtitleDelayMsChanged)
    Q_PROPERTY(QVariantList audioStreams READ audioStreams NOTIFY audioStreamsChanged)
    Q_PROPERTY(int currentAudioStream READ currentAudioStream WRITE setCurrentAudioStream NOTIFY currentAudioStreamChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged)
    Q_PROPERTY(bool flipVertical READ flipVertical NOTIFY flipVerticalChanged)
    Q_PROPERTY(QString coverArtUrl READ coverArtUrl NOTIFY coverArtChanged)
    Q_PROPERTY(QString currentLyric READ currentLyric NOTIFY currentLyricChanged)
    Q_PROPERTY(bool isNetworkStream READ isNetworkStream NOTIFY isNetworkStreamChanged)
    Q_PROPERTY(bool isLiveStream READ isLiveStream NOTIFY isLiveStreamChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QString loadingText READ loadingText NOTIFY loadingTextChanged)
    Q_PROPERTY(int bufferState READ bufferState NOTIFY bufferStateChanged)

public:
    explicit MediaEngine(QObject *parent = nullptr);
    ~MediaEngine();

    void start();
    void startPlayback();   // 启动播放线程（initFFmpeg 之后调用）
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool open(const QString &url, double initialSeekPos = -1.0);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double seconds);
    bool hasVideo() const { return m_videoStreamIndex >= 0; }

    double position() const;
    double duration() const;

    // --- 音量控制 ---
    void setVolume(double vol);     // 设置音量 0~100
    void setMuted(bool muted);      // 设置静音
    double volume() const;          // 获取当前音量
    bool muted() const;             // 是否静音

    // --- 字幕 ---
    QString currentSubtitle() const { return m_currentSubtitle; }
    QVariantList subtitleStreams() const;
    int currentSubtitleStream() const { return m_currentSubtitleStreamIndex; }
    Q_INVOKABLE void setCurrentSubtitleStream(int index);

    // 字幕显示延迟（毫秒）：正值 = 字幕推迟出现（相对音频/画面滞后时往前调）。
    // 应用层负责按文件记忆并在打开文件时恢复。
    qint64 subtitleDelayMs() const { return m_subtitleDelayMs; }
    void setSubtitleDelayMs(qint64 delayMs);

    // --- 音轨 ---
    QVariantList audioStreams() const;
    int currentAudioStream() const { return m_currentAudioStreamIndex; }
    Q_INVOKABLE void setCurrentAudioStream(int index);

    // --- 速度控制 ---
    void setSpeed(double speed);
    double speed() const;

    // --- 封面艺术 ---
    QString coverArtUrl() const { return m_coverArtUrl; }

    // --- 歌词 ---
    QString currentLyric() const { return m_currentLyric; }

    // --- 网络流 ---
    bool isNetworkStream() const;
    bool isLiveStream() const;
    bool isLoading() const { return m_isLoading; }
    QString loadingText() const { return m_loadingText; }

    enum BufferState { BufferPlaying = 0, BufferBuffering = 1 };
    Q_ENUM(BufferState)
    int bufferState() const { return static_cast<int>(m_bufferState); }

    // --- 画面旋转 (UC-07) ---
    int rotation() const { return m_rotation; }
    bool flipVertical() const { return m_flipVertical; }
    Q_INVOKABLE void setRotation(int angle);          // 设置旋转角度 (0/90/180/270)
    Q_INVOKABLE void rotateLeft();                    // 左旋 90° (逆时针)
    Q_INVOKABLE void rotateRight();                   // 右旋 90° (顺时针)
    Q_INVOKABLE void setFlipVertical(bool flip);      // 设置垂直翻转
    Q_INVOKABLE void resetRotation();                 // 重置画面

signals:
    void frameReady(const YUVFrame &frame);
    void playbackFinished();
    void pausedChanged(bool paused);
    void positionChanged(double pos);
    void durationChanged(double dur);
    void volumeChanged(double vol);     // 音量变化信号
    void mutedChanged(bool muted);      // 静音状态变化信号
    void speedChanged(double speed);
    void hasVideoChanged();
    void currentSubtitleChanged(QString text);
    void subtitleStreamsChanged();
    void currentSubtitleStreamChanged(int index);
    void subtitleDelayMsChanged(qint64 delayMs);
    void audioStreamsChanged();
    void currentAudioStreamChanged(int index);
    void rotationChanged(int angle);          // 画面旋转角度变化信号
    void flipVerticalChanged(bool flip);      // 垂直翻转状态变化信号
    void coverArtChanged();
    void currentLyricChanged(QString lyric);
    void isNetworkStreamChanged(bool isNetwork);
    void isLiveStreamChanged(bool isLive);
    void isLoadingChanged(bool loading);
    void loadingTextChanged(QString text);
    void errorOccurred(QString message, bool isNetworkRelated);  // 错误信号
    void networkStreamReady(const QString &url);  // 网络流连接成功
    void bufferStateChanged(int state);

private:
    static constexpr int kAudioFrameQueueSize = 16;

    bool initFFmpeg(const QString &filename);
    void cleanup();
    void startThreads();
    void stopThreads();
    void initAudioOutput();
    void updatePosition();
    void startDisplayThread();
    void stopDisplayThread();
    void displayLoop();

    void updateSubtitle(double clockSeconds);
    void updateLyric(double clockSeconds);
    // 取指定流的 start_time 偏移（微秒）。主时钟（音频时钟）为流内原始时间戳，
    // 外挂字幕/歌词条目加载时加上该偏移对齐，查询侧无需任何换算。
    qint64 streamStartUs(int streamIdx) const;
    // 加载外挂字幕文件并偏移对齐到音频流时间轴（与内置字幕条目同基）
    QList<SubtitleEntry> loadExternalSubtitleFile(const QString &path) const;

    void detectExternalSubtitles(const QString &videoPath);
    void detectLyrics(const QString &audioPath);

    void activateExternalSubtitle(int infoIndex);
    void stopSubtitleThread();

    // 为指定流打开并返回音频解码器上下文（失败返回 nullptr）
    AVCodecContext *openAudioCodecForStream(int streamIdx);
    // 播放中切换音轨：停止旧音频链路、换入新解码器，
    // 以当前主时钟为锚点迷你 seek 重建全部线程（直播流则原地重建）。
    // 新音轨 PTS 重基到锚点，保证切换前后音频时钟轴不变、音画连续。
    void switchAudioToStream(int newStreamIdx);

    // 网络流异步初始化完成回调
    void onNetworkInitFinished();

    // 中断回调相关
    static int interruptCallback(void *ctx);
    void setupInterruptCallback();

    // 缓冲检测
    void checkBufferState();

    // 变速过渡（不断音方案）
    // m_displaySpeed：界面显示用速度。变速瞬间新速度先作用于解码/时钟，
    // 而旧速度预缓冲（FIFO+帧队列）会以旧节奏继续播完，因此显示速度先保持旧值、
    // 待 AudioOutput 发出 speedTransitionFinished 后再切为新速度并重锚定，
    // 保证进度条与听感位置全程一致。
    void onSpeedTransitionFinished();

    QString m_filename;

    AVFormatContext *m_fmtCtx;
    int m_videoStreamIndex;
    int m_audioStreamIndex;
    int m_subtitleStreamIndex;

    AVCodecContext *m_videoCodecCtx;
    AVCodecContext *m_audioCodecCtx;
    AVCodecContext *m_subtitleCodecCtx;

    DemuxThread *m_demuxThread;
    VideoDecodeThread *m_videoThread;
    AudioDecodeThread *m_audioThread;
    SubtitleDecodeThread *m_subtitleThread;

    PacketQueue *m_videoPacketQueue;
    PacketQueue *m_audioPacketQueue;
    PacketQueue *m_subtitlePacketQueue;
    // 播放中切换字幕流时退役的旧字幕包队列：demux 线程可能仍有在途 push，
    // 需等 demux 退出（stopThreads/cleanup）后再统一销毁。
    QList<PacketQueue *> m_retiredSubtitleQueues;
    // 音频流 start_time 偏移（微秒）：主时钟（音频时钟）为流内原始时间戳，
    // 外挂字幕与歌词条目加载时加上该偏移，使条目与内置字幕同基，查询侧零换算。
    qint64 m_audioStartUs{0};
    // 音轨切换时待注入新音频线程的 PTS 重基锚点（秒）：
    // startThreads 创建音频线程时消费一次，-1 表示无锚点（正常播放/seek）。
    // 锚点 = 切换前的主时钟（旧音轨时间基），新音轨首帧原始 PTS 重基到该值，
    // 使切换前后音频时钟轴不变，A-V diff 连续，画面不跳变/不冻结。
    double m_pendingAudioPtsAnchor{-1.0};
    // 与锚点配套的原始 PTS 丢弃阈值（秒，新音轨时间基）：seek 落点在视频
    // 关键帧前时，新音轨会从锚点之前的内容起播，须丢弃这些帧（否则重基后
    // 音频内容整体落后于画面）。-1 表示不丢弃。阈值 = 锚点 + 新音轨 start_time。
    double m_pendingAudioPtsDropBefore{-1.0};
    // 音轨切换时待注入视频线程的 PTS 丢弃阈值（秒，视频流原始时间基）：
    // seek(anchor) 为 backward seek，落在 anchor 之前的上一关键帧，解码出的
    // 锚点前旧画面须丢弃（否则旧画面按正常节奏走完整个 GOP：画面卡旧内容，
    // 且视频队列积压阻塞 demux、饿死音频队列导致声音卡住）。seek 时一次性消费。
    double m_pendingVideoPtsDropBefore{-1.0};
    // 视频线程 PTS 丢弃阈值（秒，视频流原始时间基）：普通 seek 直接按目标位置
    // 计算；startThreads 创建视频线程时注入。-1 表示不丢弃。
    double m_videoPtsDropBefore{-1.0};
    FrameQueue *m_videoFrameQueue;
    FrameQueue *m_audioFrameQueue{nullptr};

    AVSyncController *m_syncController;
    AudioOutput *m_audioOutput;
    QThread *m_displayThread{nullptr};
    std::atomic<bool> m_displayStopRequested{false};

    // 帧率检测与同步统计
    double m_lastDisplayedPts{-1.0};   // 上一帧显示的PTS
    int m_fpsFrameCount{0};             // 帧率计算用帧计数
    QElapsedTimer m_fpsTimer;           // 帧率计算用计时器

    AVRational m_videoTimeBase{1, 90000};

    std::atomic<bool> m_paused;

    double m_position;
    double m_duration;
    qint64 m_startTimeUs;
    qint64 m_pausedDurationUs;
    qint64 m_pauseStartUs;
    QTimer *m_positionTimer;

    double m_volume;
    bool m_muted;
    bool m_audioOutputReady;
    double m_speed;
    double m_displaySpeed{1.0};     // 界面显示用速度（变速过渡期保持旧值）
    bool m_isLoading{false};
    QString m_loadingText;

    // 画面旋转 / 翻转状态 (UC-07)
    int m_rotation{0};
    bool m_flipVertical{false};

    QString m_coverArtUrl;

    NetworkStreamManager *m_networkManager{nullptr};

    QList<SubtitleEntry> m_lyrics;
    QString m_currentLyric;

    struct SubtitleStreamInfo {
        int streamIndex = -1;
        QString language;
        QString title;
        bool isExternal = false;
        QString filePath;
        QString label;
    };
    QVector<SubtitleStreamInfo> m_subtitleStreamsInfo;
    int m_currentSubtitleStreamIndex{-1};
    QString m_currentSubtitle;

    struct AudioStreamInfo {
        int streamIndex = -1;
        QString language;
        QString title;
        int channels = 0;
        int sampleRate = 0;
    };
    QVector<AudioStreamInfo> m_audioStreamsInfo;
    int m_currentAudioStreamIndex{-1};

    // 中断回调上下文
    struct InterruptContext {
        std::atomic<bool> interrupted{false};
        QElapsedTimer lastReadTime;
    };
    InterruptContext *m_interruptCtx{nullptr};

    // 网络流异步初始化
    QFutureWatcher<bool> *m_networkInitWatcher{nullptr};

    // 缓冲状态管理
    BufferState m_bufferState{BufferPlaying};
    QTimer *m_bufferCheckTimer{nullptr};
    bool m_bufferSuppressed{false}; // seek 后临时抑制缓冲检测
    // EOF 排空门控定时器（50ms）：demux 报 EOF 后轮询帧队列/FIFO，
    // 全部播完才发 playbackFinished（短视频不丢尾）。
    QTimer *m_eofDrainTimer{nullptr};

    std::atomic<bool> m_seekInProgress{false}; // 后台 seek 进行中标志
    // seek 代数（仅主线程读写）：网络流 seek 异步完成后排队一个"重启管线"
    // 回调。期间若发生更新的 seek/音轨切换/stop，该回调须作废（代数不匹配则
    // 直接返回），否则会与后续操作交错删除线程、重启管线（use-after-free）。
    int m_seekGeneration{0};
    // 音轨切换因在途后台 seek 而推迟的起始时刻（av_gettime 微秒，-1 未推迟）：
    // 超过上限放弃切换，防止事件循环反复重排队导致无限循环。
    qint64 m_switchRetryStartUs{-1};

    bool m_externalMode{false};
    QList<SubtitleEntry> m_externalSubtitles;
    // 字幕显示延迟（毫秒，正值 = 推迟）。updateSubtitle 按此偏移查询位置。
    qint64 m_subtitleDelayMs{0};

#ifdef ENABLE_HWACCEL
    AVBufferRef *m_hwDeviceCtx{nullptr};
    AVPixelFormat m_hwPixFmt{AV_PIX_FMT_NONE};
    bool m_useHardwareDecode{false};

    bool createHwDeviceContext(const AVCodec *codec);
#endif
};
