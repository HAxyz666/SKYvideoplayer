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

        // 视频渲染区 (铺满整个窗口)
        VideoRenderItem {
            id: videoRenderItem
            objectName: "videoRenderItem"
            anchors.fill: parent
        }

        // 底部悬浮控制区
        Rectangle {
            id: controlBar
            anchors.bottom: parent.bottom
            width: parent.width
            height: 60
            color: "#80000000"

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10

                Button {
                    text: controller.isPlaying ? "pause" : "play"
                    onClicked: controller.togglePlay()
                }

                Button {
                    text: qsTr("Back to list")
                    onClicked: controller.closeFile()
                }
            }
        }
    }

    Connections {
        target: appController
        function onRequestOpenFile() { fileDialog.open() }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Select video file")
        nameFilters: [qsTr("video file (*.mp4 *.mkv *.avi *.mov *.flv *.wmv)"), qsTr("all file (*)")]
        onAccepted: controller.openFile(selectedFile)
    }
}
