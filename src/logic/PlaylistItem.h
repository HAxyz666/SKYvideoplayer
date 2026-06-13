#pragma once

#include <QString>

// 播放列表项数据结构
struct PlaylistItem {
    QString title;          // 显示标题（文件名）
    QString filePath;       // 完整文件路径
    double duration = 0.0;  // 时长（秒），0 表示未获取
};
