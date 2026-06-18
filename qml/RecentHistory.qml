import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    signal fileDoubleClicked(string filePath)
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
            Layout.preferredHeight: 36
            color: "#e8e8e8"
            visible: listView.count > 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("选择")
                    flat: true
                    visible: !root.editing
                    onClicked: root.editing = true
                }

                CheckBox {
                    text: qsTr("全选")
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
                    text: qsTr("删除选中")
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
                    text: qsTr("取消")
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
                color: rowHovered ? "#d0d0d0" : "transparent"

                HoverHandler {
                    onHoveredChanged: parent.rowHovered = hovered
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    cursorShape: Qt.PointingHandCursor
                    onDoubleTapped: root.fileDoubleClicked(model.filePath)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        contextMenu.targetIndex = index
                        contextMenu.popup(delegateRoot, point.position.x, point.position.y)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    spacing: 8

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
                        font.pixelSize: 14
                        color: "#333333"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    Label {
                        text: model.lastPlayed instanceof Date
                            ? Qt.formatDateTime(model.lastPlayed, "yyyy-MM-dd hh:mm")
                            : ""
                        font.pixelSize: 11
                        color: "#888888"
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        text: qsTr("暂无播放记录")
        opacity: 0.5
        visible: listView.count === 0
    }

    Menu {
        id: contextMenu
        property int targetIndex: -1

        MenuItem {
            text: qsTr("删除")
            icon.name: "edit-delete"
            onTriggered: {
                if (contextMenu.targetIndex >= 0)
                    listView.model.removeFile(contextMenu.targetIndex)
            }
        }
    }
}
