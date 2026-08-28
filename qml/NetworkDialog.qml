import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: networkDialog

    property var controller

    // 网络流模式：0=原生 1=直播 2=点播（对应 StreamResolverManager::Mode）
    property int streamMode: 0

    title: ""
    anchors.centerIn: parent
    modal: true
    width: 500
    padding: 20
    onOpened: appController.modalCount = appController.modalCount + 1

    background: Rectangle {
        radius: 8
        color: appController.theme === "dark" ? "#2d2d2d" : "#f5f5f5"
        border.color: "transparent"
    }

    header: Item { height: 0 }
    footer: Item { height: 0 }

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            text: qsTr("Open Network Stream")
            font.pixelSize: 16
            font.bold: true
            color: appController.theme === "dark" ? "#ffffff" : "#333333"
        }

        // 模式选择：原生 / 直播 / 点播
        RowLayout {
            spacing: 6

            Repeater {
                model: [
                    { mode: 0, label: qsTr("Native") },
                    { mode: 1, label: qsTr("Live") },
                    { mode: 2, label: qsTr("VOD") }
                ]

                Button {
                    required property int mode
                    required property string label
                    text: label
                    flat: true
                    checkable: true
                    checked: networkDialog.streamMode === mode
                    onClicked: networkDialog.streamMode = mode
                    contentItem: Label {
                        text: parent.text
                        font.pixelSize: 13
                        font.bold: parent.checked
                        color: parent.checked
                            ? (appController.theme === "dark" ? "#ffffff" : "#000000")
                            : (appController.theme === "dark" ? "#aaaaaa" : "#888888")
                    }
                    background: Rectangle {
                        radius: 4
                        color: parent.checked
                            ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                            : "transparent"
                    }
                }
            }
        }

        Label {
            text: qsTr("Enter network URL:")
            font.pixelSize: 13
            color: appController.theme === "dark" ? "#cccccc" : "#666666"
        }

        TextField {
            id: urlInput
            Layout.fillWidth: true
            placeholderText: "http://example.com/video.mp4"
            font.pixelSize: 14
            selectByMouse: true
            padding: 8
            background: Rectangle {
                radius: 4
                color: appController.theme === "dark" ? "#3d3d3d" : "#e8e8e8"
                border.color: appController.theme === "dark" ? "#555" : "#ccc"
            }
            onAccepted: networkDialog.accept()
        }

        Label {
            text: networkDialog.streamMode === 0
                  ? qsTr("Native: HTTP, HTTPS, RTMP, RTSP, UDP, TCP (no extra tool required)")
                  : networkDialog.streamMode === 1
                    ? qsTr("Live: resolved by streamlink — covers Huya, Douyu, Bilibili, Douyin live. Install: pip install streamlink")
                    : qsTr("VOD: resolved by yt-dlp — websites and encrypted streams. Install: pip install yt-dlp")
            font.pixelSize: 11
            color: appController.theme === "dark" ? "#888888" : "#999999"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4

            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Open")
                flat: true
                onClicked: networkDialog.accept()
                contentItem: Label {
                    text: parent.text
                    font.pixelSize: 13
                    font.bold: true
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

            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: networkDialog.close()
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

    onAccepted: {
        if (urlInput.text.trim().length > 0)
            controller.openNetworkStream(urlInput.text.trim(), networkDialog.streamMode)
    }

    // 取消/关闭（非成功打开）时取消在途解析，避免子进程（yt-dlp/streamlink）空跑
    onRejected: controller.cancelNetworkResolve()

    onClosed: {
        appController.modalCount = appController.modalCount - 1
        urlInput.text = ""
        networkDialog.streamMode = 0
    }
}
