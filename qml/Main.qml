import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SKYvideoplayer 1.0

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: true
    title: appController.currentFilePath !== ""
           ? appController.currentFilePath.split("/").pop().split("\\").pop()
           : "Video Audio Sync Player"
    //flags: Qt.Window | Qt.FramelessWindowHint


    function applyTheme() {
        var d = appController.theme === "dark"
        palette.window      = d ? "#1e1e1e" : "#f5f5f5"
        palette.windowText  = d ? "#ffffff" : "#222222"
        palette.base        = d ? "#2d2d2d" : "#ffffff"
        palette.text        = d ? "#ffffff" : "#222222"
        palette.button      = d ? "#3d3d3d" : "#e0e0e0"
        palette.buttonText  = d ? "#ffffff" : "#333333"
        palette.highlight   = "#0078d7"
    }

    property string currentTheme: appController.theme
    onCurrentThemeChanged: applyTheme()
    Component.onCompleted: applyTheme()

    property bool isFullscreen: false
    property real savedWidth: 1280
    property real savedHeight: 720
    property real savedX: 0
    property real savedY: 0

    function toggleMaximize() {
        if (isFullscreen) {
            window.visibility = Window.Windowed
            window.width = savedWidth
            window.height = savedHeight
            window.x = savedX
            window.y = savedY
            isFullscreen = false
        } else {
            savedWidth = window.width
            savedHeight = window.height
            savedX = window.x
            savedY = window.y
            window.visibility = Window.FullScreen
            isFullscreen = true
        }
    }

    onVisibilityChanged: function(visibility) {
        if (visibility !== Window.FullScreen && isFullscreen) {
            isFullscreen = false
            window.width = savedWidth
            window.height = savedHeight
            window.x = savedX
            window.y = savedY
        }
    }

    // 音量 / 静音快捷键
    // ApplicationShortcut 在焦点系统之前处理，避免被焦点导航拦截
    Shortcut { sequence: settingsManager.shortcuts["volumeUp"] || "Up";   context: Qt.ApplicationShortcut; onActivated: appController.volume = appController.volume + 5 }
    Shortcut { sequence: settingsManager.shortcuts["volumeDown"] || "Down"; context: Qt.ApplicationShortcut; onActivated: appController.volume = appController.volume - 5 }
    Shortcut { sequence: settingsManager.shortcuts["toggleMute"] || "M";    onActivated: appController.toggleMute() }
    Shortcut { sequence: settingsManager.shortcuts["togglePlayback"] || "Space"; onActivated: appController.togglePlayback() }

    Shortcut { sequence: settingsManager.shortcuts["stepBackward"] || "Left";  context: Qt.ApplicationShortcut; onActivated: appController.stepBackward() }
    Shortcut { sequence: settingsManager.shortcuts["stepForward"] || "Right"; context: Qt.ApplicationShortcut; onActivated: appController.stepForward() }
    Shortcut { sequence: settingsManager.shortcuts["stepBackwardLarge"] || "Ctrl+Left";  context: Qt.ApplicationShortcut; onActivated: appController.stepBackwardLarge() }
    Shortcut { sequence: settingsManager.shortcuts["stepForwardLarge"] || "Ctrl+Right"; context: Qt.ApplicationShortcut; onActivated: appController.stepForwardLarge() }

    Shortcut { sequence: settingsManager.shortcuts["toggleFullscreen"] || "F11"; onActivated: window.toggleMaximize() }

    // 字幕延迟微调：[/] 每次 ±0.1s，按文件记忆
    Shortcut { sequence: settingsManager.shortcuts["subtitleDelayBackward"] || "["; onActivated: appController.nudgeSubtitleDelay(-100) }
    Shortcut { sequence: settingsManager.shortcuts["subtitleDelayForward"] || "]"; onActivated: appController.nudgeSubtitleDelay(100) }

    // A-B 区间循环（A 设起点 → 再按设终点 → 再按清除）/ 逐帧步进（mpv 风格 .）
    Shortcut { sequence: settingsManager.shortcuts["abLoop"] || "A"; onActivated: appController.toggleABLoop() }
    Shortcut { sequence: settingsManager.shortcuts["stepFrame"] || "."; onActivated: appController.stepFrameForward() }

    // 画面调节快捷键（mpv 风格）：3/4 亮度、5/6 对比度、7/8 饱和度，Z 循环缩放模式
    Shortcut { sequence: settingsManager.shortcuts["brightnessDown"] || "3"; context: Qt.ApplicationShortcut; onActivated: settingsManager.brightness = Math.max(-100, settingsManager.brightness - 5) }
    Shortcut { sequence: settingsManager.shortcuts["brightnessUp"] || "4";   context: Qt.ApplicationShortcut; onActivated: settingsManager.brightness = Math.min(100, settingsManager.brightness + 5) }
    Shortcut { sequence: settingsManager.shortcuts["contrastDown"] || "5";   context: Qt.ApplicationShortcut; onActivated: settingsManager.contrast = Math.max(-100, settingsManager.contrast - 5) }
    Shortcut { sequence: settingsManager.shortcuts["contrastUp"] || "6";     context: Qt.ApplicationShortcut; onActivated: settingsManager.contrast = Math.min(100, settingsManager.contrast + 5) }
    Shortcut { sequence: settingsManager.shortcuts["saturationDown"] || "7"; context: Qt.ApplicationShortcut; onActivated: settingsManager.saturation = Math.max(-100, settingsManager.saturation - 5) }
    Shortcut { sequence: settingsManager.shortcuts["saturationUp"] || "8";   context: Qt.ApplicationShortcut; onActivated: settingsManager.saturation = Math.min(100, settingsManager.saturation + 5) }
    Shortcut { sequence: settingsManager.shortcuts["cycleAspectMode"] || "Z"; context: Qt.ApplicationShortcut; onActivated: settingsManager.scaleMode = (settingsManager.scaleMode + 1) % 3 }

    Shortcut { sequence: settingsManager.shortcuts["exitFullscreen"] || "Escape"; onActivated: {
        if (window.isFullscreen) window.toggleMaximize()
    }}

    AppLayout {
        id: appLayout
        anchors.fill: parent
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onEntered: function(drag) {
            drag.accepted = drag.urls.some(function(url) {
                var path = url.toString()
                return /\.(mp4|mkv|avi|mov|flv|wmv|mp3|flac|wav|aac|ogg|opus|m4a|wma)$/i.test(path)
            })
        }

        onDropped: function(drop) {
            if (drop.urls.length > 0)
                appLayout.controller.openFile(drop.urls[0].toString())
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#88000000"
        visible: dropArea.containsDrag

        Label {
            anchors.centerIn: parent
            text: qsTr("Drop to play media file")
            font.pixelSize: 28
            font.bold: true
            color: "white"
        }
    }
}
