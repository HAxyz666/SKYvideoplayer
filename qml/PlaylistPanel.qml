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
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("暂无文件")
                opacity: 0.5
                visible: listView.count === 0
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
