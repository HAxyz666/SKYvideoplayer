import QtQuick
import QtQuick.Window
import SKYvideoplayer 1.0

Window {
    id:window
    width: 800
    height: 600
    visible: true
    title: "Video Audio Sync Player"

    VideoRenderItem {
        id: videoRenderItem
        objectName: "videoRenderItem"
        anchors.fill: parent
    }
}
