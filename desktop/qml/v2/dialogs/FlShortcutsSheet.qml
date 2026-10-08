import QtQuick
import QtQuick.Controls
import DFEE

// Keyboard shortcuts, grouped in two columns of keycaps. ?, F1 or Esc close it.
FlSheet {
    id: sheet
    objectName: "shortcutsSheet"
    title: "Keyboard shortcuts"
    width: 640
    // A modal sheet blocks the window's own ? / F1, so the sheet closes itself.
    Shortcut { sequences: ["?", "F1"]; enabled: sheet.opened; onActivated: sheet.close() }
    readonly property var columns: [
        [
            { title: "Editing", items: [
                { keys: ["Ctrl", "Z"], desc: "Undo" },
                { keys: ["Ctrl", "Y"], desc: "Redo" },
                { keys: ["Ctrl", "Shift", "R"], desc: "Reset all edits" },
                { keys: ["Ctrl", "Shift", "C"], desc: "Copy look" },
                { keys: ["Ctrl", "Shift", "V"], desc: "Paste look" },
                { keys: ["Ctrl", "S"], desc: engine.lightroomRoundTrip ? "Save & return to Lightroom" : "Export" }
            ]},
            { title: "Photos", items: [
                { keys: ["←", "→"], desc: "Previous / next photo" },
                { keys: ["Ctrl", "←", "→"], desc: "Previous / next (from a slider)" }
            ]},
            { title: "Panels", items: [
                { keys: ["Ctrl", "B"], desc: "Library and history" },
                { keys: ["Ctrl", "J"], desc: "Roll, films and looks" },
                { keys: ["Ctrl", "Alt", "B"], desc: "Adjustments" },
                { keys: ["Tab"], desc: "Hide or show all panels" }
            ]},
            { title: "Help", items: [
                { keys: ["?"], desc: "Show this help" }
            ]}
        ],
        [
            { title: "View", items: [
                { keys: ["\\"], desc: "Before / after" },
                { keys: ["B"], desc: "Cycle compare mode" },
                { keys: ["Ctrl", "0"], desc: "Fit" },
                { keys: ["Ctrl", "1"], desc: "Zoom to 200%" }
            ]},
            { title: "Film", items: [
                { keys: ["["], desc: "Previous film" },
                { keys: ["]"], desc: "Next film" },
                { keys: ["F"], desc: "Search films" }
            ]},
            { title: "Crop", items: [
                { keys: ["C"], desc: "Crop (or R)" },
                { keys: ["Return"], desc: "Apply crop" },
                { keys: ["Esc"], desc: "Cancel crop" }
            ]}
        ]
    ]
    Row {
        width: parent.width
        spacing: 28
        Repeater {
            model: sheet.columns
            delegate: Column {
                width: (sheet.availableWidth - 28) / 2
                spacing: 16
                Repeater {
                    model: modelData
                    delegate: Column {
                        width: parent.width
                        spacing: 8
                        FlGroupLabel { text: modelData.title }
                        Repeater {
                            model: modelData.items
                            delegate: Item {
                                width: parent.width
                                height: 24
                                Text {
                                    anchors.left: parent.left
                                    anchors.right: keys.left
                                    anchors.rightMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.desc
                                    elide: Text.ElideRight
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontLabel
                                }
                                Row {
                                    id: keys
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 4
                                    Repeater {
                                        model: modelData.keys
                                        delegate: FlKeycap { label: modelData }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
