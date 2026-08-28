#pragma once

#include <QString>
#include <QVariantMap>
#include <QVariantList>

// 网络流解析结果（yt-dlp/streamlink 等工具解析后的可播源描述）。
// 平台无关的共享数据契约：Resolver 产出，MediaEngine 消费。
//
// 两种形态：
//  - 单 URL（音视频合一）：directUrl + headers 非空。
//  - DASH 分离流（split 模式）：videoUrl + audioUrl 各自携带 headers。
struct ResolveResult {
    bool ok = false;             // 解析是否成功
    QString error;               // 失败原因（ok=false 时展示给用户）
    bool cancelled = false;      // 是否被用户取消（用于重试判断，取消不重试）

    // 单 URL 形态
    QString directUrl;
    QVariantMap headers;

    // DASH 分离流形态（引擎 split 模式）
    QString videoUrl;
    QString audioUrl;
    QVariantMap videoHeaders;
    QVariantMap audioHeaders;

    QString title;               // 站点标题（备用展示）
    bool isLive = false;         // 是否直播流
    QVariantList formats;        // 清晰度列表（v1 未启用选择器，预留）
};
