import QtQuick
import DFEE

// Save the current look (film + every adjustment) or rename one. Group is optional:
// a look with a group is filed under it in the tray.
FlSheet {
    id: dlg
    objectName: "lookDialog"
    property string editId: ""
    title: editId.length > 0 ? "Rename look" : "Save look"
    readonly property string name: nameField.text.trim()
    readonly property string group: groupField.text.trim()
    readonly property bool replaces: editId.length === 0 && name.length > 0 && engine.presetExists(name, group)

    function openNew() {
        editId = "";
        nameField.text = "";
        groupField.text = "";
        open();
        nameField.forceActiveFocus();
    }
    function openRename(id, name, group) {
        editId = id;
        nameField.text = name;
        groupField.text = group;
        open();
        nameField.forceActiveFocus();
    }
    function commit() {
        if (name.length === 0) return;
        const ok = editId.length > 0 ? engine.editPreset(editId, name, group) : engine.savePreset(name, group);
        if (ok) close();
    }

    Text {
        width: parent.width
        visible: dlg.editId.length === 0
        text: "Saves the current film and every adjustment as a look you can apply to any photo."
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
    }
    FlTextField {
        id: nameField
        objectName: "lookNameField"
        width: parent.width
        placeholderText: "Look name"
        escReturnsFocus: false
        onAccepted: dlg.commit()
    }
    FlTextField {
        id: groupField
        objectName: "lookGroupField"
        width: parent.width
        placeholderText: "Group (optional)"
        escReturnsFocus: false
        onAccepted: dlg.commit()
    }
    Text {
        visible: dlg.replaces
        text: "Replaces the look with this name."
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Row {
        anchors.right: parent.right
        spacing: 8
        FlButton { kind: "quiet"; text: "Cancel"; onClicked: dlg.close() }
        FlButton {
            objectName: "lookSaveButton"
            kind: "accent"
            text: dlg.editId.length > 0 ? "Rename" : "Save"
            enabled: dlg.name.length > 0
            onClicked: dlg.commit()
        }
    }
}
