import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SKYvideoplayer 1.0

Item {
    id: appLayout

    property alias controller: controller
    readonly property alias showControls: controlBar.showControls

    PlayerController { id: controller }

    // 隐藏倒计时：3秒无操作后隐藏控制栏
    Timer {
        id: hideTimer
        interval: 3000
        onTriggered: {
            console.log("hideTimer fired, hiding controlBar")
            controlBar.showControls = false
        }
    }

    // === 底层：初始选择界面 (当没有媒体文件时显示) ===
    RowLayout {
        id: selectionView
        anchors.fill: parent
        spacing: 0
        visible: !controller.hasMedia

        // 左侧：Logo + 菜单
        AppSidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 160
            onOpenFileTriggered: appController.openFile()
        }

        // 中间：主界面 + 最近播放记录
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: appController.theme === "dark" ? "#2d2d2d" : "#f0f0f0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 40
                spacing: 16

                Label {
                    text: qsTr("SKYPlayer")
                    font.bold: true
                    font.pixelSize: 36
                    color: appController.theme === "dark" ? "#ffffff" : "#333333"
                    horizontalAlignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                }

                Label {
                    text: qsTr("点击左侧「openfile」选择视频开始播放")
                    font.pixelSize: 14
                    color: appController.theme === "dark" ? "#cccccc" : "#888888"
                    horizontalAlignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: appController.theme === "dark" ? "#3d3d3d" : "#cccccc"
                    Layout.topMargin: 8
                    Layout.bottomMargin: 8
                }

                RecentHistory {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: appController.recentFilesModel
                    onFileDoubleClicked: function(filePath) {
                        controller.openFile(filePath)
                    }
                }
            }
        }
    }

    // === 顶层：沉浸式播放界面 (当加载了媒体文件时显示) ===
    Rectangle {
        id: playerView
        anchors.fill: parent
        color: "black"
        visible: controller.hasMedia

        onVisibleChanged: {
            console.log("playerView.visible =", visible)
            if (visible) {
                controlBar.showControls = true
                hideTimer.restart()
            }
        }

        HoverHandler {
            onHoveredChanged: {
                console.log("playerView hovered =", hovered)
                if (hovered) {
                    controlBar.showControls = true
                    hideTimer.restart()
                }
            }
        }

        // 双击检测定时器：单击等待 250ms 确认非双击后执行暂停/继续
        Timer {
            id: doubleTapTimer
            interval: 250
            onTriggered: {
                console.log("singleTap confirmed")
                controlBar.showControls = true
                hideTimer.restart()
                controller.togglePlay()
            }
        }

        TapHandler {
            onTapped: {
                // 过滤控制栏区域的点击
                if (point.position.y >= playerView.height - controlBar.height)
                    return
                // 过滤播放列表区域的点击
                if (playlistDrawer.opened && point.position.x >= playerView.width - playlistDrawer.width)
                    return
                if (doubleTapTimer.running) {
                    // 第二次点击在超时内 → 双击，取消定时器，切换全屏
                    doubleTapTimer.stop()
                    console.log("doubleTap detected")
                    window.toggleMaximize()
                } else {
                    // 第一次点击 → 启动定时器等待双击确认
                    doubleTapTimer.restart()
                }
            }
        }

        VideoRenderItem {
            id: videoRenderItem
            objectName: "videoRenderItem"
            anchors.fill: parent
            visible: !controller.isAudioOnly
            // 画面旋转 / 翻转绑定 (UC-07)
            videoRotation: appController.rotation
            flipVertical: appController.flipVertical
        }

        // 字幕叠加层（文本）— 背景透明
        Item {
            id: subtitleOverlay
            anchors.fill: parent
            visible: !controller.isAudioOnly
            z: 10

            Text {
                id: subtitleText

                // 水平始终居中
                anchors.horizontalCenter: parent.horizontalCenter

                // 垂直位置由 settingsManager.subtitleStyle.position 控制
                y: {
                    var pos = settingsManager.subtitleStyle.position || "bottom"
                    if (pos === "top")
                        return 96
                    if (pos === "center")
                        return (parent.height - height) / 2
                    return parent.height - height - 96   // bottom (默认)
                }

                text: appController.currentSubtitle

                // 样式来自 SettingsManager
                color: settingsManager.subtitleStyle.color || "#FFFFFF"
                font.family: settingsManager.subtitleStyle.fontFamily || "Sans Serif"
                font.pixelSize: settingsManager.subtitleStyle.fontSize || 20
                font.bold: true

                horizontalAlignment: Text.Center
                width: parent.width * 0.75
                wrapMode: Text.Wrap
                style: Text.Outline
                styleColor: "#80000000"
                lineHeight: 1.3
                visible: text.length > 0
            }
        }

        Rectangle {
            id: audioOverlay
            anchors.fill: parent
            color: "black"
            visible: controller.isAudioOnly

            Image {
                anchors.fill: parent
                source: appController.coverArtUrl
                fillMode: Image.PreserveAspectFit
                visible: appController.coverArtUrl !== ""
            }

            ColumnLayout {
                anchors.centerIn: parent
                visible: appController.coverArtUrl === ""
                spacing: 16

                Label {
                    text: "♪"
                    font.pixelSize: 72
                    color: "white"
                    Layout.alignment: Qt.AlignHCenter
                }

                Label {
                    text: qsTr("正在播放音频")
                    color: "white"
                    font.pixelSize: 28
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // 歌词叠加层 — 覆盖在封面/视频之上
        Text {
            id: lyricText
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height - height - 120
            width: parent.width * 0.8
            z: 20
            text: appController.currentLyric
            color: "#FFFFFF"
            font.pixelSize: 24
            font.bold: true
            horizontalAlignment: Text.Center
            wrapMode: Text.Wrap
            style: Text.Outline
            styleColor: "#C0000000"
            lineHeight: 1.4
            visible: text.length > 0
        }

        // 左上角：返回按钮
        Button {
            id: backBtn
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 12
            visible: controlBar.showControls
            icon.source: "qrc:/icons/icons/exit_play.svg"
            icon.width: 20
            icon.height: 20
            icon.color: "transparent"
            display: AbstractButton.IconOnly
            flat: true
            padding: 0
            width: 32
            height: 32
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Back to list")
            ToolTip.delay: 500
            onClicked: {
                videoRenderItem.clearImage()
                controller.closeFile()
            }
        }

        // 底部 hover 检测层（始终可见，用于控制栏隐藏时检测鼠标进入）
        Item {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 80
            HoverHandler {
                onHoveredChanged: {
                    if (hovered) {
                        controlBar.showControls = true
                        hideTimer.restart()
                    }
                }
            }
        }

        // 左上角 hover 检测层（返回按钮区域，触发控制栏重新显示）
        Item {
            anchors.top: parent.top
            anchors.left: parent.left
            width: 80
            height: 80
            HoverHandler {
                onHoveredChanged: {
                    if (hovered) {
                        controlBar.showControls = true
                        hideTimer.restart()
                    }
                }
            }
        }

        // 底部悬浮控制区
        Rectangle {
            id: controlBar
            anchors.bottom: parent.bottom
            width: parent.width
            height: 80
            color: appController.theme === "dark" ? "#80000000" : "#cc000000"

            property bool showControls: true

            visible: showControls

            HoverHandler {
                onHoveredChanged: {
                    console.log("controlBar hovered =", hovered)
                    if (hovered) {
                        controlBar.showControls = true
                        hideTimer.restart()
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4

                ProgressBar {
                    Layout.fillWidth: true
                    position: appController.position
                    duration: appController.duration
                    onSeekRequested: function(pos) {
                        appController.seekTo(pos)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Button {
                        icon.source: "qrc:/icons/icons/prev_file.svg"
                        icon.width: 20
                        icon.height: 20
                        icon.color: "transparent"
                        display: AbstractButton.IconOnly
                        flat: true
                        padding: 0
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Prev")
                        ToolTip.delay: 500
                        enabled: appController.playlistModel.hasPrev
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#30ffffff" : "transparent"
                        }
                        onClicked: appController.playPrev()
                    }

                    Button {
                        icon.source: controller.isPlaying ? "qrc:/icons/icons/pause.svg" : "qrc:/icons/icons/play.svg"
                        icon.width: 20
                        icon.height: 20
                        icon.color: "transparent"
                        display: AbstractButton.IconOnly
                        flat: true
                        padding: 0
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        ToolTip.visible: hovered
                        ToolTip.text: controller.isPlaying ? qsTr("Pause") : qsTr("Play")
                        ToolTip.delay: 500
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#30ffffff" : "transparent"
                        }
                        onClicked: controller.togglePlay()
                    }

                    Button {
                        icon.source: "qrc:/icons/icons/next_file.svg"
                        icon.width: 20
                        icon.height: 20
                        icon.color: "transparent"
                        display: AbstractButton.IconOnly
                        flat: true
                        padding: 0
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Next")
                        ToolTip.delay: 500
                        enabled: appController.playlistModel.hasNext
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#30ffffff" : "transparent"
                        }
                        onClicked: appController.playNext()
                    }

                    // 播放速度选择
                    Button {
                        id: speedBtn
                        text: controller.speed + "x"
                        flat: true
                        padding: 0
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 32
                        font.pixelSize: 12
                        contentItem: Text {
                            text: speedBtn.text
                            font: speedBtn.font
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 4
                            color: speedBtn.hovered ? "#30ffffff" : "transparent"
                        }
                        onClicked: {
                            var h = speedMenu.height > 0 ? speedMenu.height : 200
                            speedMenu.popup(speedBtn, 0, -h)
                        }
                    }

                    Menu {
                        id: speedMenu

                        Repeater {
                            model: [
                                { label: "0.5x",  value: 0.5 },
                                { label: "0.75x", value: 0.75 },
                                { label: "1.0x",  value: 1.0 },
                                { label: "1.25x", value: 1.25 },
                                { label: "1.5x",  value: 1.5 },
                                { label: "2.0x",  value: 2.0 }
                            ]

                            MenuItem {
                                text: modelData.label
                                checkable: true
                                checked: controller.speed === modelData.value
                                onTriggered: controller.setSpeed(modelData.value)
                            }
                        }
                    }

                    SubtitleSelector { }

                    // 画面旋转入口：底部「画面」按钮，菜单弹出在按钮上方
                    Button {
                        id: videoBtn
                        icon.source: "qrc:/icons/icons/video_adjust.svg"
                        icon.width: 20
                        icon.height: 20
                        icon.color: "transparent"
                        display: AbstractButton.IconOnly
                        flat: true
                        padding: 0
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Video")
                        ToolTip.delay: 500
                        background: Rectangle {
                            radius: 4
                            color: videoBtn.hovered ? "#30ffffff" : "transparent"
                        }
                        onClicked: {
                            var h = videoMenu.height > 0 ? videoMenu.height : 140
                            videoMenu.popup(videoBtn, 0, -h)
                        }
                    }

                    Menu {
                        id: videoMenu

                        MenuItem {
                            text: qsTr("Rotate Left 90°")
                            onTriggered: appController.rotateLeft()
                        }
                        MenuItem {
                            text: qsTr("Rotate Right 90°")
                            onTriggered: appController.rotateRight()
                        }
                        MenuItem {
                            text: qsTr("Flip Vertical")
                            checkable: true
                            checked: appController.flipVertical
                            onTriggered: appController.toggleFlipVertical()
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("Reset")
                            onTriggered: appController.resetRotation()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // 音量控制：水平滑块
                    VolumeControl {
                        Layout.preferredWidth: 140
                        Layout.preferredHeight: 32
                    }

                    Button {
                        icon.source: "qrc:/icons/icons/play_list.svg"
                        icon.width: 20
                        icon.height: 20
                        icon.color: "transparent"
                        display: AbstractButton.IconOnly
                        flat: true
                        padding: 0
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Playlist")
                        ToolTip.delay: 500
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#30ffffff" : "transparent"
                        }
                        onClicked: playlistDrawer.opened ? playlistDrawer.close() : playlistDrawer.open()
                    }

                    Button {
                        icon.source: window.isFullscreen ? "qrc:/icons/icons/exit_fullscreen.svg" : "qrc:/icons/icons/fullscreen.svg"
                        icon.width: 20
                        icon.height: 20
                        icon.color: "transparent"
                        display: AbstractButton.IconOnly
                        flat: true
                        padding: 0
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        ToolTip.visible: hovered
                        ToolTip.text: window.isFullscreen ? qsTr("Exit Fullscreen") : qsTr("Fullscreen")
                        ToolTip.delay: 500
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? "#30ffffff" : "transparent"
                        }
                        onClicked: window.toggleMaximize()
                    }
                }
            }
        }
    }

    PlaylistPanel {
        id: playlistDrawer
    }

    Connections {
        target: appController
        function onRequestOpenFile() { fileDialog.open() }
        function onPlaybackStateChanged(isPlaying) { controller.isPlaying = isPlaying }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Select media file")
        nameFilters: [
            qsTr("all file (*)"),
            qsTr("video file (*.mp4 *.mkv *.avi *.mov *.flv *.wmv)"),
            qsTr("audio file (*.mp3 *.flac *.wav *.aac *.ogg *.opus *.m4a *.wma)")
        ]
        onAccepted: controller.openFile(selectedFile)
    }
}
