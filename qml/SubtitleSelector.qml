import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Button {
    id: root
    text: qsTr("Subtitles")
    visible: appController.subtitleStreams.length > 0
    onClicked: subtitlePopup.open()
    flat: true
    contentItem: Text {
        text: root.text
        font: root.font
        color: "white"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 4
        color: root.hovered ? "#30ffffff" : "transparent"
    }

    // 取当前设置作为编辑副本，确定后写回 settingsManager
    property string fontFamily: settingsManager.subtitleStyle.fontFamily || "Sans Serif"
    property int fontSize: settingsManager.subtitleStyle.fontSize || 18
    property color textColor: settingsManager.subtitleStyle.color || "#FFFFFF"
    property string position: settingsManager.subtitleStyle.position || "bottom"

    function resetEditState() {
        fontFamily = settingsManager.subtitleStyle.fontFamily || "Sans Serif"
        fontSize = settingsManager.subtitleStyle.fontSize || 18
        textColor = settingsManager.subtitleStyle.color || "#FFFFFF"
        position = settingsManager.subtitleStyle.position || "bottom"
    }

    Popup {
        id: subtitlePopup
        y: -height - 4
        x: 0
        width: 220
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

            // === 字幕流选择列表 ===
            Repeater {
                model: ["none"].concat(appController.subtitleStreams)

                delegate: Rectangle {
                    width: parent.width
                    height: 28
                    color: subHover.hovered
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

                    HoverHandler { id: subHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            appController.setCurrentSubtitleStream(index - 1)
                            subtitlePopup.close()
                        }
                    }
                }
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: appController.theme === "dark" ? "#555" : "#bbb"
            }

            // === 字幕样式入口 ===
            Rectangle {
                width: parent.width
                height: 28
                color: styleHover.hovered
                    ? (appController.theme === "dark" ? "#30ffffff" : "#20000000")
                    : "transparent"

                Text {
                    text: qsTr("Subtitle Style…")
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    font.pixelSize: 12
                    color: appController.theme === "dark" ? "white" : "#333"
                }

                HoverHandler { id: styleHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        root.resetEditState()
                        subtitlePopup.close()
                        styleDialog.open()
                    }
                }
            }
        }
    }

    SubtitleStyleDialog {
        id: styleDialog
        fontFamily: root.fontFamily
        fontSize: root.fontSize
        textColor: root.textColor
        position: root.position
    }
}
