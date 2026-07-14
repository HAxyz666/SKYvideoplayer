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

    // === 字幕样式设置对话框（内嵌） ===
    Dialog {
        id: styleDialog
        title: qsTr("Subtitle Style")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        onOpened: appController.modalCount = appController.modalCount + 1
        onClosed: appController.modalCount = appController.modalCount - 1
        width: 420
        height: 420
        padding: 20

        background: Rectangle {
            color: appController.theme === "dark" ? "#2d2d2d" : "#f5f5f5"
            border.color: appController.theme === "dark" ? "#555" : "#ccc"
            border.width: 1
            radius: 8
        }

        contentItem: ColumnLayout {
            spacing: 16

            // 预览区
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                color: "#000000"
                radius: 4

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Subtitle preview text")
                    color: root.textColor
                    font.family: root.fontFamily
                    font.pixelSize: root.fontSize
                    font.bold: true
                    style: Text.Outline
                    styleColor: "#80000000"
                }
            }

            // 字体
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    text: qsTr("Font")
                    color: appController.theme === "dark" ? "#ddd" : "#333"
                    Layout.preferredWidth: 50
                    font.pixelSize: 13
                }

                ComboBox {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    model: Qt.fontFamilies()
                    currentIndex: Math.max(0, model.indexOf(root.fontFamily))
                    onActivated: function(index) {
                        root.fontFamily = model[index]
                    }
                }
            }

            // 字号
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    text: qsTr("Size")
                    color: appController.theme === "dark" ? "#ddd" : "#333"
                    Layout.preferredWidth: 50
                    font.pixelSize: 13
                }

                SpinBox {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 36
                    from: 10
                    to: 72
                    value: root.fontSize
                    onValueModified: root.fontSize = value
                }

                Item { Layout.fillWidth: true }
            }

            // 颜色
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label {
                    text: qsTr("Color")
                    color: appController.theme === "dark" ? "#ddd" : "#333"
                    Layout.preferredWidth: 50
                    font.pixelSize: 13
                }

                Repeater {
                    model: ["#FFFFFF", "#FFFF00", "#00FF00", "#00FFFF",
                            "#FF8000", "#FF00FF", "#FF0000", "#0000FF"]

                    delegate: Rectangle {
                        width: 24
                        height: 24
                        radius: 4
                        color: modelData
                        border.width: root.textColor.toString().toUpperCase() === modelData.toUpperCase() ? 2 : 0
                        border.color: "#4a90d9"

                        TapHandler {
                            cursorShape: Qt.PointingHandCursor
                            onTapped: root.textColor = modelData
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Custom…")
                    flat: true
                    Layout.preferredHeight: 30
                    font.pixelSize: 12
                    onClicked: colorDialog.open()
                }
            }

            // 位置
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Label {
                    text: qsTr("Position")
                    color: appController.theme === "dark" ? "#ddd" : "#333"
                    Layout.preferredWidth: 50
                    font.pixelSize: 13
                }

                ComboBox {
                    Layout.preferredWidth: 140
                    Layout.preferredHeight: 36
                    model: [
                        { value: "top",    label: qsTr("Top") },
                        { value: "center", label: qsTr("Center") },
                        { value: "bottom", label: qsTr("Bottom") }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: {
                        var v = root.position
                        for (var i = 0; i < model.length; ++i)
                            if (model[i].value === v) return i
                        return 2
                    }
                    onActivated: function(index) {
                        root.position = currentValue
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // 底部按钮区
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                spacing: 10

                Button {
                    text: qsTr("Cancel")
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 34
                    onClicked: styleDialog.reject()
                }
                Button {
                    text: qsTr("OK")
                    highlighted: true
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 34
                    onClicked: {
                        var style = {
                            "fontFamily": root.fontFamily,
                            "fontSize":   root.fontSize,
                            "color":      root.textColor.toString(),
                            "position":   root.position
                        }
                        settingsManager.subtitleStyle = style
                        styleDialog.accept()
                    }
                }
            }
        }

        ColorDialog {
            id: colorDialog
            title: qsTr("Select Subtitle Color")
            onVisibleChanged: appController.modalCount = appController.modalCount + (visible ? 1 : -1)
            onAccepted: root.textColor = colorDialog.selectedColor
        }
    }
}
