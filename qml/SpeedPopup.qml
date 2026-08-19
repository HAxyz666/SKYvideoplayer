import QtQuick
import QtQuick.Controls

Popup {
    id: speedPopup

    property var controller

    x: parent ? parent.width / 2 - width / 2 : 0
    y: parent ? -height - 4 : 0
    width: 80
    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutsideParent

    background: Rectangle {
        radius: 8
        color: appController.theme === "dark" ? "#2d2d2d" : "#ffffff"
        border.width: 0
    }

    contentItem: ListView {
        id: speedList
        implicitHeight: contentHeight
        model: [
            { label: "0.5x",  value: 0.5 },
            { label: "0.75x", value: 0.75 },
            { label: "1.0x",  value: 1.0 },
            { label: "1.25x", value: 1.25 },
            { label: "1.5x",  value: 1.5 },
            { label: "2.0x",  value: 2.0 }
        ]
        clip: true
        delegate: ItemDelegate {
            width: speedList.width
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
                    : (controller.speed === modelData.value
                        ? (appController.theme === "dark" ? "#30ffffff" : "#25000000")
                        : "transparent")
            }
            onClicked: {
                controller.setSpeed(modelData.value)
                speedPopup.close()
            }
        }
    }
}
