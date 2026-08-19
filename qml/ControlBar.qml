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
    // 任一子菜单打开时为 true，用于暂停自动隐藏（不含 ToolTip，避免误判）
    property bool anyPopupOpen: speedPopup.opened
        || videoPopup.opened
        || subtitleSelector.isPopupOpen
        || audioSelector.isPopupOpen
    height: 80
    color: appController.theme === "dark" ? "#80000000" : "#cc000000"
    visible: showControls

    HoverHandler {
        onHoveredChanged: {
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
            position: controller.position
            duration: controller.duration
            enabled: controller.canSeek
            opacity: controller.canSeek ? 1.0 : 0.3
            onSeekRequested: function(pos) {
                if (controller.canSeek)
                    appController.seekTo(pos)
            }
            onScrubStarted: appController.scrubStart()
            onScrubPositionChanged: function(pos) {
                appController.scrubTo(pos)
            }
            onScrubEnded: function(pos) {
                appController.scrubEnd(pos)
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
                    if (speedPopup.opened)
                        speedPopup.close()
                    else
                        speedPopup.open()
                }

                SpeedPopup {
                    id: speedPopup
                    controller: controlBar.controller
                    onClosed: controlBar.userInteracted()
                }
            }

            SubtitleSelector {
                id: subtitleSelector
                onPopupClosed: controlBar.userInteracted()
            }

            AudioSelector {
                id: audioSelector
                onPopupClosed: controlBar.userInteracted()
            }

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
                    if (videoPopup.opened)
                        videoPopup.close()
                    else
                        videoPopup.open()
                }

                VideoAdjustPopup {
                    id: videoPopup
                    onClosed: controlBar.userInteracted()
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

            Button {
                id: abLoopBtn
                text: "AB"
                flat: true
                padding: 0
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                font.pixelSize: 11
                font.bold: true
                enabled: appController.currentFilePath !== ""
                ToolTip.visible: hovered
                ToolTip.text: appController.abLoopState === 2
                    ? qsTr("A-B loop active, click to clear")
                    : qsTr("A-B loop: set A, then B")
                ToolTip.delay: 500
                contentItem: Text {
                    text: abLoopBtn.text
                    font: abLoopBtn.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 4
                    color: abLoopBtn.hovered ? "#30ffffff"
                         : (appController.abLoopState === 2 ? "#4060c0ff"
                            : (appController.abLoopState === 1 ? "#30ffb74d" : "transparent"))
                }
                onClicked: appController.toggleABLoop()
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
