import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs

Window {
    id: root

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "DFEE"
    color: bg

    readonly property color bg: "#101114"
    readonly property color canvas: "#141519"
    readonly property color panel: "#1b1d22"
    readonly property color panelRaised: "#23262d"
    readonly property color border: "#343842"
    readonly property color textPrimary: "#f1f2f4"
    readonly property color textSecondary: "#a9adb7"
    readonly property color textMuted: "#747985"
    readonly property color accent: "#d7b46a"
    readonly property color accentDark: "#735a2b"
    readonly property color danger: "#e28a8a"

    FileDialog {
        id: openDialog
        title: "Open image"
        nameFilters: ["Supported images (*.tif *.tiff *.arw *.nef *.cr3 *.raf *.rw2 *.dng)"]
        onAccepted: engine.openFile(selectedFile)
    }

    component InspectorLabel: Text {
        color: root.textSecondary
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    component InspectorSlider: Slider {
        id: control
        width: parent.width
        implicitHeight: 24

        background: Rectangle {
            x: control.leftPadding
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: control.availableWidth
            height: 4
            radius: 2
            color: root.border

            Rectangle {
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: root.accent
            }
        }

        handle: Rectangle {
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: control.pressed ? root.textPrimary : root.accent
            border.width: 2
            border.color: root.panel
        }
    }

    component PrimaryButton: Button {
        id: button
        width: parent.width
        height: 38
        font.pixelSize: 13
        font.weight: Font.DemiBold

        contentItem: Text {
            text: button.text
            color: button.enabled ? root.bg : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: 5
            color: !button.enabled ? root.panelRaised : button.down ? "#c89e4d" : root.accent
        }
    }

    component SecondaryButton: Button {
        id: button
        width: parent.width
        height: 38
        font.pixelSize: 13
        font.weight: Font.DemiBold

        contentItem: Text {
            text: button.text
            color: button.enabled ? root.textPrimary : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: 5
            color: button.down ? root.panelRaised : "transparent"
            border.width: 1
            border.color: button.enabled ? root.border : root.panelRaised
        }
    }

    Rectangle {
        id: previewCanvas
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: inspector.left
        color: root.canvas

        Image {
            anchors.fill: parent
            anchors.margins: 28
            fillMode: Image.PreserveAspectFit
            cache: false
            source: engine.hasImage ? ("image://preview/frame?rev=" + engine.previewRevision) : ""
            visible: engine.hasImage
        }

        Column {
            anchors.centerIn: parent
            visible: !engine.hasImage
            spacing: 8

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "DFEE"
                color: root.textPrimary
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Open a RAW or TIFF image to begin"
                color: root.textMuted
                font.pixelSize: 13
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: statusText.visible ? 38 : 0
            color: "#cc101114"
            visible: height > 0

            Text {
                id: statusText
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
                text: engine.status
                color: engine.status.startsWith("Open failed:") || engine.status.startsWith("Render failed:") || engine.status.startsWith("Export failed:") ? root.danger : root.textSecondary
                font.pixelSize: 12
                visible: text.length > 0
            }
        }
    }

    Rectangle {
        id: inspector
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        width: 340
        color: root.panel
        border.width: 1
        border.color: root.border

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Column {
                width: parent.width
                spacing: 4

                Text {
                    text: "Film lab"
                    color: root.textPrimary
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                }
                Text {
                    text: engine.hasImage ? "Native preview" : "No image loaded"
                    color: root.textMuted
                    font.pixelSize: 12
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: root.border
            }

            PrimaryButton {
                text: "Open image"
                onClicked: openDialog.open()
            }

            SecondaryButton {
                text: "Export TIFF"
                enabled: engine.hasImage
                onClicked: engine.exportImage()
            }

            Column {
                width: parent.width
                spacing: 7

                InspectorLabel { text: "Film stock" }
                ComboBox {
                    id: stockBox
                    width: parent.width
                    height: 38
                    model: engine.stockNames
                    onActivated: engine.stock = engine.stockIdAt(currentIndex)

                    contentItem: Text {
                        leftPadding: 12
                        rightPadding: 34
                        text: stockBox.displayText
                        color: root.textPrimary
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.pixelSize: 13
                    }
                    background: Rectangle {
                        radius: 5
                        color: root.panelRaised
                        border.width: 1
                        border.color: stockBox.activeFocus ? root.accentDark : root.border
                    }
                    indicator: Text {
                        x: stockBox.width - width - 12
                        y: stockBox.topPadding + (stockBox.availableHeight - height) / 2
                        text: "⌄"
                        color: root.textSecondary
                        font.pixelSize: 16
                    }
                    popup: Popup {
                        y: stockBox.height + 4
                        width: stockBox.width
                        implicitHeight: Math.min(contentItem.implicitHeight + 8, 280)
                        padding: 4
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: stockBox.popup.visible ? stockBox.delegateModel : null
                            currentIndex: stockBox.highlightedIndex
                            ScrollIndicator.vertical: ScrollIndicator { }
                        }
                        background: Rectangle {
                            radius: 5
                            color: root.panelRaised
                            border.width: 1
                            border.color: root.border
                        }
                    }
                    delegate: ItemDelegate {
                        width: stockBox.width - 8
                        height: 34
                        highlighted: stockBox.highlightedIndex === index
                        contentItem: Text {
                            text: modelData
                            color: root.textPrimary
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }
                        background: Rectangle {
                            radius: 4
                            color: parent.highlighted ? root.border : "transparent"
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 5

                Row {
                    width: parent.width
                    InspectorLabel { text: "Film exposure" }
                    Text {
                        width: parent.width - x
                        text: (engine.filmExposure >= 0 ? "+" : "") + engine.filmExposure.toFixed(1) + " EV"
                        color: root.textMuted
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 12
                    }
                }
                InspectorSlider {
                    from: -5
                    to: 5
                    value: engine.filmExposure
                    onMoved: engine.filmExposure = value
                }
            }

            Column {
                width: parent.width
                spacing: 5

                Row {
                    width: parent.width
                    InspectorLabel { text: "Shadow lift" }
                    Text {
                        width: parent.width - x
                        text: (engine.shadowLift >= 0 ? "+" : "") + engine.shadowLift.toFixed(0)
                        color: root.textMuted
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: 12
                    }
                }
                InspectorSlider {
                    from: -100
                    to: 100
                    value: engine.shadowLift
                    onMoved: engine.shadowLift = value
                }
            }

            Text {
                width: parent.width
                text: "DFEE Native Engine"
                color: root.textMuted
                font.pixelSize: 11
            }
        }
    }
}
