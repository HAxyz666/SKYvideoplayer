import QtQuick

Item {
    id: playerController
    visible: false

    // ── 从 appController 直接读取 ──
    readonly property bool   isAudioOnly:      appController.isAudioOnly
    readonly property bool   isLiveStream:     appController.isLiveStream
    readonly property real   position:         appController.position
    readonly property real   duration:         appController.duration
    readonly property real   volume:           appController.volume
    readonly property bool   muted:            appController.muted
    readonly property real   speed:            appController.speed
    readonly property bool   isLoading:        appController.isLoading
    readonly property int    bufferState:      appController.bufferState
    readonly property string currentSubtitle:  appController.currentSubtitle

    // ── PlayerController 自身管理的状态 ──
    property bool   hasMedia: false
    property bool   isPlaying: false
    property bool   networkError: false
    property string networkErrorMessage: ""

    // ── 派生属性：View 直接消费，无需重复计算 ──
    readonly property real   progress:           duration > 0 ? position / duration : 0.0
    readonly property bool   canSeek:            hasMedia && !isLiveStream
    readonly property string formattedPosition:  formatTime(position)
    readonly property string formattedDuration:  formatTime(duration)

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

    function seekTo(pos) {
        appController.seekTo(pos)
    }

    // ── 工具函数 ──
    function formatTime(seconds) {
        var s = Math.floor(seconds || 0)
        var h = Math.floor(s / 3600)
        var m = Math.floor((s % 3600) / 60)
        var sec = s % 60
        var pad = function(v) { return v < 10 ? "0" + v : "" + v }
        if (h > 0) return h + ":" + pad(m) + ":" + pad(sec)
        return pad(m) + ":" + pad(sec)
    }
}
