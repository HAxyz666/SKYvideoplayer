import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Button {
    id: root
    text: qsTr("Subtitles")
    visible: appController.subtitleStreams.length > 0
    onClicked: {
        if (subtitlePopup.opened)
            subtitlePopup.close()
        else
            subtitlePopup.open()
    }
    flat: true

    signal popupClosed()
    property bool isPopupOpen: subtitlePopup.opened
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

            // === 字幕流选择列表 ===
            // 用 ItemDelegate（同倍速/画面弹窗）以消费点击事件，
            // 防止弹窗（向上弹出、覆盖进度条区域）内的点击穿透到进度条触发 seek。
            Repeater {
                model: ["none"].concat(appController.subtitleStreams)

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
                            property bool itemChecked: (index === 0 && appController.currentSubtitleStream < 0)
                                || (index > 0 && appController.currentSubtitleStream === index - 1)
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
                        appController.setCurrentSubtitleStream(index - 1)
                        subtitlePopup.close()
                    }
                }
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: appController.theme === "dark" ? "#555" : "#bbb"
            }

            // === 字幕延迟调节（每次 0.1s，长按连续调节） ===
            // 提示文字与调节按钮同行，构成同一区域
            Item {
                width: 8 + 64 + 8 + 30 + 2 + 52 + 2 + 30
                anchors.right: parent.right
                height: 32

                function delayText() {
                    var ms = appController.subtitleDelayMs
                    var sec = ms / 1000
                    return (ms > 0 ? "+" : "") + sec.toFixed(1) + "s"
                }

                // 区域提示文字：与调节按钮拉开一点距离
                Text {
                    id: label
                    text: qsTr("Delay Adjust")
                    width: 64
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    font.pixelSize: 12
                    color: appController.theme === "dark" ? "white" : "#333"
                }

                ItemDelegate {
                    id: minusBtn
                    width: 30
                    height: parent.height
                    padding: 0
                    hoverEnabled: true
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: label.right
                    anchors.leftMargin: 8

                    contentItem: Text {
                        text: "−"
                        font.pixelSize: 16
                        font.bold: true
                        color: appController.theme === "dark" ? "white" : "#333"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#30ffffff" : "#20000000")
                            : "transparent"
                    }

                    // 长按连发（按下 350ms 后开始，每 60ms 调节一次）
                    onPressedChanged: {
                        if (pressed) {
                            repeatTimer.delta = -100
                            repeatDelayTimer.restart()
                        } else {
                            repeatDelayTimer.stop()
                            repeatTimer.running = false
                        }
                    }
                    onClicked: appController.nudgeSubtitleDelay(-100)
                }

                Text {
                    id: delayValue
                    text: parent.delayText()
                    width: 52
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: minusBtn.right
                    anchors.leftMargin: 2
                    font.pixelSize: 12
                    color: appController.theme === "dark" ? "white" : "#333"
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight

                    // 说明提示：悬停显示延迟含义
                    ToolTip.text: qsTr("Subtitle delay: + means subtitles appear later\nHold - / + to adjust continuously (0.1s steps)")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                }

                ItemDelegate {
                    id: plusBtn
                    width: 30
                    height: parent.height
                    padding: 0
                    hoverEnabled: true
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: delayValue.right
                    anchors.leftMargin: 2

                    contentItem: Text {
                        text: "+"
                        font.pixelSize: 16
                        font.bold: true
                        color: appController.theme === "dark" ? "white" : "#333"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#30ffffff" : "#20000000")
                            : "transparent"
                    }

                    // 长按连发（按下 350ms 后开始，每 60ms 调节一次）
                    onPressedChanged: {
                        if (pressed) {
                            repeatTimer.delta = 100
                            repeatDelayTimer.restart()
                        } else {
                            repeatDelayTimer.stop()
                            repeatTimer.running = false
                        }
                    }
                    onClicked: appController.nudgeSubtitleDelay(100)
                }
            }

            // 长按连发：先经 350ms 首次延迟再进入 60ms 连发，单击不误触
            Timer {
                id: repeatDelayTimer
                interval: 350
                repeat: false
                onTriggered: repeatTimer.running = true
            }
            Timer {
                id: repeatTimer
                property int delta: 0
                interval: 60
                repeat: true
                onTriggered: appController.nudgeSubtitleDelay(delta)
            }

            // 分隔线
            Rectangle {
                width: parent.width
                height: 1
                color: appController.theme === "dark" ? "#555" : "#bbb"
            }

            // === 字幕样式入口 ===
            ItemDelegate {
                width: parent.width
                height: 28
                padding: 0
                hoverEnabled: true

                contentItem: Text {
                    text: qsTr("Subtitle Style…")
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    font.pixelSize: 12
                    color: appController.theme === "dark" ? "white" : "#333"
                }

                background: Rectangle {
                    radius: 4
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#30ffffff" : "#20000000")
                        : "transparent"
                }

                onClicked: {
                    root.resetEditState()
                    subtitlePopup.close()
                    styleDialog.open()
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
