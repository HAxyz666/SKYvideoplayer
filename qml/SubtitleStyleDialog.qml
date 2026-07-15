import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

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
                color: styleDialog.textColor
                font.family: styleDialog.fontFamily
                font.pixelSize: styleDialog.fontSize
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
                currentIndex: Math.max(0, model.indexOf(styleDialog.fontFamily))
                onActivated: function(index) {
                    styleDialog.fontFamily = model[index]
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
                value: styleDialog.fontSize
                onValueModified: styleDialog.fontSize = value
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
                    border.width: styleDialog.textColor.toString().toUpperCase() === modelData.toUpperCase() ? 2 : 0
                    border.color: "#4a90d9"

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: styleDialog.textColor = modelData
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
                    var v = styleDialog.position
                    for (var i = 0; i < model.length; ++i)
                        if (model[i].value === v) return i
                    return 2
                }
                onActivated: function(index) {
                    styleDialog.position = currentValue
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
                        "fontFamily": styleDialog.fontFamily,
                        "fontSize":   styleDialog.fontSize,
                        "color":      styleDialog.textColor.toString(),
                        "position":   styleDialog.position
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
        onAccepted: styleDialog.textColor = colorDialog.selectedColor
    }
}
