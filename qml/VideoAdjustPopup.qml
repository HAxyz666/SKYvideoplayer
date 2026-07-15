import QtQuick
import QtQuick.Controls

Popup {
    id: videoPopup

    x: parent ? parent.width / 2 - width / 2 : 0
    y: parent ? -height - 4 : 0
    width: 170
    padding: 4

    background: Rectangle {
        radius: 8
        color: appController.theme === "dark" ? "#2d2d2d" : "#ffffff"
        border.width: 0
    }

    contentItem: Column {
        spacing: 0
        Repeater {
            model: [
                { label: qsTr("Rotate Left 90°"),  action: "rotateLeft" },
                { label: qsTr("Rotate Right 90°"), action: "rotateRight" },
                { label: qsTr("Flip Vertical"),    action: "flipVertical" }
            ]
            delegate: ItemDelegate {
                width: videoPopup.width - 8
                height: 32
                contentItem: Text {
                    text: modelData.label
                    font.pixelSize: 13
                    color: appController.theme === "dark" ? "#ffffff" : "#222222"
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                background: Rectangle {
                    radius: 4
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#40ffffff" : "#20000000")
                        : ((modelData.action === "flipVertical" && appController.flipVertical)
                            ? (appController.theme === "dark" ? "#30ffffff" : "#25000000")
                            : "transparent")
                }
                onClicked: {
                    if (modelData.action === "rotateLeft") appController.rotateLeft()
                    else if (modelData.action === "rotateRight") appController.rotateRight()
                    else if (modelData.action === "flipVertical") appController.toggleFlipVertical()
                }
            }
        }
        Item {
            width: videoPopup.width - 8
            height: 9
            Rectangle {
                width: parent.width - 16
                height: 1
                anchors.centerIn: parent
                color: appController.theme === "dark" ? "#40ffffff" : "#20000000"
            }
        }
        ItemDelegate {
            width: videoPopup.width - 8
            height: 32
            contentItem: Text {
                text: qsTr("Reset")
                font.pixelSize: 13
                color: appController.theme === "dark" ? "#ffffff" : "#222222"
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
            background: Rectangle {
                radius: 4
                color: parent.hovered
                    ? (appController.theme === "dark" ? "#40ffffff" : "#20000000")
                    : "transparent"
            }
            onClicked: {
                appController.resetRotation()
                videoPopup.close()
            }
        }
    }
}
