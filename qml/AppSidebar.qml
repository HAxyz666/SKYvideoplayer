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
                // 文字颜色（常规/悬停），Exit 按钮通过这两个属性定制为红色系
                property color textColor: appController.theme === "dark" ? "#ffffff" : "#333333"
                property color textHoverColor: appController.theme === "dark" ? "#ffffff" : "#000000"
                width: parent.width
                height: 40
                flat: true
                font.pixelSize: 14
                contentItem: Label {
                    text: parent.text
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 20
                    color: parent.hovered ? parent.textHoverColor : parent.textColor
                }
                background: Rectangle {
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                        : "transparent"
                }
            }

            MenuButton {
                text: qsTr("Open File")
                onClicked: openFileTriggered()
            }

            MenuButton {
                text: qsTr("Open Network")
                onClicked: openNetworkTriggered()
            }

            MenuButton {
                text: qsTr("Settings")
                onClicked: settingsPopup.open()
            }
            // ...

            MenuButton {
                text: qsTr("Exit")
                textColor: "#C0392B"
                textHoverColor: "#A93226"
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
                text: appController.theme === "dark" ? qsTr("☀ Light") : qsTr("🌙 Dark")
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

    SettingsDialog {
        id: settingsPopup
        onRequestScreenshotPath: screenshotFolderDialog.open()
    }

    FolderDialog {
        id: screenshotFolderDialog
        title: qsTr("Select screenshot save folder")
        onVisibleChanged: appController.modalCount = appController.modalCount + (visible ? 1 : -1)
        onAccepted: {
            var path = selectedFolder.toString()
            if (path.startsWith("file://"))
                path = path.substring(7)
            appController.screenshotPath = path
        }
    }
}
