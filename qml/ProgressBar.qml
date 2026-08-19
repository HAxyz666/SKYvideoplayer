import QtQuick

Item {
    id: root

    property real position: 0.0
    property real duration: 0.0
    property real progress: duration > 0 ? position / duration : 0.0
    property bool dragging: false
    property real dragPosition: 0.0

    signal seekRequested(real pos)
    signal scrubStarted()
    signal scrubPositionChanged(real pos)
    signal scrubEnded(real pos)

    height: 28

    // 时间标签 (左侧)
    Text {
        id: timeLabel
        anchors.left: parent.left
        anchors.bottom: track.top
        anchors.bottomMargin: 2
        text: TimeUtils.formatTime(root.dragging ? root.dragPosition : root.position) + " / " + TimeUtils.formatTime(root.duration)
        color: appController.theme === "dark" ? "#cccccc" : "#666666"
        font.pixelSize: 11
    }

    // 背景轨道
    Rectangle {
        id: track
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 6
        height: 6
        radius: 3
        color: appController.theme === "dark" ? "#444444" : "#d0d0d0"

        // 已播放进度
        Rectangle {
            height: parent.height
            radius: 3
            color: "#0078d7"
            width: parent.width * (root.dragging
                ? (root.duration > 0 ? root.dragPosition / root.duration : 0.0)
                : root.progress)
        }
    }

    // 拖拽进度：拖动中先暂停并实时预览目标帧（scrubPositionChanged），
    // 松开时 scrubEnded 携带最终位置。
    DragHandler {
        target: null
        cursorShape: Qt.PointingHandCursor
        onActiveChanged: {
            if (active) {
                root.dragPosition = Math.max(0, centroid.position.x / root.width * root.duration)
                root.dragging = true
                root.scrubStarted()
            } else {
                root.dragging = false
                root.scrubEnded(root.dragPosition)
            }
        }
        onTranslationChanged: {
            root.dragPosition = Math.max(0, centroid.position.x / root.width * root.duration)
            root.scrubPositionChanged(root.dragPosition)
        }
    }

    // 点击跳转
    TapHandler {
        cursorShape: Qt.PointingHandCursor
        onTapped: {
            root.seekRequested(Math.max(0, point.position.x / root.width * root.duration))
        }
    }
}
