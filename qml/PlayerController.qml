import QtQuick

QtObject {
    id: playerController

    property bool hasMedia: false
    property bool isPlaying: false
    property bool isAudioOnly: appController.isAudioOnly
    property bool isLiveStream: appController.isLiveStream
    property double speed: 1.0

    // 网络流连接失败时的提示
    property bool networkError: false
    property string networkErrorMessage: ""

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

    function setSpeed(v) {
        speed = v;
        appController.setSpeed(v);
    }
}
