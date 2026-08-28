import QtQuick
import QtQuick.Controls

Rectangle {
    anchors.fill: parent
    color: "black"
    visible: appController.isLoading || appController.bufferState === 1
    z: 5

    Column {
        anchors.centerIn: parent
        spacing: 16

        // 自绘加载动画：不依赖任何 QQC2 样式，确保 Qt Creator 与打包安装后完全一致
        Canvas {
            id: spinner
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48; height: 48
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.lineWidth = 4
                ctx.lineCap = "round"
                ctx.strokeStyle = "white"
                var r = width / 2 - 4
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + Math.PI * 1.4)
                ctx.stroke()
            }
            RotationAnimation on rotation {
                from: 0
                to: 360
                duration: 900
                loops: Animation.Infinite
                running: parent.visible
            }
        }

        Text {
            text: appController.isLoading ? appController.loadingText : qsTr("Buffering...")
            color: "white"
            font.pixelSize: 16
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
