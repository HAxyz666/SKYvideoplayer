import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SKYvideoplayer 1.0

Item {
    id: appLayout

    property alias controller: controller
    readonly property alias showControls: playerView.showControls

    PlayerController { id: controller }

    Timer {
        id: hideTimer
        interval: 3000
        onTriggered: {
            console.log("hideTimer fired, hiding controlBar")
            playerView.showControls = false
        }
    }

    // === 底层：初始选择界面 (当没有媒体文件时显示) ===
    RowLayout {
        id: selectionView
        anchors.fill: parent
        spacing: 0
        visible: !controller.hasMedia

        AppSidebar {
            Layout.fillHeight: true
            Layout.preferredWidth: 160
            onOpenFileTriggered: appController.openFile()
            onOpenNetworkTriggered: networkDialog.open()
        }

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
    PlayerView {
        id: playerView
        anchors.fill: parent
        visible: controller.hasMedia
        controller: appLayout.controller
        playlistDrawer: playlistDrawer
        isFullscreen: window.isFullscreen
        onToggleFullscreen: window.toggleMaximize()
        onUserInteracted: hideTimer.restart()
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

    Dialog {
        id: networkDialog
        title: qsTr("Open Network Stream")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 500

        property alias url: urlInput.text

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label {
                text: qsTr("Enter network URL:")
                font.pixelSize: 14
                color: appController.theme === "dark" ? "#ffffff" : "#333333"
            }

            TextField {
                id: urlInput
                Layout.fillWidth: true
                placeholderText: "http://example.com/video.mp4"
                font.pixelSize: 14
                selectByMouse: true
                onAccepted: networkDialog.accept()
            }

            Label {
                text: qsTr("Supported: HTTP, HTTPS, RTMP, RTSP, UDP, TCP")
                font.pixelSize: 11
                color: appController.theme === "dark" ? "#888888" : "#666666"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        onAccepted: {
            if (urlInput.text.trim().length > 0)
                controller.openFile(urlInput.text.trim())
            urlInput.text = ""
        }

        onClosed: urlInput.text = ""
    }
}
