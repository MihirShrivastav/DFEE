import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects

Window {
    id: root

    width: 1280
    height: 800
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: "Film Lab"
    color: bg

    // Graphite palette — charcoal, monochrome. Color lives only in the photo + boxart.
    readonly property color bg: "#0f0f10"
    readonly property color canvas: "#0f0f10"
    readonly property color panel: "#1a1a1c"
    readonly property color panelRaised: "#1c1c1e"    // dialogs/popovers — same family as the develop cards
    // Input surface. On these already-dark cards a near-black "well" reads as a
    // cheap pasted-on box, so inputs are a subtle surface a touch LIGHTER than the
    // card (Linear/Vercel-style), defined by a soft border rather than by darkness.
    readonly property color inset: "#232327"
    readonly property color well: "#141416"           // true recess — slider grooves + histogram only
    readonly property color border: "#26262b"
    readonly property color hair: "#14ffffff"           // ~0.08 white hairline (#AARRGGBB)
    readonly property color textPrimary: "#c7c7cc"      // softened — no pure white
    readonly property color textSecondary: "#8b8b90"
    readonly property color textMuted: "#5a5a60"
    readonly property color textValue: "#74747a"        // dim right-hand slider readouts
    readonly property color accent: "#e9e9ec"           // inverted light chip (checkbox tick only)
    readonly property color accentDark: "#c9c9ce"       // pressed light chip
    readonly property color accentText: "#161618"       // text on a light accent fill
    readonly property color knob: "#c4c4c9"             // slider knob
    readonly property color danger: "#e0655b"

    // Control state fills — one token per interaction state so buttons, chips and
    // list rows share exactly the same greys instead of a dozen hand-picked hexes.
    readonly property color ctrl: "#2e2e34"             // raised control base (buttons, chips)
    readonly property color ctrlPressed: "#26262b"      // pressed raised control
    readonly property color ctrlHover: "#232327"        // hover fill on flat rows / list items
    readonly property color ctrlActive: "#33333a"       // selected / active control
    readonly property color bevel: "#16ffffff"          // 1px top highlight on raised controls
    readonly property color raiseTop: "#34343a"         // primary-button gradient — top
    readonly property color raiseBottom: "#242429"      // primary-button gradient — bottom
    readonly property color raiseTopDown: "#26262b"     // primary-button gradient (pressed) — top
    readonly property color raiseBottomDown: "#1d1d20"  // primary-button gradient (pressed) — bottom

    // Corner radii — a control radius shared by every button/field/card, plus a
    // small radius for chips inside rows. Pills use height/2 at the call site.
    readonly property int radiusControl: 8
    readonly property int radiusSmall: 6

    // Type scale (documented so nothing drifts off it):
    //   22  app title            15  dialog title
    //   13  section title / body  12  field label      11  secondary / caption
    //   10  ALL-CAPS micro label (letter-spaced, muted, used sparingly)
    // Weights: Medium for titles/labels, Normal for body/values.
    property bool libraryOpen: true       // left folders pane
    property bool filmstripOpen: true     // bottom thumbnail strip
    property bool leftPanelOpen: true     // left presets/history panel (Lightroom mode)
    property bool peekBefore: false       // "\" — momentary before/after (Lightroom-style toggle)

    // True while any text field has focus — bare-letter/character shortcuts are
    // suppressed then so typing never triggers them.
    readonly property bool textEntry: stockSearch.activeFocus || presetNameField.activeFocus
        || newGroupField.activeFocus || editNameField.activeFocus
        || editNewGroupField.activeFocus || groupRenameField.activeFocus

    // Step the film stock (dir -1/+1) through the picker's model, wrapping around.
    function cycleStock(dir) {
        var m = engine.stockModel;
        if (!m || m.length === 0) return;
        var cur = 0;
        for (var i = 0; i < m.length; ++i) if (m[i].id === engine.stock) { cur = i; break; }
        engine.stock = m[(cur + dir + m.length) % m.length].id;
    }

    // ── Keyboard shortcuts ──────────────────────────────────────────────
    // Bare-letter/character shortcuts are suppressed while typing (root.textEntry).
    Shortcut {
        sequences: ["Ctrl+S", "Ctrl+Return", "Ctrl+Enter"]
        enabled: engine.hasImage && !engine.exporting
        onActivated: engine.exportImage()
    }
    Shortcut {
        sequence: "\\"                       // toggle before/after
        enabled: engine.hasBefore && !stockSearch.activeFocus
        onActivated: { previewCanvas.compareMode = 0; root.peekBefore = !root.peekBefore; }
    }
    Shortcut {
        sequence: "B"                        // cycle Edited → Split → Side by side
        enabled: engine.hasBefore && !stockSearch.activeFocus
        onActivated: { root.peekBefore = false; previewCanvas.compareMode = (previewCanvas.compareMode + 1) % 3; }
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"             // reset all edits (confirmed)
        enabled: engine.hasImage
        onActivated: resetConfirm.open()
    }
    Shortcut {
        sequences: ["Ctrl+F", "F"]           // focus the film-stock search
        enabled: !stockSearch.activeFocus
        onActivated: stockBox.popup.open()
    }
    Shortcut {
        sequence: "Ctrl+Z"                   // undo
        enabled: engine.canUndo && !stockSearch.activeFocus && !presetNameField.activeFocus
        onActivated: engine.undo()
    }
    Shortcut {
        sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]   // redo
        enabled: engine.canRedo && !stockSearch.activeFocus && !presetNameField.activeFocus
        onActivated: engine.redo()
    }
    Shortcut {
        sequence: "["                        // previous film stock
        enabled: engine.hasImage && !root.textEntry
        onActivated: root.cycleStock(-1)
    }
    Shortcut {
        sequence: "]"                        // next film stock
        enabled: engine.hasImage && !root.textEntry
        onActivated: root.cycleStock(1)
    }
    Shortcut {
        sequences: ["?", "F1"]               // keyboard shortcuts help
        enabled: !root.textEntry
        onActivated: helpDialog.opened ? helpDialog.close() : helpDialog.open()
    }

    // New-preset wizard (from the Presets panel ＋): name + group picker. Film Lab styled.
    Popup {
        id: presetNameDialog
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        width: 380
        padding: 20
        background: Rectangle { radius: 12; color: root.panelRaised; border.width: 1; border.color: root.border }
        Overlay.modal: Rectangle { color: "#99000000" }

        // "" = ungrouped; when creatingGroup, the target group is the typed name.
        property string selectedGroup: ""
        property bool creatingGroup: false
        readonly property string targetGroup: creatingGroup ? newGroupField.text.trim() : selectedGroup
        readonly property bool nameValid: presetNameField.text.trim().length > 0
        readonly property bool groupValid: !creatingGroup || newGroupField.text.trim().length > 0
        readonly property bool overwrites: nameValid && groupValid
                                           && engine.presetExists(presetNameField.text.trim(), targetGroup)

        onOpened: {
            presetNameField.text = "";
            newGroupField.text = "";
            selectedGroup = "";
            creatingGroup = false;
            presetNameField.forceActiveFocus();
        }
        function commit() {
            if (!nameValid || !groupValid) return;
            if (engine.savePreset(presetNameField.text.trim(), targetGroup)) presetNameDialog.close();
        }

        contentItem: Column {
            spacing: 14
            Text { width: parent.width; text: "Save preset"; color: root.textPrimary; font.pixelSize: 15; font.weight: Font.Medium }
            Text { width: parent.width; text: "Saves the current film stock and all develop settings as a reusable look."; color: root.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap }

            Rectangle {
                width: parent.width; height: 36; radius: 8; color: root.inset
                border.width: 1; border.color: presetNameField.activeFocus ? root.border : root.hair
                TextField {
                    id: presetNameField
                    anchors.fill: parent
                    leftPadding: 10; rightPadding: 10
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: "Preset name"
                    color: root.textPrimary; placeholderTextColor: root.textMuted
                    font.pixelSize: 13; selectByMouse: true; background: null
                    Keys.onReturnPressed: presetNameDialog.commit()
                    Keys.onEnterPressed: presetNameDialog.commit()
                    Keys.onEscapePressed: presetNameDialog.close()
                }
            }

            Text { width: parent.width; text: "GROUP"; color: root.textMuted; font.pixelSize: 10; font.weight: Font.SemiBold }
            Flow {
                width: parent.width
                spacing: 6
                PresetChip {
                    label: "Ungrouped"
                    selected: !presetNameDialog.creatingGroup && presetNameDialog.selectedGroup === ""
                    onClicked: { presetNameDialog.creatingGroup = false; presetNameDialog.selectedGroup = ""; }
                }
                Repeater {
                    model: engine.presetGroups
                    PresetChip {
                        label: modelData
                        selected: !presetNameDialog.creatingGroup && presetNameDialog.selectedGroup === modelData
                        onClicked: { presetNameDialog.creatingGroup = false; presetNameDialog.selectedGroup = modelData; }
                    }
                }
                PresetChip {
                    label: "New group"
                    selected: presetNameDialog.creatingGroup
                    onClicked: { presetNameDialog.creatingGroup = true; newGroupField.forceActiveFocus(); }
                }
            }
            Rectangle {
                width: parent.width; height: presetNameDialog.creatingGroup ? 36 : 0
                visible: presetNameDialog.creatingGroup
                radius: 8; color: root.inset
                border.width: 1; border.color: newGroupField.activeFocus ? root.border : root.hair
                TextField {
                    id: newGroupField
                    anchors.fill: parent
                    leftPadding: 10; rightPadding: 10
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: "New group name"
                    color: root.textPrimary; placeholderTextColor: root.textMuted
                    font.pixelSize: 13; selectByMouse: true; background: null
                    Keys.onReturnPressed: presetNameDialog.commit()
                    Keys.onEnterPressed: presetNameDialog.commit()
                    Keys.onEscapePressed: presetNameDialog.close()
                }
            }

            Text {
                width: parent.width
                visible: presetNameDialog.overwrites
                text: "A preset with this name already exists here — saving replaces it."
                color: root.danger; font.pixelSize: 11; wrapMode: Text.WordWrap
            }

            Row {
                anchors.right: parent.right
                spacing: 8
                SecondaryButton { width: 96; text: "Cancel"; onClicked: presetNameDialog.close() }
                PrimaryButton {
                    width: 110
                    text: presetNameDialog.overwrites ? "Replace" : "Save"
                    enabled: presetNameDialog.nameValid && presetNameDialog.groupValid
                    onClicked: presetNameDialog.commit()
                }
            }
        }
    }

    // Edit preset — rename and/or move to another group. Same picker as saving,
    // prefilled with the preset's current name and group.
    Popup {
        id: editPresetDialog
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        width: 380
        padding: 20
        background: Rectangle { radius: 12; color: root.panelRaised; border.width: 1; border.color: root.border }
        Overlay.modal: Rectangle { color: "#99000000" }

        property string targetId: ""
        property string selectedGroup: ""
        property bool creatingGroup: false
        readonly property string targetGroup: creatingGroup ? editNewGroupField.text.trim() : selectedGroup
        readonly property bool nameValid: editNameField.text.trim().length > 0
        readonly property bool groupValid: !creatingGroup || editNewGroupField.text.trim().length > 0
        readonly property string resultId: (targetGroup.length ? targetGroup + "/" : "") + editNameField.text.trim()
        readonly property bool overwrites: nameValid && groupValid && resultId !== targetId
                                           && engine.presetExists(editNameField.text.trim(), targetGroup)

        function openFor(id, name, group) {
            targetId = id;
            editNameField.text = name;
            selectedGroup = group;
            creatingGroup = false;
            editNewGroupField.text = "";
            open();
        }
        onOpened: editNameField.forceActiveFocus()
        function commit() {
            if (!nameValid || !groupValid) return;
            if (engine.editPreset(targetId, editNameField.text.trim(), targetGroup)) editPresetDialog.close();
        }

        contentItem: Column {
            spacing: 14
            Text { width: parent.width; text: "Edit preset"; color: root.textPrimary; font.pixelSize: 15; font.weight: Font.Medium }
            Rectangle {
                width: parent.width; height: 36; radius: 8; color: root.inset
                border.width: 1; border.color: editNameField.activeFocus ? root.border : root.hair
                TextField {
                    id: editNameField
                    anchors.fill: parent
                    leftPadding: 10; rightPadding: 10
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: "Preset name"
                    color: root.textPrimary; placeholderTextColor: root.textMuted
                    font.pixelSize: 13; selectByMouse: true; background: null
                    Keys.onReturnPressed: editPresetDialog.commit()
                    Keys.onEnterPressed: editPresetDialog.commit()
                    Keys.onEscapePressed: editPresetDialog.close()
                }
            }
            Text { width: parent.width; text: "GROUP"; color: root.textMuted; font.pixelSize: 10; font.weight: Font.SemiBold }
            Flow {
                width: parent.width
                spacing: 6
                PresetChip {
                    label: "Ungrouped"
                    selected: !editPresetDialog.creatingGroup && editPresetDialog.selectedGroup === ""
                    onClicked: { editPresetDialog.creatingGroup = false; editPresetDialog.selectedGroup = ""; }
                }
                Repeater {
                    model: engine.presetGroups
                    PresetChip {
                        label: modelData
                        selected: !editPresetDialog.creatingGroup && editPresetDialog.selectedGroup === modelData
                        onClicked: { editPresetDialog.creatingGroup = false; editPresetDialog.selectedGroup = modelData; }
                    }
                }
                PresetChip {
                    label: "New group"
                    selected: editPresetDialog.creatingGroup
                    onClicked: { editPresetDialog.creatingGroup = true; editNewGroupField.forceActiveFocus(); }
                }
            }
            Rectangle {
                width: parent.width; height: editPresetDialog.creatingGroup ? 36 : 0
                visible: editPresetDialog.creatingGroup
                radius: 8; color: root.inset
                border.width: 1; border.color: editNewGroupField.activeFocus ? root.border : root.hair
                TextField {
                    id: editNewGroupField
                    anchors.fill: parent
                    leftPadding: 10; rightPadding: 10
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: "New group name"
                    color: root.textPrimary; placeholderTextColor: root.textMuted
                    font.pixelSize: 13; selectByMouse: true; background: null
                    Keys.onReturnPressed: editPresetDialog.commit()
                    Keys.onEnterPressed: editPresetDialog.commit()
                    Keys.onEscapePressed: editPresetDialog.close()
                }
            }
            Text {
                width: parent.width
                visible: editPresetDialog.overwrites
                text: "Another preset with this name already exists here — saving replaces it."
                color: root.danger; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                SecondaryButton { width: 96; text: "Cancel"; onClicked: editPresetDialog.close() }
                PrimaryButton {
                    width: 110
                    text: editPresetDialog.overwrites ? "Replace" : "Save"
                    enabled: editPresetDialog.nameValid && editPresetDialog.groupValid
                    onClicked: editPresetDialog.commit()
                }
            }
        }
    }

    // Rename a group.
    Popup {
        id: groupRenameDialog
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        width: 340
        padding: 20
        background: Rectangle { radius: 12; color: root.panelRaised; border.width: 1; border.color: root.border }
        Overlay.modal: Rectangle { color: "#99000000" }
        property string oldName: ""
        function openFor(g) { oldName = g; groupRenameField.text = g; open(); }
        onOpened: { groupRenameField.selectAll(); groupRenameField.forceActiveFocus(); }
        function commit() {
            var n = groupRenameField.text.trim();
            if (n.length === 0) return;
            if (engine.renameGroup(oldName, n)) groupRenameDialog.close();
        }
        contentItem: Column {
            spacing: 14
            Text { width: parent.width; text: "Rename group"; color: root.textPrimary; font.pixelSize: 15; font.weight: Font.Medium }
            Rectangle {
                width: parent.width; height: 36; radius: 8; color: root.inset
                border.width: 1; border.color: groupRenameField.activeFocus ? root.border : root.hair
                TextField {
                    id: groupRenameField
                    anchors.fill: parent
                    leftPadding: 10; rightPadding: 10
                    verticalAlignment: TextInput.AlignVCenter
                    placeholderText: "Group name"
                    color: root.textPrimary; placeholderTextColor: root.textMuted
                    font.pixelSize: 13; selectByMouse: true; background: null
                    Keys.onReturnPressed: groupRenameDialog.commit()
                    Keys.onEnterPressed: groupRenameDialog.commit()
                    Keys.onEscapePressed: groupRenameDialog.close()
                }
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                SecondaryButton { width: 96; text: "Cancel"; onClicked: groupRenameDialog.close() }
                PrimaryButton { width: 96; text: "Rename"; enabled: groupRenameField.text.trim().length > 0; onClicked: groupRenameDialog.commit() }
            }
        }
    }

    // Confirm deleting a group (and the presets inside it).
    Popup {
        id: groupDeleteConfirm
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        width: 360
        padding: 20
        background: Rectangle { radius: 12; color: root.panelRaised; border.width: 1; border.color: root.border }
        Overlay.modal: Rectangle { color: "#99000000" }
        property string groupName: ""
        property int groupCount: 0
        function openFor(g, c) { groupName = g; groupCount = c; open(); }
        contentItem: Column {
            spacing: 16
            Text { width: parent.width; text: "Delete group?"; color: root.textPrimary; font.pixelSize: 15; font.weight: Font.Medium }
            Text {
                width: parent.width
                text: "Deletes \"" + groupDeleteConfirm.groupName + "\" and the "
                      + groupDeleteConfirm.groupCount + " preset"
                      + (groupDeleteConfirm.groupCount === 1 ? "" : "s") + " inside it. This can't be undone."
                color: root.textSecondary; font.pixelSize: 12; wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                SecondaryButton { width: 96; text: "Cancel"; onClicked: groupDeleteConfirm.close() }
                PrimaryButton { width: 96; text: "Delete"; onClicked: { engine.deleteGroup(groupDeleteConfirm.groupName); groupDeleteConfirm.close(); } }
            }
        }
    }

    // Themed confirmation for a full reset — shared by the Reset button and
    // the Ctrl+Shift+R shortcut. Destructive until edit history lands.
    Popup {
        id: resetConfirm
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        width: 340
        padding: 20
        background: Rectangle { radius: 12; color: root.panelRaised; border.width: 1; border.color: root.border }
        Overlay.modal: Rectangle { color: "#99000000" }
        contentItem: Column {
            spacing: 16
            Text {
                width: parent.width
                text: "Reset all edits?"
                color: root.textPrimary
                font.pixelSize: 15
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: "This clears the film stock, exposure and every develop control back to how the image first opened. This can't be undone yet."
                color: root.textSecondary
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Row {
                anchors.right: parent.right
                spacing: 8
                SecondaryButton { width: 96; text: "Cancel"; onClicked: resetConfirm.close() }
                PrimaryButton { width: 96; text: "Reset"; onClicked: { engine.resetAllEdits(); resetConfirm.close(); } }
            }
        }
    }

    // Keyboard-shortcuts help sheet (opened with ? or F1, or the header button).
    // macOS-style: grouped rows with keycaps, dimmed backdrop, Esc/click to close.
    Popup {
        id: helpDialog
        modal: true
        dim: true
        anchors.centerIn: Overlay.overlay
        width: 620
        padding: 28
        background: Rectangle { radius: 14; color: root.panelRaised; border.width: 1; border.color: root.border }
        Overlay.modal: Rectangle { color: "#99000000" }

        readonly property var leftGroups: [
            { title: "EDITING", items: [
                { keys: ["Ctrl", "Z"], desc: "Undo" },
                { keys: ["Ctrl", "Y"], desc: "Redo" },
                { keys: ["Ctrl", "Shift", "R"], desc: "Reset all edits" },
                { keys: ["Ctrl", "S"], desc: "Save & return to Lightroom" }
            ]},
            { title: "HELP", items: [
                { keys: ["?"], desc: "Show this help" }
            ]}
        ]
        readonly property var rightGroups: [
            { title: "VIEW", items: [
                { keys: ["\\"], desc: "Before / after" },
                { keys: ["B"], desc: "Cycle compare mode" }
            ]},
            { title: "FILM STOCK", items: [
                { keys: ["F"], desc: "Search film stocks" },
                { keys: ["["], desc: "Previous stock" },
                { keys: ["]"], desc: "Next stock" }
            ]}
        ]

        // One column of grouped shortcut rows.
        component ShortcutColumn: Column {
            property var groups: []
            spacing: 18
            Repeater {
                model: parent.groups
                Column {
                    width: parent.width
                    spacing: 9
                    Text { text: modelData.title; color: root.textMuted; font.pixelSize: 10; font.weight: Font.SemiBold }
                    Repeater {
                        model: modelData.items
                        Item {
                            width: parent.width
                            height: 26
                            Text {
                                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                anchors.right: keyRow.left; anchors.rightMargin: 12
                                text: modelData.desc; color: root.textSecondary; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            Row {
                                id: keyRow
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                                spacing: 4
                                Repeater {
                                    model: modelData.keys
                                    Keycap { label: modelData }
                                }
                            }
                        }
                    }
                }
            }
        }

        contentItem: Column {
            spacing: 20

            // Header row.
            Item {
                width: parent.width
                height: 24
                Row {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    AppIcon { anchors.verticalCenter: parent.verticalCenter; name: "keyboard"; size: 18; color: root.textPrimary }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "Keyboard shortcuts"; color: root.textPrimary; font.pixelSize: 15; font.weight: Font.Medium }
                }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: "Esc to close"; color: root.textMuted; font.pixelSize: 11
                }
            }

            // Two balanced columns.
            Row {
                width: parent.width
                spacing: 28
                ShortcutColumn { width: (parent.width - 28) / 2; groups: helpDialog.leftGroups }
                ShortcutColumn { width: (parent.width - 28) / 2; groups: helpDialog.rightGroups }
            }
        }
    }

    function exportFormatLabel(format) {
        if (format === "png8") return "8-bit PNG"
        if (format === "png16") return "16-bit PNG"
        if (format === "jpeg") return "JPEG"
        return "16-bit TIFF"
    }

    // Boxart tile for a stock id (bundled SVGs at :/boxart/<id>.svg). Empty for "none".
    function boxartFor(stockId) {
        return (stockId && stockId !== "none") ? ("qrc:/boxart/" + stockId + ".svg") : ""
    }

    // Film-stock box art tile. When a stock is selected it frames the colourful
    // box art; when empty it is NOT a grey block — just a muted film glyph, so an
    // empty slot reads as intentional ("no film loaded") rather than a broken image.
    component BoxartSwatch: Rectangle {
        property string stockId: ""
        property int cell: 26
        readonly property string boxSource: root.boxartFor(stockId)
        readonly property bool empty: boxSource === ""
        width: cell
        height: cell
        radius: 5
        clip: true
        color: empty ? "transparent" : root.inset
        border.width: empty ? 0 : 1
        border.color: root.hair
        Image {
            anchors.fill: parent
            sourceSize.width: parent.cell * 2
            sourceSize.height: parent.cell * 2
            fillMode: Image.PreserveAspectCrop
            smooth: true
            source: parent.boxSource
            visible: !parent.empty
        }
        AppIcon {
            anchors.centerIn: parent
            visible: parent.empty
            name: "film-strip"
            size: Math.round(parent.cell * 0.62)
            color: root.textMuted
        }
    }

    FileDialog {
        id: openDialog
        title: "Open image"
        nameFilters: ["Supported images (*.tif *.tiff *.arw *.nef *.cr3 *.raf *.rw2 *.dng)"]
        onAccepted: engine.openFile(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: "Add folder to library"
        onAccepted: library.addFolder(selectedFolder)
    }

    // A library thumbnail tile (grid + filmstrip share it).
    component ThumbTile: Rectangle {
        property string path: ""
        property string name: ""
        property int edge: 128
        radius: 6
        color: root.inset
        border.width: 1
        border.color: root.hair
        clip: true
        Image {
            anchors.fill: parent
            anchors.margins: 1
            asynchronous: true
            cache: true
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: parent.edge
            sourceSize.height: parent.edge
            source: parent.path !== "" ? ("image://thumb/" + encodeURIComponent(parent.path)) : ""
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: engine.openFile(Qt.resolvedUrl("file:///" + parent.path.replace(/\\/g, "/")))
        }
    }

    component InspectorLabel: Text {
        color: root.textSecondary
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    // Graphite-styled hover tooltip. Pair with a HoverHandler: `visible: hh.hovered`.
    // Caps its own width so long copy wraps instead of stretching across the screen.
    component GraphiteTip: ToolTip {
        id: tip
        delay: 450
        padding: 10
        // Cap the tooltip width; short copy shrinks, long copy wraps at 260.
        width: Math.min(implicitWidth, 260)
        contentItem: Text {
            text: tip.text
            color: root.textPrimary
            font.pixelSize: 11
            lineHeight: 1.3
            wrapMode: Text.WordWrap
            width: tip.availableWidth
        }
        background: Rectangle {
            color: "#232327"
            radius: 7
            border.width: 1
            border.color: root.hair
        }
    }

    // Live RGB histogram of the current preview — additive channel fills, sqrt-scaled
    // so low counts stay visible. Reads engine.histogramR/G/B (256 raw-count bins).
    component Histogram: Rectangle {
        width: parent.width
        height: 84
        radius: 10
        color: root.well
        border.width: 1
        border.color: root.hair

        Canvas {
            anchors.fill: parent
            anchors.margins: 7
            property var hr: engine.histogramR
            property var hg: engine.histogramG
            property var hb: engine.histogramB
            onHrChanged: requestPaint()
            onHgChanged: requestPaint()
            onHbChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var w = width, hgt = height;
                ctx.clearRect(0, 0, w, hgt);
                if (!hr || hr.length < 2) return;
                var n = hr.length;
                var mx = 1;
                for (var i = 1; i < n - 1; ++i) {
                    if (hr[i] > mx) mx = hr[i];
                    if (hg[i] > mx) mx = hg[i];
                    if (hb[i] > mx) mx = hb[i];
                }
                function drawCh(arr, style) {
                    ctx.beginPath();
                    ctx.moveTo(0, hgt);
                    for (var i = 0; i < n; ++i) {
                        var v = Math.sqrt(arr[i] / mx);
                        if (v > 1) v = 1;
                        ctx.lineTo(i / (n - 1) * w, hgt - v * hgt);
                    }
                    ctx.lineTo(w, hgt);
                    ctx.closePath();
                    ctx.fillStyle = style;
                    ctx.fill();
                }
                ctx.globalCompositeOperation = "lighter";
                drawCh(hb, "rgba(70,120,235,0.5)");
                drawCh(hg, "rgba(70,200,110,0.5)");
                drawCh(hr, "rgba(235,80,80,0.5)");
                ctx.globalCompositeOperation = "source-over";
            }
        }

        Text {
            anchors.centerIn: parent
            visible: !engine.hasImage
            text: "Histogram"
            color: root.textMuted
            font.pixelSize: 11
        }
    }

    // Small down/up chevron used in card headers (and reused as the combo indicator style).
    // Tintable monochrome icon from the bundled Phosphor set (white SVGs at
    // :/icons/<name>.svg). Recolored to `color` via MultiEffect so icons honour
    // the palette tokens and react to state exactly like text did.
    component AppIcon: Item {
        id: appIcon
        property string name: ""
        property color color: root.textSecondary
        property int size: 16
        implicitWidth: size
        implicitHeight: size
        Image {
            id: appIconSrc
            anchors.fill: parent
            source: appIcon.name.length ? ("qrc:/icons/" + appIcon.name + ".svg") : ""
            sourceSize.width: appIcon.size * 2   // supersample for crisp edges at any DPI
            sourceSize.height: appIcon.size * 2
            fillMode: Image.PreserveAspectFit
            smooth: true
            visible: false
        }
        MultiEffect {
            anchors.fill: appIconSrc
            source: appIconSrc
            colorization: 1.0
            colorizationColor: appIcon.color
        }
    }

    // Expand/collapse caret (Phosphor). open → points up (collapse), closed →
    // points down (expand), preserving the prior convention. ~14px footprint.
    component ChevronToggle: Item {
        property bool open: true
        property color color: root.textSecondary
        width: 14
        height: 14
        AppIcon {
            anchors.centerIn: parent
            name: "caret-down"
            size: 12
            color: parent.color
            rotation: parent.open ? 180 : 0
            Behavior on rotation { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }

    // Hover-revealed row action (edit / delete on preset & group rows). Tints on
    // its own hover and carries a padded hit area around the small glyph.
    component RowAction: Item {
        id: ra
        property string name: ""
        property color hoverColor: root.textPrimary
        signal triggered()
        implicitWidth: 16
        implicitHeight: 16
        AppIcon {
            anchors.centerIn: parent
            name: ra.name
            size: 15
            color: raMouse.containsMouse ? ra.hoverColor : root.textSecondary
        }
        MouseArea {
            id: raMouse
            anchors.fill: parent; anchors.margins: -5
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ra.triggered()
        }
    }

    // A single keyboard keycap (raised chip with a top bevel) for the help sheet.
    component Keycap: Rectangle {
        property string label: ""
        implicitWidth: Math.max(24, kcText.implicitWidth + 14)
        height: 24
        radius: root.radiusSmall
        color: root.ctrl
        border.width: 1
        border.color: root.hair
        Text {
            id: kcText
            anchors.centerIn: parent
            text: parent.label
            color: root.textPrimary
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }

    // Beveled action button for the Geometry tab (rotate / flip / reset). `active`
    // highlights it as an engaged toggle (used by the Flip buttons).
    component GeoButton: Button {
        id: geoBtn
        property bool active: false
        property string tooltip: ""
        height: 30
        contentItem: Text {
            text: geoBtn.text
            color: geoBtn.active ? root.textPrimary : root.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        background: Rectangle {
            radius: root.radiusControl
            color: geoBtn.active ? "#3a3a42" : (geoBtn.down ? root.ctrlPressed : root.ctrl)
            border.width: 1
            border.color: geoBtn.active ? "#4a4a52" : root.hair
            }
        HoverHandler { id: geoHover }
        GraphiteTip {
            parent: geoBtn
            x: 0
            y: geoBtn.height + 4
            visible: geoHover.hovered && geoBtn.tooltip.length > 0
            text: geoBtn.tooltip
        }
    }

    // Tactile checkbox — recessed square when off, light chip with a drawn tick when on.
    component GraphiteCheck: CheckBox {
        id: cb
        property string caption: ""
        property string tooltip: ""
        spacing: 9
        implicitHeight: 20

        HoverHandler { id: cbHover; enabled: cb.tooltip.length > 0 }
        GraphiteTip {
            parent: cb
            x: 0
            y: cb.height + 4
            visible: cbHover.hovered && cb.tooltip.length > 0
            text: cb.tooltip
        }

        indicator: Rectangle {
            implicitWidth: 18
            implicitHeight: 18
            x: 0
            y: (cb.height - height) / 2
            radius: 5
            // Tactile: recessed inset when off, raised dark bevel chip when on
            // (matches the buttons and segmented — no clashing flat white).
            gradient: Gradient {
                GradientStop { position: 0.0; color: cb.checked ? "#34343a" : root.inset }
                GradientStop { position: 1.0; color: cb.checked ? "#242429" : root.inset }
            }
            border.width: 1
            border.color: root.hair
            opacity: cb.enabled ? 1.0 : 0.5
            Canvas {
                anchors.fill: parent
                visible: cb.checked
                onVisibleChanged: if (visible) requestPaint()
                onPaint: {
                    var c = getContext("2d");
                    c.reset();
                    c.strokeStyle = "#d7d7db";
                    c.lineWidth = 2;
                    c.lineCap = "round";
                    c.lineJoin = "round";
                    c.beginPath();
                    c.moveTo(4.5, 9); c.lineTo(8, 12.5); c.lineTo(13.5, 5.5);
                    c.stroke();
                }
            }
        }

        contentItem: Text {
            text: cb.caption !== "" ? cb.caption : cb.text
            color: cb.enabled ? root.textSecondary : root.textMuted
            leftPadding: cb.indicator.width + cb.spacing
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 12
        }
    }

    // Number field in Graphite style: recessed inset value + raised bevel -/+ chips.
    component GraphiteSpin: SpinBox {
        id: spin
        implicitHeight: 30
        implicitWidth: 118
        font.pixelSize: 12

        contentItem: TextInput {
            z: 2
            text: spin.textFromValue(spin.value, spin.locale)
            color: root.textPrimary
            font: spin.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            readOnly: !spin.editable
            validator: spin.validator
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            selectByMouse: true
            selectionColor: root.textSecondary
        }

        background: Rectangle {
            radius: 8
            color: root.inset
            border.width: 1
            border.color: root.hair
        }

        down.indicator: Rectangle {
            x: 0; y: 0
            width: 32
            height: spin.height
            topLeftRadius: 8; bottomLeftRadius: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: spin.down.pressed ? "#26262b" : "#33333a" }
                GradientStop { position: 1.0; color: spin.down.pressed ? "#1d1d20" : "#242429" }
            }
            border.width: 1
            border.color: root.hair
            Text { anchors.centerIn: parent; text: "−"; color: root.textPrimary; font.pixelSize: 15 }
        }

        up.indicator: Rectangle {
            x: spin.width - width; y: 0
            width: 32
            height: spin.height
            topRightRadius: 8; bottomRightRadius: 8
            gradient: Gradient {
                GradientStop { position: 0.0; color: spin.up.pressed ? "#26262b" : "#33333a" }
                GradientStop { position: 1.0; color: spin.up.pressed ? "#1d1d20" : "#242429" }
            }
            border.width: 1
            border.color: root.hair
            Text { anchors.centerIn: parent; text: "+"; color: root.textPrimary; font.pixelSize: 15 }
        }
    }

    // Draggable color-grading wheel: angle = hue, radius = saturation. Reads/writes
    // engine.filmControls["cg_<zone>_hue"/"_sat"]. Double-click resets to neutral.
    component ColorWheel: Item {
        id: wheel
        property string zone: ""
        property string label: ""
        property int diameter: 84
        width: diameter
        height: diameter + 18
        readonly property real hueVal: Number(engine.filmControls["cg_" + zone + "_hue"])
        readonly property real satVal: Number(engine.filmControls["cg_" + zone + "_sat"])

        Rectangle {
            id: disc
            width: wheel.diameter; height: wheel.diameter; radius: wheel.diameter / 2
            anchors.horizontalCenter: parent.horizontalCenter
            color: root.inset
            border.width: 1; border.color: root.hair
            clip: true

            Canvas {
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset();
                    var w = width, cx = w / 2, cy = w / 2, r = w / 2;
                    for (var a = 0; a < 360; a += 3) {
                        ctx.beginPath();
                        ctx.moveTo(cx, cy);
                        ctx.arc(cx, cy, r, (-(a + 3)) * Math.PI / 180, (-a) * Math.PI / 180, false);
                        ctx.closePath();
                        ctx.fillStyle = "hsl(" + a + ",68%,52%)";
                        ctx.fill();
                    }
                    var g = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
                    g.addColorStop(0.0, "rgba(20,20,22,1.0)");
                    g.addColorStop(0.35, "rgba(20,20,22,0.35)");
                    g.addColorStop(1.0, "rgba(20,20,22,0.0)");
                    ctx.fillStyle = g;
                    ctx.beginPath(); ctx.arc(cx, cy, r, 0, 2 * Math.PI); ctx.fill();
                }
            }

            Rectangle {                          // handle
                width: 13; height: 13; radius: 7
                border.width: 2; border.color: "#f4f4f6"
                x: disc.width / 2 + Math.cos(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.width / 2 - 9) - width / 2
                y: disc.height / 2 - Math.sin(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.height / 2 - 9) - height / 2
                color: wheel.satVal > 0 ? Qt.hsla(((wheel.hueVal % 360) + 360) % 360 / 360, Math.min(wheel.satVal / 100, 1.0), 0.55, 1.0) : root.panelRaised
            }

            MouseArea {
                id: wheelDrag
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                onPressed: (mouse) => wheel.pick(mouse.x, mouse.y)
                onPositionChanged: (mouse) => { if (pressed) wheel.pick(mouse.x, mouse.y) }
                onDoubleClicked: {
                    engine.setFilmControl("cg_" + wheel.zone + "_hue", 0);
                    engine.setFilmControl("cg_" + wheel.zone + "_sat", 0);
                }
            }

            HoverHandler { id: wheelHover }
            GraphiteTip {
                parent: wheel
                x: 0
                y: wheel.height + 4
                visible: wheelHover.hovered && !wheelDrag.pressed
                text: "Tints the " + wheel.label.toLowerCase() + " — drag from the centre to add color, double-click to reset."
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            text: wheel.label
            color: root.textSecondary
            font.pixelSize: 11
        }

        function pick(mx, my) {
            var cx = wheel.diameter / 2, cy = wheel.diameter / 2;
            var dx = mx - cx, dy = my - cy;
            var ang = Math.atan2(-dy, dx) * 180 / Math.PI;
            if (ang < 0) ang += 360;
            var rad = Math.min(Math.sqrt(dx * dx + dy * dy) / (wheel.diameter / 2), 1.0);
            engine.setFilmControl("cg_" + wheel.zone + "_hue", Math.round(ang));
            engine.setFilmControl("cg_" + wheel.zone + "_sat", Math.round(rad * 100));
        }
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
            color: root.well                        // recessed groove
            // subtle top inset line for depth
            Rectangle { width: parent.width; height: 1; radius: 1; color: "#66000000" }
            Rectangle {                              // filled portion — subtle grey, monochrome
                width: control.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: "#3c3c41"
            }
        }

        handle: Rectangle {                          // raised metallic knob
            x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
            y: control.topPadding + control.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            gradient: Gradient {
                GradientStop { position: 0.0; color: control.pressed ? "#d7d7db" : "#cdcdd2" }
                GradientStop { position: 1.0; color: "#9a9aa1" }
            }
            border.width: 1
            border.color: "#6e000000"
        }
    }

    // Primary action — a raised dark chip with a top bevel highlight (no more white).
    component PrimaryButton: Button {
        id: button
        width: parent.width
        height: 38
        font.pixelSize: 13
        font.weight: Font.Medium

        contentItem: Text {
            text: button.text
            color: button.enabled ? root.textPrimary : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: root.radiusControl
            gradient: Gradient {
                GradientStop { position: 0.0; color: !button.enabled ? root.panel : (button.down ? root.raiseTopDown : root.raiseTop) }
                GradientStop { position: 1.0; color: !button.enabled ? root.panel : (button.down ? root.raiseBottomDown : root.raiseBottom) }
            }
            border.width: 1
            border.color: root.hair
        }
    }

    // Secondary action — a gentle raised control (subtler than PrimaryButton),
    // sharing the same radius and hairline for a consistent family.
    component SecondaryButton: Button {
        id: button
        width: parent.width
        height: 38
        font.pixelSize: 13
        font.weight: Font.Medium

        contentItem: Text {
            text: button.text
            color: button.enabled ? root.textPrimary : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: button.font
        }

        background: Rectangle {
            radius: root.radiusControl
            color: button.down ? root.panelRaised : root.inset
            border.width: 1
            border.color: root.hair
        }
    }

    // Compact raised utility button (Reset, Open, New preset, Add). Flat ctrl fill,
    // hairline border. Size via width/height at the call site.
    component UtilityButton: Button {
        id: ub
        height: 28
        font.pixelSize: 12
        font.weight: Font.Medium
        contentItem: Text {
            text: ub.text
            color: ub.enabled ? root.textPrimary : root.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font: ub.font
        }
        background: Rectangle {
            radius: root.radiusControl
            color: ub.down ? root.ctrlPressed : root.ctrl
            border.width: 1
            border.color: root.hair
            opacity: ub.enabled ? 1.0 : 0.5
        }
    }

    // Ghost icon button — transparent, subtle hover/press fill. For chevrons and
    // other icon-only affordances that shouldn't read as a raised chip. Set
    // iconName for a Phosphor glyph, or text for a character.
    component GhostButton: Button {
        id: gb
        property string iconName: ""
        property int iconSize: 16
        width: 24
        height: 24
        font.pixelSize: 16
        contentItem: Item {
            Text {
                anchors.centerIn: parent
                visible: gb.iconName.length === 0
                text: gb.text
                color: root.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font: gb.font
            }
            AppIcon {
                anchors.centerIn: parent
                visible: gb.iconName.length > 0
                name: gb.iconName
                size: gb.iconSize
                color: root.textSecondary
            }
        }
        background: Rectangle {
            radius: root.radiusSmall
            color: gb.down ? "#20ffffff" : (gb.hovered ? root.hair : "transparent")
        }
    }

    // Small pill/chip toggle — used by the preset save dialog's group picker.
    component PresetChip: Rectangle {
        property string label: ""
        property bool selected: false
        signal clicked()
        implicitWidth: chipText.implicitWidth + 22
        height: 28
        radius: height / 2
        color: selected ? root.ctrlActive : root.inset
        border.width: 1
        border.color: selected ? root.border : root.hair
        Text {
            id: chipText
            anchors.centerIn: parent
            text: parent.label
            color: parent.selected ? root.textPrimary : root.textSecondary
            font.pixelSize: 11
            font.weight: parent.selected ? Font.Medium : Font.Normal
        }
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.clicked() }
    }

    // Beveled group card — subtle raised gradient, hairline border, top highlight, and a
    // collapsible header. Content is provided inline (avoids the QML default-property trap).
    component FilmSlider: Column {
        id: sliderRow
        property string controlKey: ""
        property string label: ""
        property string tooltip: ""
        property real minimum: 0
        property real maximum: 100
        property real increment: 1
        property real neutral: 0
        property bool bipolar: false
        property bool decimals: false
        property bool available: true
        property bool autoValue: false
        width: parent.width
        spacing: 4

        readonly property real currentValue: Number(engine.filmControls[controlKey])
        readonly property bool dirty: Math.abs(currentValue - neutral) > 0.0001

        // Fixed height so the row never grows when the Reset button appears —
        // the slider below must not shift. Children are vertically centred.
        Row {
            width: parent.width
            height: 18
            InspectorLabel {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - valueLabel.width - resetButton.width - 8
                text: sliderRow.label
                color: sliderRow.available ? root.textSecondary : root.textMuted
                elide: Text.ElideRight
            }
            Button {
                id: resetButton
                anchors.verticalCenter: parent.verticalCenter
                visible: sliderRow.available && sliderRow.dirty
                width: visible ? 38 : 0
                height: 18
                text: "Reset"
                font.pixelSize: 10
                onClicked: engine.setFilmControl(sliderRow.controlKey, sliderRow.neutral)
                contentItem: Text { text: parent.text; color: root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font: parent.font }
                background: Rectangle { color: "transparent" }
            }
            Text {
                id: valueLabel
                anchors.verticalCenter: parent.verticalCenter
                width: sliderRow.autoValue ? 34 : 42
                text: sliderRow.autoValue ? "Auto" : ((sliderRow.bipolar && sliderRow.currentValue > 0 ? "+" : "") + (sliderRow.decimals ? sliderRow.currentValue.toFixed(2) : sliderRow.currentValue.toFixed(0)))
                color: sliderRow.available && sliderRow.dirty ? root.textPrimary : root.textValue
                horizontalAlignment: Text.AlignRight
                font.pixelSize: 12
            }
        }
        InspectorSlider {
            id: sliderControl
            width: parent.width
            enabled: sliderRow.available
            from: sliderRow.minimum
            to: sliderRow.maximum
            stepSize: sliderRow.increment
            value: sliderRow.autoValue ? sliderRow.neutral : sliderRow.currentValue
            opacity: sliderRow.available ? 1.0 : 0.35
            onMoved: engine.setFilmControl(sliderRow.controlKey, value)
        }

        HoverHandler { id: sliderHover; enabled: sliderRow.tooltip.length > 0 }
        GraphiteTip {
            parent: sliderRow
            x: 0
            y: sliderRow.height + 4
            // Hide while dragging the slider — a tooltip over a moving control is distracting.
            visible: sliderHover.hovered && !sliderControl.pressed && sliderRow.tooltip.length > 0
            text: sliderRow.tooltip
        }
    }

    // ── Left panel (Lightroom mode) — accordion of Presets + edit History. ──
    // Both sections are collapsible; open sections share the vertical space.
    Rectangle {
        id: leftPanel
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: filmstrip.top
        // Presets and history are navigational, not the primary working
        // surface. Keep the rail readable while giving the photo more room.
        // Collapses to a slim reopen rail so the photo can fill the window.
        width: engine.lightroomRoundTrip ? (root.leftPanelOpen ? 232 : 22) : 0
        visible: engine.lightroomRoundTrip
        color: root.bg
        border.width: 1
        border.color: root.border
        clip: true

        property bool presetsOpen: true
        property bool historyOpen: true
        readonly property int headerH: 40
        readonly property int topBarH: 30
        // Body height shared by the two lists once the fixed chrome is removed.
        readonly property real bodyH: Math.max(0, height - topBarH - 2 * headerH - 44)
        function sectionH(mine, other) { return !mine ? 0 : (other ? bodyH * 0.5 : bodyH) }

        // Collapsed: a slim rail with a reopen chevron (matches the library pane).
        Item {
            anchors.fill: parent
            visible: !root.leftPanelOpen
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.leftPanelOpen = true }
            AppIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top; anchors.topMargin: 18
                name: "caret-right"; size: 16
            }
        }

        // Group collapse state (map group name -> collapsed bool).
        property var collapsedGroups: ({})
        function isCollapsed(g) { return leftPanel.collapsedGroups[g] === true }
        function toggleGroup(g) {
            var c = Object.assign({}, leftPanel.collapsedGroups);
            c[g] = !leftPanel.isCollapsed(g);
            leftPanel.collapsedGroups = c;
        }
        // Flatten presets into display rows: ungrouped first, then each group as a
        // header row followed by its (uncollapsed) presets. Re-evaluates when the
        // preset list, group list, or collapse state changes.
        function presetRows() {
            var rows = [];
            var pres = engine.presets;
            for (var i = 0; i < pres.length; ++i)
                if (!pres[i].group) rows.push({ kind: "preset", id: pres[i].id, name: pres[i].name, group: "", grouped: false });
            var groups = engine.presetGroups;
            for (var j = 0; j < groups.length; ++j) {
                var g = groups[j];
                var items = [];
                for (var k = 0; k < pres.length; ++k) if (pres[k].group === g) items.push(pres[k]);
                rows.push({ kind: "group", name: g, count: items.length });
                if (!leftPanel.isCollapsed(g))
                    for (var m = 0; m < items.length; ++m)
                        rows.push({ kind: "preset", id: items[m].id, name: items[m].name, group: g, grouped: true });
            }
            return rows;
        }

        // Reusable accordion section header.
        component SectionHeader: Rectangle {
            property string title: ""
            property bool expanded: true
            signal toggled()
            width: parent ? parent.width : 0
            height: leftPanel.headerH
            color: "transparent"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.hair }
            Text {
                anchors.left: parent.left; anchors.leftMargin: 14; anchors.verticalCenter: parent.verticalCenter
                // Tier-1 section title: matches the right-pane card headers exactly
                // (sentence case, 13px Medium, primary) for a consistent hierarchy.
                text: title; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium
            }
            ChevronToggle {
                anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                open: expanded
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.toggled() }
        }

        Column {
            anchors.fill: parent
            visible: root.leftPanelOpen

            // Panel top bar — right-aligned collapse control.
            Item {
                width: parent.width
                height: leftPanel.topBarH
                GhostButton {
                    id: panelCollapseBtn
                    anchors.right: parent.right; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "caret-left"
                    onClicked: root.leftPanelOpen = false
                    HoverHandler { id: panelCollapseHover }
                    GraphiteTip {
                        parent: panelCollapseBtn
                        x: -6; y: panelCollapseBtn.height + 4
                        visible: panelCollapseHover.hovered
                        text: "Hide panel"
                    }
                }
            }

            // ── PRESETS ────────────────────────────────────────────────
            SectionHeader {
                title: "Presets"; expanded: leftPanel.presetsOpen
                onToggled: leftPanel.presetsOpen = !leftPanel.presetsOpen
            }
            // New-from-current action row (only when presets section is open).
            Item {
                width: parent.width; height: leftPanel.presetsOpen ? 40 : 0
                visible: leftPanel.presetsOpen
                UtilityButton {
                    id: newPresetBtn
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.margins: 10; anchors.verticalCenter: parent.verticalCenter
                    enabled: engine.hasImage
                    text: "New preset from current"
                    contentItem: Row {
                        spacing: 6
                        anchors.centerIn: parent
                        AppIcon { anchors.verticalCenter: parent.verticalCenter; name: "plus"; size: 13; color: newPresetBtn.enabled ? root.textPrimary : root.textMuted }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: newPresetBtn.text; color: newPresetBtn.enabled ? root.textPrimary : root.textMuted; font.pixelSize: 12; font.weight: Font.Medium }
                    }
                    onClicked: presetNameDialog.open()
                }
            }
            // Presets body — grouped list, with the empty-state message centered.
            Item {
                width: parent.width
                height: leftPanel.presetsOpen
                        ? Math.max(0, leftPanel.sectionH(leftPanel.presetsOpen, leftPanel.historyOpen) - 40)
                        : 0
                visible: leftPanel.presetsOpen
                clip: true

                Text {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    visible: engine.presets.length === 0
                    horizontalAlignment: Text.AlignHCenter
                    text: "No presets yet.\nBuild a look and save it as a preset."
                    color: root.textMuted; font.pixelSize: 12; wrapMode: Text.WordWrap; lineHeight: 1.3
                }

                ListView {
                    id: presetList
                    anchors.fill: parent
                    visible: engine.presets.length > 0
                    clip: true
                    model: leftPanel.presetRows()
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Item {
                        width: presetList.width
                        height: modelData.kind === "group" ? 30 : 32

                        // Group header row: chevron + name; count when idle, rename/
                        // delete actions on hover. Click the row to collapse/expand.
                        Item {
                            anchors.fill: parent
                            visible: modelData.kind === "group"
                            HoverHandler { id: groupHover }
                            ChevronToggle {
                                anchors.left: parent.left; anchors.leftMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                open: !leftPanel.isCollapsed(modelData.name)
                            }
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: 32
                                anchors.right: parent.right; anchors.rightMargin: 48
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name; elide: Text.ElideRight
                                color: root.textSecondary; font.pixelSize: 12; font.weight: Font.Medium
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: leftPanel.toggleGroup(modelData.name)
                            }
                            Text {
                                visible: !groupHover.hovered
                                anchors.right: parent.right; anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.count; color: root.textMuted; font.pixelSize: 11
                            }
                            Row {
                                visible: groupHover.hovered
                                anchors.right: parent.right; anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 12
                                RowAction { name: "pencil-simple"; onTriggered: groupRenameDialog.openFor(modelData.name) }
                                RowAction { name: "x"; hoverColor: root.danger; onTriggered: groupDeleteConfirm.openFor(modelData.name, modelData.count) }
                            }
                        }

                        // Preset row: indented under its group; apply on click, edit /
                        // delete on hover.
                        Item {
                            anchors.fill: parent
                            visible: modelData.kind === "preset"
                            Rectangle {
                                anchors.fill: parent; anchors.margins: 2; radius: 6
                                color: presetHover.hovered ? root.ctrlHover : "transparent"
                            }
                            HoverHandler { id: presetHover }
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: modelData.grouped ? 32 : 14
                                anchors.right: parent.right; anchors.rightMargin: 46
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.name; color: root.textPrimary; elide: Text.ElideRight; font.pixelSize: 13
                            }
                            MouseArea {
                                anchors.fill: parent; anchors.rightMargin: 46
                                cursorShape: Qt.PointingHandCursor
                                onClicked: engine.applyPreset(modelData.id)
                            }
                            Row {
                                visible: presetHover.hovered
                                anchors.right: parent.right; anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 12
                                RowAction { name: "pencil-simple"; onTriggered: editPresetDialog.openFor(modelData.id, modelData.name, modelData.group) }
                                RowAction { name: "x"; hoverColor: root.danger; onTriggered: engine.deletePreset(modelData.id) }
                            }
                        }
                    }
                }
            }

            // ── HISTORY ────────────────────────────────────────────────
            SectionHeader {
                title: "History"; expanded: leftPanel.historyOpen
                onToggled: leftPanel.historyOpen = !leftPanel.historyOpen
            }
            ListView {
                id: historyList
                width: parent.width
                height: leftPanel.sectionH(leftPanel.historyOpen, leftPanel.presetsOpen)
                visible: leftPanel.historyOpen
                clip: true
                model: engine.history
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Item {
                    width: historyList.width
                    height: 32                       // match the preset-row height for a uniform list rhythm
                    readonly property bool current: index === engine.historyIndex
                    // Rows newer than the current step (undone future) read dimmed.
                    readonly property bool future: index < engine.historyIndex
                    Rectangle {
                        anchors.fill: parent; anchors.margins: 2; radius: 6
                        color: current ? root.ctrlActive : (histHover.hovered ? root.ctrlHover : "transparent")
                        border.width: current ? 1 : 0; border.color: root.hair
                    }
                    HoverHandler { id: histHover }
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 14
                        anchors.right: parent.right; anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: current ? root.textPrimary : (future ? root.textMuted : root.textSecondary)
                        elide: Text.ElideRight; font.pixelSize: 12
                        font.weight: current ? Font.Medium : Font.Normal
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: engine.jumpToHistory(index) }
                }
            }
        }
    }

    // ── Library pane (left) — pinned FOLDERS only. Collapsible. Standalone only. ──
    Rectangle {
        id: libraryPane
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: filmstrip.top
        width: engine.lightroomRoundTrip ? 0 : (root.libraryOpen ? 232 : 22)
        visible: !engine.lightroomRoundTrip
        color: root.bg
        border.width: 1
        border.color: root.border

        // Collapsed: a slim rail with a reopen chevron.
        Item {
            anchors.fill: parent
            visible: !root.libraryOpen
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.libraryOpen = true }
            AppIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top; anchors.topMargin: 18
                name: "caret-right"; size: 16
            }
        }

        // Expanded content.
        Column {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12
            visible: root.libraryOpen

            Item {
                width: parent.width
                height: 26
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: "Library"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium
                }
                Row {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    UtilityButton {
                        id: addFolderBtn
                        width: 50; height: 24
                        text: "Add"
                        onClicked: folderDialog.open()
                    }
                    GhostButton {
                        id: libCollapseBtn
                        iconName: "caret-left"
                        onClicked: root.libraryOpen = false
                    }
                }
            }

            // Pinned folders (folders only — thumbnails live in the filmstrip)
            ListView {
                id: folderList
                width: parent.width
                height: parent.height - y
                clip: true
                model: library.folders
                spacing: 2
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 8 }
                delegate: Rectangle {
                    width: folderList.width
                    height: 30
                    radius: 6
                    readonly property bool selected: library.currentFolder === modelData.path
                    color: selected ? "#20ffffff" : (folderHover.hovered ? "#12ffffff" : "transparent")
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 10
                        anchors.right: rmBtn.left; anchors.verticalCenter: parent.verticalCenter
                        text: modelData.name; elide: Text.ElideMiddle
                        color: parent.selected ? root.textPrimary : root.textSecondary
                        font.pixelSize: 12
                    }
                    HoverHandler { id: folderHover }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: library.selectFolder(modelData.path) }
                    GhostButton {
                        id: rmBtn
                        anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                        width: 22; height: 22
                        visible: folderHover.hovered
                        iconName: "x"; iconSize: 14
                        onClicked: library.removeFolder(modelData.path)
                    }
                }
            }
        }
        // Empty-state hint
        Text {
            visible: root.libraryOpen && library.folders.length === 0
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 14; anchors.topMargin: 56
            text: "Add a folder to browse your shots."
            color: root.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
    }

    // ── Filmstrip (bottom) — current folder's thumbnails. Collapsible. Standalone only. ──
    Rectangle {
        id: filmstrip
        anchors.left: parent.left
        anchors.right: inspector.left
        anchors.bottom: parent.bottom
        height: engine.lightroomRoundTrip ? 0 : (root.filmstripOpen ? 108 : 22)
        visible: !engine.lightroomRoundTrip
        color: root.bg
        border.width: 1
        border.color: root.border

        // Collapsed: a slim rail with a reopen chevron.
        Item {
            anchors.fill: parent
            visible: !root.filmstripOpen
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.filmstripOpen = true }
            AppIcon { anchors.centerIn: parent; name: "caret-up"; size: 14 }
        }

        // Expanded: thumbnails + collapse toggle.
        Item {
            anchors.fill: parent
            visible: root.filmstripOpen

            ListView {
                anchors.fill: parent
                anchors.margins: 8
                anchors.rightMargin: 28
                orientation: ListView.Horizontal
                clip: true
                spacing: 6
                model: library.files
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; height: 7 }
                delegate: ThumbTile {
                    width: height * 1.4
                    height: filmstrip.height - 26
                    edge: 200
                    path: modelData.path
                    name: modelData.name
                }
            }
            GhostButton {
                id: filmCollapseBtn
                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 4
                width: 22; height: 22
                iconName: "caret-down"; iconSize: 14
                onClicked: root.filmstripOpen = false
            }
            Text {
                anchors.centerIn: parent
                visible: library.files.length === 0
                text: "No images in this folder"
                color: root.textMuted; font.pixelSize: 12
            }
        }
    }

    Rectangle {
        id: previewCanvas
        anchors.left: engine.lightroomRoundTrip ? leftPanel.right : libraryPane.right
        anchors.top: parent.top
        anchors.bottom: filmstrip.top
        anchors.right: inspector.left
        color: root.canvas
        property int compareMode: 0   // 0 = Edited, 1 = Split, 2 = Side by side

        // ── Crop tool state (Slice 1c) ──────────────────────────────────
        // While cropMode is on, the engine renders the FULL frame (crop = whole
        // image) and the overlay below lets the user draw the crop rect; the rect
        // is applied to the engine only on "Apply". cropAspect 0 = free.
        property bool cropMode: false
        property real cropAspect: 0
        property real cropX: 0
        property real cropY: 0
        property real cropW: 1
        property real cropH: 1

        function imageAspect() {
            return (afterImg.paintedHeight > 0) ? (afterImg.paintedWidth / afterImg.paintedHeight) : 1.0;
        }
        function enterCropMode() {
            cropX = engine.filmControls.crop_x;
            cropY = engine.filmControls.crop_y;
            cropW = engine.filmControls.crop_w;
            cropH = engine.filmControls.crop_h;
            compareMode = 0;
            cropMode = true;
            engine.setCrop(0, 0, 1, 1);   // show the whole frame underneath
        }
        function applyCropMode() {
            cropMode = false;
            engine.setCrop(cropX, cropY, cropW, cropH);
        }
        function selectAspect(r) {
            if (r < 0) {                    // Original — clear the crop
                cropAspect = 0;
                cropX = 0; cropY = 0; cropW = 1; cropH = 1;
                if (!cropMode) engine.setCrop(0, 0, 1, 1);
                return;
            }
            if (!cropMode) enterCropMode();
            if (r === 0) { cropAspect = 0; return; }   // free — keep current rect
            cropAspect = r;
            var A = imageAspect();
            var R = r / A;                              // normalized width/height
            var wN, hN;
            if (R >= 1) { wN = 1; hN = 1 / R; } else { hN = 1; wN = R; }
            cropW = wN; cropH = hN;
            cropX = (1 - wN) / 2; cropY = (1 - hN) / 2;
        }
        // Resize the crop rect from a handle drag. grab is a 1-or-2 char code of
        // edges (l/r/t/b); normalized pointer (nx,ny). Enforces a min size and,
        // when an aspect is locked, keeps the ratio (corner grabs only).
        function updateCrop(grab, nx, ny) {
            var minS = 0.05;
            nx = Math.max(0, Math.min(1, nx));
            ny = Math.max(0, Math.min(1, ny));
            var l = cropX, t = cropY, r = cropX + cropW, b = cropY + cropH;
            if (grab.indexOf("l") >= 0) l = Math.min(nx, r - minS);
            if (grab.indexOf("r") >= 0) r = Math.max(nx, l + minS);
            if (grab.indexOf("t") >= 0) t = Math.min(ny, b - minS);
            if (grab.indexOf("b") >= 0) b = Math.max(ny, t + minS);
            if (cropAspect > 0 && grab.length === 2) {
                var ratioN = cropAspect / imageAspect();     // normalized w/h
                var ax = (grab.indexOf("l") >= 0) ? r : l;   // anchor = opposite corner
                var ay = (grab.indexOf("t") >= 0) ? b : t;
                var wN = Math.abs(((grab.indexOf("l") >= 0) ? l : r) - ax);
                var hN = Math.abs(((grab.indexOf("t") >= 0) ? t : b) - ay);
                var w2 = Math.max(wN, hN * ratioN);
                if ((grab.indexOf("l") >= 0) && ax - w2 < 0) w2 = ax;
                if ((grab.indexOf("r") >= 0) && ax + w2 > 1) w2 = 1 - ax;
                var h2 = w2 / ratioN;
                if ((grab.indexOf("t") >= 0) && ay - h2 < 0) { h2 = ay; w2 = h2 * ratioN; }
                if ((grab.indexOf("b") >= 0) && ay + h2 > 1) { h2 = 1 - ay; w2 = h2 * ratioN; }
                l = (grab.indexOf("l") >= 0) ? ax - w2 : ax;
                r = (grab.indexOf("l") >= 0) ? ax : ax + w2;
                t = (grab.indexOf("t") >= 0) ? ay - h2 : ay;
                b = (grab.indexOf("t") >= 0) ? ay : ay + h2;
            }
            cropX = l; cropY = t; cropW = r - l; cropH = b - t;
        }

        Item {
            id: imageArea
            anchors.fill: parent
            anchors.margins: 28
            // Reserve room for the floating compare switch so it never overlaps the
            // image (matters for tall/portrait frames that fill the height).
            anchors.topMargin: (engine.hasImage && engine.hasBefore) ? 62 : 28
            visible: engine.hasImage

            readonly property string afterSrc: engine.hasImage ? ("image://preview/frame?rev=" + engine.previewRevision) : ""
            readonly property string beforeSrc: engine.hasBefore ? ("image://preview/before?rev=" + engine.beforeRevision) : ""
            readonly property bool split: previewCanvas.compareMode === 1 && engine.hasBefore
            readonly property bool sideBySide: previewCanvas.compareMode === 2 && engine.hasBefore
            readonly property bool peeking: root.peekBefore && engine.hasBefore && previewCanvas.compareMode === 0

            // Edited / Split — the film ("after") fills; "before" is clipped on the left.
            // While peeking ("\") the full frame shows the unedited original instead.
            Image {
                id: afterImg
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                cache: false
                source: imageArea.peeking ? imageArea.beforeSrc : imageArea.afterSrc
                visible: !imageArea.sideBySide
            }
            Rectangle {
                visible: imageArea.peeking && !imageArea.sideBySide
                anchors { left: parent.left; top: parent.top; margins: 6 }
                width: peekLabel.implicitWidth + 14; height: 20; radius: 6
                color: "#cc161618"; border.width: 1; border.color: root.hair
                Text { id: peekLabel; anchors.centerIn: parent; text: "Before"; color: "#e9e9ec"; font.pixelSize: 11; font.weight: Font.Medium }
            }
            Item {
                anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
                width: divider.x
                clip: true
                visible: imageArea.split
                Image {
                    width: imageArea.width
                    height: imageArea.height
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    source: imageArea.beforeSrc
                }
            }
            Text {
                visible: imageArea.split
                anchors { left: parent.left; top: parent.top; margins: 6 }
                text: "Before"
                color: "#e9e9ec"
                font.pixelSize: 11
                style: Text.Outline; styleColor: "#80000000"
            }
            Item {
                id: divider
                visible: imageArea.split
                y: 0
                x: imageArea.width / 2
                width: 2
                height: imageArea.height
                Rectangle { anchors.fill: parent; color: "#e9e9ec" }
                Rectangle {
                    anchors.centerIn: parent
                    width: 28; height: 28; radius: 14
                    color: "#e9e9ec"
                    border.width: 1; border.color: "#40000000"
                    AppIcon { anchors.centerIn: parent; name: "arrows-left-right"; size: 16; color: "#161618" }
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -14
                    cursorShape: Qt.SizeHorCursor
                    drag.target: divider
                    drag.axis: Drag.XAxis
                    drag.minimumX: 0
                    drag.maximumX: imageArea.width
                }
            }

            // Side by side — before | after.
            Row {
                anchors.fill: parent
                visible: imageArea.sideBySide
                spacing: 2
                Item {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; source: imageArea.beforeSrc }
                    Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 2 } text: "Before"; color: root.textSecondary; font.pixelSize: 11 }
                }
                Item {
                    width: (parent.width - 2) / 2
                    height: parent.height
                    Image { anchors.fill: parent; fillMode: Image.PreserveAspectFit; cache: false; source: imageArea.afterSrc }
                    Text { anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 2 } text: "After"; color: root.textSecondary; font.pixelSize: 11 }
                }
            }

            // ── Interactive crop overlay (Slice 1c) ──────────────────────
            Item {
                id: cropOverlay
                anchors.fill: parent
                visible: previewCanvas.cropMode && !imageArea.split && !imageArea.sideBySide && engine.hasImage
                // Painted image rect (PreserveAspectFit) within imageArea.
                readonly property real pw: afterImg.paintedWidth
                readonly property real ph: afterImg.paintedHeight
                readonly property real ox: (width - pw) / 2
                readonly property real oy: (height - ph) / 2
                // Crop rect in overlay/screen coords.
                readonly property real rx: ox + previewCanvas.cropX * pw
                readonly property real ry: oy + previewCanvas.cropY * ph
                readonly property real rw: previewCanvas.cropW * pw
                readonly property real rh: previewCanvas.cropH * ph
                readonly property bool free: previewCanvas.cropAspect <= 0

                // Dim the four regions outside the crop rect.
                Rectangle { color: "#99000000"; x: cropOverlay.ox; y: cropOverlay.oy; width: cropOverlay.pw; height: Math.max(0, cropOverlay.ry - cropOverlay.oy) }
                Rectangle { color: "#99000000"; x: cropOverlay.ox; y: cropOverlay.ry + cropOverlay.rh; width: cropOverlay.pw; height: Math.max(0, (cropOverlay.oy + cropOverlay.ph) - (cropOverlay.ry + cropOverlay.rh)) }
                Rectangle { color: "#99000000"; x: cropOverlay.ox; y: cropOverlay.ry; width: Math.max(0, cropOverlay.rx - cropOverlay.ox); height: cropOverlay.rh }
                Rectangle { color: "#99000000"; x: cropOverlay.rx + cropOverlay.rw; y: cropOverlay.ry; width: Math.max(0, (cropOverlay.ox + cropOverlay.pw) - (cropOverlay.rx + cropOverlay.rw)); height: cropOverlay.rh }

                // Crop frame + rule-of-thirds grid.
                Rectangle {
                    x: cropOverlay.rx; y: cropOverlay.ry; width: cropOverlay.rw; height: cropOverlay.rh
                    color: "transparent"; border.width: 1; border.color: "#f0ffffff"
                    Rectangle { color: "#40ffffff"; width: 1; height: parent.height; x: Math.round(parent.width / 3) }
                    Rectangle { color: "#40ffffff"; width: 1; height: parent.height; x: Math.round(2 * parent.width / 3) }
                    Rectangle { color: "#40ffffff"; height: 1; width: parent.width; y: Math.round(parent.height / 3) }
                    Rectangle { color: "#40ffffff"; height: 1; width: parent.width; y: Math.round(2 * parent.height / 3) }
                }

                // Handles: 4 corners always, 4 edge-midpoints only when aspect is free.
                Repeater {
                    model: [
                        { k: "lt", hx: cropOverlay.rx,                    hy: cropOverlay.ry,                    corner: true },
                        { k: "rt", hx: cropOverlay.rx + cropOverlay.rw,   hy: cropOverlay.ry,                    corner: true },
                        { k: "lb", hx: cropOverlay.rx,                    hy: cropOverlay.ry + cropOverlay.rh,   corner: true },
                        { k: "rb", hx: cropOverlay.rx + cropOverlay.rw,   hy: cropOverlay.ry + cropOverlay.rh,   corner: true },
                        { k: "t",  hx: cropOverlay.rx + cropOverlay.rw/2, hy: cropOverlay.ry,                    corner: false },
                        { k: "b",  hx: cropOverlay.rx + cropOverlay.rw/2, hy: cropOverlay.ry + cropOverlay.rh,   corner: false },
                        { k: "l",  hx: cropOverlay.rx,                    hy: cropOverlay.ry + cropOverlay.rh/2, corner: false },
                        { k: "r",  hx: cropOverlay.rx + cropOverlay.rw,   hy: cropOverlay.ry + cropOverlay.rh/2, corner: false }
                    ]
                    delegate: Rectangle {
                        visible: modelData.corner || cropOverlay.free
                        width: modelData.corner ? 12 : 10
                        height: width
                        radius: modelData.corner ? 2 : 5
                        color: "#f2ffffff"
                        border.width: 1; border.color: "#60000000"
                        x: modelData.hx - width / 2
                        y: modelData.hy - height / 2
                    }
                }

                MouseArea {
                    id: cropMouse
                    anchors.fill: parent
                    enabled: previewCanvas.cropMode
                    cursorShape: grab === "" ? Qt.ArrowCursor : (grab === "move" ? Qt.SizeAllCursor : Qt.CrossCursor)
                    property string grab: ""
                    property real startNx: 0
                    property real startNy: 0
                    property real startCropX: 0
                    property real startCropY: 0
                    onPressed: (m) => {
                        var hs = 16;
                        var l = cropOverlay.rx, t = cropOverlay.ry;
                        var r = cropOverlay.rx + cropOverlay.rw, b = cropOverlay.ry + cropOverlay.rh;
                        var cx = (l + r) / 2, cy = (t + b) / 2;
                        var near = function(px, py) { return Math.abs(m.x - px) <= hs && Math.abs(m.y - py) <= hs; };
                        var free = cropOverlay.free;
                        grab = "";
                        if (near(l, t)) grab = "lt";
                        else if (near(r, t)) grab = "rt";
                        else if (near(l, b)) grab = "lb";
                        else if (near(r, b)) grab = "rb";
                        else if (free && near(cx, t)) grab = "t";
                        else if (free && near(cx, b)) grab = "b";
                        else if (free && near(l, cy)) grab = "l";
                        else if (free && near(r, cy)) grab = "r";
                        else if (m.x > l && m.x < r && m.y > t && m.y < b) {
                            grab = "move";
                            startNx = (m.x - cropOverlay.ox) / cropOverlay.pw;
                            startNy = (m.y - cropOverlay.oy) / cropOverlay.ph;
                            startCropX = previewCanvas.cropX;
                            startCropY = previewCanvas.cropY;
                        }
                    }
                    onPositionChanged: (m) => {
                        if (grab === "" || cropOverlay.pw <= 0 || cropOverlay.ph <= 0) return;
                        var nx = (m.x - cropOverlay.ox) / cropOverlay.pw;
                        var ny = (m.y - cropOverlay.oy) / cropOverlay.ph;
                        if (grab === "move") {
                            var dx = nx - startNx, dy = ny - startNy;
                            previewCanvas.cropX = Math.max(0, Math.min(1 - previewCanvas.cropW, startCropX + dx));
                            previewCanvas.cropY = Math.max(0, Math.min(1 - previewCanvas.cropH, startCropY + dy));
                        } else {
                            previewCanvas.updateCrop(grab, nx, ny);
                        }
                    }
                    onReleased: grab = ""
                }
            }
        }

        // Floating before/after mode switch (only when a "before" is available).
        Rectangle {
            visible: engine.hasImage && engine.hasBefore
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 16
            width: compareRow.width + 8
            height: 32
            radius: 9
            color: "#cc161618"
            border.width: 1
            border.color: root.hair
            Row {
                id: compareRow
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: [{ label: "Edited", m: 0 }, { label: "Split", m: 1 }, { label: "Side by side", m: 2 }]
                    delegate: Button {
                        id: cmpBtn
                        height: 26
                        width: cmpText.implicitWidth + 20
                        readonly property bool selected: previewCanvas.compareMode === modelData.m
                        onClicked: previewCanvas.compareMode = modelData.m
                        contentItem: Text { id: cmpText; text: modelData.label; color: cmpBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                        background: Rectangle {
                            radius: 7
                            color: cmpBtn.selected ? root.ctrlActive : "transparent"
                            border.width: cmpBtn.selected ? 1 : 0
                            border.color: root.hair
                        }
                    }
                }
            }
        }

        Column {
            anchors.centerIn: parent
            visible: !engine.hasImage
            spacing: 8

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Film Lab"
                color: root.textPrimary
                font.pixelSize: 22
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Preparing your photo…" : "Open a RAW or TIFF image to begin"
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
        property int activeTab: 0                     // 0 = Develop, 1 = Geometry, 2 = Export
        // Leaving the Geometry tab commits an in-progress crop so the overlay never
        // lingers over the preview on another tab.
        onActiveTabChanged: if (activeTab !== 1 && previewCanvas.cropMode) previewCanvas.applyCropMode()
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        // The inspector keeps enough width for labels and full slider travel,
        // but the image remains the primary surface in Lightroom round-trips.
        width: 328
        color: root.bg                               // darker rail so the cards read as raised
        border.width: 1
        border.color: root.border

        // Fixed top: header + histogram + tab switcher (pinned, does not scroll).
        Column {
            id: inspectorTop
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: 18
            spacing: 12

            Item {
                width: parent.width
                height: 32
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Film Lab"
                    color: root.textPrimary
                    font.pixelSize: 20
                    font.weight: Font.Medium
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    GhostButton {
                        id: helpBtn
                        width: 28; height: 28
                        iconName: "keyboard"; iconSize: 18
                        onClicked: helpDialog.open()
                        HoverHandler { id: helpHover }
                        GraphiteTip {
                            parent: helpBtn
                            x: 0; y: helpBtn.height + 4
                            visible: helpHover.hovered
                            text: "Keyboard shortcuts (?)"
                        }
                    }

                    UtilityButton {
                        id: resetAllBtn
                        width: visible ? 68 : 0
                        // Reset is meaningless with no image; disable rather than hide it
                        // so its slot in the header never jumps around (Lightroom-style).
                        enabled: engine.hasImage
                        visible: true
                        text: "Reset"
                        onClicked: resetConfirm.open()
                        HoverHandler { id: resetAllHover }
                        GraphiteTip {
                            parent: resetAllBtn
                            x: 0
                            y: resetAllBtn.height + 4
                            visible: resetAllHover.hovered && resetAllBtn.enabled
                            text: "Reset every edit — film stock, exposure and all develop controls — back to how this image first opened."
                        }
                    }

                    UtilityButton {
                        id: openBtn
                        width: visible ? 68 : 0
                        visible: !engine.lightroomRoundTrip
                        text: "Open"
                        onClicked: openDialog.open()
                    }
                }
            }

            Histogram {}

            Text {
                width: parent.width
                text: engine.lightroomRoundTrip ? "Editing from Lightroom" : (engine.hasImage ? "Live preview" : "No image loaded")
                color: root.textMuted
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }

            // Save & Return — the plugin's primary action. Pinned in the fixed top area
            // (where the tab switcher sits in standalone) so it's always reachable, not
            // buried at the bottom of the develop scroll.
            PrimaryButton {
                width: parent.width
                visible: engine.lightroomRoundTrip
                text: engine.exporting ? "Saving…" : "Save & Return to Lightroom"
                enabled: engine.hasImage && !engine.exporting
                onClicked: engine.exportImage()
            }

            // Tab switcher — hidden in Lightroom edit-in mode (Develop only there).
            Rectangle {
                width: parent.width
                height: 34
                radius: 9
                color: root.inset
                border.width: 1
                border.color: root.hair
                visible: !engine.lightroomRoundTrip
                Row {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 4
                    Repeater {
                        model: ["Develop", "Geometry", "Export"]
                        delegate: Button {
                            id: tabBtn
                            width: (parent.width - 8) / 3
                            height: parent.height
                            text: modelData
                            readonly property bool selected: inspector.activeTab === index
                            onClicked: inspector.activeTab = index
                            contentItem: Text { text: tabBtn.text; color: tabBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12; font.weight: Font.Medium }
                            background: Rectangle {
                                radius: 7
                                color: tabBtn.selected ? root.ctrlActive : "transparent"
                                border.width: tabBtn.selected ? 1 : 0
                                border.color: root.hair
                            }
                        }
                    }
                }
            }
        }

        Flickable {
            anchors.top: inspectorTop.bottom
            anchors.topMargin: 14
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            clip: true
            contentWidth: width
            contentHeight: controls.implicitHeight + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 9
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: 3
                    color: "#5a5a60"
                    opacity: parent.pressed ? 0.9 : (parent.hovered ? 0.65 : 0.4)
                }
                background: Rectangle { color: "transparent" }
            }

            Column {
                id: controls
                x: 18
                y: 4
                width: parent.width - 36
                spacing: 14

                // ── Develop tab ────────────────────────────────────────
                Column {
                    id: developContent
                    width: parent.width
                    spacing: 14
                    visible: inspector.activeTab === 0 || engine.lightroomRoundTrip

                // ── Film recipe card ───────────────────────────────────
                Rectangle {
                    id: recipeCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: recipeCol.implicitHeight + 32
                    
                    Column {
                        id: recipeCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film Recipe"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: recipeCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: recipeCard.open = !recipeCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 8
                            visible: recipeCard.open

                            InspectorLabel { text: "Film stock" }
                            ComboBox {
                                id: stockBox
                                width: parent.width
                                height: 52
                                model: engine.stockModel
                                textRole: "name"
                                valueRole: "id"
                                currentIndex: stockBox.indexOfValue(engine.stock)
                                onActivated: engine.stock = stockBox.currentValue

                                // Searchable stock picker. Empty query shows the full sectioned
                                // list; typing runs a smart match (prefix/word/substring/fuzzy on
                                // both the display name and the stock id) ranked best-first.
                                property string query: ""
                                property var filteredStocks: query.trim().length === 0
                                    ? engine.stockModel : stockBox.rankStocks(query)
                                function pick(id) { engine.stock = id; stockBox.popup.close(); }
                                function chooseTop() {
                                    if (!filteredStocks || filteredStocks.length === 0) return;
                                    var i = Math.max(0, Math.min(stockList.currentIndex, filteredStocks.length - 1));
                                    pick(filteredStocks[i].id);
                                }
                                function rankStocks(qraw) {
                                    var q = qraw.toLowerCase().trim();
                                    var qn = q.replace(/[^a-z0-9]/g, "");
                                    var src = engine.stockModel;
                                    var scored = [];
                                    for (var k = 0; k < src.length; ++k) {
                                        var it = src[k];
                                        var s = stockBox.scoreStock((it.name || "").toLowerCase(),
                                                                    (it.id || "").toLowerCase(), q, qn);
                                        if (s >= 0) scored.push({ item: it, score: s, ord: k });
                                    }
                                    scored.sort(function(a, b) { return b.score !== a.score ? b.score - a.score : a.ord - b.ord; });
                                    var res = [];
                                    for (var j = 0; j < scored.length; ++j) res.push(scored[j].item);
                                    return res;
                                }
                                function scoreStock(n, idl, q, qn) {
                                    if (q.length === 0) return 1;
                                    if (n === q) return 1000;                              // exact
                                    if (n.indexOf(q) === 0) return 900;                    // name prefix
                                    var words = n.split(/[^a-z0-9]+/);
                                    for (var w = 0; w < words.length; ++w)
                                        if (words[w].length && words[w].indexOf(q) === 0) return 820 - w; // word prefix
                                    var pos = n.indexOf(q);
                                    if (pos >= 0) return 700 - pos;                        // substring
                                    var idflat = idl.replace(/[^a-z0-9]/g, "");
                                    if (qn.length && idflat.indexOf(qn) >= 0) return 550;  // id substring (e.g. 800t, tri x)
                                    var qi = 0;                                            // fuzzy subsequence on name
                                    for (var i = 0; i < n.length && qi < q.length; ++i) if (n[i] === q[qi]) qi++;
                                    if (qi === q.length) return 350;
                                    qi = 0;                                                // fuzzy subsequence on id (trix -> tri_x, p400 -> portra_400)
                                    for (var b = 0; b < idflat.length && qi < qn.length; ++b) if (idflat[b] === qn[qi]) qi++;
                                    if (qn.length && qi === qn.length) return 300;
                                    return -1;                                             // no match
                                }

                                contentItem: Item {
                                    Row {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.right: parent.right
                                        anchors.rightMargin: 32
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 12
                                        BoxartSwatch {
                                            anchors.verticalCenter: parent.verticalCenter
                                            cell: 34
                                            stockId: engine.stock
                                        }
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: parent.width - 46
                                            spacing: 2
                                            readonly property bool noneSelected: engine.stock === "none"
                                            Text {
                                                width: parent.width
                                                text: parent.noneSelected ? "Choose a film stock" : stockBox.displayText
                                                color: parent.noneSelected ? root.textSecondary : root.textPrimary
                                                elide: Text.ElideRight
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                            }
                                            Text {
                                                width: parent.width
                                                visible: !parent.noneSelected
                                                text: (stockBox.currentIndex >= 0 && engine.stockModel[stockBox.currentIndex]
                                                       ? engine.stockModel[stockBox.currentIndex].typeLabel : "")
                                                color: root.textMuted
                                                elide: Text.ElideRight
                                                font.pixelSize: 11
                                            }
                                        }
                                    }
                                }
                                // Flush at rest (defined by a hairline, not a grey block); fills
                                // subtly only on hover/open, so the card stays calm.
                                background: Rectangle {
                                    radius: 8
                                    color: (stockBox.hovered || stockBox.popup.visible) ? root.inset : "transparent"
                                    border.width: 1
                                    border.color: stockBox.popup.visible ? root.border : root.hair
                                    Behavior on color { ColorAnimation { duration: 90 } }
                                }
                                indicator: ChevronToggle {
                                    x: stockBox.width - 26
                                    y: (stockBox.height - 14) / 2
                                    open: stockBox.popup.visible
                                }
                                popup: Popup {
                                    id: stockPopup
                                    y: stockBox.height + 4
                                    width: stockBox.width
                                    implicitHeight: Math.min(searchCol.implicitHeight + 8, 400)
                                    padding: 4
                                    onOpened: { stockBox.query = ""; stockSearch.text = ""; stockSearch.forceActiveFocus(); stockList.currentIndex = 0; }
                                    contentItem: Column {
                                        id: searchCol
                                        spacing: 4
                                        // A clean search header — no grey box, just the glyph and a
                                        // hairline underline that brightens on focus.
                                        Rectangle {
                                            width: parent.width
                                            height: 38
                                            color: "transparent"
                                            Rectangle {
                                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                                anchors.leftMargin: 4; anchors.rightMargin: 4
                                                height: 1
                                                color: stockSearch.activeFocus ? root.border : root.hair
                                            }
                                            AppIcon {
                                                anchors.left: parent.left; anchors.leftMargin: 8
                                                anchors.verticalCenter: parent.verticalCenter
                                                name: "magnifying-glass"; size: 15
                                                color: stockSearch.activeFocus ? root.textSecondary : root.textMuted
                                            }
                                            TextField {
                                                id: stockSearch
                                                anchors.fill: parent
                                                leftPadding: 33; rightPadding: 10
                                                verticalAlignment: TextInput.AlignVCenter
                                                placeholderText: "Search film stocks…"
                                                color: root.textPrimary
                                                placeholderTextColor: root.textMuted
                                                font.pixelSize: 13
                                                selectByMouse: true
                                                background: null
                                                onTextChanged: { stockBox.query = text; stockList.currentIndex = 0; }
                                                Keys.onReturnPressed: stockBox.chooseTop()
                                                Keys.onEnterPressed: stockBox.chooseTop()
                                                Keys.onEscapePressed: stockPopup.close()
                                                Keys.onDownPressed: stockList.incrementCurrentIndex()
                                                Keys.onUpPressed: stockList.decrementCurrentIndex()
                                            }
                                        }
                                        ListView {
                                            id: stockList
                                            width: parent.width
                                            height: Math.min(contentHeight, 340)
                                            clip: true
                                            model: stockBox.filteredStocks
                                            currentIndex: 0
                                            ScrollIndicator.vertical: ScrollIndicator { }
                                            section.property: stockBox.query.trim().length ? "" : "typeLabel"
                                            section.criteria: ViewSection.FullString
                                            section.delegate: Item {
                                                width: ListView.view.width
                                                height: section === "" ? 0 : 24
                                                visible: section !== ""
                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.leftMargin: 10
                                                    anchors.bottom: parent.bottom
                                                    anchors.bottomMargin: 4
                                                    text: section
                                                    color: root.textMuted
                                                    font.pixelSize: 10
                                                    font.weight: Font.Medium
                                                }
                                            }
                                            delegate: ItemDelegate {
                                                id: stockRow
                                                width: stockList.width - 8
                                                height: 50
                                                hoverEnabled: true
                                                highlighted: stockList.currentIndex === index
                                                onHoveredChanged: if (hovered) stockList.currentIndex = index
                                                onClicked: stockBox.pick(modelData.id)
                                                contentItem: Row {
                                                    leftPadding: 6
                                                    spacing: 12
                                                    BoxartSwatch {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        cell: 32
                                                        stockId: modelData.id
                                                    }
                                                    Text {
                                                        anchors.verticalCenter: parent.verticalCenter
                                                        text: modelData.name
                                                        color: root.textPrimary
                                                        elide: Text.ElideRight
                                                        font.pixelSize: 13
                                                    }
                                                }
                                                background: Rectangle {
                                                    radius: 8
                                                    color: stockRow.highlighted ? "#16ffffff" : "transparent"
                                                }
                                            }
                                            Text {
                                                anchors.centerIn: parent
                                                visible: stockList.count === 0
                                                text: "No stocks match “" + stockBox.query + "”"
                                                color: root.textMuted
                                                font.pixelSize: 12
                                            }
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 10
                                        color: root.panelRaised
                                        border.width: 1
                                        border.color: root.border
                                    }
                                }
                                delegate: ItemDelegate {
                                    id: stockItem
                                    width: stockBox.width - 8
                                    height: 50
                                    highlighted: stockBox.highlightedIndex === index
                                    contentItem: Row {
                                        leftPadding: 6
                                        spacing: 12
                                        BoxartSwatch {
                                            anchors.verticalCenter: parent.verticalCenter
                                            cell: 32
                                            stockId: modelData.id
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.name
                                            color: root.textPrimary
                                            elide: Text.ElideRight
                                            font.pixelSize: 13
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 8
                                        color: stockItem.highlighted ? "#16ffffff" : "transparent"
                                    }
                                }
                            }
                        }
                    }
                }

                // ── Print finish card ──────────────────────────────────
                Rectangle {
                    id: printCard
                    property bool open: false
                    readonly property bool active: engine.filmControls.print_stock !== undefined && engine.filmControls.print_stock !== "none"
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: printCol.implicitHeight + 32
                    
                    Column {
                        id: printCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Print Finish"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            Text { anchors.right: chev1.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; visible: printCard.active && !printCard.open; text: "On"; color: root.textValue; font.pixelSize: 11 }
                            ChevronToggle { id: chev1; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: printCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: printCard.open = !printCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: printCard.open

                            InspectorLabel { text: "Print stock" }
                            ComboBox {
                                id: printBox
                                width: parent.width
                                height: 38
                                model: engine.printStockNames
                                currentIndex: {
                                    for (var i = 0; i < engine.printStockNames.length; ++i)
                                        if (engine.printStockIdAt(i) === engine.filmControls.print_stock) return i;
                                    return 0;
                                }
                                onActivated: engine.setFilmControl("print_stock", engine.printStockIdAt(currentIndex))
                                contentItem: Text {
                                    leftPadding: 12; rightPadding: 32
                                    text: printBox.displayText
                                    color: root.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: (printBox.hovered || printBox.popup.visible) ? root.inset : "transparent"
                                    border.width: 1
                                    border.color: printBox.popup.visible ? root.border : root.hair
                                    Behavior on color { ColorAnimation { duration: 90 } }
                                }
                                indicator: ChevronToggle { x: printBox.width - 26; y: (printBox.height - 14) / 2; open: printBox.popup.visible }
                                popup: Popup {
                                    y: printBox.height + 4
                                    width: printBox.width
                                    implicitHeight: Math.min(contentItem.implicitHeight + 8, 300)
                                    padding: 4
                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: printBox.popup.visible ? printBox.delegateModel : null
                                        currentIndex: printBox.highlightedIndex
                                        ScrollIndicator.vertical: ScrollIndicator { }
                                    }
                                    background: Rectangle { radius: 10; color: root.panelRaised; border.width: 1; border.color: root.border }
                                }
                                delegate: ItemDelegate {
                                    id: printItem
                                    width: printBox.width - 8
                                    height: 36
                                    highlighted: printBox.highlightedIndex === index
                                    contentItem: Text { leftPadding: 8; text: modelData; color: root.textPrimary; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; font.pixelSize: 13 }
                                    background: Rectangle { radius: 8; color: printItem.highlighted ? "#16ffffff" : "transparent" }
                                }
                            }

                            Column {
                                width: parent.width
                                spacing: 12
                                visible: printCard.active
                                opacity: printCard.active ? 1.0 : 0.4
                                FilmSlider { controlKey: "print_strength"; label: "Print strength"; minimum: 0; maximum: 2; increment: 0.05; decimals: true; neutral: 1.0; tooltip: "How strongly the print-stock emulation is applied over the negative." }
                                FilmSlider { controlKey: "print_c"; label: "Color head: cyan"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger color head — subtractive cyan filtration (removes red). Forward cools the print." }
                                FilmSlider { controlKey: "print_m"; label: "Color head: magenta"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger color head — subtractive magenta filtration (removes green)." }
                                FilmSlider { controlKey: "print_y"; label: "Color head: yellow"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Enlarger color head — subtractive yellow filtration (removes blue). Forward warms the print." }
                                FilmSlider { controlKey: "print_contrast"; label: "Print contrast"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Steepness of the print's tone curve — the paper grade." }
                                FilmSlider { controlKey: "print_black_point"; label: "Black point (lift)"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Print base density — lifts or deepens the darkest blacks of the print." }
                            }
                        }
                    }
                }

                // ── Film exposure card ─────────────────────────────────
                Rectangle {
                    id: exposureCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: exposureCol.implicitHeight + 32
                    
                    Column {
                        id: exposureCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film Exposure"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: exposureCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: exposureCard.open = !exposureCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: exposureCard.open

                            InspectorLabel { text: "Scene placement" }
                            Rectangle {                              // recessed segmented track
                                id: placementTrack
                                width: parent.width
                                height: 36
                                radius: 9
                                color: root.inset
                                border.width: 1
                                border.color: root.hair
                                HoverHandler { id: placementHover }
                                GraphiteTip {
                                    parent: placementTrack
                                    x: 0
                                    y: placementTrack.height + 4
                                    visible: placementHover.hovered
                                    text: "Auto balanced sets a stock-aware starting exposure for the scene. As shot preserves the RAW's own exposure placement before the film response."
                                }
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    spacing: 4
                                    Repeater {
                                        model: [{ label: "Auto balanced", value: "auto_balanced" }, { label: "As shot", value: "as_shot" }]
                                        delegate: Button {
                                            id: segBtn
                                            width: (parent.width - 4) / 2
                                            height: parent.height
                                            text: modelData.label
                                            readonly property bool selected: engine.filmControls.exposure_placement === modelData.value
                                            onClicked: engine.setFilmControl("exposure_placement", modelData.value)
                                            contentItem: Text { text: segBtn.text; color: segBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                                            background: Rectangle {
                                                radius: 7
                                                color: segBtn.selected ? root.ctrlActive : "transparent"
                                                border.width: segBtn.selected ? 1 : 0
                                                border.color: root.hair
                                            }
                                        }
                                    }
                                }
                            }
                            FilmSlider { controlKey: "film_exposure_ev"; label: "Film exposure"; minimum: -3; maximum: 3; increment: 0.05; decimals: true; bipolar: true; tooltip: "Virtual exposure (in stops) reaching the film before its tone and color response — like rating the stock faster or slower." }
                        }
                    }
                }

                // ── Film tone card ─────────────────────────────────────
                Rectangle {
                    id: toneCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: toneCol.implicitHeight + 32
                    
                    Column {
                        id: toneCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Film Tone"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: toneCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: toneCard.open = !toneCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: toneCard.open

                            // Applies before the film tone, and only to already-rendered
                            // (TIFF) inputs — disabled for RAW, Lightroom-style.
                            FilmSlider {
                                controlKey: "rendered_input"; label: "Preserve rendered tone"
                                minimum: 0; maximum: 100; neutral: 80
                                available: engine.renderedInput
                                tooltip: "For files that are already developed (TIFF/JPEG, e.g. sent from Lightroom): higher keeps the file's existing exposure and tone and applies the film look gently, protecting skies and bright highlights from being re-pushed. Lower treats it like a RAW and applies the full film tone. Has no effect on RAW files."
                            }
                            GraphiteCheck {
                                caption: "Adaptive scene tone"
                                checked: engine.filmControls.adaptive
                                onToggled: engine.setFilmControl("adaptive", checked)
                                tooltip: "Lets the film read the scene and auto-adjust its tone for flat, high-dynamic-range files. Turn off for a fixed, predictable response."
                            }
                            FilmSlider { controlKey: "profile_strength"; label: "Film profile strength"; minimum: 0; maximum: 200; neutral: 100; tooltip: "Master strength of this stock's authored tone curve. At 100, you get the stock's intended baseline; lower softens its toe, midtones, and shoulder together, while higher reinforces them within that stock's safe limits." }
                            FilmSlider { controlKey: "highlight_rolloff"; label: "Highlight rolloff"; minimum: 0; maximum: 200; neutral: 100; tooltip: "How gently the brightest tones roll off instead of clipping — higher for softer, glowier film highlights." }
                            FilmSlider { controlKey: "film_contrast"; label: "Film contrast"; minimum: 0; maximum: 200; neutral: 100; tooltip: "The punch of the film's tone curve — higher for a deeper, more contrasty look; lower for flatter." }
                            FilmSlider { controlKey: "shadow_lift"; label: "Shadow lift"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Base-fog fade in the deepest shadows, the way negative film never quite reaches pure black. Forward lifts shadows into a soft matte; back deepens them toward true black." }
                        }
                    }
                }

                // ── Color character card ───────────────────────────────
                Rectangle {
                    id: colorCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: colorCol.implicitHeight + 32
                    
                    Column {
                        id: colorCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color Character"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: colorCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: colorCard.open = !colorCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: colorCard.open
                            opacity: engine.currentStockMonochrome ? 0.45 : 1.0

                            Text { visible: engine.currentStockMonochrome; text: "Unavailable for monochrome stocks"; color: root.textMuted; font.pixelSize: 11 }
                            FilmSlider { controlKey: "film_color_density"; label: "Color density"; minimum: 0; maximum: 200; neutral: 100; available: !engine.currentStockMonochrome; tooltip: "How dense and cohesive the film's colors are — forward for richer, deeper, more film-like color; back for a thinner, more digital look." }
                            FilmSlider { controlKey: "emulsion_color_density"; label: "Color boost"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "Overall saturation of the stock's color dyes — forward for punchier color, back for a muted look." }
                            FilmSlider { controlKey: "highlight_color_hold"; label: "Highlight saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "How much color survives in the highlights — back bleaches bright areas toward clean white (rescues blown, over-warm highlights)." }
                            FilmSlider { controlKey: "shadow_color_retention"; label: "Shadow saturation"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "How much color survives in the shadows — forward keeps darks colorful, back mutes them toward neutral." }
                            FilmSlider { controlKey: "crossover"; label: "Crossover"; minimum: 0; maximum: 200; neutral: 100; available: !engine.currentStockMonochrome; tooltip: "Strength of the film's natural color crossover — the way its dye layers render cool shadows and warm highlights (and shift greens/blues). 100 is the stock's authentic amount; higher exaggerates it, 0 removes it." }
                            FilmSlider { controlKey: "cg_crossbalance"; label: "Split toning"; minimum: -100; maximum: 100; bipolar: true; available: !engine.currentStockMonochrome; tooltip: "Adds your own split-tone on top of the film — forward for teal shadows and warm highlights, back for the inverse." }
                        }
                    }
                }

                // ── Color balance card ────────────────────────────────
                Rectangle {
                    id: colorBalanceCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: colorBalanceCol.implicitHeight + 32
                    
                    Column {
                        id: colorBalanceCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color Balance"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: colorBalanceCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: colorBalanceCard.open = !colorBalanceCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: colorBalanceCard.open
                            FilmSlider { controlKey: "temp"; label: "Temperature"; minimum: -100; maximum: 100; bipolar: true; tooltip: "White balance warmth — forward warms (more amber), back cools (more blue)." }
                            FilmSlider { controlKey: "tint"; label: "Tint"; minimum: -100; maximum: 100; bipolar: true; tooltip: "White balance green/magenta — forward toward magenta, back toward green." }
                            FilmSlider { controlKey: "vibrance"; label: "Vibrance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Smart saturation that protects skin tones and already-saturated colors." }
                            FilmSlider { controlKey: "saturation"; label: "Saturation"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Overall color intensity, applied evenly to all hues." }
                        }
                    }
                }

                // ── Light card (basic tone) ────────────────────────────
                Rectangle {
                    id: lightCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: lightCol.implicitHeight + 32
                    
                    Column {
                        id: lightCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Light"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: lightCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: lightCard.open = !lightCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: lightCard.open
                            FilmSlider { controlKey: "exposure"; label: "Exposure"; minimum: -3; maximum: 3; increment: 0.05; decimals: true; bipolar: true; tooltip: "Overall brightness of the finished image, in stops — a grade applied after the film response. For the film's own exposure (which drives its tone and rolloff), use Film exposure in the Film Recipe." }
                            FilmSlider { controlKey: "contrast"; label: "Contrast"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Global contrast — spreads or compresses the tonal range around the midtones." }
                            FilmSlider { controlKey: "highlights"; label: "Highlights"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Recovers or brightens the brighter tones without moving whites." }
                            FilmSlider { controlKey: "shadows"; label: "Shadows"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Opens or deepens the darker tones without moving blacks." }
                            FilmSlider { controlKey: "whites"; label: "Whites"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Sets the white clipping point — how bright the brightest tones become." }
                            FilmSlider { controlKey: "blacks"; label: "Blacks"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Sets the black clipping point — how deep the darkest tones become." }
                            FilmSlider { controlKey: "midtones"; label: "Midtones"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the mid-tones, leaving the extremes anchored." }
                        }
                    }
                }

                // ── Detail card ────────────────────────────────────────
                Rectangle {
                    id: detailCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: detailCol.implicitHeight + 32
                    
                    Column {
                        id: detailCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Detail"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: detailCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: detailCard.open = !detailCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: detailCard.open
                            FilmSlider { controlKey: "texture"; label: "Texture"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Medium-scale detail like skin and foliage — forward enhances, back smooths." }
                            FilmSlider { controlKey: "clarity"; label: "Clarity"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Midtone local contrast — forward adds punch and presence, back softens." }
                            FilmSlider { controlKey: "dehaze"; label: "Dehaze"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Cuts or adds atmospheric haze and low-contrast veiling." }
                            FilmSlider { controlKey: "sharpness"; label: "Detail sharpness"; minimum: 0; maximum: 2; increment: 0.05; decimals: true; tooltip: "Edge sharpening amount." }
                            FilmSlider { controlKey: "sharpness_mask"; label: "Luminance mask"; minimum: 0; maximum: 1; increment: 0.05; decimals: true; neutral: 0.5; tooltip: "Limits sharpening to edges, protecting smooth areas (like skies) from being sharpened into noise." }
                        }
                    }
                }

                // ── HSL card ───────────────────────────────────────────
                Rectangle {
                    id: hslCard
                    property bool open: false
                    property string suffix: "h"
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: hslCol.implicitHeight + 32
                    
                    Column {
                        id: hslCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "HSL"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: hslCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: hslCard.open = !hslCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: hslCard.open

                            Rectangle {                          // H/S/L tab track
                                width: parent.width
                                height: 34
                                radius: 9
                                color: root.inset
                                border.width: 1
                                border.color: root.hair
                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 3
                                    spacing: 4
                                    Repeater {
                                        model: [{ label: "Hue", v: "h" }, { label: "Saturation", v: "s" }, { label: "Luminance", v: "l" }]
                                        delegate: Button {
                                            id: hslTabBtn
                                            width: (parent.width - 8) / 3
                                            height: parent.height
                                            text: modelData.label
                                            readonly property bool selected: hslCard.suffix === modelData.v
                                            onClicked: hslCard.suffix = modelData.v
                                            contentItem: Text { text: hslTabBtn.text; color: hslTabBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                                            background: Rectangle {
                                                radius: 7
                                                color: hslTabBtn.selected ? root.ctrlActive : "transparent"
                                                border.width: hslTabBtn.selected ? 1 : 0
                                                border.color: root.hair
                                            }
                                        }
                                    }
                                }
                            }

                            Repeater {
                                model: [
                                    { key: "red", label: "Red", dot: "#f25c5c" },
                                    { key: "orange", label: "Orange", dot: "#f2944a" },
                                    { key: "yellow", label: "Yellow", dot: "#d4c62a" },
                                    { key: "green", label: "Green", dot: "#4db858" },
                                    { key: "aqua", label: "Aqua", dot: "#38c0c0" },
                                    { key: "blue", label: "Blue", dot: "#4a85e8" },
                                    { key: "purple", label: "Purple", dot: "#9b5de5" },
                                    { key: "magenta", label: "Magenta", dot: "#d44fa8" }
                                ]
                                delegate: FilmSlider {
                                    controlKey: "hsl_" + modelData.key + "_" + hslCard.suffix
                                    label: modelData.label
                                    minimum: -100; maximum: 100; bipolar: true
                                }
                            }
                        }
                    }
                }

                // ── Color grading card ────────────────────────────────
                Rectangle {
                    id: gradeCard
                    property bool open: false
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: gradeCol.implicitHeight + 32
                    
                    Column {
                        id: gradeCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Color Grading"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: gradeCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: gradeCard.open = !gradeCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 14
                            visible: gradeCard.open

                            Grid {
                                anchors.horizontalCenter: parent.horizontalCenter
                                columns: 2
                                columnSpacing: 26
                                rowSpacing: 14
                                ColorWheel { zone: "shadow"; label: "Shadows" }
                                ColorWheel { zone: "midtone"; label: "Midtones" }
                                ColorWheel { zone: "highlight"; label: "Highlights" }
                                ColorWheel { zone: "global"; label: "Global" }
                            }

                            FilmSlider { controlKey: "cg_shadow_lum"; label: "Shadow luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the shadow zone only." }
                            FilmSlider { controlKey: "cg_midtone_lum"; label: "Midtone luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the midtone zone only." }
                            FilmSlider { controlKey: "cg_highlight_lum"; label: "Highlight luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Brightness of the highlight zone only." }
                            FilmSlider { controlKey: "cg_global_lum"; label: "Global luminance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Overall brightness applied by the grade." }

                            InspectorLabel { text: "Grade" }
                            FilmSlider { controlKey: "cg_balance"; label: "Balance"; minimum: -100; maximum: 100; bipolar: true; tooltip: "Shifts where shadows end and highlights begin, weighting the grade toward darks or lights." }
                            FilmSlider { controlKey: "cg_blending"; label: "Blending"; minimum: 0; maximum: 100; tooltip: "How softly the shadow, midtone and highlight zones overlap." }
                        }
                    }
                }

                // ── Material finish card ───────────────────────────────
                Rectangle {
                    id: materialCard
                    property bool open: true
                    width: parent.width
                    radius: 14
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "#1d1d20" }
                        GradientStop { position: 1.0; color: "#191a1c" }
                    }
                    border.width: 1
                    border.color: root.hair
                    implicitHeight: materialCol.implicitHeight + 32
                    
                    Column {
                        id: materialCol
                        x: 16; y: 16
                        width: parent.width - 32
                        spacing: 14

                        Item {
                            width: parent.width
                            height: 20
                            Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Material Finish"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                            ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: materialCard.open }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: materialCard.open = !materialCard.open }
                        }

                        Column {
                            width: parent.width
                            spacing: 12
                            visible: materialCard.open

                            InspectorLabel { text: "Grain" }
                            GraphiteCheck {
                                caption: engine.grainResolving ? "Resolving stock grain" : "Match grain to film speed"
                                checked: engine.filmControls.grain_auto
                                enabled: !engine.grainResolving
                                onToggled: engine.setAutoGrain(checked)
                                tooltip: "Automatically matches grain to the film speed (ISO) and stock. Turn off to seed and edit Strength, Size and Roughness manually."
                            }
                            FilmSlider { controlKey: "grain_strength"; label: "Strength"; minimum: 0; maximum: 2; increment: 0.05; decimals: true; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto; tooltip: "How visible the grain is — the apparent film speed." }
                            FilmSlider { controlKey: "grain_size"; label: "Size"; minimum: 0.1; maximum: 2; increment: 0.05; decimals: true; neutral: 0.6; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto; tooltip: "Particle size — larger reads as a coarser, higher-ISO stock." }
                            FilmSlider { controlKey: "grain_roughness"; label: "Roughness"; minimum: 0; maximum: 1; increment: 0.05; decimals: true; neutral: 0.5; available: !engine.filmControls.grain_auto; autoValue: engine.filmControls.grain_auto; tooltip: "Irregularity of the grain clumping — higher is grittier and more organic, lower is finer and more even." }
                            InspectorLabel { text: "Halation" }
                            FilmSlider { controlKey: "halation_strength"; label: "Strength"; minimum: 0; maximum: 200; neutral: 100; tooltip: "Strength of the warm red-orange glow that bleeds around bright edges against dark backgrounds." }
                            FilmSlider { controlKey: "halation_threshold"; label: "Threshold"; minimum: 0; maximum: 100; neutral: 50; tooltip: "How bright an area must be before it starts to halate — higher restricts the glow to the brightest highlights." }
                            InspectorLabel { text: "Bloom" }
                            FilmSlider { controlKey: "bloom"; label: "Amount"; minimum: 0; maximum: 100; tooltip: "Soft optical glow spreading from the highlights, like light diffusing in the lens." }
                        }
                    }
                }
                }

                // ── Geometry tab ───────────────────────────────────────
                Column {
                    id: geometryContent
                    width: parent.width
                    spacing: 14
                    visible: inspector.activeTab === 1 && !engine.lightroomRoundTrip

                    // ── Crop card ──────────────────────────────────────
                    Rectangle {
                        id: cropCard
                        property bool open: true
                        width: parent.width
                        radius: 14
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#1d1d20" }
                            GradientStop { position: 1.0; color: "#191a1c" }
                        }
                        border.width: 1
                        border.color: root.hair
                        implicitHeight: cropCol.implicitHeight + 32
                        
                        Column {
                            id: cropCol
                            x: 16; y: 16
                            width: parent.width - 32
                            spacing: 14

                            Item {
                                width: parent.width
                                height: 20
                                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Crop"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                                ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: cropCard.open }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: cropCard.open = !cropCard.open }
                            }

                            Column {
                                width: parent.width
                                spacing: 12
                                visible: cropCard.open

                                InspectorLabel { text: "Aspect ratio" }
                                ComboBox {
                                    id: aspectBox
                                    width: parent.width
                                    height: 34
                                    textRole: "label"
                                    model: [
                                        { label: "Original", r: -1 },
                                        { label: "Free", r: 0 },
                                        { label: "1:1", r: 1 },
                                        { label: "3:2", r: 1.5 },
                                        { label: "2:3", r: 0.6666667 },
                                        { label: "4:5", r: 0.8 },
                                        { label: "5:4", r: 1.25 },
                                        { label: "16:9", r: 1.7777778 },
                                        { label: "9:16", r: 0.5625 }
                                    ]
                                    onActivated: previewCanvas.selectAspect(model[currentIndex].r)
                                    contentItem: Text { leftPadding: 10; text: aspectBox.displayText; color: root.textPrimary; verticalAlignment: Text.AlignVCenter; font.pixelSize: 12 }
                                    background: Rectangle {
                                        radius: root.radiusControl
                                        color: (aspectBox.hovered || aspectBox.popup.visible) ? root.inset : "transparent"
                                        border.width: 1
                                        border.color: aspectBox.popup.visible ? root.border : root.hair
                                        Behavior on color { ColorAnimation { duration: 90 } }
                                    }
                                }

                                GeoButton {
                                    width: parent.width
                                    text: previewCanvas.cropMode ? "Apply crop" : "Crop image"
                                    active: previewCanvas.cropMode
                                    onClicked: previewCanvas.cropMode ? previewCanvas.applyCropMode() : previewCanvas.enterCropMode()
                                    tooltip: "Draw a crop on the image — drag the handles or inside to reposition. Pick an aspect ratio to lock the shape."
                                }
                                Text {
                                    width: parent.width
                                    visible: previewCanvas.cropMode
                                    text: "Drag the handles to crop; drag inside to move. Click Apply crop when done."
                                    color: root.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    // ── Rotate & Flip card ─────────────────────────────
                    Rectangle {
                        id: orientCard
                        property bool open: true
                        width: parent.width
                        radius: 14
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#1d1d20" }
                            GradientStop { position: 1.0; color: "#191a1c" }
                        }
                        border.width: 1
                        border.color: root.hair
                        implicitHeight: orientCol.implicitHeight + 32
                        
                        Column {
                            id: orientCol
                            x: 16; y: 16
                            width: parent.width - 32
                            spacing: 14

                            Item {
                                width: parent.width
                                height: 20
                                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; text: "Rotate & Flip"; color: root.textPrimary; font.pixelSize: 13; font.weight: Font.Medium }
                                ChevronToggle { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; open: orientCard.open }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: orientCard.open = !orientCard.open }
                            }

                            Column {
                                width: parent.width
                                spacing: 12
                                visible: orientCard.open

                                FilmSlider { controlKey: "straighten_deg"; label: "Straighten"; minimum: -45; maximum: 45; increment: 0.1; decimals: true; bipolar: true; tooltip: "Level a tilted horizon — rotates by a fine angle and trims the corners so there's no black edge." }

                                InspectorLabel { text: "Rotate" }
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    GeoButton { text: "⟲ 90°"; width: (parent.width - 8) / 2; onClicked: engine.rotateQuadrant(-1); tooltip: "Rotate 90° counter-clockwise." }
                                    GeoButton { text: "⟳ 90°"; width: (parent.width - 8) / 2; onClicked: engine.rotateQuadrant(1); tooltip: "Rotate 90° clockwise." }
                                }

                                InspectorLabel { text: "Flip" }
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    GeoButton { text: "⇄ Horizontal"; width: (parent.width - 8) / 2; active: engine.filmControls.flip_h; onClicked: engine.setFilmControl("flip_h", !engine.filmControls.flip_h); tooltip: "Mirror the image left to right." }
                                    GeoButton { text: "⇅ Vertical"; width: (parent.width - 8) / 2; active: engine.filmControls.flip_v; onClicked: engine.setFilmControl("flip_v", !engine.filmControls.flip_v); tooltip: "Mirror the image top to bottom." }
                                }

                                Item { width: parent.width; height: 2 }
                                GeoButton { width: parent.width; text: "Reset geometry"; onClicked: engine.resetGeometry(); tooltip: "Clear crop, straighten, rotation and flips." }
                            }
                        }
                    }
                }

                // ── Export tab ─────────────────────────────────────────
                Column {
                    id: exportContent
                    width: parent.width
                    spacing: 14
                    visible: inspector.activeTab === 2 && !engine.lightroomRoundTrip

                    Rectangle {
                        width: parent.width
                        radius: 14
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#1d1d20" }
                            GradientStop { position: 1.0; color: "#191a1c" }
                        }
                        border.width: 1
                        border.color: root.hair
                        implicitHeight: exportCol.implicitHeight + 32
                        
                        Column {
                            id: exportCol
                            x: 16; y: 16
                            width: parent.width - 32
                            spacing: 12

                            InspectorLabel { text: "Format" }
                            Grid {
                                width: parent.width
                                columns: 2
                                columnSpacing: 6
                                rowSpacing: 6
                                Repeater {
                                    model: [
                                        { id: "png8", label: "8-bit PNG" },
                                        { id: "png16", label: "16-bit PNG" },
                                        { id: "tiff", label: "16-bit TIFF" },
                                        { id: "jpeg", label: "JPEG" }
                                    ]
                                    delegate: Button {
                                        id: fmtBtn
                                        width: (parent.width - 6) / 2
                                        height: 32
                                        text: modelData.label
                                        readonly property bool selected: engine.exportFormat === modelData.id
                                        enabled: !engine.exporting
                                        onClicked: engine.exportFormat = modelData.id
                                        contentItem: Text { text: fmtBtn.text; color: fmtBtn.selected ? root.textPrimary : root.textSecondary; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11; font.weight: Font.Medium }
                                        background: Rectangle {
                                            radius: 6
                                            color: fmtBtn.selected ? root.ctrlActive : "transparent"
                                            border.width: 1
                                            border.color: fmtBtn.selected ? root.hair : root.border
                                        }
                                    }
                                }
                            }
                            Row {
                                width: parent.width
                                visible: engine.exportFormat === "jpeg"
                                spacing: 8
                                InspectorLabel { text: "JPEG quality"; width: parent.width - qualityBox.width - 8; anchors.verticalCenter: parent.verticalCenter }
                                GraphiteSpin {
                                    id: qualityBox
                                    from: 1; to: 100
                                    value: engine.jpegQuality
                                    editable: true
                                    enabled: !engine.exporting
                                    onValueModified: engine.jpegQuality = value
                                }
                            }
                            Row {
                                width: parent.width
                                visible: engine.exportFormat === "tiff"
                                spacing: 8
                                InspectorLabel { text: "TIFF DPI"; width: parent.width - dpiBox.width - 8; anchors.verticalCenter: parent.verticalCenter }
                                GraphiteSpin {
                                    id: dpiBox
                                    from: 72; to: 1200; stepSize: 1
                                    value: engine.exportDpi
                                    editable: true
                                    enabled: !engine.exporting
                                    onValueModified: engine.exportDpi = value
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: "Saved as a new file beside the original. 16-bit TIFF is uncompressed sRGB."
                        color: root.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    PrimaryButton {
                        text: engine.exporting ? "Exporting…" : ("Export " + root.exportFormatLabel(engine.exportFormat))
                        enabled: engine.hasImage && !engine.exporting
                        onClicked: engine.exportImage()
                    }
                }

                Text {
                    width: parent.width
                    topPadding: 4
                    text: "Film Lab"
                    color: root.textMuted
                    font.pixelSize: 11
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 10
        visible: engine.exporting
        color: "#d9101114"

        Column {
            anchors.centerIn: parent
            spacing: 12

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: engine.exporting
                width: 42
                height: 42
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Saving to Lightroom…" : "Exporting your image…"
                color: root.textPrimary
                font.pixelSize: 16
                font.weight: Font.Medium
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Applying your film recipe…" : "Rendering and saving " + root.exportFormatLabel(engine.exportFormat)
                color: root.textSecondary
                font.pixelSize: 12
            }
        }
    }
}
