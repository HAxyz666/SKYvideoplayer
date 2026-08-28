#pragma once

#include <QString>

// 播放列表项数据结构
struct PlaylistItem {
    QString title;          // 显示标题（文件名/网络流真实标题）
    QString filePath;       // 完整文件路径（网络流为原始输入 URL）
    double duration = 0.0;  // 时长（秒），0 表示未获取
    int mode = 0;           // 播放模式（0=原生 1=直播 2=点播），网络流条目点击时据此重新解析
};
