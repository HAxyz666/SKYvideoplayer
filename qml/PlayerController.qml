import QtQuick

Item {
    id: playerController
    visible: false

    // ── 从 appController 直接读取 ──
    readonly property bool   isAudioOnly:      appController.isAudioOnly
    readonly property bool   isLiveStream:     appController.isLiveStream
    readonly property real   position:         appController.position
    readonly property real   duration:         appController.duration
    readonly property real   speed:            appController.speed

    // ── PlayerController 自身管理的状态 ──
    property bool   hasMedia: false
    property bool   isPlaying: false
    property bool   networkError: false
    property string networkErrorMessage: ""

    // ── 派生属性：View 直接消费，无需重复计算 ──
    readonly property bool   canSeek:          hasMedia && !isLiveStream

    // ── 内部信号监听：状态变更在此闭环 ──
    Connections {
        target: appController
        function onPlaybackStateChanged(playing) {
            playerController.isPlaying = playing
        }
        function onErrorOccurred(message, isNetworkRelated) {
            playerController.hasMedia = false
            playerController.isPlaying = false
            playerController.networkError = true
            playerController.networkErrorMessage = message
            errorTimer.restart()
        }
    }

    Timer {
        id: errorTimer
        interval: 3000
        onTriggered: {
            networkError = false
            networkErrorMessage = ""
        }
    }

    // ── Action API ──
    function openFile(url) {
        var ok = appController.loadFile(url)
        hasMedia = ok
        isPlaying = ok
    }

    function closeFile() {
        hasMedia = false
        isPlaying = false
        appController.stop()
    }

    function togglePlay() {
        appController.togglePlayback()
    }

    function setSpeed(v) {
        appController.setSpeed(v)
    }
}
