#include "NetworkStreamManager.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

NetworkStreamManager::NetworkStreamManager(QObject *parent)
    : QObject(parent)
{
}

bool NetworkStreamManager::isNetworkUrl(const QString &url)
{
    return url.startsWith("http://")  || url.startsWith("https://") ||
           url.startsWith("rtmp://")  || url.startsWith("rtmps://") ||
           url.startsWith("rtsp://")  || url.startsWith("rtsps://") ||
           url.startsWith("mms://")   || url.startsWith("mmsh://")  ||
           url.startsWith("udp://")   || url.startsWith("tcp://");
}

void NetworkStreamManager::buildOpenOptions(AVDictionary **opts, const QString &url) const
{
    buildOpenOptions(opts, url, {});
}

void NetworkStreamManager::buildOpenOptions(AVDictionary **opts, const QString &url, const QVariantMap &headers) const
{
    // 通用网络 I/O 超时（微秒）
    av_dict_set(opts, "timeout", "5000000", 0);

    // 限制探测数据量，加速 avformat_find_stream_info
    av_dict_set(opts, "probesize", "5000000", 0);
    av_dict_set(opts, "max_analyze_duration", "5000000", 0);

    if (url.startsWith("http://") || url.startsWith("https://")) {
        av_dict_set(opts, "connect_timeout", "5", 0);
        av_dict_set(opts, "reconnect", "1", 0);
        av_dict_set(opts, "reconnect_streamed", "1", 0);
        av_dict_set(opts, "reconnect_delay_max", "5", 0);
        av_dict_set(opts, "multiple_requests", "1", 0);

        // 自定义 HTTP 头：防盗链（UA/Referer/Cookie/签名）透传。
        // yt-dlp 解析出的 http_headers 即此格式；键必须含冒号，值含冒号
        // 也可（"Key: value" 拆分在第一个冒号处，其余保留）。
        if (!headers.isEmpty()) {
            QStringList lines;
            for (auto it = headers.constBegin(); it != headers.constEnd(); ++it) {
                const QString key = it.key().trimmed();
                if (key.isEmpty())
                    continue;
                lines << key + u": " + it.value().toString();
            }
            if (!lines.isEmpty())
                av_dict_set(opts, "headers", lines.join("\r\n").toUtf8().constData(), 0);
        }
    }

    if (url.startsWith("rtmp://")  || url.startsWith("rtmps://") ||
        url.startsWith("rtsp://")  || url.startsWith("rtsps://")) {
        av_dict_set(opts, "rtsp_transport", "tcp", 0);
        av_dict_set(opts, "stimeout", "5000000", 0);
    }
}

void NetworkStreamManager::detectLiveStream(AVFormatContext *ctx)
{
    m_isNetwork = true;
    m_isLive = (ctx->duration <= 0 || ctx->duration == AV_NOPTS_VALUE);
}

void NetworkStreamManager::reset()
{
    m_isNetwork = false;
    m_isLive = false;
}
