import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: videoPopup

    x: parent ? parent.width / 2 - width / 2 : 0
    y: parent ? -height - 4 : 0
    width: 224
    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutsideParent

    background: Rectangle {
        radius: 8
        color: appController.theme === "dark" ? "#2d2d2d" : "#ffffff"
        border.width: 0
    }

    contentItem: Column {
        spacing: 0
        // 不裁剪：行内控件被不同 QQC2 样式撑高时裁掉内容会导致数值显示残缺
        clip: false

        // === 画面调节：亮度/对比度/饱和度 ===
        Label {
            width: videoPopup.width - 8
            leftPadding: 8
            height: 24
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Picture Adjust")
            font.pixelSize: 11
            font.bold: true
            color: appController.theme === "dark" ? "#9ab8d8" : "#4070a0"
        }

        Repeater {
            model: [
                { key: "brightness", label: qsTr("Brightness") },
                { key: "contrast",   label: qsTr("Contrast") },
                { key: "saturation", label: qsTr("Saturation") }
            ]
            delegate: RowLayout {
                id: adjRow
                width: videoPopup.width - 8
                height: 26
                spacing: 4

                function sliderValue() {
                    if (modelData.key === "brightness") return settingsManager.brightness
                    if (modelData.key === "contrast") return settingsManager.contrast
                    return settingsManager.saturation
                }

                Label {
                    text: modelData.label
                    font.pixelSize: 11
                    color: appController.theme === "dark" ? "#dddddd" : "#333333"
                    Layout.preferredWidth: 62
                    Layout.fillHeight: true
                    verticalAlignment: Text.AlignVCenter
                }

                Slider {
                    id: slide
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 40
                    from: -100
                    to: 100
                    stepSize: 1
                    value: adjRow.sliderValue()
                    implicitHeight: 20

                    onMoved: {
                        if (modelData.key === "brightness") settingsManager.brightness = value
                        else if (modelData.key === "contrast") settingsManager.contrast = value
                        else settingsManager.saturation = value
                    }

                    background: Rectangle {
                        x: slide.leftPadding
                        y: slide.topPadding + slide.availableHeight / 2 - height / 2
                        implicitHeight: 3
                        width: slide.availableWidth
                        height: 3
                        radius: 1.5
                        color: slide.hovered
                            ? (appController.theme === "dark" ? "#808080" : "#c0c0c0")
                            : (appController.theme === "dark" ? "#555555" : "#d0d0d0")
                        Rectangle {
                            width: slide.visualPosition * parent.width
                            height: parent.height
                            radius: 1.5
                            color: "#0078d7"
                        }
                    }

                    handle: Rectangle {
                        x: slide.leftPadding + slide.visualPosition * (slide.availableWidth - width)
                        y: slide.topPadding + slide.availableHeight / 2 - height / 2
                        implicitWidth: 12
                        implicitHeight: 12
                        radius: 6
                        color: slide.pressed ? "#ffffff" : (appController.theme === "dark" ? "#cccccc" : "#666666")
                        border.color: "#0078d7"
                        border.width: 1
                    }
                }

                // 手动精确输入：可编辑数字框（拖动滑块不够精确，直接键入数值）
                SpinBox {
                    id: spin
                    editable: true
                    from: -100
                    to: 100
                    stepSize: 1
                    // 固定紧凑宽度：自定义 + / - 按钮并限制总宽度，
                    // 避免不同 QQC2 样式（Fusion/GTK 等）让 SpinBox 变宽导致与滑块重叠
                    implicitWidth: 58
                    implicitHeight: 24
                    Layout.preferredWidth: 58
                    Layout.maximumWidth: 58
                    Layout.fillHeight: true
                    // 归零内边距并撑满行高，避免样式的 padding/字号把文本挤出可视区
                    padding: 0
                    leftPadding: 2
                    rightPadding: 18

                    // 自定义紧凑的 + / - 精细调节按钮（保留 ±1 步进，不再依赖样式自带按钮）
                    up.indicator: Rectangle {
                        x: parent.width - width
                        y: 0
                        width: 18
                        height: parent.height / 2
                        color: "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            font.pixelSize: 14
                            color: appController.theme === "dark" ? "#dddddd" : "#333333"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: spin.value += spin.stepSize
                        }
                    }
                    down.indicator: Rectangle {
                        x: parent.width - width
                        y: parent.height / 2
                        width: 18
                        height: parent.height / 2
                        color: "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "−"
                            font.pixelSize: 14
                            color: appController.theme === "dark" ? "#dddddd" : "#333333"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: spin.value -= spin.stepSize
                        }
                    }

                    contentItem: TextField {
                        anchors.fill: parent
                        anchors.leftMargin: 2
                        anchors.rightMargin: 18
                        text: spin.textFromValue(spin.value, spin.locale)
                        font.pixelSize: 11
                        color: appController.theme === "dark" ? "#dddddd" : "#333333"
                        // 样式自带的上下 padding 会把数字挤出输入框可视范围，这里清零
                        padding: 0
                        horizontalAlignment: Qt.AlignHCenter
                        verticalAlignment: Qt.AlignVCenter
                        background: null
                        readOnly: !spin.editable
                        selectByMouse: true
                        validator: spin.validator
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onAccepted: spin.value = spin.valueFromText(text, spin.locale)
                    }

                    // 用 Binding 而非 value: 绑定——SpinBox 内部提交编辑时会
                    // 覆盖 QML 绑定，Binding 对象可在设置值变化后重新同步
                    Binding {
                        target: spin
                        property: "value"
                        value: adjRow.sliderValue()
                    }

                    onValueModified: {
                        if (modelData.key === "brightness") settingsManager.brightness = value
                        else if (modelData.key === "contrast") settingsManager.contrast = value
                        else settingsManager.saturation = value
                    }

                    background: Rectangle {
                        implicitWidth: 58
                        implicitHeight: 24
                        radius: 4
                        border.color: spin.activeFocus ? "#0078d7" : (appController.theme === "dark" ? "#555" : "#bbb")
                        border.width: spin.activeFocus ? 1 : 0
                        color: appController.theme === "dark" ? "#3d3d3d" : "#f0f0f0"
                    }
                }
            }
        }

        Item {
            width: videoPopup.width - 8
            height: 9
            Rectangle {
                width: parent.width - 16
                height: 1
                anchors.centerIn: parent
                color: appController.theme === "dark" ? "#40ffffff" : "#20000000"
            }
        }

        // === 缩放模式：Fit / Fill / Stretch ===
        Label {
            width: videoPopup.width - 8
            leftPadding: 8
            height: 24
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Aspect Mode")
            font.pixelSize: 11
            font.bold: true
            color: appController.theme === "dark" ? "#9ab8d8" : "#4070a0"
        }

        Row {
            width: videoPopup.width - 8
            height: 30
            spacing: 4
            leftPadding: 8
            rightPadding: 8

            Repeater {
                model: [
                    { mode: 0, label: qsTr("Fit") },
                    { mode: 1, label: qsTr("Fill") },
                    { mode: 2, label: qsTr("Stretch") }
                ]
                delegate: ItemDelegate {
                    width: (parent.width - 16 - 8) / 3
                    height: parent.height
                    padding: 0
                    hoverEnabled: true

                    contentItem: Text {
                        text: modelData.label
                        font.pixelSize: 11
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: settingsManager.scaleMode === modelData.mode
                            ? "#0078d7"
                            : (appController.theme === "dark" ? "#dddddd" : "#333333")
                    }

                    background: Rectangle {
                        radius: 4
                        border.color: settingsManager.scaleMode === modelData.mode
                            ? "#0078d7"
                            : (appController.theme === "dark" ? "#555" : "#ccc")
                        border.width: settingsManager.scaleMode === modelData.mode ? 1 : 0
                        color: parent.hovered
                            ? (appController.theme === "dark" ? "#30ffffff" : "#20000000")
                            : "transparent"
                    }

                    onClicked: settingsManager.scaleMode = modelData.mode
                }
            }
        }

        Item {
            width: videoPopup.width - 8
            height: 9
            Rectangle {
                width: parent.width - 16
                height: 1
                anchors.centerIn: parent
                color: appController.theme === "dark" ? "#40ffffff" : "#20000000"
            }
        }

        // === 旋转 / 翻转 ===
        Repeater {
            model: [
                { label: qsTr("Rotate Left 90°"),  action: "rotateLeft" },
                { label: qsTr("Rotate Right 90°"), action: "rotateRight" },
                { label: qsTr("Flip Vertical"),    action: "flipVertical" }
            ]
            delegate: ItemDelegate {
                width: videoPopup.width - 8
                height: 32
                contentItem: Text {
                    text: modelData.label
                    font.pixelSize: 13
                    color: appController.theme === "dark" ? "#ffffff" : "#222222"
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                }
                background: Rectangle {
                    radius: 4
                    color: parent.hovered
                        ? (appController.theme === "dark" ? "#40ffffff" : "#20000000")
                        : ((modelData.action === "flipVertical" && appController.flipVertical)
                            ? (appController.theme === "dark" ? "#30ffffff" : "#25000000")
                            : "transparent")
                }
                onClicked: {
                    if (modelData.action === "rotateLeft") appController.rotateLeft()
                    else if (modelData.action === "rotateRight") appController.rotateRight()
                    else if (modelData.action === "flipVertical") appController.toggleFlipVertical()
                }
            }
        }

        Item {
            width: videoPopup.width - 8
            height: 9
            Rectangle {
                width: parent.width - 16
                height: 1
                anchors.centerIn: parent
                color: appController.theme === "dark" ? "#40ffffff" : "#20000000"
            }
        }

        // === 重置 ===
        ItemDelegate {
            width: videoPopup.width - 8
            height: 32
            contentItem: Text {
                text: qsTr("Reset")
                font.pixelSize: 13
                color: appController.theme === "dark" ? "#ffffff" : "#222222"
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
            background: Rectangle {
                radius: 4
                color: parent.hovered
                    ? (appController.theme === "dark" ? "#40ffffff" : "#20000000")
                    : "transparent"
            }
            onClicked: {
                appController.resetRotation()
                settingsManager.brightness = 0
                settingsManager.contrast = 0
                settingsManager.saturation = 0
                settingsManager.scaleMode = 0
                videoPopup.close()
            }
        }
    }
}
