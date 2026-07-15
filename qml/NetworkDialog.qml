import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: networkDialog

    property var controller

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
