import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import SKYvideoplayer 1.0

Item {
    id: appLayout

    property alias controller: controller

    PlayerController { id: controller }

    Timer {
        id: hideTimer
        interval: 3000
        onTriggered: {
            // 子菜单打开期间不隐藏控制条，关闭时由 onClosed 重启计时
            if (!playerView.anyPopupOpen)
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
                    onFileDoubleClicked: function(filePath, mode) {
                        controller.openRecentFile(filePath, mode)
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

    // 字幕延迟调整提示条（快捷键 [/] 触发，2.5s 自动隐藏）
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 90
        width: Math.min(subDelayLabel.implicitWidth + 40, parent.width * 0.6)
        height: 36
        radius: 18
        color: "#cc333333"
        visible: appController.subtitleDelayToastVisible
        z: 100

        Label {
            id: subDelayLabel
            anchors.centerIn: parent
            width: parent.width - 40
            text: appController.subtitleDelayToastText
            font.pixelSize: 13
            color: "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    // A-B 循环状态提示条（快捷键 A / 控件条按钮触发，2.5s 自动隐藏）
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 135
        width: Math.min(abLoopLabel.implicitWidth + 40, parent.width * 0.6)
        height: 36
        radius: 18
        color: "#cc333333"
        visible: appController.abLoopToastVisible
        z: 100

        Label {
            id: abLoopLabel
            anchors.centerIn: parent
            width: parent.width - 40
            text: appController.abLoopToastText
            font.pixelSize: 13
            color: "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
    }

    Connections {
        target: appController
        function onRequestOpenFile() { fileDialog.open() }
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

    NetworkDialog {
        id: networkDialog
        controller: appLayout.controller
    }
}
