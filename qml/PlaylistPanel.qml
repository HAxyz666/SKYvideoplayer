import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Drawer {
    id: playlistDrawer
    edge: Qt.RightEdge
    width: 280
    height: parent.height
    modal: true
    interactive: true

    background: Rectangle {
        color: appController.theme === "dark" ? "#dd1e1e1e" : "#bbffffff"
    }

    property int sortField: 0
    property bool sortAscending: true

    function toggleSort(field) {
        if (sortField === field) {
            sortAscending = !sortAscending
        } else {
            sortField = field
            sortAscending = true
        }
        rebuildModel()
    }

    function formatDuration(sec) {
        if (isNaN(sec) || sec <= 0) return "0:00"
        var t = Math.floor(sec)
        var h = Math.floor(t / 3600)
        var m = Math.floor((t % 3600) / 60)
        var s = t % 60
        if (h > 0)
            return h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    function rebuildModel() {
        sortedModel.clear()
        var src = appController.playlistModel
        var count = src.count
        if (count === 0) return

        var items = []
        for (var i = 0; i < count; i++)
            items.push(src.getItem(i))

        var field = sortField
        var asc = sortAscending ? 1 : -1
        items.sort(function(a, b) {
            var va = field === 0 ? a.title : a.duration
            var vb = field === 0 ? b.title : b.duration
            if (typeof va === "string") {
                va = va.toLowerCase()
                vb = vb.toLowerCase()
            }
            if (va < vb) return -1 * asc
            if (va > vb) return 1 * asc
            return 0
        })

        for (var j = 0; j < items.length; j++)
            sortedModel.append(items[j])
    }

    ListModel { id: sortedModel }

    Connections {
        target: appController.playlistModel
        function onCountChanged() { rebuildModel() }
        function onDataChanged() { rebuildModel() }
        function onModelReset() { rebuildModel() }
    }

    Component.onCompleted: rebuildModel()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ToolBar {
            Layout.fillWidth: true
            background: null

            RowLayout {
                anchors.fill: parent
                spacing: 4

                Label {
                    text: qsTr("播放列表")
                    font.bold: true
                    font.pixelSize: 16
                    Layout.fillWidth: true
                    leftPadding: 8
                }

                ToolButton {
                    text: qsTr("名称") + (sortField === 0 ? (sortAscending ? " ↑" : " ↓") : "")
                    font.pixelSize: 11
                    onClicked: toggleSort(0)
                }

                ToolButton {
                    text: qsTr("时长") + (sortField === 1 ? (sortAscending ? " ↑" : " ↓") : "")
                    font.pixelSize: 11
                    onClicked: toggleSort(1)
                }

                ToolButton {
                    icon.name: "edit-delete"
                    text: qsTr("清空")
                    display: AbstractButton.TextBesideIcon
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.clear()
                }

                ToolButton {
                    icon.name: "window-close"
                    onClicked: playlistDrawer.close()
                }
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: sortedModel
            clip: true

            Rectangle {
                x: 40
                y: 0
                width: 1
                height: listView.contentHeight
                color: appController.theme === "dark" ? "#444444" : "#dddddd"
            }

            delegate: Rectangle {
                id: delegateRoot
                width: ListView.view.width
                height: 44

                property bool hovered: false

                color: model.isPlaying
                    ? (appController.theme === "dark" ? "#440078d7" : "#440078d7")
                    : (hovered
                        ? (appController.theme === "dark" ? "#33ffffff" : "#22000000")
                        : "transparent")

                HoverHandler {
                    onHoveredChanged: delegateRoot.hovered = hovered
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onDoubleTapped: appController.playItem(model.sourceRow)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        contextMenu.sourceRow = model.sourceRow
                        contextMenu.popup()
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 0

                    Label {
                        text: (index + 1)
                        font.pixelSize: 13
                        font.bold: true
                        color: appController.theme === "dark" ? "#666666" : "#bbbbbb"
                        Layout.preferredWidth: 24
                        horizontalAlignment: Qt.AlignHCenter
                    }

                    Item { Layout.preferredWidth: 8 }

                    Label {
                        text: model.title
                        font.pixelSize: 13
                        color: appController.theme === "dark" ? "#ffffff" : "#333333"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: formatDuration(model.duration)
                        font.pixelSize: 11
                        color: appController.theme === "dark" ? "#888888" : "#999999"
                        Layout.preferredWidth: 50
                        horizontalAlignment: Qt.AlignRight
                        Layout.rightMargin: 4
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 0
                    height: 1
                    color: appController.theme === "dark" ? "#33ffffff" : "#22000000"
                }

                Menu {
                    id: contextMenu
                    property int sourceRow: -1
                    MenuItem {
                        text: qsTr("移除")
                        onTriggered: appController.playlistModel.removeItem(contextMenu.sourceRow)
                    }
                }
            }

            removeDisplaced: Transition {
                NumberAnimation { property: "y"; duration: 200 }
            }

            addDisplaced: Transition {
                NumberAnimation { property: "y"; duration: 200 }
            }

            Label {
                anchors.centerIn: parent
                text: qsTr("暂无文件")
                opacity: 0.5
                visible: listView.count === 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4

            Label {
                text: qsTr("播放模式:")
                font.pixelSize: 12
                opacity: 0.7
            }

            Row {
                id: modeGroup
                spacing: 2

                ToolButton {
                    text: qsTr("顺序播放")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 0
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 0
                }

                ToolButton {
                    text: qsTr("列表循环")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 1
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 1
                }

                ToolButton {
                    text: qsTr("单集循环")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 2
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 2
                }
            }
        }

        Label {
            text: listView.count + qsTr(" 个文件")
            padding: 10
            font.pixelSize: 12
            opacity: 0.7
            Layout.fillWidth: true
            horizontalAlignment: Qt.AlignHCenter
        }
    }
}
