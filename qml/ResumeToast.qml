import QtQuick
import QtQuick.Controls

Rectangle {
    id: resumeToast

    property var controller
    anchors.top: parent.top
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.topMargin: 12
    width: resumeRow.width + 32
    height: 36
    radius: 18
    color: "#90000000"
    visible: false
    z: 100

    property double savedPosition: 0

    Row {
        id: resumeRow
        anchors.centerIn: parent
        spacing: 8

        Label {
            text: qsTr("Last played at ") + controller.formatTime(resumeToast.savedPosition)
            color: "white"
            font.pixelSize: 13
            anchors.verticalCenter: parent.verticalCenter
        }

        // 用 Button（而非 Label+TapHandler）消费点击事件，
        // 否则点击会穿透到播放区的 TapHandler，触发播放/暂停。
        Button {
            text: qsTr("Play from start")
            flat: true
            padding: 0
            anchors.verticalCenter: parent.verticalCenter
            contentItem: Text {
                text: parent.text
                font.pixelSize: 13
                font.bold: true
                color: "#4FC3F7"
            }
            background: Rectangle {
                radius: 4
                color: parent.hovered ? "#30ffffff" : "transparent"
            }
            onClicked: {
                appController.resumeFromBeginning()
                resumeToast.visible = false
                resumeHideTimer.stop()
            }
        }
    }

    Timer {
        id: resumeHideTimer
        interval: 3000
        onTriggered: resumeToast.visible = false
    }

    function show(pos) {
        savedPosition = pos
        visible = true
        resumeHideTimer.restart()
    }

    Connections {
        target: appController
        function onResumePositionFound(path, position) {
            if (position > 5.0)
                resumeToast.show(position)
        }
    }
}
