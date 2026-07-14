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
                    text: qsTr("Click 'Open File' on the left to start playing")
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

    // 网络连接失败提示条
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 40
        width: Math.min(toastLabel.implicitWidth + 40, parent.width * 0.6)
        height: 36
        radius: 18
        color: "#cc333333"
        visible: controller.networkError
        z: 100

        Label {
            id: toastLabel
            anchors.centerIn: parent
            width: parent.width - 40
            text: controller.networkErrorMessage
            font.pixelSize: 13
            color: "#ffffff"
            elide: Text.ElideRight
        }
    }

    Connections {
        target: appController
        function onRequestOpenFile() { fileDialog.open() }
        function onPlaybackStateChanged(isPlaying) { controller.isPlaying = isPlaying }
        function onErrorOccurred(message, isNetworkRelated) {
            controller.hasMedia = false;
            controller.isPlaying = false;
            controller.networkError = true;
            controller.networkErrorMessage = message;
            errorTimer.restart();
        }
    }

    Timer {
        id: errorTimer
        interval: 3000
        onTriggered: {
            controller.networkError = false;
            controller.networkErrorMessage = "";
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Select media file")
        nameFilters: [
            qsTr("all file (*)"),
            qsTr("video file (*.mp4 *.mkv *.avi *.mov *.flv *.wmv)"),
            qsTr("audio file (*.mp3 *.flac *.wav *.aac *.ogg *.opus *.m4a *.wma)")
        ]
        onVisibleChanged: appController.modalCount = appController.modalCount + (visible ? 1 : -1)
        onAccepted: controller.openFile(selectedFile)
    }

    Dialog {
        id: networkDialog
        title: ""
        anchors.centerIn: parent
        modal: true
        width: 500
        padding: 20
        onOpened: appController.modalCount = appController.modalCount + 1

        background: Rectangle {
            radius: 8
            color: appController.theme === "dark" ? "#2d2d2d" : "#f5f5f5"
            border.color: "transparent"
        }

        header: Item { height: 0 }
        footer: Item { height: 0 }

        property alias url: urlInput.text

        contentItem: ColumnLayout {
            spacing: 14

            Label {
                text: qsTr("Open Network Stream")
                font.pixelSize: 16
                font.bold: true
                color: appController.theme === "dark" ? "#ffffff" : "#333333"
            }

            Label {
                text: qsTr("Enter network URL:")
                font.pixelSize: 13
                color: appController.theme === "dark" ? "#cccccc" : "#666666"
            }

            TextField {
                id: urlInput
                Layout.fillWidth: true
                placeholderText: "http://example.com/video.mp4"
                font.pixelSize: 14
                selectByMouse: true
                padding: 8
                background: Rectangle {
                    radius: 4
                    color: appController.theme === "dark" ? "#3d3d3d" : "#e8e8e8"
                    border.color: appController.theme === "dark" ? "#555" : "#ccc"
                }
                onAccepted: networkDialog.accept()
            }

            Label {
                text: qsTr("Supported: HTTP, HTTPS, RTMP, RTSP, UDP, TCP")
                font.pixelSize: 11
                color: appController.theme === "dark" ? "#888888" : "#999999"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Open")
                    flat: true
                    onClicked: networkDialog.accept()
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 13
                        font.bold: true
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                            : (appController.theme === "dark" ? "#cccccc" : "#666666")
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                            : "transparent"
                    }
                }

                Button {
                    text: qsTr("Cancel")
                    flat: true
                    onClicked: networkDialog.close()
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 13
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                            : (appController.theme === "dark" ? "#cccccc" : "#666666")
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                            : "transparent"
                    }
                }
            }
        }

        onAccepted: {
            if (urlInput.text.trim().length > 0)
                controller.openFile(urlInput.text.trim())
            urlInput.text = ""
        }

        onClosed: {
            appController.modalCount = appController.modalCount - 1
            urlInput.text = ""
        }
    }
}
