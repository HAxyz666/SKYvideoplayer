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

            component ExitButton: Button {
                text: qsTr("Exit")
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
    }
}
