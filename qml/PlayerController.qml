import QtQuick

QtObject {
    id: playerController

    property bool hasMedia: false
    property bool isPlaying: false
    property bool isAudioOnly: appController.isAudioOnly
    property string mediaSource: ""
    property real playbackPosition: 0.0
    property real duration: 0.0
    property real speed: 1.0

    function openFile(url) {
        mediaSource = url;
        var ok = appController.loadFile(url);
        hasMedia = ok;
        isPlaying = ok;
    }

    function closeFile() {
        mediaSource = "";
        hasMedia = false;
        isPlaying = false;
        appController.stop();
    }

    function togglePlay() { appController.togglePlayback(); }

    function setSpeed(v) {
        speed = v;
        appController.setSpeed(v);
    }
}
