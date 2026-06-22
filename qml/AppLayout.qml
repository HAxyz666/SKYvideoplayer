import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SKYvideoplayer 1.0

Item {
    id: appLayout

    property alias controller: controller

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

                Label {
                    text: qsTr("最近播放")
                    font.bold: true
                    font.pixelSize: 16
                    color: appController.theme === "dark" ? "#ffffff" : "#333333"
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
        }

        // 左上角：返回按钮
        Button {
            id: backBtn
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: 12
            visible: controlBar.showControls
            icon.name: "go-previous"
            icon.width: 24
            icon.height: 24
            text: qsTr("Back to list")
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
                    spacing: 8

                    Button {
                        icon.name: "media-skip-backward"
                        icon.width: 24
                        icon.height: 24
                        text: qsTr("Prev")
                        enabled: appController.playlistModel.hasPrev
                        onClicked: appController.playPrev()
                    }

                    Button {
                        icon.name: controller.isPlaying ? "media-playback-pause" : "media-playback-start"
                        icon.width: 24
                        icon.height: 24
                        text: controller.isPlaying ? qsTr("Pause") : qsTr("Play")
                        onClicked: controller.togglePlay()
                    }

                    Button {
                        icon.name: "media-skip-forward"
                        icon.width: 24
                        icon.height: 24
                        text: qsTr("Next")
                        enabled: appController.playlistModel.hasNext
                        onClicked: appController.playNext()
                    }

                    // 播放速度选择
                    ComboBox {
                        id: speedSelector
                        model: ["0.5x", "1.0x", "1.5x", "2.0x"]
                        currentIndex: 1
                        Layout.preferredWidth: 80
                        font.pixelSize: 12
                        onActivated: function(index) {
                            var speeds = [0.5, 1.0, 1.5, 2.0];
                            controller.setSpeed(speeds[index]);
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // 音量控制：水平滑块
                    VolumeControl {
                        Layout.preferredWidth: 140
                        Layout.preferredHeight: 32
                    }

                    Button {
                        icon.name: "view-list-details"
                        icon.width: 24
                        icon.height: 24
                        text: qsTr("Playlist")
                        onClicked: playlistDrawer.opened ? playlistDrawer.close() : playlistDrawer.open()
                    }

                    Button {
                        icon.name: window.isFullscreen ? "view-restore" : "view-fullscreen"
                        icon.width: 24
                        icon.height: 24
                        text: window.isFullscreen ? qsTr("Exit Fullscreen") : qsTr("Fullscreen")
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
        title: qsTr("Select video file")
        nameFilters: [qsTr("video file (*.mp4 *.mkv *.avi *.mov *.flv *.wmv)"), qsTr("all file (*)")]
        onAccepted: controller.openFile(selectedFile)
    }
}
