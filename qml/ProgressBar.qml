import QtQuick

Item {
    id: root

    property real position: 0.0
    property real duration: 0.0
    property real progress: duration > 0 ? position / duration : 0.0
    property bool dragging: false

    signal seekRequested(real pos)

    height: 28

    function formatTime(seconds) {
        if (seconds <= 0) return "00:00"
        var s = Math.floor(seconds)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        var pad = function(v) { return v < 10 ? "0" + v : "" + v }
        if (h > 0) return h + ":" + pad(m) + ":" + pad(sec)
        return pad(m) + ":" + pad(sec)
    }

    // 时间标签 (左侧)
    Text {
        id: timeLabel
        anchors.left: parent.left
        anchors.bottom: track.top
        anchors.bottomMargin: 2
        text: formatTime(root.position) + " / " + formatTime(root.duration)
        color: "#cccccc"
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
        color: "#444444"

        // 已播放进度
        Rectangle {
            height: parent.height
            radius: 3
            color: "#0078d7"
            width: parent.width * root.progress
        }
    }

    // 拖拽进度
    DragHandler {
        id: dragHandler
        target: null
        cursorShape: Qt.PointingHandCursor
        onActiveChanged: {
            root.dragging = active
            if (active) {
                root.seekRequested(Math.max(0, centroid.position.x / root.width * root.duration))
            }
        }
        // translationChanged 是原生信号，比属性绑定更及时
        onTranslationChanged: {
            root.seekRequested(Math.max(0, centroid.position.x / root.width * root.duration))
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
