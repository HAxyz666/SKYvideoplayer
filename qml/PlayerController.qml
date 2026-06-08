import QtQuick

QtObject {
    id: playerController

    property bool hasMedia: false
    property bool isPlaying: false
    property string mediaSource: ""
    property real playbackPosition: 0.0
    property real duration: 0.0

    function openFile(url) {
        mediaSource = url;
        hasMedia = true;
        isPlaying = true;
        appController.loadFile(url);
    }

    function closeFile() {
        mediaSource = "";
        hasMedia = false;
        isPlaying = false;
        appController.stop();
    }

    function togglePlay() { isPlaying = !isPlaying; }
}
