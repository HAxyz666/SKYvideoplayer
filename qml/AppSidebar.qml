import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Rectangle {
    id: sidebar
    color: appController.theme === "dark" ? "#2a2a2a" : "#e0e0e0"

    signal openFileTriggered()
    signal openNetworkTriggered()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 0
        spacing: 0

        // Logo 区域
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            color: "transparent"

            Image {
                id: logoImg
                anchors.centerIn: parent
                source: appController.theme === "dark" ? "qrc:/icons/logos/LogoD.svg" : "qrc:/icons/logos/LogoL.svg"
                sourceSize: Qt.size(80, 80)
            }

        }

        // 分割线
        Rectangle { Layout.fillWidth: true; height: 1; color: appController.theme === "dark" ? "#3d3d3d" : "#cccccc" }

        // 自定义菜单栏 (垂直排列)
        Column {
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 0

            component MenuButton: Button {
                width: parent.width
                height: 40
                flat: true
                font.pixelSize: 14
                contentItem: Label {
                    text: parent.text
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 20
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                        : (appController.theme === "dark" ? "#ffffff" : "#333333")
                }
                background: Rectangle {
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : "transparent"
                }
            }

            MenuButton {
                text: "open file"
                onClicked: openFileTriggered()
            }

            MenuButton {
                text: "open network"
                onClicked: openNetworkTriggered()
            }

            MenuButton {
                text: "screenshot path"
                onClicked: {
                    screenshotPathLabel.text = appController.screenshotPath
                    screenshotPathPopup.open()
                }
            }
            // ...

            component ExitButton: Button {
                text: "exit"
                width: parent.width
                height: 40
                flat: true
                font.pixelSize: 14
                contentItem: Label {
                    text: parent.text
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 20
                    color: parent.hovered ? "#A93226" : "#C0392B"
                }
                background: Rectangle {
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : "transparent"
                }
            }
            ExitButton{
                onClicked: Qt.quit()
            }
        }

        // 底部弹簧
        Item { Layout.fillHeight: true }

        // 主题切换（左下角）
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.bottomMargin: 4
            spacing: 6

            Button {
                id: themeBtn
                text: appController.theme === "dark" ? "☀ Light" : "🌙 Dark"
                flat: true
                font.pixelSize: 13
                onClicked: {
                    appController.theme = appController.theme === "dark" ? "light" : "dark"
                }
                contentItem: Label {
                    text: parent.text
                    verticalAlignment: Text.AlignVCenter
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                        : (appController.theme === "dark" ? "#cccccc" : "#666666")
                    font.pixelSize: 13
                }
                background: Rectangle {
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : "transparent"
                }
            }

            Item { Layout.fillWidth: true }
        }

        // 底部信息
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 16
            text: "v1.0"
            color: appController.theme === "dark" ? "#888888" : "#888888"
            font.pixelSize: 12
        }
    }

    Popup {
        id: screenshotPathPopup
        parent: Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 360
        height: 110
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
                text: "Screenshot save path"
                font.pixelSize: 14
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
                        screenshotPathPopup.close()
                        screenshotFolderDialog.open()
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

            RowLayout {
                Layout.fillWidth: true

                Item { Layout.fillWidth: true }

                Button {
                    text: "Close"
                    flat: true
                    onClicked: screenshotPathPopup.close()
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

    FolderDialog {
        id: screenshotFolderDialog
        title: "Select screenshot save folder"
        onAccepted: {
            var path = selectedFolder.toString()
            if (path.startsWith("file://"))
                path = path.substring(7)
            appController.screenshotPath = path
        }
    }
}
