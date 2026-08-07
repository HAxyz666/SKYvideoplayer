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

    // 按下时若已处于弹窗上方，该次 tap 不再触发播放控制。
    // 弹窗要到 release 才可能被关闭，因此按下时的判定不受关闭时序影响。
    property bool pressOverPopup: false

    // 当任意弹窗/对话框处于打开状态时返回 true，用于避免点击穿透到播放区。
    // 打开的 Popup 在 Overlay 中表现为可见的 QQuickPopupItem：它自身没有
    // opened 属性（只有 visible 为 true），关闭时会被移出 Overlay，
    // 因此按 visible 判定。
    function anyOpenPopupUnder(item) {
        if (!item || !item.children)
            return false
        var kids = item.children
        for (var i = 0; i < kids.length; ++i) {
            var c = kids[i]
            if (c && (c.opened === true || c.visible === true))
                return true
            if (anyOpenPopupUnder(c))
                return true
        }
        return false
    }
    function isAnyPopupOpen() {
        try {
            if (appController.modalCount > 0)
                return true
        } catch (e) {}
        try {
            return anyOpenPopupUnder(Overlay.overlay)
        } catch (e) {
            return false
        }
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
            if (playerView.isAnyPopupOpen())
                return
            console.log("singleTap confirmed")
            controlBar.showControls = true
            playerView.userInteracted()
            controller.togglePlay()
        }
    }

    TapHandler {
        onPressedChanged: {
            if (pressed)
                pressOverPopup = playerView.isAnyPopupOpen()
        }
        onTapped: {
            if (pressOverPopup || playerView.isAnyPopupOpen())
                return
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

    LoadingOverlay {}

    SubtitleOverlay {
        isAudioOnly: controller.isAudioOnly
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
                text: qsTr("Playing Audio")
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
        z: 10
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
        background: Rectangle {
            radius: 4
            color: parent.hovered ? "#30ffffff" : "transparent"
        }
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
        z: 10
        controller: playerView.controller
        playlistDrawer: playerView.playlistDrawer
        isFullscreen: playerView.isFullscreen
        onToggleFullscreen: playerView.toggleFullscreen()
        onUserInteracted: playerView.userInteracted()
    }

    ResumeToast {
        id: resumeToast
        controller: playerView.controller
    }
}
