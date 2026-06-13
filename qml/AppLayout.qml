import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SKYvideoplayer 1.0

Item {
    id: appLayout

    PlayerController { id: controller }

    // === 底层：初始选择界面 (当没有媒体文件时显示) ===
    RowLayout {
        id: selectionView
        anchors.fill: parent
        spacing: 0
        visible: !controller.hasMedia

        // 左侧：Logo + 菜单
        AppSidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            onOpenFileTriggered: appController.openFile()
        }

        // 中间：历史列表功能区 (预留，尚未实现)
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "#f0f0f0"
        }
    }

    // === 顶层：沉浸式播放界面 (当加载了媒体文件时显示) ===
    Rectangle {
        id: playerView
        anchors.fill: parent
        color: "black"
        visible: controller.hasMedia

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
            icon.name: "go-previous"
            icon.width: 24
            icon.height: 24
            text: qsTr("Back to list")
            onClicked: {
                videoRenderItem.clearImage()
                controller.closeFile()
            }
        }

        // 底部悬浮控制区
        Rectangle {
            id: controlBar
            anchors.bottom: parent.bottom
            width: parent.width
            height: 80
            color: "#80000000"

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
                        icon.name: controller.isPlaying ? "media-playback-pause" : "media-playback-start"
                        icon.width: 24
                        icon.height: 24
                        text: controller.isPlaying ? qsTr("Pause") : qsTr("Play")
                        onClicked: controller.togglePlay()
                    }

                    Item { Layout.fillWidth: true }

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
