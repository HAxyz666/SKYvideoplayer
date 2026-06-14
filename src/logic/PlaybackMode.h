#pragma once

#include <QObject>

enum class PlaybackMode {
    Sequential,  // 顺序播放
    Loop,        // 列表循环
    Shuffle,     // 随机播放
    LoopOne      // 单曲循环
};

Q_DECLARE_METATYPE(PlaybackMode)
