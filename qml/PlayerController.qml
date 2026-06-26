import QtQuick

QtObject {
    id: playerController

    property bool hasMedia: false
    property bool isPlaying: false
    property bool isAudioOnly: appController.isAudioOnly

    function openFile(url) {
        var ok = appController.loadFile(url);
        hasMedia = ok;
        isPlaying = ok;
    }

    function closeFile() {
        hasMedia = false;
        isPlaying = false;
        appController.stop();
    }

    function togglePlay() { appController.togglePlayback(); }

    function setSpeed(v) { appController.setSpeed(v); }
}
