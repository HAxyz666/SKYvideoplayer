import QtQuick

Item {
    id: root
    anchors.fill: parent
    visible: !isAudioOnly
    z: 10

    property bool isAudioOnly: false

    Text {
        id: subtitleText

        anchors.horizontalCenter: parent.horizontalCenter

        y: {
            var pos = settingsManager.subtitleStyle.position || "bottom"
            if (pos === "top")
                return 96
            if (pos === "center")
                return (parent.height - height) / 2
            return parent.height - height - 96
        }

        text: appController.currentSubtitle

        color: settingsManager.subtitleStyle.color || "#FFFFFF"
        font.family: settingsManager.subtitleStyle.fontFamily || "Sans Serif"
        font.pixelSize: settingsManager.subtitleStyle.fontSize || 20
        font.bold: true

        horizontalAlignment: Text.Center
        width: parent.width * 0.75
        wrapMode: Text.Wrap
        style: Text.Outline
        styleColor: "#80000000"
        lineHeight: 1.3
        visible: text.length > 0
    }
}
