import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root
    text: qsTr("Audio")
    // 仅当存在多条音轨可供切换时才显示（单音轨无可切换内容，隐藏按钮）
    visible: appController.audioStreams.length > 1
    onClicked: {
        if (audioPopup.opened)
            audioPopup.close()
        else
            audioPopup.open()
    }
    flat: true

    signal popupClosed()
    property bool isPopupOpen: audioPopup.opened
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

    Popup {
        id: audioPopup
        y: -height - 4
        x: 0
        width: 220
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutsideParent
        onClosed: root.popupClosed()

        background: Rectangle {
            color: appController.theme === "dark" ? "#3d3d3d" : "#e8e8e8"
            radius: 4
            border.color: appController.theme === "dark" ? "#555" : "#bbb"
            border.width: 1
        }

        Column {
            width: parent.width
            spacing: 0

            // === 音轨选择列表 ===
            // 用 ItemDelegate（同倍速/画面弹窗）以消费点击事件，
            // 防止弹窗（向上弹出、覆盖进度条区域）内的点击穿透到进度条触发 seek。
            Repeater {
                model: appController.audioStreams

                delegate: ItemDelegate {
                    width: parent.width
                    height: 28
                    padding: 0
                    hoverEnabled: true

                    contentItem: Row {
                        spacing: 6
                        anchors.left: parent.left
                        anchors.leftMargin: 8

                        Rectangle {
                            property bool itemChecked: appController.currentAudioStream === index
                            width: 14
                            height: 14
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
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

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#30ffffff" : "#20000000")
                            : "transparent"
                    }

                    onClicked: {
                        appController.setCurrentAudioStream(index)
                        audioPopup.close()
                    }
                }
            }
        }
    }
}