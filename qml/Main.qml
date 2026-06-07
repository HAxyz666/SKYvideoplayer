import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SKYvideoplayer 1.0

ApplicationWindow {
    id:window
    width: 1280
    height: 720
    visible: true
    title: "Video Audio Sync Player"

    palette.window: "#1e1e1e"
    palette.windowText: "#ffffff"
    palette.base: "#2d2d2d"
    palette.text: "#ffffff"
    palette.button: "#3d3d3d"
    palette.buttonText: "#ffffff"
    palette.highlight: "#0078d7"

    // 实例化主布局
    AppLayout {
        anchors.fill: parent
    }
}
