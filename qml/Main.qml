import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SKYvideoplayer 1.0

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    visible: true
    title: "Video Audio Sync Player"

    palette.window: "#1e1e1e"
    palette.windowText: "#ffffff"
    palette.base: "#2d2d2d"
    palette.text: "#ffffff"
    palette.button: "#3d3d3d"
    palette.buttonText: "#ffffff"
    palette.highlight: "#0078d7"

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

    function toggleMaximized() {
        if (window.visibility === Window.Maximized) {
            window.visibility = Window.Windowed
        } else {
            window.visibility = Window.Maximized
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

    // 音量 / 静音快捷键（对应 UML §3.3.1）
    // ApplicationShortcut 在焦点系统之前处理，避免被焦点导航拦截
    Shortcut { sequence: "Up";   context: Qt.ApplicationShortcut; onActivated: appController.volume = appController.volume + 5 }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: appController.volume = appController.volume - 5 }
    Shortcut { sequence: "M";    onActivated: appController.toggleMute() }

    Shortcut { sequence: "F11"; onActivated: window.toggleMaximize() }
    Shortcut { sequence: "Escape"; onActivated: {
        if (window.isFullscreen) window.toggleMaximize()
    }}

    AppLayout {
        anchors.fill: parent
    }
}
