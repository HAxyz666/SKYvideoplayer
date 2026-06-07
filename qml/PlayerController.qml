import QtQuick

QtObject {
    id: playerController

    // 核心状态：当前是否加载了媒体
    property bool hasMedia: false

    property string mediaSource: ""
    property bool isPlaying: false
    property real playbackPosition: 0.0
    property real duration: 0.0

    function openFile(url) {
        mediaSource = url;
        hasMedia = true;
        isPlaying = true;
        mediaEngine.open(url);
    }

    function closeFile() {
        mediaSource = "";
        hasMedia = false;
        isPlaying = false;
        mediaEngine.stop();
    }

    function togglePlay() { isPlaying = !isPlaying; }
}
