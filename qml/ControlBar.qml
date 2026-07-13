import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SKYvideoplayer 1.0

Rectangle {
    id: controlBar

    property var controller
    property var playlistDrawer
    property bool isFullscreen: false
    signal toggleFullscreen()
    signal userInteracted()

    property bool showControls: true
    height: 80
    color: appController.theme === "dark" ? "#80000000" : "#cc000000"
    visible: showControls

    HoverHandler {
        onHoveredChanged: {
            console.log("controlBar hovered =", hovered)
            if (hovered) {
                controlBar.showControls = true
                controlBar.userInteracted()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4

        ProgressBar {
            Layout.fillWidth: true
            position: appController.position
            duration: appController.duration
            enabled: !controller.isLiveStream
            opacity: controller.isLiveStream ? 0.3 : 1.0
            onSeekRequested: function(pos) {
                if (!controller.isLiveStream)
                    appController.seekTo(pos)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Button {
                icon.source: "qrc:/icons/icons/prev_file.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "transparent"
                display: AbstractButton.IconOnly
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Prev")
                ToolTip.delay: 500
                enabled: appController.playlistModel.hasPrev
                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: appController.playPrev()
            }

            Button {
                icon.source: controller.isPlaying ? "qrc:/icons/icons/pause.svg" : "qrc:/icons/icons/play.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "transparent"
                display: AbstractButton.IconOnly
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: controller.isPlaying ? qsTr("Pause") : qsTr("Play")
                ToolTip.delay: 500
                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: controller.togglePlay()
            }

            Button {
                icon.source: "qrc:/icons/icons/next_file.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "transparent"
                display: AbstractButton.IconOnly
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Next")
                ToolTip.delay: 500
                enabled: appController.playlistModel.hasNext
                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: appController.playNext()
            }

            Button {
                id: speedBtn
                text: controller.speed + "x"
                flat: true
                padding: 0
                Layout.preferredWidth: 48
                Layout.preferredHeight: 32
                font.pixelSize: 12
                contentItem: Text {
                    text: speedBtn.text
                    font: speedBtn.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 4
                    color: speedBtn.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: {
                    var h = speedMenu.height > 0 ? speedMenu.height : 200
                    speedMenu.popup(speedBtn, 0, -h)
                }
            }

            Menu {
                id: speedMenu

                Repeater {
                    model: [
                        { label: "0.5x",  value: 0.5 },
                        { label: "0.75x", value: 0.75 },
                        { label: "1.0x",  value: 1.0 },
                        { label: "1.25x", value: 1.25 },
                        { label: "1.5x",  value: 1.5 },
                        { label: "2.0x",  value: 2.0 }
                    ]

                    MenuItem {
                        text: modelData.label
                        checkable: true
                        checked: controller.speed === modelData.value
                        onTriggered: controller.setSpeed(modelData.value)
                    }
                }
            }

            SubtitleSelector { }

            Button {
                id: videoBtn
                icon.source: "qrc:/icons/icons/video_adjust.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "transparent"
                display: AbstractButton.IconOnly
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Video")
                ToolTip.delay: 500
                background: Rectangle {
                    radius: 4
                    color: videoBtn.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: {
                    var h = videoMenu.height > 0 ? videoMenu.height : 140
                    videoMenu.popup(videoBtn, 0, -h)
                }
            }

            Menu {
                id: videoMenu

                MenuItem {
                    text: qsTr("Rotate Left 90°")
                    onTriggered: appController.rotateLeft()
                }
                MenuItem {
                    text: qsTr("Rotate Right 90°")
                    onTriggered: appController.rotateRight()
                }
                MenuItem {
                    text: qsTr("Flip Vertical")
                    checkable: true
                    checked: appController.flipVertical
                    onTriggered: appController.toggleFlipVertical()
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Reset")
                    onTriggered: appController.resetRotation()
                }
            }

            Button {
                id: screenshotBtn
                icon.source: "qrc:/icons/icons/screenshot.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "transparent"
                display: AbstractButton.IconOnly
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Screenshot")
                ToolTip.delay: 500
                enabled: appController.currentFilePath !== ""
                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: {
                    var path = appController.takeScreenshot()
                    if (path !== "") {
                        screenshotTip.text = qsTr("Saved: %1").arg(path.split("/").pop())
                        screenshotTip.visible = true
                        screenshotTipTimer.restart()
                    }
                }
            }

            Item { Layout.fillWidth: true }

            VolumeControl {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 32
            }

            Button {
                icon.source: "qrc:/icons/icons/play_list.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "transparent"
                display: AbstractButton.IconOnly
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Playlist")
                ToolTip.delay: 500
                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: playlistDrawer.opened ? playlistDrawer.close() : playlistDrawer.open()
            }

            Button {
                icon.source: controlBar.isFullscreen ? "qrc:/icons/icons/exit_fullscreen.svg" : "qrc:/icons/icons/fullscreen.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "transparent"
                display: AbstractButton.IconOnly
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                ToolTip.visible: hovered
                ToolTip.text: controlBar.isFullscreen ? qsTr("Exit Fullscreen") : qsTr("Fullscreen")
                ToolTip.delay: 500
                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? "#30ffffff" : "transparent"
                }
                onClicked: controlBar.toggleFullscreen()
            }
        }
    }

    Timer {
        id: screenshotTipTimer
        interval: 2000
        onTriggered: screenshotTip.visible = false
    }

    ToolTip {
        id: screenshotTip
        visible: false
        timeout: 2000
    }
}
