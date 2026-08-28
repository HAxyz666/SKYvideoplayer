import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal fileDoubleClicked(string filePath, int mode)
    property alias model: listView.model

    property bool editing: false
    property var selectedIndices: ({})

    function _selectAll(select) {
        var newIndices = {}
        for (var i = 0; i < listView.count; ++i)
            newIndices[i] = select
        root.selectedIndices = newIndices
    }

    function _deleteSelected() {
        var indices = []
        for (var key in root.selectedIndices) {
            if (root.selectedIndices[key])
                indices.push(parseInt(key))
        }
        indices.sort(function(a, b) { return b - a })
        for (var i = 0; i < indices.length; ++i)
            listView.model.removeFile(indices[i])
        root.selectedIndices = {}
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: appController.theme === "dark" ? "#333333" : "#e8e8e8"
            visible: listView.count > 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4

                Label {
                    text: qsTr("Recent")
                    font.bold: true
                    font.pixelSize: 16
                    color: appController.theme === "dark" ? "#ffffff" : "#333333"
                    Layout.alignment: Qt.AlignVCenter
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Select")
                    flat: true
                    visible: !root.editing
                    onClicked: root.editing = true
                }

                CheckBox {
                    text: qsTr("Select All")
                    visible: root.editing
                    checked: {
                        if (listView.count === 0) return false
                        for (var i = 0; i < listView.count; ++i)
                            if (root.selectedIndices[i] !== true) return false
                        return true
                    }
                    onToggled: root._selectAll(checked)
                }

                Button {
                    text: qsTr("Delete Selected")
                    visible: root.editing
                    enabled: {
                        for (var key in root.selectedIndices)
                            if (root.selectedIndices[key]) return true
                        return false
                    }
                    flat: true
                    onClicked: {
                        root._deleteSelected()
                        root.editing = false
                    }
                }

                Button {
                    text: qsTr("Cancel")
                    visible: root.editing
                    flat: true
                    onClicked: {
                        root.selectedIndices = {}
                        root.editing = false
                    }
                }
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            delegate: Rectangle {
                id: delegateRoot
                width: ListView.view.width
                height: 40
                property bool rowHovered: false
                color: rowHovered
                    ? (appController.theme === "dark" ? "#3d3d3d" : "#d0d0d0")
                    : "transparent"

                HoverHandler {
                    onHoveredChanged: parent.rowHovered = hovered
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onDoubleTapped: root.fileDoubleClicked(model.filePath, model.mode)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        contextMenu.targetIndex = index
                        contextMenu.popup(delegateRoot, point.position.x, point.position.y)
                    }
                }

                Item {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4

                    CheckBox {
                        id: itemCheck
                        visible: root.editing
                        checked: root.selectedIndices[index] === true
                        onCheckedChanged: {
                            var newIndices = Object.assign({}, root.selectedIndices)
                            newIndices[index] = checked
                            root.selectedIndices = newIndices
                        }
                    }

                    Label {
                        text: model.fileName
                        font.pixelSize: 13
                        color: appController.theme === "dark" ? "#ffffff" : "#000000"
                        elide: Text.ElideRight
                        anchors.left: itemCheck.visible ? itemCheck.right : parent.left
                        anchors.right: parent.horizontalCenter
                        anchors.rightMargin: 78
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Item {
                        id: rightHalf
                        anchors.left: parent.horizontalCenter
                        anchors.right: parent.right
                    }

                    Label {
                        text: model.filePath
                        font.pixelSize: 10
                        color: appController.theme === "dark" ? "#888888" : "#999999"
                        elide: Text.ElideRight
                        width: 140
                        anchors.horizontalCenter: rightHalf.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        ToolTip.text: model.filePath
                        ToolTip.visible: historyPathHover.hovered
                        ToolTip.delay: 0
                        HoverHandler { id: historyPathHover }
                    }

                    Label {
                        text: model.lastPlayed instanceof Date
                            ? Qt.formatDateTime(model.lastPlayed, "yyyy-MM-dd hh:mm")
                            : ""
                        font.pixelSize: 11
                        color: appController.theme === "dark" ? "#888888" : "#999999"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        text: qsTr("No playback history")
        opacity: 0.5
        visible: listView.count === 0
    }

    Menu {
        id: contextMenu
        property int targetIndex: -1

        MenuItem {
            text: qsTr("Delete")
            icon.name: "edit-delete"
            onTriggered: {
                if (contextMenu.targetIndex >= 0)
                    listView.model.removeFile(contextMenu.targetIndex)
            }
        }
    }
}
