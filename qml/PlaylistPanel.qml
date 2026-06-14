import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Drawer {
    id: playlistDrawer
    edge: Qt.RightEdge
    width: 280
    height: parent.height
    modal: false
    interactive: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            RowLayout {
                anchors.fill: parent
                spacing: 4

                Label {
                    text: qsTr("播放列表")
                    font.bold: true
                    font.pixelSize: 16
                    Layout.fillWidth: true
                    leftPadding: 8
                }

                ToolButton {
                    icon.name: "window-close"
                    onClicked: playlistDrawer.close()
                }
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: appController.playlistModel
            clip: true

            delegate: ItemDelegate {
                width: ListView.view.width
                text: model.title
                highlighted: model.isPlaying
                onDoubleClicked: appController.playItem(index)
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("暂无文件")
                opacity: 0.5
                visible: listView.count === 0
            }
        }

        // 播放模式选择
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4

            Label {
                text: qsTr("播放模式:")
                font.pixelSize: 12
                opacity: 0.7
            }

            Row {
                id: modeGroup
                spacing: 2

                ToolButton {
                    text: qsTr("顺序")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 0
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 0
                }

                ToolButton {
                    text: qsTr("循环")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 1
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 1
                }

                ToolButton {
                    text: qsTr("随机")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 2
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 2
                }

                ToolButton {
                    text: qsTr("单曲")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 3
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 3
                }
            }
        }

        Label {
            text: listView.count + qsTr(" 个文件")
            padding: 10
            font.pixelSize: 12
            opacity: 0.7
            Layout.fillWidth: true
            horizontalAlignment: Qt.AlignHCenter
        }
    }
}
