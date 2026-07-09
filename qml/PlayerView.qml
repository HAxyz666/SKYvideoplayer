import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SKYvideoplayer 1.0

Rectangle {
    id: playerView

    property var controller
    property var playlistDrawer
    property bool isFullscreen: false
    signal toggleFullscreen()
    signal userInteracted()

    property alias showControls: controlBar.showControls

    function formatTime(seconds) {
        var m = Math.floor(seconds / 60)
        var s = Math.floor(seconds % 60)
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    color: "black"

    onVisibleChanged: {
        console.log("playerView.visible =", visible)
        if (visible) {
            controlBar.showControls = true
            playerView.userInteracted()
        }
    }

    HoverHandler {
        onHoveredChanged: {
            console.log("playerView hovered =", hovered)
            if (hovered) {
                controlBar.showControls = true
                playerView.userInteracted()
            }
        }
    }

    Timer {
        id: doubleTapTimer
        interval: 250
        onTriggered: {
            console.log("singleTap confirmed")
            controlBar.showControls = true
            playerView.userInteracted()
            controller.togglePlay()
        }
    }

    TapHandler {
        onTapped: {
            if (point.position.y >= playerView.height - controlBar.height)
                return
            if (playlistDrawer.opened && point.position.x >= playerView.width - playlistDrawer.width)
                return
            if (doubleTapTimer.running) {
                doubleTapTimer.stop()
                console.log("doubleTap detected")
                playerView.toggleFullscreen()
            } else {
                doubleTapTimer.restart()
            }
        }
    }

    VideoRenderItem {
        id: videoRenderItem
        objectName: "videoRenderItem"
        anchors.fill: parent
        visible: !controller.isAudioOnly
        videoRotation: appController.rotation
        flipVertical: appController.flipVertical
    }

    Item {
        id: subtitleOverlay
        anchors.fill: parent
        visible: !controller.isAudioOnly
        z: 10

        Text {
            id: subtitleText

            anchors.horizontalCenter: parent.horizontalCenter

            y: {
                var pos = settingsManager.subtitleStyle.position || "bottom"
                if (pos === "top")
                    return 96
                if (pos === "center")
                    return (parent.height - height) / 2
                return parent.height - height - 96
            }

            text: appController.currentSubtitle

            color: settingsManager.subtitleStyle.color || "#FFFFFF"
            font.family: settingsManager.subtitleStyle.fontFamily || "Sans Serif"
            font.pixelSize: settingsManager.subtitleStyle.fontSize || 20
            font.bold: true

            horizontalAlignment: Text.Center
            width: parent.width * 0.75
            wrapMode: Text.Wrap
            style: Text.Outline
            styleColor: "#80000000"
            lineHeight: 1.3
            visible: text.length > 0
        }
    }

    Rectangle {
        id: audioOverlay
        anchors.fill: parent
        color: "black"
        visible: controller.isAudioOnly

        Image {
            anchors.fill: parent
            source: appController.coverArtUrl
            fillMode: Image.PreserveAspectFit
            visible: appController.coverArtUrl !== ""
        }

        ColumnLayout {
            anchors.centerIn: parent
            visible: appController.coverArtUrl === ""
            spacing: 16

            Label {
                text: "♪"
                font.pixelSize: 72
                color: "white"
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: qsTr("正在播放音频")
                color: "white"
                font.pixelSize: 28
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }

    Text {
        id: lyricText
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - 120
        width: parent.width * 0.8
        z: 20
        text: appController.currentLyric
        color: "#FFFFFF"
        font.pixelSize: 24
        font.bold: true
        horizontalAlignment: Text.Center
        wrapMode: Text.Wrap
        style: Text.Outline
        styleColor: "#C0000000"
        lineHeight: 1.4
        visible: text.length > 0
    }

    Button {
        id: backBtn
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 12
        visible: controlBar.showControls
        icon.source: "qrc:/icons/icons/exit_play.svg"
        icon.width: 20
        icon.height: 20
        icon.color: "transparent"
        display: AbstractButton.IconOnly
        flat: true
        padding: 0
        width: 32
        height: 32
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Back to list")
        ToolTip.delay: 500
        onClicked: {
            videoRenderItem.clearImage()
            controller.closeFile()
        }
    }

    Item {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 80
        HoverHandler {
            onHoveredChanged: {
                if (hovered) {
                    controlBar.showControls = true
                    playerView.userInteracted()
                }
            }
        }
    }

    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        width: 80
        height: 80
        HoverHandler {
            onHoveredChanged: {
                if (hovered) {
                    controlBar.showControls = true
                    playerView.userInteracted()
                }
            }
        }
    }

    ControlBar {
        id: controlBar
        anchors.bottom: parent.bottom
        width: parent.width
        controller: playerView.controller
        playlistDrawer: playerView.playlistDrawer
        isFullscreen: playerView.isFullscreen
        onToggleFullscreen: playerView.toggleFullscreen()
        onUserInteracted: playerView.userInteracted()
    }

    // 恢复播放提示条
    Rectangle {
        id: resumeToast
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: resumeRow.width + 32
        height: 36
        radius: 18
        color: "#90000000"
        visible: false
        z: 100

        property double savedPosition: 0

        Row {
            id: resumeRow
            anchors.centerIn: parent
            spacing: 8

            Label {
                text: qsTr("上次播放到 ") + playerView.formatTime(resumeToast.savedPosition)
                color: "white"
                font.pixelSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }

            Label {
                text: qsTr("从头播放")
                color: "#4FC3F7"
                font.pixelSize: 13
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter

                TapHandler {
                    onTapped: {
                        appController.resumeFromBeginning()
                        resumeToast.visible = false
                        resumeHideTimer.stop()
                    }
                }
            }
        }

        Timer {
            id: resumeHideTimer
            interval: 3000
            onTriggered: resumeToast.visible = false
        }

        function show(pos) {
            savedPosition = pos
            visible = true
            resumeHideTimer.restart()
        }
    }

    Connections {
        target: appController
        function onResumePositionFound(path, position) {
            if (position > 5.0)
                resumeToast.show(position)
        }
    }
}
