import QtQuick
import QtQuick.Controls

Button {
    id: root
    text: qsTr("字幕")
    visible: appController.subtitleStreams.length > 0
    onClicked: subtitlePopup.open()

    Popup {
        id: subtitlePopup
        y: -height - 4
        x: 0
        width: root.width + 20
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: appController.theme === "dark" ? "#3d3d3d" : "#e8e8e8"
            radius: 4
            border.color: appController.theme === "dark" ? "#555" : "#bbb"
            border.width: 1
        }

        Column {
            width: parent.width
            spacing: 0

            Repeater {
                model: ["none"].concat(appController.subtitleStreams)

                delegate: Rectangle {
                    width: parent.width
                    height: 28
                    color: mouse.containsMouse
                        ? (appController.theme === "dark" ? "#30ffffff" : "#20000000")
                        : "transparent"

                    Row {
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8

                        Rectangle {
                            property bool itemChecked: (index === 0 && appController.currentSubtitleStream < 0)
                                || (index > 0 && appController.currentSubtitleStream === index - 1)
                            width: 14
                            height: 14
                            radius: 2
                            border.width: 1
                            border.color: appController.theme === "dark" ? "#888" : "#666"
                            color: itemChecked ? "#4a90d9" : "transparent"

                            Text {
                                anchors.centerIn: parent
                                text: "✓"
                                color: "white"
                                font.pixelSize: 10
                                visible: parent.itemChecked
                            }
                        }

                        Text {
                            text: modelData
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 12
                            color: appController.theme === "dark" ? "white" : "#333"
                        }
                    }

                    MouseArea {
                        id: mouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            appController.setCurrentSubtitleStream(index - 1)
                            subtitlePopup.close()
                        }
                    }
                }
            }
        }
    }
}
