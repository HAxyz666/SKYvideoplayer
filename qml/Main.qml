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

    // 音量 / 静音快捷键
    // ApplicationShortcut 在焦点系统之前处理，避免被焦点导航拦截
    Shortcut { sequence: "Up";   context: Qt.ApplicationShortcut; onActivated: appController.volume = appController.volume + 5 }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: appController.volume = appController.volume - 5 }
    Shortcut { sequence: "M";    onActivated: appController.toggleMute() }
    Shortcut { sequence: "Space"; onActivated: appController.togglePlayback() }

    Shortcut { sequence: "Left";  context: Qt.ApplicationShortcut; onActivated: appController.stepBackward() }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: appController.stepForward() }
    Shortcut { sequence: "Ctrl+Left";  context: Qt.ApplicationShortcut; onActivated: appController.stepBackwardLarge() }
    Shortcut { sequence: "Ctrl+Right"; context: Qt.ApplicationShortcut; onActivated: appController.stepForwardLarge() }

    Shortcut { sequence: "F11"; onActivated: window.toggleMaximize() }

    Shortcut { sequence: "Escape"; onActivated: {
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
            text: qsTr("松开以播放媒体文件")
            font.pixelSize: 28
            font.bold: true
            color: "white"
        }
    }
}
