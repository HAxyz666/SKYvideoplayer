import QtQuick
import QtQuick.Controls

// 音量控制组件：音量图标 + 垂直滑块（悬停显示），上下拖动调节音量
// 对应 UML §3.3.5 VolumeControl.qml
Item {
    id: root

    implicitWidth: 32
    implicitHeight: 32

    // 滑块可见性
    property bool showSlider: false

    // 垂直音量滑块（图标上方悬停时出现）
    Slider {
        id: volSlider
        anchors.bottom: muteBtn.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 4
        orientation: Qt.Vertical
        from: 0
        to: 100
        value: appController.volume
        stepSize: 1
        height: 80

        opacity: root.showSlider ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 150 } }
        visible: opacity > 0

        onMoved: appController.volume = value

        // 滑块上悬停时保持显示
        hoverEnabled: true
        onHoveredChanged: {
            if (hovered) {
                root.showSlider = true
                hideTimer.stop()
            } else {
                hideTimer.start()
            }
        }
    }

    // 音量百分比标签，悬浮在滑块右侧对应位置
    Label {
        id: volLabel
        text: Math.round(volSlider.value) + "%"
        font.pixelSize: 10
        color: "#ffffff"
        padding: 2
        background: Rectangle {
            color: "#bb000000"
            radius: 2
        }

        // 跟随滑块值定位到滑块高度对应位置
        x: volSlider.x + volSlider.width + 4
        y: volSlider.y + (volSlider.height - implicitHeight)
           * (1 - volSlider.value / (volSlider.to - volSlider.from))

        opacity: root.showSlider ? 1.0 : 0.0
        Behavior on opacity { NumberAnimation { duration: 100 } }
    }

    // 音量图标（点击切换静音，悬停显示滑块）
    ToolButton {
        id: muteBtn
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        display: AbstractButton.IconOnly

        icon.name: appController.muted
            ? "audio-volume-muted"
            : (appController.volume > 66 ? "audio-volume-high"
                : (appController.volume > 33 ? "audio-volume-medium"
                                             : "audio-volume-low"))
        icon.width: 20
        icon.height: 20

        onClicked: appController.toggleMute()

        // 悬停显示滑块
        hoverEnabled: true
        onHoveredChanged: {
            if (hovered) {
                root.showSlider = true
                hideTimer.stop()
            } else {
                hideTimer.start()
            }
        }
    }

    Timer {
        id: hideTimer
        interval: 1500
        onTriggered: root.showSlider = false
    }
}
