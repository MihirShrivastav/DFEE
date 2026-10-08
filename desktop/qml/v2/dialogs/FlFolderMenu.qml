import QtQuick
import QtQuick.Controls
import DFEE

// Export folder menu: favourites (star) and recent folders (clock) first, then
// Film Lab Exports and Next to the original, then Choose folder…. One line per
// row: an icon for what the row is, the folder name, and its parent in quiet type
// (the full path in a tooltip); hairlines between the groups; a check on the
// current choice. Row objectNames are "exportFolder_<id>". Picking emits picked(id).
Popup {
    id: menu
    property var favorites: []
    property var recents: []          // already without favourites and the default folder
    property string defaultFolder: ""
    property string currentId: ""
    property Item focusReturn: null   // inside a sheet: focus goes back there on close
    signal picked(string id)
    padding: 6
    margins: 8
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    onClosed: Qt.callLater(function() { if (menu.focusReturn) menu.focusReturn.forceActiveFocus(); })

    function folderName(p) { const parts = p.split("/"); return parts[parts.length - 1] || p; }
    // The parent folder, shortened to its last two parts: "…/mihir/Pictures".
    function parentHint(p) {
        const parts = p.split("/");
        parts.pop();
        return parts.length > 2 ? "…/" + parts.slice(-2).join("/") : parts.join("/");
    }
    readonly property var rows: {
        const r = [];
        for (const p of favorites) r.push({ id: "path:" + p, icon: "star-fill", name: folderName(p), hint: parentHint(p), tip: p });
        for (const p of recents) r.push({ id: "path:" + p, icon: "clock-counter-clockwise", name: folderName(p), hint: parentHint(p), tip: p });
        if (r.length > 0) r.push({ id: "sep1" });
        r.push({ id: "default", icon: "folder-simple", name: "Film Lab Exports", hint: "", tip: defaultFolder });
        r.push({ id: "next", icon: "image-square", name: "Next to the original", hint: "", tip: "Each photo's own folder" });
        r.push({ id: "sep2" });
        r.push({ id: "choose", icon: "plus", name: "Choose folder…", hint: "", tip: "" });
        return r;
    }

    background: Rectangle {
        color: Theme.popover
        radius: Theme.radiusPopover
        border.width: 1
        border.color: Theme.hairline
    }
    contentItem: Column {
        spacing: 0
        Repeater {
            model: menu.rows
            delegate: Rectangle {
                id: r
                readonly property bool separator: modelData.id.startsWith("sep")
                readonly property bool current: modelData.id === menu.currentId
                objectName: separator ? "" : "exportFolder_" + modelData.id
                width: menu.availableWidth
                height: separator ? 9 : 30
                radius: Theme.radiusControl
                color: !separator && hover.hovered ? Theme.rowSelected : "transparent"
                HoverHandler { id: hover; enabled: !r.separator }
                FlHairline {
                    visible: r.separator
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                }
                FlIcon {
                    id: glyph
                    visible: !r.separator
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    name: modelData.icon || ""
                    size: 14
                    color: modelData.icon === "star-fill" ? Theme.accent : Theme.textSecondary
                }
                Text {
                    id: label
                    visible: !r.separator
                    anchors.left: glyph.right
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - 70)
                    text: modelData.name || ""
                    elide: Text.ElideRight
                    color: r.current ? Theme.text : Theme.textBody
                    font.pixelSize: Theme.fontBody
                    font.weight: r.current ? Font.Medium : Font.Normal
                }
                Text {
                    visible: !r.separator
                    anchors.left: label.right
                    anchors.leftMargin: 10
                    anchors.right: check.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    text: modelData.hint || ""
                    elide: Text.ElideLeft
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontCaption
                }
                FlIcon {
                    id: check
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    visible: r.current
                    name: "check"
                    size: 13
                    color: Theme.accent
                }
                FlTip { visible: hover.hovered && (modelData.tip || "").length > 0; text: modelData.tip || "" }
                MouseArea {
                    anchors.fill: parent
                    enabled: !r.separator
                    onClicked: { menu.picked(modelData.id); menu.close(); }
                }
            }
        }
    }
}
