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

        // 中间：欢迎界面 + 最近播放记录
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "#f0f0f0"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 40
                spacing: 16

                Label {
                    text: qsTr("SKYPlayer")
                    font.bold: true
                    font.pixelSize: 36
                    color: "#333333"
                    horizontalAlignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                }

                Label {
                    text: qsTr("点击左侧「打开文件」选择视频开始播放")
                    font.pixelSize: 14
                    color: "#888888"
                    horizontalAlignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: "#cccccc"
                    Layout.topMargin: 8
                    Layout.bottomMargin: 8
                }

                Label {
                    text: qsTr("最近播放")
                    font.bold: true
                    font.pixelSize: 16
                    color: "#333333"
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: appController.recentFilesModel
                    clip: true

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 40
                        color: mouseArea.containsMouse ? "#d0d0d0" : "transparent"

                        property var entryModel: model

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onDoubleClicked: {
                                controller.openFile(model.filePath)
                            }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            spacing: 8

                            Label {
                                text: model.fileName
                                font.pixelSize: 14
                                color: "#333333"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }

                            Label {
                                text: model.lastPlayed instanceof Date
                                    ? Qt.formatDateTime(model.lastPlayed, "yyyy-MM-dd hh:mm")
                                    : ""
                                font.pixelSize: 11
                                color: "#888888"
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        text: qsTr("暂无播放记录")
                        opacity: 0.5
                        visible: parent.count === 0
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

                    Item { Layout.fillWidth: true }

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
