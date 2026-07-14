import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

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

    onClosed: appController.persistPlaylists()

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

    // 切换列表后重建显示模型
    Connections {
        target: appController
        function onPlaylistModelChanged() { rebuildModel() }
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
                    text: qsTr("Playlist")
                    font.bold: true
                    font.pixelSize: 16
                    Layout.fillWidth: true
                    leftPadding: 8
                }

                ToolButton {
                    text: qsTr("Name") + (sortField === 0 ? (sortAscending ? " ↑" : " ↓") : "")
                    font.pixelSize: 11
                    onClicked: toggleSort(0)
                }

                ToolButton {
                    text: qsTr("Duration") + (sortField === 1 ? (sortAscending ? " ↑" : " ↓") : "")
                    font.pixelSize: 11
                    onClicked: toggleSort(1)
                }

                ToolButton {
                    icon.name: "edit-delete"
                    text: qsTr("Clear")
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.fillHeight: false
            color: "transparent"

            RowLayout {
                id: listRow
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 10
                anchors.bottomMargin: 10
                spacing: 10

                Label {
                    text: qsTr("Current List:")
                    font.pixelSize: 15
                    opacity: 0.8
                }

                ComboBox {
                    id: playlistCombo
                    Layout.fillWidth: true
                    font.pixelSize: 14
                    model: appController.playlistNames
                    currentIndex: appController.currentPlaylistIndex
                    onActivated: appController.switchPlaylist(index)
                    background: Rectangle {
                        color: "transparent"
                    }
                }

                ToolButton {
                    icon.name: "document-open"
                    text: qsTr("Add")
                    display: AbstractButton.TextBesideIcon
                    font.pixelSize: 13
                    onClicked: addMenu.popup()
                }

                ToolButton {
                    text: "+"
                    font.pixelSize: 18
                    font.bold: true
                    onClicked: newListDialog.open()
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
                        text: qsTr("Remove")
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
                text: qsTr("No files")
                opacity: 0.5
                visible: listView.count === 0
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 4

            Label {
                text: qsTr("Playback Mode:")
                font.pixelSize: 12
                opacity: 0.7
            }

            Row {
                id: modeGroup
                spacing: 2

                ToolButton {
                    text: qsTr("Sequential")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 0
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 0
                }

                ToolButton {
                    text: qsTr("Loop All")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 1
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 1
                }

                ToolButton {
                    text: qsTr("Loop One")
                    checkable: true
                    checked: appController.playlistModel.playbackMode === 2
                    font.pixelSize: 11
                    onClicked: appController.playlistModel.playbackMode = 2
                }
            }
        }

        Label {
            text: listView.count + qsTr(" files")
            padding: 10
            font.pixelSize: 12
            opacity: 0.7
            Layout.fillWidth: true
            horizontalAlignment: Qt.AlignHCenter
        }
    }

    FileDialog {
        id: addFileDialog
        title: qsTr("Add Files")
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("all file (*)"),
            qsTr("video file (*.mp4 *.mkv *.avi *.mov *.flv *.wmv)"),
            qsTr("audio file (*.mp3 *.flac *.wav *.aac *.ogg *.opus *.m4a *.wma)")
        ]
        onAccepted: appController.addFiles(selectedFiles)
    }

    Menu {
        id: addMenu
        MenuItem {
            text: qsTr("Files")
            onTriggered: addFileDialog.open()
        }
        MenuItem {
            text: qsTr("URL")
            onTriggered: addUrlDialog.open()
        }
    }

    Dialog {
        id: addUrlDialog
        title: qsTr("Add URL")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        contentItem: TextField {
            id: urlInput
            placeholderText: qsTr("Enter media URL")
            focus: true
            onAccepted: addUrlDialog.accept()
        }
        onAboutToShow: urlInput.text = ""
        onAccepted: {
            appController.addUrl(urlInput.text)
            urlInput.text = ""
        }
    }

    Dialog {
        id: newListDialog
        title: qsTr("New List")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        contentItem: TextField {
            id: newListName
            placeholderText: qsTr("Enter list name")
            focus: true
            onAccepted: newListDialog.accept()
        }
        onAboutToShow: newListName.text = ""
        onAccepted: {
            appController.createPlaylist(newListName.text)
            newListName.text = ""
        }
    }
}
