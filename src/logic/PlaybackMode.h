#pragma once

#include <QObject>

enum class PlaybackMode {
    Sequential,  // 顺序播放
    Loop,        // 列表循环
    LoopOne      // 单集循环
};

Q_DECLARE_METATYPE(PlaybackMode)
