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
