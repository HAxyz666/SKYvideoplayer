#pragma once

#include <QObject>
#include <QString>

extern "C" {
#include <libavformat/avformat.h>
}

// 封装网络流相关逻辑：URL 判定、FFmpeg 参数配置、直播流检测。
// 从 MediaEngine 中分离，职责单一，避免网络阻塞操作污染主引擎。
class NetworkStreamManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isNetworkStream READ isNetworkStream NOTIFY changed)
    Q_PROPERTY(bool isLiveStream READ isLiveStream NOTIFY changed)

public:
    explicit NetworkStreamManager(QObject *parent = nullptr);

    // 判断 URL 是否为网络流
    static bool isNetworkUrl(const QString &url);

    // 在 avformat_open_input 之前调用，向字典写入协议层参数
    void buildOpenOptions(AVDictionary **opts, const QString &url) const;

    // 在 avformat_find_stream_info 之后调用，检测是否为直播流
    void detectLiveStream(AVFormatContext *ctx);

    bool isNetworkStream() const { return m_isNetwork; }
    bool isLiveStream() const { return m_isLive; }
    void reset();

signals:
    void changed();

private:
    bool m_isNetwork{false};
    bool m_isLive{false};
};
