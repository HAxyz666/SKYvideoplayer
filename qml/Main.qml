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

    readonly property color windowBtnColor: appController.theme === "dark" ? "#ffffff" : "#555555"
    readonly property color windowBtnHover:  appController.theme === "dark" ? "#40ffffff" : "#40000000"
    readonly property color windowBtnCloseHover: "#80e04040"

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

    // 右上角窗口控制按钮（简约风格）— 已禁用
    // Row {
    //     anchors.top: parent.top
    //     anchors.right: parent.right
    //     anchors.margins: 8
    //     spacing: 6
    //     z: 999
    //     visible: !appLayout.controller.hasMedia || appLayout.showControls
    //
    //     Rectangle {
    //         width: 36; height: 28; radius: 5
    //         color: minHover.hovered ? windowBtnHover : "transparent"
    //         Behavior on color { ColorAnimation { duration: 150 } }
    //         Text {
    //             anchors.centerIn: parent
    //             text: "\u2500"
    //             color: windowBtnColor
    //             font.pixelSize: 16
    //         }
    //         HoverHandler { id: minHover }
    //         TapHandler { onTapped: window.showMinimized() }
    //     }
    //
    //     Rectangle {
    //         width: 36; height: 28; radius: 5
    //         color: maxHover.hovered ? windowBtnHover : "transparent"
    //         Behavior on color { ColorAnimation { duration: 150 } }
    //         Text {
    //             anchors.centerIn: parent
    //             text: window.visibility === Window.Maximized ? "\u2750" : "\u25A1"
    //             color: windowBtnColor
    //             font.pixelSize: 16
    //         }
    //         HoverHandler { id: maxHover }
    //         TapHandler {
    //             onTapped: {
    //                 if (window.visibility === Window.Maximized)
    //                     window.showNormal()
    //                 else
    //                     window.showMaximized()
    //             }
    //         }
    //     }
    //
    //     Rectangle {
    //         width: 36; height: 28; radius: 5
    //         color: closeHover.hovered ? windowBtnCloseHover : "transparent"
    //         Behavior on color { ColorAnimation { duration: 150 } }
    //         Text {
    //             anchors.centerIn: parent
    //             text: "\u2715"
    //             color: windowBtnColor
    //             font.pixelSize: 16
    //         }
    //         HoverHandler { id: closeHover }
    //         TapHandler { onTapped: window.close() }
    //     }
    // }
}
