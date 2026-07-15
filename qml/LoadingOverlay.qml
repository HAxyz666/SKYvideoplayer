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

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: appController.isLoading || appController.bufferState === 1
            width: 48; height: 48
        }

        Text {
            text: appController.isLoading ? appController.loadingText : qsTr("Buffering...")
            color: "white"
            font.pixelSize: 16
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }
}
