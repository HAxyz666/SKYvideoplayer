pragma Singleton
import QtQuick

// 时间格式化工具：mm:ss，超过 1 小时为 h:mm:ss
QtObject {
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