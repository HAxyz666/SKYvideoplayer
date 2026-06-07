import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    color: "#e0e0e0"

    signal openFileTriggered()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // Logo 区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: "transparent"

            Row {
                anchors.centerIn: parent
                spacing: 10
                Label { text: "SKYPlayer"; font.bold: true; font.pixelSize: 20; color: "#333333" }
            }
        }

        // 分割线
        Rectangle { Layout.fillWidth: true; height: 1; color: "#cccccc" }

        // 自定义菜单栏 (垂直排列)
        Column {
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 0

            component MenuButton: Button {
                width: parent.width
                height: 40
                flat: true
                font.pixelSize: 14
                contentItem: Label {
                    text: parent.text
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 20
                    color: parent.hovered ? "#000000" : "#333333"
                }
                background: Rectangle {
                    color: parent.hovered ? "#d0d0d0" : "transparent"
                }
            }

            MenuButton {
                text: "open file"
                onClicked: openFileTriggered()
            }
            // ...
        }

        // 底部弹簧
        Item { Layout.fillHeight: true }

        // 底部信息
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 20
            text: "v0.1"
            color: "#888888"
            font.pixelSize: 12
        }
    }
}
