import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: root

    signal fileDoubleClicked(string filePath)

    clip: true

    delegate: Rectangle {
        width: ListView.view.width
        height: 40
        property bool hovered: false
        color: hovered ? "#d0d0d0" : "transparent"

        HoverHandler {
            onHoveredChanged: parent.hovered = hovered
        }

        TapHandler {
            cursorShape: Qt.PointingHandCursor
            onDoubleTapped: root.fileDoubleClicked(model.filePath)
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
        visible: root.count === 0
    }
}
