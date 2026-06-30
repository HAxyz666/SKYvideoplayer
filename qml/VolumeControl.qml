import QtQuick
import QtQuick.Controls

Item {
    id: root

    implicitWidth: 120
    implicitHeight: 32

    Row {
        anchors.fill: parent
        spacing: 8

        // 音量图标
        ToolButton {
            id: muteBtn
            display: AbstractButton.IconOnly
            icon.name: appController.muted
                ? "audio-volume-muted"
                : (appController.volume > 66 ? "audio-volume-high"
                    : (appController.volume > 33 ? "audio-volume-medium"
                                                 : "audio-volume-low"))
            icon.width: 20
            icon.height: 20
            onClicked: appController.toggleMute()
        }

        // 水平滑块
        Slider {
            id: volSlider
            width: root.width - muteBtn.width - volLabel.width - parent.spacing * 2
            height: parent.height
            anchors.verticalCenter: parent.verticalCenter
            from: 0
            to: 100
            value: appController.volume
            stepSize: 1

            // 用户拖拽时不要回写控制器——onMoved 处理此事。
            // 仅使用绑定会在用户首次拖拽时失效，
            // 因此通过 Connections 仅在滑块未按下时恢复值。
            onMoved: appController.volume = value

            Connections {
                target: appController
                function onVolumeChanged(vol) {
                    if (!volSlider.pressed)
                        volSlider.value = vol
                }
            }

            handle: Rectangle {
                x: volSlider.leftPadding + volSlider.visualPosition * (volSlider.availableWidth - width)
                y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                width: 14
                height: 14
                radius: 7
                color: volSlider.pressed
                    ? (appController.theme === "dark" ? "#ffffff" : "#333333")
                    : (appController.theme === "dark" ? "#cccccc" : "#666666")
                border.color: appController.theme === "dark" ? "#888888" : "#999999"
                border.width: 1
            }

            background: Rectangle {
                x: volSlider.leftPadding
                y: volSlider.topPadding + volSlider.availableHeight / 2 - height / 2
                width: volSlider.availableWidth
                height: 4
                radius: 2
                color: appController.theme === "dark" ? "#444444" : "#d0d0d0"

                Rectangle {
                    width: volSlider.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: "#0078d7"
                }
            }
        }

        // 音量百分比
        Label {
            id: volLabel
            text: Math.round(volSlider.value) + "%"
            font.pixelSize: 11
            color: appController.theme === "dark" ? "#cccccc" : "#666666"
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            horizontalAlignment: Text.AlignRight
        }
    }
}
