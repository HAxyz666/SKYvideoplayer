import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: settingsPopup
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: 420
    height: 450
    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onOpened: appController.modalCount = appController.modalCount + 1
    onClosed: appController.modalCount = appController.modalCount - 1

    property string currentTab: "screenshot"
    property string editingShortcut: ""
    signal requestScreenshotPath()

    background: Rectangle {
        radius: 8
        color: appController.theme === "dark" ? "#2d2d2d" : "#f5f5f5"
        border.color: appController.theme === "dark" ? "#555" : "#ccc"
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            text: qsTr("Settings")
            font.pixelSize: 16
            font.bold: true
            color: appController.theme === "dark" ? "#ffffff" : "#333333"
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: appController.theme === "dark" ? "#555" : "#ccc"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: qsTr("Screenshot Path")
                flat: true
                Layout.fillWidth: true
                onClicked: settingsPopup.currentTab = "screenshot"
                contentItem: Label {
                    text: parent.text
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    color: settingsPopup.currentTab === "screenshot"
                        ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                        : (appController.theme === "dark" ? "#cccccc" : "#666666")
                }
                background: Rectangle {
                    radius: 4
                    color: settingsPopup.currentTab === "screenshot"
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : (parent.hovered ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0") : "transparent")
                }
            }

            Button {
                text: qsTr("Shortcuts")
                flat: true
                Layout.fillWidth: true
                onClicked: settingsPopup.currentTab = "shortcuts"
                contentItem: Label {
                    text: parent.text
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    color: settingsPopup.currentTab === "shortcuts"
                        ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                        : (appController.theme === "dark" ? "#cccccc" : "#666666")
                }
                background: Rectangle {
                    radius: 4
                    color: settingsPopup.currentTab === "shortcuts"
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : (parent.hovered ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0") : "transparent")
                }
            }

            Button {
                text: qsTr("About")
                flat: true
                Layout.fillWidth: true
                onClicked: settingsPopup.currentTab = "about"
                contentItem: Label {
                    text: parent.text
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    color: settingsPopup.currentTab === "about"
                        ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                        : (appController.theme === "dark" ? "#cccccc" : "#666666")
                }
                background: Rectangle {
                    radius: 4
                    color: settingsPopup.currentTab === "about"
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : (parent.hovered ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0") : "transparent")
                }
            }

            Button {
                text: qsTr("Language")
                flat: true
                Layout.fillWidth: true
                onClicked: settingsPopup.currentTab = "language"
                contentItem: Label {
                    text: parent.text
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    color: settingsPopup.currentTab === "language"
                        ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                        : (appController.theme === "dark" ? "#cccccc" : "#666666")
                }
                background: Rectangle {
                    radius: 4
                    color: settingsPopup.currentTab === "language"
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : (parent.hovered ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0") : "transparent")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: appController.theme === "dark" ? "#555" : "#ccc"
        }

        FocusScope {
            id: shortcutRecorder
            visible: settingsPopup.editingShortcut !== ""
            focus: visible

            Keys.onPressed: function(event) {
                if (settingsPopup.editingShortcut === "") return

                if (event.key === Qt.Key_Escape) {
                    settingsPopup.editingShortcut = ""
                    event.accepted = true
                    return
                }

                var modifiers = ""
                if (event.modifiers & Qt.ControlModifier) modifiers += "Ctrl+"
                if (event.modifiers & Qt.AltModifier) modifiers += "Alt+"
                if (event.modifiers & Qt.ShiftModifier) modifiers += "Shift+"

                var keyName = ""
                switch (event.key) {
                    case Qt.Key_Up: keyName = "Up"; break
                    case Qt.Key_Down: keyName = "Down"; break
                    case Qt.Key_Left: keyName = "Left"; break
                    case Qt.Key_Right: keyName = "Right"; break
                    case Qt.Key_Space: keyName = "Space"; break
                    case Qt.Key_F1: keyName = "F1"; break
                    case Qt.Key_F2: keyName = "F2"; break
                    case Qt.Key_F3: keyName = "F3"; break
                    case Qt.Key_F4: keyName = "F4"; break
                    case Qt.Key_F5: keyName = "F5"; break
                    case Qt.Key_F6: keyName = "F6"; break
                    case Qt.Key_F7: keyName = "F7"; break
                    case Qt.Key_F8: keyName = "F8"; break
                    case Qt.Key_F9: keyName = "F9"; break
                    case Qt.Key_F10: keyName = "F10"; break
                    case Qt.Key_F11: keyName = "F11"; break
                    case Qt.Key_F12: keyName = "F12"; break
                    default:
                        if (event.text && event.text.length === 1 && event.text !== "") {
                            keyName = event.text.toUpperCase()
                        }
                        break
                }

                if (keyName !== "") {
                    var shortcuts = JSON.parse(JSON.stringify(settingsManager.shortcuts))
                    shortcuts[settingsPopup.editingShortcut] = modifiers + keyName
                    settingsManager.shortcuts = shortcuts
                    settingsPopup.editingShortcut = ""
                    event.accepted = true
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: settingsPopup.currentTab === "screenshot" ? 0 : (settingsPopup.currentTab === "shortcuts" ? 1 : (settingsPopup.currentTab === "about" ? 2 : 3))

            // Screenshot Path
            ColumnLayout {
                spacing: 12

                Label {
                    text: qsTr("Screenshot save path")
                    font.pixelSize: 13
                    font.bold: true
                    color: appController.theme === "dark" ? "#ffffff" : "#333333"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        id: screenshotPathLabel
                        text: appController.screenshotPath
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        color: appController.theme === "dark" ? "#cccccc" : "#666666"
                    }

                    Button {
                        text: "..."
                        flat: true
                        implicitWidth: 28
                        implicitHeight: 24
                        onClicked: {
                            settingsPopup.close()
                            Qt.callLater(function() {
                                settingsPopup.requestScreenshotPath()
                            })
                        }
                        contentItem: Label {
                            text: parent.text
                            font.pixelSize: 13
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: parent.hovered
                                ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                                : (appController.theme === "dark" ? "#cccccc" : "#666666")
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered
                                ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                                : "transparent"
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }

            // Shortcuts
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ScrollView {
                    anchors.centerIn: parent
                    width: parent.width
                    height: Math.min(contentHeight, parent.height)
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: [
                                { key: "volumeUp", label: "Volume Up" },
                                { key: "volumeDown", label: "Volume Down" },
                                { key: "toggleMute", label: "Toggle Mute" },
                                { key: "togglePlayback", label: "Play / Pause" },
                                { key: "stepBackward", label: "Step Backward" },
                                { key: "stepForward", label: "Step Forward" },
                                { key: "stepBackwardLarge", label: "Large Backward" },
                                { key: "stepForwardLarge", label: "Large Forward" },
                                { key: "toggleFullscreen", label: "Toggle Fullscreen" },
                                { key: "exitFullscreen", label: "Exit Fullscreen" }
                            ]

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignHCenter
                                spacing: 8

                                Label {
                                    text: modelData.label
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    color: appController.theme === "dark" ? "#cccccc" : "#333333"
                                }

                                Rectangle {
                                    width: 120
                                    height: 28
                                    radius: 4
                                    color: appController.theme === "dark" ? "#3d3d3d" : "#e0e0e0"
                                    border.color: settingsPopup.editingShortcut === modelData.key
                                        ? "#0078d7"
                                        : (appController.theme === "dark" ? "#555" : "#ccc")

                                    property string shortcutKey: modelData.key
                                    property string shortcutValue: settingsManager.shortcuts[modelData.key] || ""

                                    Text {
                                        id: shortcutText
                                        anchors.centerIn: parent
                                        text: settingsPopup.editingShortcut === modelData.key ? qsTr("Press new key...") : (settingsManager.shortcuts[modelData.key] || "")
                                        font.pixelSize: 11
                                        color: settingsPopup.editingShortcut === modelData.key
                                            ? "#0078d7"
                                            : (appController.theme === "dark" ? "#ffffff" : "#333333")

                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                settingsPopup.editingShortcut = modelData.key
                                                shortcutRecorder.forceActiveFocus()
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }

            // About
            Item {
                id: aboutItem
                Layout.fillWidth: true
                Layout.fillHeight: true

                property bool showDetails: false

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 12

                    Image {
                        Layout.alignment: Qt.AlignHCenter
                        source: appController.theme === "dark" ? "qrc:/icons/logos/LogoD.svg" : "qrc:/icons/logos/LogoL.svg"
                        sourceSize: Qt.size(80, 80)
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("SKY Video Player")
                        font.pixelSize: 18
                        font.bold: true
                        color: appController.theme === "dark" ? "#ffffff" : "#333333"
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Version 2.0")
                        font.pixelSize: 13
                        color: appController.theme === "dark" ? "#cccccc" : "#666666"
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("A Qt6-based video player\nwith hardware acceleration")
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        color: appController.theme === "dark" ? "#aaaaaa" : "#888888"
                    }

                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: aboutItem.showDetails ? qsTr("Hide") : qsTr("Details")
                        flat: true
                        onClicked: aboutItem.showDetails = !aboutItem.showDetails
                        contentItem: Label {
                            text: parent.text
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            color: "#0078d7"
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.hovered
                                ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                                : "transparent"
                        }
                    }

                    // 作者信息（可折叠）
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 6
                        visible: aboutItem.showDetails

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: appController.theme === "dark" ? "#555" : "#ccc"
                            Layout.topMargin: 4
                            Layout.bottomMargin: 4
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Author")
                            font.pixelSize: 12
                            font.bold: true
                            color: appController.theme === "dark" ? "#cccccc" : "#333333"
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Daisen Zhou (3563248115@qq.com)\nChao Li(3042525170@qq.com)\nCheng Li(3530606868@qq.com)"
                            font.pixelSize: 12
                            color: appController.theme === "dark" ? "#aaaaaa" : "#666666"
                        }

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Built with Qt 6.11 and FFmpeg")
                            font.pixelSize: 11
                            color: appController.theme === "dark" ? "#888888" : "#888888"
                        }
                    }
                }
            }

            // Language
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 16

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Language")
                        font.pixelSize: 14
                        font.bold: true
                        color: appController.theme === "dark" ? "#ffffff" : "#333333"
                    }

                    // 语言列表
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8

                        Repeater {
                            model: [
                                { code: "en", name: "English", nativeName: "English" },
                                { code: "zh_CN", name: "Chinese (Simplified)", nativeName: "简体中文" }
                            ]

                            Rectangle {
                                width: 200
                                height: 40
                                radius: 6
                                color: settingsManager.language === modelData.code
                                    ? (appController.theme === "dark" ? "#0078d7" : "#0078d7")
                                    : (appController.theme === "dark" ? "#3d3d3d" : "#e0e0e0")
                                border.color: settingsManager.language === modelData.code
                                    ? "#0078d7"
                                    : (appController.theme === "dark" ? "#555" : "#ccc")

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12

                                    Label {
                                        text: modelData.nativeName
                                        font.pixelSize: 13
                                        color: settingsManager.language === modelData.code
                                            ? "#ffffff"
                                            : (appController.theme === "dark" ? "#ffffff" : "#333333")
                                        Layout.fillWidth: true
                                    }

                                    Label {
                                        text: modelData.name
                                        font.pixelSize: 11
                                        color: settingsManager.language === modelData.code
                                            ? "#ffffff"
                                            : (appController.theme === "dark" ? "#aaaaaa" : "#888888")
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        settingsManager.language = modelData.code
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Close")
                flat: true
                onClicked: settingsPopup.close()
                contentItem: Label {
                    text: parent.text
                    font.pixelSize: 13
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                        : (appController.theme === "dark" ? "#cccccc" : "#666666")
                }
                background: Rectangle {
                    radius: 4
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : "transparent"
                }
            }
        }
    }

}
