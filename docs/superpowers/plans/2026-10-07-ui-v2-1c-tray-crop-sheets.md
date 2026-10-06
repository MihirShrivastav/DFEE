# UI v2 — 1C Tray, Crop, Sheets and the Switch-over Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish Phase 1: the bottom tray (Roll | Films | Looks), crop as a canvas mode with a floating toolbar, the export / shortcuts / reset sheets, the Lightroom-mode rules — then make v2 the app and delete the v1 `Main.qml`.

**Architecture:** Each region is its own component under `desktop/qml/v2/` (`tray/`, `dialogs/`, `canvas/FlCropToolbar.qml`), talking to MainV2 through signals and to the engine through the `engine`, `library` and `editStore` context properties. Small C++ additions: `EditStore::isEdited(path)` for the Roll badge, a `stock` field and a test-overridable folder for presets. Text fields carry a marker (`flTextEntry`) so MainV2 switches bare-key shortcuts off while one has focus.

**Tech Stack:** Qt 6.8.3 Quick/Controls (Basic style), QtCore `Settings`, C++20, CMake, Inno Setup.

**Spec:** `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` (Layout: bottom tray, canvas crop mode, export, keyboard; Phasing step 1) and `desktop/DESIGN.md` v2; mockups https://claude.ai/artifact/595M73HfHvmwkhUFEiJQpB (Main, Crop). Builds on plans `2026-10-06-ui-v2-1a-shell.md` and `2026-10-06-ui-v2-1b-inspector.md`.

## Global Constraints

- Tokens, type, geometry per `desktop/DESIGN.md` v2 (`Theme.qml`): tray height 176 (header 44), tiles radius 6, selected tile = 2px gap + 2px accent ring, popovers radius 10, floating crop toolbar 44 tall radius 12 `rgba(36,36,38,.92)` with inner hairline; never below 11px; sentence case; US spelling.
- Every v2 QML file except `Theme.qml` has `import DFEE`; component files never reference MainV2 ids — they emit signals or use `engine` / `library` / `editStore` / `uiSettingsLocation`.
- Controlled components (owner sets state); no `on<Upper>` property names; never redeclare existing members (`Button.icon`, `Slider.moved`, `Popup.margins` is fine to *set*); clicks via `MouseArea`; popups and sheets hand focus back with `Window.window.returnFocus()`.
- Text fields are `FlTextField` (marker `flTextEntry: true`); while one has focus, bare-key shortcuts (letters, `[ ] \`, `?`, arrows) are off.
- Tests never touch the user's data: catalog (`DFEE_CATALOG_PATH`), UI settings (`DFEE_UI_SETTINGS`) and presets (`DFEE_PRESETS_DIR`, new) all go to the run's work folder. No script clicks Export's confirm button (exports are run only on request).
- Lightroom Edit-In: no library and no Roll tab; History opens from the toolbar; Ctrl+S and **Save & Return** export straight back; crop stays unavailable (as in 1A).
- Verify with `DFEE_UI_SCRIPT` via `desktop/tests/run_ui.ps1` (offscreen); never inject system-wide input. Rebuild Release after each task; commits carry no Co-Authored-By lines.

## Review Focus

1. **Typing in the tray search or a dialog field** must never fire bare-key shortcuts (`R`/`C` crop, `B` compare, `F`, `[ ]`, arrows). Task 1 script types `PORTRA` into the search (two `R`s) and checks crop mode stays off; Task 2 types `WARM` into the save dialog.
2. **Cancelling crop (Esc)** restores the photo's previous crop exactly; Done/Return keeps the new one. Task 3 script.
3. **Portrait photos and aspect presets**: a preset applies in the photo's orientation (3:2 on a portrait photo crops 2:3). Task 3 script.
4. **Lightroom Edit-In**: no library or Roll, History reachable from the toolbar, Ctrl+S exports straight back without a sheet. Task 5 script (all but the export itself).
5. **v2 as the default window**: every former v1 script (smoke, memory, info, Lightroom bypass) passes on the v2 window with no `-Ui`, and nothing loads the deleted `Main.qml`. Task 6 suite run.

---

### Task 1: Tray with Roll and Films

**Files:**
- Modify: `desktop/src/EditStore.h`, `desktop/src/EditStore.cpp`, `desktop/tests/edit_store_test.cpp`
- Create: `desktop/qml/v2/controls/FlTextField.qml`, `desktop/qml/v2/tray/FlTray.qml`, `desktop/qml/v2/tray/FlRollStrip.qml`, `desktop/qml/v2/tray/FlFilmsStrip.qml`
- Create: `desktop/tests/ui/v2_tray.script`
- Modify: `desktop/qml/v2/MainV2.qml`, `desktop/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `Q_INVOKABLE bool EditStore::isEdited(const QString& photoPath) const` — true when the catalog row exists with `edited = 1`.
  - `FlTextField { iconName: string; escReturnsFocus: bool (default true); readonly flTextEntry: true }`.
  - `FlTray` (objectName `tray`) `{ readonly tabNames; readonly activeTab: "roll"|"films"|"looks"; function showFilmsSearch(); signal openRequested(url fileUrl) }`; segmented `traySegmented`; search `traySearch`; the tab persists (Settings category `tray`, key `tab`, default `films`).
  - `FlRollStrip` (objectName `rollStrip`): tiles `rollTile_<file name, dots → underscores>` with `current`, `edited`; `summary` text.
  - `FlFilmsStrip` (objectName `filmsStrip`): `property string group` (current group label), `readonly groups`; tiles `filmTile_<stock id>` (first tile `filmTile_none`); group tabs `filmGroup_<index>` in the tray header.
  - MainV2: `textEntry` becomes `activeFocusItem.flTextEntry === true`; Shortcut `F` / `Ctrl+F` → `tray.showFilmsSearch()`.

- [ ] **Step 1: Failing unit test for `isEdited`**

Add to `desktop/tests/edit_store_test.cpp` before `};`:

```cpp
    void reportsEditedPhotos()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "portra_400"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/A.ARW", r, true));
        QVERIFY(store.isEdited("e:\\shoot\\a.arw"));
        QVERIFY(!store.isEdited("E:/shoot/B.ARW"));
        EditRecord clean; clean.stock = "none"; clean.history = {{"Import", "", "none", {}}}; clean.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/A.ARW", clean, false));   // edits undone: row stays, not edited
        QVERIFY(!store.isEdited("E:/shoot/A.ARW"));
    }
```

Run: `cmake --build desktop/out/build --config Release`
Expected: compile error — `isEdited` is not a member of `EditStore`.

- [ ] **Step 2: Implement `isEdited`**

`desktop/src/EditStore.h`, after `int recordCount() const;`:

```cpp
    // True when the photo has a catalog row marked edited (drives the Roll badge).
    Q_INVOKABLE bool isEdited(const QString& photoPath) const;
```

`desktop/src/EditStore.cpp`, after `recordCount()`:

```cpp
bool EditStore::isEdited(const QString& photoPath) const
{
    if (!open_) return false;
    QSqlQuery q(QSqlDatabase::database(connection_));
    q.prepare(QStringLiteral("SELECT edited FROM photos WHERE path_key = ?"));
    q.addBindValue(pathKey(photoPath));
    return q.exec() && q.next() && q.value(0).toInt() == 1;
}
```

Run: `cmake --build desktop/out/build --config Release` then
`powershell -ExecutionPolicy Bypass -Command "$env:PATH='C:\Qt\6.8.3\msvc2022_64\bin;'+$env:PATH; $o=[IO.Path]::GetTempFileName(); & desktop\out\build\Release\desktop_tests.exe -o \"$o,txt\"; Get-Content $o | Select-String 'Totals|FAIL'"`
Expected: `Totals: 11 passed, 0 failed`.

- [ ] **Step 3: Write the failing UI script `desktop/tests/ui/v2_tray.script`**

```
# Films: F focuses the tray search; typing never fires shortcuts (two R's here);
# a film tile applies the stock. Roll: tiles open photos and show an edited badge.
# Roll navigation needs the library's current folder to hold SAMPLE_A (E:/new_raws).
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:tray.visible=true
expect:tray.activeTab=films
expect:filmTile_none.current=true
key:70
wait:200
expect:v2Root.textEntry=true
key:80
key:79
key:82
key:84
key:82
key:65
wait:300
expect:traySearch.text=PORTRA
expect:photoCanvas.cropMode=false
click:filmTile_portra_400@0.5,0.3
wait:300
expect:engine.stock=portra_400
expect:filmTile_portra_400.current=true
key:16777216
wait:150
expect:v2Root.textEntry=false
expect:traySearch.text=PORTRA
set:traySearch.text=
click:traySegmented@0.1,0.5
wait:300
expect:tray.activeTab=roll
expect:rollStrip.visible=true
click:rollTile_7033866904_arw@0.5,0.5
waitfor:engine.currentFile=${SAMPLE_B},15000
expect:rollTile_7033866904_arw.current=true
waitfor:rollTile_3071874357_arw.edited=true,5000
expect:rollTile_7033866904_arw.edited=false
shot:${TEMP}/v2_tray.png
quit
```

(Esc only gives focus back, so the search still reads `PORTRA`; the Roll filters by the same text, so the script clears it with `set:` before switching tabs. Tile names replace the file name's dots with underscores — the harness reads `object.property` paths.)

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_tray.script -Ui v2`
Expected: FAIL from `expect:tray.visible=true` (`<missing>`).

- [ ] **Step 4: Create `desktop/qml/v2/controls/FlTextField.qml`**

```qml
import QtQuick
import QtQuick.Controls
import DFEE

// Text field per DESIGN.md: inset well, 26 tall, radius 6, accent focus ring. While
// it has focus MainV2 turns bare-key shortcuts off (flTextEntry). Esc hands focus
// back to the window unless a sheet owns Esc (escReturnsFocus: false).
TextField {
    id: f
    readonly property bool flTextEntry: true
    property string iconName: ""
    property bool escReturnsFocus: true
    implicitHeight: Theme.controlHeight
    leftPadding: iconName.length > 0 ? 26 : 8
    rightPadding: 8
    color: Theme.text
    placeholderTextColor: Theme.textTertiary
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    font.pixelSize: Theme.fontLabel
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radiusControl
        color: Theme.inset
        border.width: 1
        border.color: f.activeFocus ? "#730a84ff" : Theme.hairline
    }
    FlIcon {
        visible: f.iconName.length > 0
        name: f.iconName
        size: 13
        color: Theme.textTertiary
        x: 8
        anchors.verticalCenter: parent.verticalCenter
    }
    Keys.onEscapePressed: (e) => {
        if (!f.escReturnsFocus) { e.accepted = false; return; }
        const w = f.Window.window;
        if (w && w.returnFocus) w.returnFocus();
        e.accepted = true;
    }
}
```

- [ ] **Step 5: Create the strips and the tray**

`desktop/qml/v2/tray/FlFilmsStrip.qml`:

```qml
import QtQuick
import QtQuick.Controls
import DFEE

// Films: one tile per stock (box art, name, ISO) in the chosen group, or every
// stock matching the search. A click applies the stock. Box-art tiles until Phase 2
// renders the photo through each stock.
ListView {
    id: strip
    property string query: ""
    property string group: ""
    readonly property var groups: {
        const g = [];
        const m = engine.stockModel;
        for (let i = 0; i < m.length; ++i)
            if (m[i].id !== "none" && g.indexOf(m[i].groupLabel) < 0) g.push(m[i].groupLabel);
        return g;
    }
    readonly property string activeGroup: group.length > 0 ? group : (groups.length > 0 ? groups[0] : "")
    orientation: ListView.Horizontal
    spacing: 14
    leftMargin: 16
    rightMargin: 16
    topMargin: 8
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
    model: {
        const q = query.trim().toLowerCase();
        const rows = [{ id: "none", name: "No film", iso: 0 }];
        const m = engine.stockModel;
        for (let i = 0; i < m.length; ++i) {
            const s = m[i];
            if (s.id === "none") continue;
            if (q.length > 0 ? s.name.toLowerCase().indexOf(q) < 0 : s.groupLabel !== strip.activeGroup) continue;
            rows.push(s);
        }
        return rows;
    }
    delegate: Item {
        id: tile
        objectName: "filmTile_" + modelData.id
        readonly property bool current: engine.stock === modelData.id
        width: 124
        height: 116
        Rectangle {                                   // selection ring: 2px gap + 2px accent
            x: -4; y: -4
            width: 132; height: 90
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: tile.current
        }
        Rectangle {
            id: art
            width: 124
            height: 82
            radius: Theme.radiusTile
            color: Theme.inset
            clip: true
            border.width: tileHover.hovered && !tile.current ? 1 : 0
            border.color: "#40ffffff"
            Image {
                anchors.fill: parent
                visible: modelData.id !== "none"
                source: modelData.id !== "none" ? "qrc:/boxart/" + modelData.id + ".svg" : ""
                sourceSize: Qt.size(248, 164)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Text {
                anchors.centerIn: parent
                visible: modelData.id === "none"
                text: "No film"
                color: Theme.textCaption
                font.pixelSize: Theme.fontLabel
            }
        }
        Column {
            anchors.top: art.bottom
            anchors.topMargin: 7
            width: parent.width
            spacing: 1
            Text {
                width: parent.width
                text: modelData.name
                elide: Text.ElideRight
                color: tile.current ? Theme.text : Theme.textBody
                font.pixelSize: Theme.fontLabel
                font.weight: Font.Medium
            }
            Text {
                visible: modelData.iso > 0
                text: "ISO " + modelData.iso
                color: Theme.textTertiary
                font.pixelSize: Theme.fontCaption
                font.features: { "tnum": 1 }
            }
        }
        HoverHandler { id: tileHover }
        MouseArea { anchors.fill: parent; onClicked: engine.stock = modelData.id }
    }
}
```

`desktop/qml/v2/tray/FlRollStrip.qml`:

```qml
import QtQuick
import QtQuick.Controls
import DFEE

// Roll: the current library folder as thumbnails; the open photo is ringed and
// edited photos carry a badge. A click asks the window to open the photo.
ListView {
    id: strip
    property string query: ""
    signal openRequested(url fileUrl)
    readonly property string summary: {
        const folder = library.currentFolder;
        if (!folder) return "";
        const name = folder.split("/").pop();
        const n = library.files.length;
        return name + " · " + n + (n === 1 ? " photo" : " photos");
    }
    orientation: ListView.Horizontal
    spacing: 8
    leftMargin: 16
    rightMargin: 16
    topMargin: 8
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
    model: {
        const q = query.trim().toLowerCase();
        return library.files.filter(f => q.length === 0 || f.name.toLowerCase().indexOf(q) >= 0);
    }
    delegate: Item {
        id: tile
        objectName: "rollTile_" + modelData.name.replace(/\./g, "_")
        readonly property bool current: modelData.path.toLowerCase() === engine.currentFile.toLowerCase()
        // recordCount's NOTIFY (EditStore::changed) fires on every save, so this re-reads.
        readonly property bool edited: { editStore.recordCount; return editStore.isEdited(modelData.path); }
        width: 128
        height: 96
        Rectangle {
            x: -4; y: -4
            width: 136; height: 104
            radius: 9
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: tile.current
        }
        Rectangle {
            anchors.fill: parent
            radius: 5
            color: Theme.inset
            clip: true
            Image {
                anchors.fill: parent
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(256, 192)
                source: "image://thumb/" + encodeURIComponent(modelData.path)
            }
        }
        Rectangle {
            visible: tile.edited
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 5
            width: 16; height: 16; radius: 8
            color: "#99000000"
            FlIcon { anchors.centerIn: parent; name: "pencil-simple"; size: 10; color: Theme.text }
        }
        HoverHandler { id: tileHover }
        FlTip { visible: tileHover.hovered; text: modelData.name }
        MouseArea {
            anchors.fill: parent
            onClicked: strip.openRequested(Qt.resolvedUrl("file:///" + modelData.path.replace(/\\/g, "/")))
        }
    }
    Text {
        anchors.centerIn: parent
        visible: library.files.length === 0
        text: "Add a folder to the library to see its photos here."
        color: Theme.textTertiary
        font.pixelSize: Theme.fontLabel
    }
}
```

`desktop/qml/v2/tray/FlTray.qml`:

```qml
import QtQuick
import QtQuick.Controls
import QtCore
import DFEE

// Bottom tray (176): Roll | Films | Looks with a filter field. Lightroom Edit-In
// shows Films | Looks only. The tab persists (QtCore Settings, category "tray").
Rectangle {
    id: tray
    objectName: "tray"
    color: Theme.window
    implicitHeight: Theme.trayHeight
    signal openRequested(url fileUrl)
    readonly property var tabNames: engine.lightroomRoundTrip ? ["films", "looks"] : ["roll", "films", "looks"]
    readonly property var tabLabels: engine.lightroomRoundTrip ? ["Films", "Looks"] : ["Roll", "Films", "Looks"]
    readonly property string activeTab: tabNames.indexOf(prefs.tab) >= 0 ? prefs.tab : "films"
    function showFilmsSearch() {
        prefs.tab = "films";
        search.forceActiveFocus();
        search.selectAll();
    }

    Settings {
        id: prefs
        category: "tray"
        location: uiSettingsLocation
        property string tab: "films"
    }
    FlHairline { anchors.top: parent.top }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
        FlSegmented {
            id: tabs
            objectName: "traySegmented"
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            model: tray.tabLabels
            currentIndex: tray.tabNames.indexOf(tray.activeTab)
            onActivated: (i) => prefs.tab = tray.tabNames[i]
        }
        Row {
            anchors.left: tabs.right
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16
            visible: tray.activeTab === "films" && search.text.trim().length === 0
            Repeater {
                model: films.groups
                delegate: Text {
                    objectName: "filmGroup_" + index
                    text: modelData
                    color: modelData === films.activeGroup ? Theme.text : Theme.textCaption
                    font.pixelSize: Theme.fontLabel
                    font.weight: modelData === films.activeGroup ? Font.Medium : Font.Normal
                    MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: films.group = modelData }
                }
            }
        }
        Text {
            anchors.left: tabs.right
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            visible: tray.activeTab === "roll"
            text: roll.summary
            color: Theme.textCaption
            font.pixelSize: Theme.fontLabel
            font.features: { "tnum": 1 }
        }
        Text {
            anchors.left: tabs.right
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            visible: tray.activeTab === "looks"
            text: "Your saved looks"
            color: Theme.textCaption
            font.pixelSize: Theme.fontLabel
        }
        FlTextField {
            id: search
            objectName: "traySearch"
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            width: 190
            placeholderText: "Search"
            iconName: "magnifying-glass"
        }
    }

    FlRollStrip {
        id: roll
        objectName: "rollStrip"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: tray.activeTab === "roll"
        query: search.text
        onOpenRequested: (u) => tray.openRequested(u)
    }
    FlFilmsStrip {
        id: films
        objectName: "filmsStrip"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: tray.activeTab === "films"
        query: search.text
    }
}
```

- [ ] **Step 6: Wire the tray into MainV2**

In `desktop/qml/v2/MainV2.qml`:
- Replace `property bool textEntry: false` (and its comment line) with:

```qml
    // True while a text field (FlTextField) has focus: bare-key shortcuts stay off.
    readonly property bool textEntry: activeFocusItem !== null && activeFocusItem.flTextEntry === true
```

- In the `FlCanvas` block change `anchors.bottom: parent.bottom` to `anchors.bottom: tray.top`.
- Add after the `FlCanvas` block:

```qml
    FlTray {
        id: tray
        anchors.left: sidebar.right
        anchors.right: inspector.left
        anchors.bottom: parent.bottom
        height: Theme.trayHeight
        onOpenRequested: (u) => root.openPhoto(u)
    }
```

- Add with the other Shortcuts:

```qml
    Shortcut { sequences: ["F", "Ctrl+F"]; enabled: !root.textEntry; onActivated: tray.showFilmsSearch() }
```

Register in `desktop/CMakeLists.txt` `QML_FILES` (after `qml/v2/inspector/FlFineTuneSection.qml`):

```cmake
        qml/v2/controls/FlTextField.qml
        qml/v2/tray/FlTray.qml
        qml/v2/tray/FlRollStrip.qml
        qml/v2/tray/FlFilmsStrip.qml
```

- [ ] **Step 7: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 3 command, `v2_inspector.script`, `v2_keys.script` and `v2_crop.script` with `-Ui v2`, and the unit command from Step 2.
Expected: all `failures=0`; unit `11 passed`. Check `v2_tray.png`: Roll with two tiles, the second ringed, the first with an edited badge.

- [ ] **Step 8: Commit**

```bash
git add desktop/src/EditStore.h desktop/src/EditStore.cpp desktop/tests/edit_store_test.cpp desktop/qml/v2 desktop/CMakeLists.txt desktop/tests/ui/v2_tray.script
git commit -m "feat(desktop): v2 tray with roll and films; text fields switch shortcuts off"
```

---

### Task 2: Looks in the tray

**Files:**
- Modify: `desktop/src/EngineController.cpp` (`presetsDir`, preset rows), `desktop/tests/run_ui.ps1`
- Modify: `desktop/qml/v2/controls/FlMenu.qml` (item objectNames)
- Create: `desktop/qml/v2/dialogs/FlSheet.qml`, `desktop/qml/v2/dialogs/FlLookDialog.qml`, `desktop/qml/v2/tray/FlLooksStrip.qml`
- Create: `desktop/tests/ui/v2_looks.script`
- Add icon: `desktop/resources/icons/dots-three.svg`
- Modify: `desktop/qml/v2/tray/FlTray.qml`, `desktop/qml/v2/MainV2.qml`, `desktop/CMakeLists.txt`

**Interfaces:**
- Consumes: `engine.presets` rows, `engine.savePreset(name, group)`, `engine.editPreset(id, name, group)`, `engine.deletePreset(id)`, `engine.applyPreset(id)`, `engine.presetExists(name, group)`; `FlTextField`, `FlTray` (Task 1).
- Produces:
  - Preset rows gain `stock` (the look's film id); `presetsDir()` honours env `DFEE_PRESETS_DIR`; `run_ui.ps1` sets it to `<work>/presets`.
  - `FlMenu` items take `objectName = <action objectName> + "Item"` when the Action has one.
  - `FlSheet { title; default content }` — modal, dimmed, centred, Esc / outside click closes, focus returns.
  - `FlLookDialog` (objectName `lookDialog`) `{ function openNew(); function openRename(id, name, group) }` with `lookNameField`, `lookGroupField`, `lookSaveButton`.
  - `FlLooksStrip` (objectName `looksStrip`) `{ query; signal saveRequested(); signal renameRequested(string id, string name, string group) }`; tiles `lookSave`, `lookTile_<id>`, menu buttons `lookMore_<id>`, menu items `lookRenameItem`, `lookDeleteItem`.
  - `FlTray` gains `signal saveLookRequested()` and `signal renameLookRequested(string id, string name, string group)`.

- [ ] **Step 1: Isolate presets in tests and write the failing script**

`desktop/tests/run_ui.ps1`, after the `DFEE_UI_SETTINGS` line:

```powershell
$env:DFEE_PRESETS_DIR = Join-Path $work "presets"
```

`desktop/tests/ui/v2_looks.script`:

```
# Save the current look from the tray (typing never fires shortcuts), apply it,
# rename and delete it from its menu. Presets live in the run's work folder.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:engine.presets.length=0
stock:portra_400
wait:200
click:traySegmented@0.85,0.5
wait:300
expect:tray.activeTab=looks
click:lookSave@0.5,0.4
wait:300
expect:lookDialog.visible=true
expect:v2Root.textEntry=true
key:87
key:65
key:82
key:77
wait:200
expect:lookNameField.text=WARM
expect:photoCanvas.cropMode=false
key:16777220
wait:400
expect:lookDialog.visible=false
expect:engine.presets.length=1
expect:engine.presets.0.name=WARM
expect:engine.presets.0.stock=portra_400
stock:none
wait:200
click:lookTile_WARM@0.5,0.3
wait:400
expect:engine.stock=portra_400
click:lookMore_WARM@0.5,0.5
wait:300
click:lookRenameItem@0.5,0.5
wait:300
expect:lookDialog.visible=true
expect:lookNameField.text=WARM
key:69
wait:150
click:lookSaveButton@0.5,0.5
wait:400
expect:engine.presets.0.name=WARME
click:lookMore_WARME@0.5,0.5
wait:300
click:lookDeleteItem@0.5,0.5
wait:300
expect:engine.presets.length=0
shot:${TEMP}/v2_looks.png
quit
```

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_looks.script -Ui v2`
Expected: FAIL at `expect:tray.activeTab=looks` (got `films`: there is no Looks tab yet) and every Looks step after it. (`presets.length=0` already holds: the run's presets folder is empty.)

- [ ] **Step 2: Engine — test folder override and `stock` on preset rows**

In `desktop/src/EngineController.cpp`, replace the body of `presetsDir()`:

```cpp
QString EngineController::presetsDir() const
{
    // DFEE_PRESETS_DIR keeps UI tests out of the user's Documents.
    const QString override = qEnvironmentVariable("DFEE_PRESETS_DIR");
    const QString dir = !override.isEmpty()
        ? override
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
              + QStringLiteral("/Film Lab/Presets");
    QDir().mkpath(dir);
    return dir;
}
```

and in `refreshPresets()`'s `readDir` lambda, after `m["group"] = group;`:

```cpp
            m["stock"] = obj.value("stock").toString();
```

- [ ] **Step 3: Fetch the menu icon; FlMenu item names**

```bash
cd desktop/resources/icons
curl -fsSL -o dots-three.svg https://raw.githubusercontent.com/phosphor-icons/core/main/assets/regular/dots-three.svg
sed -i 's/fill="currentColor"/fill="#ffffff"/' dots-three.svg
head -c 80 dots-three.svg
```
Expected: `<svg … fill="#ffffff">`.

In `desktop/qml/v2/controls/FlMenu.qml`, inside `delegate: MenuItem {`, after `id: item`:

```qml
        objectName: item.action && item.action.objectName.length > 0 ? item.action.objectName + "Item" : ""
```

- [ ] **Step 4: Create the sheet, the dialog and the strip**

`desktop/qml/v2/dialogs/FlSheet.qml`:

```qml
import QtQuick
import QtQuick.Controls
import DFEE

// Modal sheet: popover surface, radius 10, dimmed backdrop, 13/600 title. Esc and a
// click outside close it; closing hands focus back to the window.
Popup {
    id: sheet
    property string title: ""
    default property alias content: body.data
    modal: true
    dim: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 420
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: Qt.callLater(function() {
        const w = sheet.parent ? sheet.parent.Window.window : null;
        if (w && w.returnFocus) w.returnFocus();
    })
    Overlay.modal: Rectangle { color: "#80000000" }
    background: Rectangle {
        color: Theme.popover
        radius: Theme.radiusPopover
        border.width: 1
        border.color: Theme.hairline
    }
    contentItem: Column {
        spacing: 16
        Text {
            width: parent.width
            text: sheet.title
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }
        Column {
            id: body
            width: parent.width
            spacing: 14
        }
    }
}
```

`desktop/qml/v2/dialogs/FlLookDialog.qml`:

```qml
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
```

`desktop/qml/v2/tray/FlLooksStrip.qml`:

```qml
import QtQuick
import QtQuick.Controls
import DFEE

// Looks: "Save current look" first, then each saved look (its film's box art, name,
// group). A click applies the look; the "…" menu renames or deletes it.
ListView {
    id: strip
    property string query: ""
    signal saveRequested()
    signal renameRequested(string id, string name, string group)
    orientation: ListView.Horizontal
    spacing: 14
    leftMargin: 16
    rightMargin: 16
    topMargin: 8
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
    model: {
        const q = query.trim().toLowerCase();
        const rows = [{ kind: "save" }];
        const p = engine.presets;
        for (let i = 0; i < p.length; ++i)
            if (q.length === 0 || p[i].name.toLowerCase().indexOf(q) >= 0 || (p[i].group || "").toLowerCase().indexOf(q) >= 0)
                rows.push(p[i]);
        return rows;
    }
    delegate: Item {
        id: tile
        readonly property bool isSave: modelData.kind === "save"
        objectName: isSave ? "lookSave" : "lookTile_" + modelData.id
        width: 124
        height: 116
        Rectangle {
            id: art
            width: 124
            height: 82
            radius: Theme.radiusTile
            color: tile.isSave ? "transparent" : Theme.inset
            border.width: 1
            border.color: tile.isSave ? "#2effffff" : (tileHover.hovered ? "#40ffffff" : "transparent")
            clip: true
            opacity: tile.isSave && !engine.hasImage ? 0.4 : 1.0
            Image {
                anchors.fill: parent
                visible: !tile.isSave && (modelData.stock || "none") !== "none"
                source: visible ? "qrc:/boxart/" + modelData.stock + ".svg" : ""
                sourceSize: Qt.size(248, 164)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Column {
                anchors.centerIn: parent
                spacing: 6
                visible: tile.isSave || (modelData.stock || "none") === "none"
                FlIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: tile.isSave ? "plus" : "image-square"
                    size: 16
                    color: Theme.textSecondary
                }
                Text {
                    visible: tile.isSave
                    text: "Save current look"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontLabel
                }
            }
        }
        Column {
            visible: !tile.isSave
            anchors.top: art.bottom
            anchors.topMargin: 7
            width: parent.width
            spacing: 1
            Text {
                width: parent.width
                text: modelData.name || ""
                elide: Text.ElideRight
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                font.weight: Font.Medium
            }
            Text {
                width: parent.width
                text: modelData.group || ""
                elide: Text.ElideRight
                color: Theme.textTertiary
                font.pixelSize: Theme.fontCaption
            }
        }
        HoverHandler { id: tileHover }
        MouseArea {
            anchors.fill: art
            enabled: !tile.isSave || engine.hasImage
            onClicked: tile.isSave ? strip.saveRequested() : engine.applyPreset(modelData.id)
        }
        FlIconButton {
            id: moreButton
            objectName: tile.isSave ? "" : "lookMore_" + modelData.id
            visible: !tile.isSave
            opacity: tileHover.hovered || menu.visible ? 1.0 : 0.0
            x: art.width - width - 4
            y: 4
            width: 24
            height: 22
            iconName: "dots-three"
            tip: "More"
            onClicked: menu.popup(moreButton, 0, moreButton.height + 4)
        }
        FlMenu {
            id: menu
            Action {
                objectName: "lookRename"
                text: "Rename…"
                onTriggered: strip.renameRequested(modelData.id, modelData.name, modelData.group || "")
            }
            Action {
                objectName: "lookDelete"
                text: "Delete"
                onTriggered: engine.deletePreset(modelData.id)
            }
        }
    }
}
```

- [ ] **Step 5: Add Looks to the tray, the dialog to MainV2**

In `desktop/qml/v2/tray/FlTray.qml` add the signals after `signal openRequested(url fileUrl)`:

```qml
    signal saveLookRequested()
    signal renameLookRequested(string id, string name, string group)
```

and after the `FlFilmsStrip` block:

```qml
    FlLooksStrip {
        id: looks
        objectName: "looksStrip"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: tray.activeTab === "looks"
        query: search.text
        onSaveRequested: tray.saveLookRequested()
        onRenameRequested: (id, name, group) => tray.renameLookRequested(id, name, group)
    }
```

In `desktop/qml/v2/MainV2.qml` add to the `FlTray` block:

```qml
        onSaveLookRequested: lookDialog.openNew()
        onRenameLookRequested: (id, name, group) => lookDialog.openRename(id, name, group)
```

and after the `FolderDialog`:

```qml
    FlLookDialog { id: lookDialog }
```

Register in `desktop/CMakeLists.txt` `QML_FILES`:

```cmake
        qml/v2/dialogs/FlSheet.qml
        qml/v2/dialogs/FlLookDialog.qml
        qml/v2/tray/FlLooksStrip.qml
```

- [ ] **Step 6: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 1 command, and `v2_tray.script`, `v2_inspector.script` with `-Ui v2`.
Expected: all `failures=0`. Check `v2_looks.png` (Looks tab, only the Save tile left).

- [ ] **Step 7: Commit**

```bash
git add desktop/src/EngineController.cpp desktop/tests/run_ui.ps1 desktop/qml/v2 desktop/CMakeLists.txt desktop/resources/icons/dots-three.svg desktop/tests/ui/v2_looks.script
git commit -m "feat(desktop): v2 looks in the tray (save, apply, rename, delete)"
```

---

### Task 3: Crop mode with the floating toolbar

**Files:**
- Create: `desktop/qml/v2/canvas/FlCropToolbar.qml`
- Create: `desktop/tests/ui/v2_crop_mode.script`
- Modify: `desktop/qml/v2/canvas/FlCanvas.qml`, `desktop/qml/v2/MainV2.qml`, `desktop/CMakeLists.txt`

**Interfaces:**
- Consumes: `FlCanvas` crop API (1A: `cropMode`, `cropAspect`, `cropX/Y/W/H`, `enterCropMode()`, `applyCropMode()`, `selectAspect(r)`); `FlListPopup` (1B); `engine.rotateQuadrant(int)`, `engine.resetGeometry()`, `engine.setFilmControl("straighten_deg"|"flip_h", v)`.
- Produces:
  - `FlCanvas.cancelCropMode()` — leaves crop mode and restores the crop the photo had on entry; `FlCanvas.resetCropMode()` — clears geometry, full frame; `selectAspect(r)` orients the ratio to the photo (portrait photo + landscape ratio → 1/r).
  - `FlCropToolbar` (objectName `cropToolbar`) `{ signal aspectChosen(real ratio); signal resetRequested(); signal doneRequested() }` with `aspectButton` (picker `aspectPicker`, rows `aspect_<0..5>`: Original, 3:2, 4:5, 1:1, 16:9, Free), `straightenSlider`, `rotateLeftButton`, `flipButton`, `cropResetButton`, `cropDoneButton`.
  - Shortcuts: `C` and `R` toggle crop (enter / apply); `Return`/`Enter` apply; `Esc` cancels. The inspector dims (0.4) and is disabled while cropping.

- [ ] **Step 1: Write the failing script `desktop/tests/ui/v2_crop_mode.script`**

```
# Crop mode: the toolbar appears, the inspector dims; aspect presets follow the
# photo's orientation (SAMPLE_A is portrait: 3:2 becomes 2:3); Esc restores the
# previous crop exactly; Return keeps the new one; straighten/rotate/flip work.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
crop:0.1,0.1,0.5,0.5
waitfor:engine.filmControls.crop_w=0.5,5000
key:67
wait:300
expect:photoCanvas.cropMode=true
expect:cropToolbar.visible=true
expect:inspector.enabled=false
click:aspectButton@0.5,0.5
wait:300
expect:aspectPicker.visible=true
click:aspect_1@0.5,0.5
wait:300
expect:photoCanvas.cropAspect=0.6666666666666666
key:16777216
wait:300
expect:photoCanvas.cropMode=false
expect:inspector.enabled=true
expect:engine.filmControls.crop_x=0.1
expect:engine.filmControls.crop_w=0.5
key:82
wait:300
expect:photoCanvas.cropMode=true
click:aspectButton@0.5,0.5
wait:300
click:aspect_3@0.5,0.5
wait:300
expect:photoCanvas.cropAspect=1
key:16777220
wait:300
expect:photoCanvas.cropMode=false
expect:engine.filmControls.crop_w=1
expect:engine.history.0.label=Crop
key:67
wait:300
click:straightenSlider@0.75,0.5
wait:200
expect:engine.filmControls.straighten_deg=22.5
click:rotateLeftButton@0.5,0.5
wait:200
expect:engine.filmControls.rotate_quadrant=3
click:flipButton@0.5,0.5
wait:200
expect:engine.filmControls.flip_h=true
shot:${TEMP}/v2_crop_mode.png
click:cropResetButton@0.5,0.5
wait:200
expect:engine.filmControls.straighten_deg=0
expect:engine.filmControls.flip_h=false
click:cropDoneButton@0.5,0.5
wait:300
expect:photoCanvas.cropMode=false
quit
```

(1:1 on the portrait sample keeps the full width: `crop_w = 1`.)

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_crop_mode.script -Ui v2`
Expected: FAIL from `expect:cropToolbar.visible=true` (`<missing>`).

- [ ] **Step 2: Canvas — remember the entry crop, cancel, reset, orient presets**

In `desktop/qml/v2/canvas/FlCanvas.qml`:
- after `property real cropH: 1` add:

```qml
    // The crop the photo had when crop mode started (Esc restores it).
    property real cropStartX: 0
    property real cropStartY: 0
    property real cropStartW: 1
    property real cropStartH: 1
```

- in `enterCropMode()`, after the four `cropX/Y/W/H = engine.filmControls.crop_*` lines add:

```qml
        cropStartX = cropX; cropStartY = cropY; cropStartW = cropW; cropStartH = cropH;
```

- after `applyCropMode()` add:

```qml
    function cancelCropMode() {
        cropMode = false;
        engine.setCrop(cropStartX, cropStartY, cropStartW, cropStartH);
    }
    function resetCropMode() {
        engine.resetGeometry();
        cropAspect = 0;
        cropX = 0; cropY = 0; cropW = 1; cropH = 1;
    }
```

- in `selectAspect(r)`, replace `cropAspect = r;` and the line `const A = imageAspect();` that follows with:

```qml
        const A = imageAspect();
        // Presets follow the photo's orientation: 3:2 on a portrait photo crops 2:3.
        if ((A < 1) !== (r < 1) && Math.abs(r - 1) > 0.0001) r = 1 / r;
        cropAspect = r;
```

(and delete the original `const A = imageAspect();` so `A` is declared once).

- before the empty-state `Column`, add the toolbar:

```qml
    FlCropToolbar {
        visible: canvas.cropMode
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18 + statusBar.height
        compact: canvas.width < 560
        onAspectChosen: (r) => canvas.selectAspect(r)
        onResetRequested: canvas.resetCropMode()
        onDoneRequested: canvas.applyCropMode()
    }
```

- [ ] **Step 3: Create `desktop/qml/v2/canvas/FlCropToolbar.qml`**

```qml
import QtQuick
import DFEE

// Floating crop toolbar (DESIGN.md): 44 tall, radius 12, translucent toolbar color
// with an inner hairline and a soft shadow, centred at the canvas bottom. Aspect,
// straighten, rotate, flip, Reset, Done. `compact` drops the straighten readout.
Item {
    id: bar
    objectName: "cropToolbar"
    property bool compact: false
    property int presetIndex: 5                       // Free until a preset is chosen
    signal aspectChosen(real ratio)
    signal resetRequested()
    signal doneRequested()
    readonly property var presets: [
        { label: "Original", r: -1 }, { label: "3:2", r: 1.5 }, { label: "4:5", r: 0.8 },
        { label: "1:1", r: 1 }, { label: "16:9", r: 1.7777778 }, { label: "Free", r: 0 }
    ]
    readonly property real straighten: Number(engine.filmControls.straighten_deg)
    width: row.implicitWidth + 24
    height: 44
    onVisibleChanged: if (visible) presetIndex = 5

    Rectangle {                                       // shadow
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: -10
        radius: 14
        color: "#59000000"
    }
    Rectangle {
        anchors.fill: parent
        radius: 12
        color: "#eb242426"
        border.width: 1
        border.color: "#14ffffff"
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 10
        FlButton {
            id: aspectButton
            objectName: "aspectButton"
            kind: "quiet"
            text: bar.presets[bar.presetIndex].label
            anchors.verticalCenter: parent.verticalCenter
            onClicked: aspectPicker.open()
            FlListPopup {
                id: aspectPicker
                objectName: "aspectPicker"
                rowPrefix: "aspect_"
                width: 150
                y: -height - 6
                currentId: String(bar.presetIndex)
                rows: bar.presets.map((p, i) => ({ id: String(i), name: p.label, group: "", detail: "", art: "" }))
                onPicked: (id) => { bar.presetIndex = Number(id); bar.aspectChosen(bar.presets[bar.presetIndex].r); }
            }
        }
        Rectangle { width: 1; height: 22; color: "#1affffff"; anchors.verticalCenter: parent.verticalCenter }
        FlSlider {
            id: straightenSlider
            objectName: "straightenSlider"
            width: 96
            anchors.verticalCenter: parent.verticalCenter
            from: -45; to: 45; stepSize: 0.1
            bipolar: true; neutral: 0
            onValueMoved: (v) => engine.setFilmControl("straighten_deg", v)
            Binding { target: straightenSlider; property: "value"; value: bar.straighten }
            FlTip { visible: straightenHover.hovered; text: "Straighten — level a tilted horizon. Double-click resets." }
            HoverHandler { id: straightenHover }
        }
        Text {
            visible: !bar.compact
            width: 44
            anchors.verticalCenter: parent.verticalCenter
            text: (bar.straighten > 0 ? "+" : "") + bar.straighten.toFixed(1) + "°"
            color: Math.abs(bar.straighten) > 0.001 ? Theme.text : Theme.textCaption
            font.pixelSize: Theme.fontLabel
            font.features: { "tnum": 1 }
        }
        Rectangle { width: 1; height: 22; color: "#1affffff"; anchors.verticalCenter: parent.verticalCenter }
        FlIconButton {
            objectName: "rotateLeftButton"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "arrow-counter-clockwise"
            tip: "Rotate left 90°"
            onClicked: engine.rotateQuadrant(-1)
        }
        FlIconButton {
            objectName: "flipButton"
            anchors.verticalCenter: parent.verticalCenter
            iconName: "flip-horizontal"
            tip: "Flip horizontal"
            active: engine.filmControls.flip_h === true
            onClicked: engine.setFilmControl("flip_h", engine.filmControls.flip_h !== true)
        }
        FlButton {
            objectName: "cropResetButton"
            kind: "text"
            text: "Reset"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: bar.resetRequested()
        }
        FlButton {
            objectName: "cropDoneButton"
            kind: "accent"
            text: "Done"
            anchors.verticalCenter: parent.verticalCenter
            onClicked: bar.doneRequested()
        }
    }
}
```

- [ ] **Step 4: MainV2 — shortcuts, dim the inspector**

In `desktop/qml/v2/MainV2.qml`:
- replace the existing `Shortcut { sequence: "C"; … }` with:

```qml
    Shortcut { sequences: ["C", "R"]; enabled: engine.hasImage && !engine.lightroomRoundTrip && !root.textEntry; onActivated: canvas.cropMode ? canvas.applyCropMode() : canvas.enterCropMode() }
    Shortcut { sequences: ["Return", "Enter"]; enabled: canvas.cropMode && !root.textEntry; onActivated: canvas.applyCropMode() }
    Shortcut { sequence: "Esc"; enabled: canvas.cropMode; onActivated: canvas.cancelCropMode() }
```

- in the `FlInspector` block add:

```qml
        enabled: !canvas.cropMode
        opacity: canvas.cropMode ? 0.4 : 1.0
        Behavior on opacity { NumberAnimation { duration: Theme.motionNormal } }
```

Register `qml/v2/canvas/FlCropToolbar.qml` in `desktop/CMakeLists.txt` `QML_FILES`.

- [ ] **Step 5: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 1 command, `v2_crop.script` and `v2_canvas.script` with `-Ui v2`.
Expected: all `failures=0`. Check `v2_crop_mode.png`: floating toolbar centred over the photo, inspector dimmed.

- [ ] **Step 6: Commit**

```bash
git add desktop/qml/v2 desktop/CMakeLists.txt desktop/tests/ui/v2_crop_mode.script
git commit -m "feat(desktop): v2 crop mode with floating toolbar; Esc cancels, presets follow orientation"
```

---

### Task 4: Export, shortcuts and reset sheets

**Files:**
- Create: `desktop/qml/v2/dialogs/FlExportSheet.qml`, `FlShortcutsSheet.qml`, `FlResetSheet.qml`, `desktop/qml/v2/controls/FlKeycap.qml`
- Create: `desktop/tests/ui/v2_sheets.script`
- Modify: `desktop/qml/v2/shell/FlToolbar.qml`, `desktop/qml/v2/MainV2.qml`, `desktop/src/UiScript.cpp` (`+shift`, `+alt` key modifiers), `desktop/CMakeLists.txt`

**Interfaces:**
- Consumes: `FlSheet` (Task 2), `FlSegmented`, `FlSliderRow`, `FlTextField`, `FlButton`; `engine.exportFormat` (`png8|png16|tiff|jpeg`), `engine.jpegQuality` (1–100), `engine.exportDpi` (72–1200), `engine.exportImage()`, `engine.resetAllEdits()`.
- Produces:
  - `exportSheet` (format segmented `exportFormatSegmented` in order JPEG, 8-bit PNG, 16-bit PNG, 16-bit TIFF; `jpegQualityRow`; `exportDpiField`; `exportConfirm`); `shortcutsSheet`; `resetSheet` (`resetConfirm`); `exportOverlay` while exporting.
  - MainV2 `function exportRequested()` — Lightroom: export straight back; standalone: open the export sheet.
  - FlToolbar: `exportButton` objectName, `signal exportRequested()`, `signal resetAllRequested()`; Edit menu "Reset All Edits…" (`Ctrl+Shift+R`).
  - UiScript `key:<code>[+ctrl][+shift][+alt]`.

- [ ] **Step 1: Key modifiers in the harness**

In `desktop/src/UiScript.cpp` `postKey`, replace the `mods` line with:

```cpp
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (spec.contains("+ctrl")) mods |= Qt::ControlModifier;
    if (spec.contains("+shift")) mods |= Qt::ShiftModifier;
    if (spec.contains("+alt")) mods |= Qt::AltModifier;
```

- [ ] **Step 2: Write the failing script `desktop/tests/ui/v2_sheets.script`**

```
# ? and F1 open the shortcuts sheet, Esc closes it; Export… and Ctrl+S open the
# export sheet (no export is run); Ctrl+Shift+R asks before resetting everything.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
key:63
wait:300
expect:shortcutsSheet.visible=true
key:16777216
wait:300
expect:shortcutsSheet.visible=false
key:16777264
wait:300
expect:shortcutsSheet.visible=true
key:16777264
wait:300
expect:shortcutsSheet.visible=false
click:exportButton@0.5,0.5
wait:300
expect:exportSheet.visible=true
click:exportFormatSegmented@0.1,0.5
wait:200
expect:engine.exportFormat=jpeg
expect:jpegQualityRow.visible=true
click:exportFormatSegmented@0.9,0.5
wait:200
expect:engine.exportFormat=tiff
expect:exportDpiField.visible=true
expect:engine.exporting=false
key:16777216
wait:300
expect:exportSheet.visible=false
key:83+ctrl
wait:300
expect:exportSheet.visible=true
key:16777216
wait:300
control:film_contrast=150
key:82+ctrl+shift
wait:300
expect:resetSheet.visible=true
click:resetConfirm@0.5,0.5
wait:300
expect:resetSheet.visible=false
expect:engine.filmControls.film_contrast=100
expect:engine.history.0.label=Reset all
quit
```

Run: `cmake --build desktop/out/build --config Release` then `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_sheets.script -Ui v2`
Expected: FAIL from `expect:shortcutsSheet.visible=true` (`<missing>`).

- [ ] **Step 3: Create the sheets**

`desktop/qml/v2/controls/FlKeycap.qml`:

```qml
import QtQuick
import DFEE

// A key in the shortcuts sheet.
Rectangle {
    property string label: ""
    implicitWidth: Math.max(22, keyText.implicitWidth + 12)
    implicitHeight: 22
    radius: 5
    color: Theme.control
    border.width: 1
    border.color: Theme.hairline
    Text {
        id: keyText
        anchors.centerIn: parent
        text: parent.label
        color: Theme.text
        font.pixelSize: Theme.fontCaption
        font.weight: Font.Medium
    }
}
```

`desktop/qml/v2/dialogs/FlExportSheet.qml`:

```qml
import QtQuick
import DFEE

// Export: format, JPEG quality or TIFF resolution, and where the file goes (next to
// the original; choosing a folder comes with Phase 4). Lightroom mode never opens
// this sheet — it exports straight back.
FlSheet {
    id: sheet
    objectName: "exportSheet"
    title: "Export"
    width: 440
    readonly property var formats: [
        { id: "jpeg", label: "JPEG" }, { id: "png8", label: "8-bit PNG" },
        { id: "png16", label: "16-bit PNG" }, { id: "tiff", label: "16-bit TIFF" }
    ]
    function formatIndex() {
        for (let i = 0; i < formats.length; ++i) if (formats[i].id === engine.exportFormat) return i;
        return 0;
    }

    Item {
        width: parent.width
        height: Theme.segmentHeight + 4
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Format"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlSegmented {
            objectName: "exportFormatSegmented"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            model: sheet.formats.map(f => f.label)
            currentIndex: sheet.formatIndex()
            onActivated: (i) => engine.exportFormat = sheet.formats[i].id
        }
    }
    FlSliderRow {
        objectName: "jpegQualityRow"
        visible: engine.exportFormat === "jpeg"
        label: "Quality"
        from: 1; to: 100; neutral: 100
        value: engine.jpegQuality
        onMoved: (v) => engine.jpegQuality = Math.round(v)
    }
    Item {
        width: parent.width
        height: Theme.controlHeight
        visible: engine.exportFormat === "tiff"
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Resolution"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            FlTextField {
                objectName: "exportDpiField"
                width: 72
                escReturnsFocus: false
                horizontalAlignment: TextInput.AlignRight
                text: String(engine.exportDpi)
                validator: IntValidator { bottom: 72; top: 1200 }
                onEditingFinished: if (acceptableInput) engine.exportDpi = parseInt(text)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "dpi"
                color: Theme.textCaption
                font.pixelSize: Theme.fontLabel
            }
        }
    }
    Item {
        width: parent.width
        height: 20
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Saves to"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: "Next to the original"
            color: Theme.text
            font.pixelSize: Theme.fontLabel
        }
    }
    Text {
        visible: engine.exportFormat === "tiff"
        text: "16-bit TIFF is uncompressed sRGB."
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
    }
    Row {
        anchors.right: parent.right
        spacing: 8
        FlButton { kind: "quiet"; text: "Cancel"; onClicked: sheet.close() }
        FlButton {
            objectName: "exportConfirm"
            kind: "accent"
            text: "Export"
            enabled: engine.hasImage && !engine.exporting
            onClicked: { engine.exportImage(); sheet.close(); }
        }
    }
}
```

`desktop/qml/v2/dialogs/FlShortcutsSheet.qml`:

```qml
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
                { keys: ["Ctrl", "S"], desc: engine.lightroomRoundTrip ? "Save & return to Lightroom" : "Export" }
            ]},
            { title: "Photos", items: [
                { keys: ["←", "→"], desc: "Previous / next photo" },
                { keys: ["Ctrl", "←", "→"], desc: "Previous / next (from a slider)" }
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
```

`desktop/qml/v2/dialogs/FlResetSheet.qml`:

```qml
import QtQuick
import DFEE

// Confirms a full reset (film and every adjustment). Undo brings it back.
FlSheet {
    id: sheet
    objectName: "resetSheet"
    title: "Reset all edits?"
    width: 360
    Text {
        width: parent.width
        text: "Clears the film and every adjustment on this photo. You can undo it."
        wrapMode: Text.WordWrap
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
    }
    Row {
        anchors.right: parent.right
        spacing: 8
        FlButton { kind: "quiet"; text: "Cancel"; onClicked: sheet.close() }
        FlButton {
            objectName: "resetConfirm"
            kind: "accent"
            text: "Reset"
            onClicked: { engine.resetAllEdits(); sheet.close(); }
        }
    }
}
```

- [ ] **Step 4: Toolbar and MainV2 wiring**

`desktop/qml/v2/shell/FlToolbar.qml`:
- add `signal exportRequested()` and `signal resetAllRequested()` after `signal cycleStockRequested(int dir)`;
- the accent Export button gets `objectName: "exportButton"` and `onClicked: bar.exportRequested()`;
- in `fileMenu`, the Export action's `onTriggered` becomes `bar.exportRequested()`;
- in `editMenu`, after Redo add:

```qml
        MenuSeparator {}
        Action { text: "Reset All Edits…"; property string keys: "Ctrl+Shift+R"; enabled: engine.hasImage; onTriggered: bar.resetAllRequested() }
```

`desktop/qml/v2/MainV2.qml`:
- add the function after `openPhoto`:

```qml
    // Lightroom Edit-In saves straight back; standalone shows the export sheet.
    function exportRequested() {
        if (engine.lightroomRoundTrip) engine.exportImage();
        else exportSheet.open();
    }
```

- in the `FlToolbar` block add `onExportRequested: root.exportRequested()`, `onResetAllRequested: resetSheet.open()`, `onHelpRequested: shortcutsSheet.open()`;
- replace the `Ctrl+S` Shortcut's `onActivated: engine.exportImage()` with `onActivated: root.exportRequested()`;
- add Shortcuts:

```qml
    Shortcut { sequences: ["?", "F1"]; enabled: !root.textEntry; onActivated: shortcutsSheet.open() }
    Shortcut { sequence: "Ctrl+Shift+R"; enabled: engine.hasImage && !root.textEntry; onActivated: resetSheet.open() }
```

- after `FlLookDialog { id: lookDialog }` add:

```qml
    FlExportSheet { id: exportSheet }
    FlShortcutsSheet { id: shortcutsSheet }
    FlResetSheet { id: resetSheet }

    // While exporting: a quiet veil with progress (ported from v1).
    Rectangle {
        objectName: "exportOverlay"
        anchors.fill: parent
        z: 10
        visible: engine.exporting
        color: "#d9101114"
        MouseArea { anchors.fill: parent }        // swallow clicks while saving
        Column {
            anchors.centerIn: parent
            spacing: 12
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: engine.exporting; width: 36; height: 36 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: engine.lightroomRoundTrip ? "Saving to Lightroom…" : "Exporting…"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
            }
        }
    }
```

Register in `desktop/CMakeLists.txt` `QML_FILES`:

```cmake
        qml/v2/controls/FlKeycap.qml
        qml/v2/dialogs/FlExportSheet.qml
        qml/v2/dialogs/FlShortcutsSheet.qml
        qml/v2/dialogs/FlResetSheet.qml
```

- [ ] **Step 5: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 2 command, `v2_keys.script` and `v2_shell.script` with `-Ui v2`.
Expected: all `failures=0`.

- [ ] **Step 6: Commit**

```bash
git add desktop/qml/v2 desktop/src/UiScript.cpp desktop/CMakeLists.txt desktop/tests/ui/v2_sheets.script
git commit -m "feat(desktop): v2 export, keyboard shortcuts and reset sheets"
```

---

### Task 5: Lightroom Edit-In rules

**Files:**
- Create: `desktop/qml/v2/shell/FlHistoryList.qml`
- Create: `desktop/tests/ui/v2_lightroom.script`
- Modify: `desktop/qml/v2/shell/FlSidebar.qml`, `desktop/qml/v2/shell/FlToolbar.qml`, `desktop/qml/v2/MainV2.qml`, `desktop/CMakeLists.txt`
- Add icon: `desktop/resources/icons/clock-counter-clockwise.svg`

**Interfaces:**
- Consumes: `engine.history`, `engine.historyIndex`, `engine.jumpToHistory(row)`; `FlTray.tabNames` (Task 1); `exportRequested()` (Task 4).
- Produces: `FlHistoryList` (a `ListView` of history rows, long labels elide); sidebar objectName `sidebar`, hidden in Lightroom mode; toolbar `historyButton` (Lightroom only, replaces the sidebar toggle) opening `historyPopover` whose list is `historyPopoverList`.

- [ ] **Step 1: Write the failing script `desktop/tests/ui/v2_lightroom.script`**

```
# Lightroom Edit-In: no library sidebar and no Roll; History opens from the toolbar;
# the commit button reads Save & Return.
waitfor:engine.hasImage=true,30000
expect:engine.lightroomRoundTrip=true
expect:sidebar.visible=false
expect:historyButton.visible=true
expect:tray.tabNames.length=2
expect:tray.activeTab=films
expect:exportButton.text=Save & Return
click:historyButton@0.5,0.5
wait:300
expect:historyPopover.visible=true
expect:historyPopoverList.count=1
key:16777216
wait:300
expect:historyPopover.visible=false
quit
```

Run: `powershell -ExecutionPolicy Bypass -Command "& ./desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_lightroom.script -Ui v2 -AppArgs @('--lightroom-edit','`${SAMPLE_TIFF}')"`
Expected: FAIL from `expect:sidebar.visible=false` (got `true`) / `historyButton` missing.

- [ ] **Step 2: Fetch the icon; extract `FlHistoryList`**

```bash
cd desktop/resources/icons
curl -fsSL -o clock-counter-clockwise.svg https://raw.githubusercontent.com/phosphor-icons/core/main/assets/regular/clock-counter-clockwise.svg
sed -i 's/fill="currentColor"/fill="#ffffff"/' clock-counter-clockwise.svg
```

`desktop/qml/v2/shell/FlHistoryList.qml` (the sidebar's history list, now shared; labels elide):

```qml
import QtQuick
import DFEE

// Edit history rows: the current step highlighted, steps after it (redo-able)
// dimmed; a click jumps there. Used by the sidebar and the Lightroom popover.
ListView {
    id: list
    clip: true
    model: engine.history
    spacing: 2
    delegate: Rectangle {
        id: rowItem
        readonly property bool current: index === engine.historyIndex
        readonly property bool future: index < engine.historyIndex
        width: list.width
        height: 26
        radius: Theme.radiusControl
        color: current ? Theme.rowSelected : (hover.hovered ? Theme.rowHover : "transparent")
        HoverHandler { id: hover }
        MouseArea { anchors.fill: parent; onClicked: engine.jumpToHistory(index) }
        Rectangle {
            id: dot
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 5; height: 5; radius: 3
            color: rowItem.current ? Theme.accent : "#2effffff"
        }
        Text {
            anchors.left: dot.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: modelData.label
            elide: Text.ElideRight
            font.pixelSize: Theme.fontLabel
            color: rowItem.current ? Theme.text : (rowItem.future ? Theme.textTertiary : Theme.textSecondary)
        }
    }
}
```

In `desktop/qml/v2/shell/FlSidebar.qml` replace the whole `ListView { id: historyList … }` block with:

```qml
            FlHistoryList {
                id: historyList
                objectName: "historyList"
                width: parent.width
                height: side.height - y - 40
            }
```

- [ ] **Step 3: Toolbar history popover; hide the sidebar in Lightroom mode**

`desktop/qml/v2/shell/FlToolbar.qml`, in the left `Row`, replace the sidebar-toggle `FlIconButton` with:

```qml
        FlIconButton {
            visible: !engine.lightroomRoundTrip
            iconName: "sidebar-simple"
            tip: "Show or hide sidebar"
            onClicked: bar.sidebarToggled()
        }
        FlIconButton {
            id: historyButton
            objectName: "historyButton"
            visible: engine.lightroomRoundTrip
            iconName: "clock-counter-clockwise"
            tip: "History"
            active: historyPopover.visible
            onClicked: historyPopover.open()
            Popup {
                id: historyPopover
                objectName: "historyPopover"
                y: historyButton.height + 8
                width: 260
                height: Math.min(popoverList.contentHeight + 16, 360)
                padding: 8
                focus: true
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onClosed: Qt.callLater(function() {
                    const w = bar.Window.window;
                    if (w && w.returnFocus) w.returnFocus();
                })
                background: Rectangle {
                    color: Theme.popover
                    radius: Theme.radiusPopover
                    border.width: 1
                    border.color: Theme.hairline
                }
                contentItem: FlHistoryList { id: popoverList; objectName: "historyPopoverList" }
            }
        }
```

`desktop/qml/v2/MainV2.qml`: the `FlSidebar` block gets `objectName: "sidebar"` and its width becomes:

```qml
        width: root.sidebarOpen && !engine.lightroomRoundTrip ? Theme.sidebarWidth : 0
```

Register `qml/v2/shell/FlHistoryList.qml` in `desktop/CMakeLists.txt` `QML_FILES`.

- [ ] **Step 4: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 1 command, `v2_inspector_lr.script` (same `-Command` form) and `v2_shell.script` with `-Ui v2`.
Expected: all `failures=0`.

- [ ] **Step 5: Commit**

```bash
git add desktop/qml/v2 desktop/CMakeLists.txt desktop/resources/icons/clock-counter-clockwise.svg desktop/tests/ui/v2_lightroom.script
git commit -m "feat(desktop): v2 Lightroom mode — history popover, no library or roll"
```

---

### Task 6: Polish, v2 becomes the app, v1 retired

**Files:**
- Modify: `desktop/qml/v2/controls/FlSwitch.qml`, `FlSegmented.qml`, `FlSectionHeader.qml`, `desktop/qml/v2/inspector/FlScope.qml` (give arrows back), `desktop/qml/v2/controls/FlListPopup.qml` (stay in window), `desktop/qml/v2/shell/FlToolbar.qml` (View menu), `desktop/qml/v2/MainV2.qml` (zoom centre)
- Modify: `desktop/src/main.cpp`, `desktop/CMakeLists.txt`; Delete: `desktop/qml/Main.qml`
- Modify: `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` (status)
- Create: `desktop/tests/ui/v2_polish.script`

**Interfaces:**
- Consumes: everything above.
- Produces: no `FILMLAB_UI` needed — MainV2 loads by default (`FILMLAB_UI=gallery` still opens the control gallery); View-menu actions `zoomFitAction`, `zoomActualAction` (renamed "Zoom to 200%") enabled only with a photo; `Ctrl+1` zooms about the photo's centre.

- [ ] **Step 1: Write the failing script `desktop/tests/ui/v2_polish.script`**

```
# Clicking a switch after a slider gives the arrows back; zoom menu items wait for a
# photo; Ctrl+1 zooms about the photo's centre; pickers keep inside the window.
expect:zoomActualAction.enabled=false
expect:stockPicker.margins=8
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:zoomActualAction.enabled=true
click:slider_film_contrast@0.5,0.5
wait:150
expect:v2Root.arrowKeysFree=false
click:adaptiveSwitch@0.95,0.5
wait:150
expect:v2Root.arrowKeysFree=true
key:49+ctrl
wait:300
expect:photoCanvas.zoom=2
expect:imageArea.panX=0
expect:imageArea.panY=0
quit
```

Run (no `-Ui`: this is also the first run against the new default):
`powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_polish.script`
Expected: FAIL — `zoomActualAction` missing (v1 window loads without `-Ui`).

- [ ] **Step 2: Make v2 the app and delete v1**

`desktop/src/main.cpp`: replace the block from `// FILMLAB_UI=v2 loads the redesigned window` through the closing `}` of `if (v2 && …)` with:

```cpp
    // FILMLAB_UI=gallery opens the v2 control gallery (development); otherwise the app.
    const bool gallery = qEnvironmentVariable("FILMLAB_UI") == QLatin1String("gallery");
    engine.loadFromModule("DFEE", gallery ? "ControlsGallery" : "MainV2");
    if (!engine.rootObjects().isEmpty()) {
        applyWindowChrome(qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst()));
    }
```

`desktop/CMakeLists.txt`: remove the line `        qml/Main.qml` from `QML_FILES`. Then:

```bash
git rm desktop/qml/Main.qml
grep -rn "Main.qml\|\"Main\"" desktop/src desktop/CMakeLists.txt desktop/packaging
```
Expected: no hits (the packaging `--qmldir desktop/qml` still covers `qml/v2`).

- [ ] **Step 3: Give the arrows back after non-slider clicks**

In each of `FlSwitch.qml` (its `MouseArea`), `FlSegmented.qml` (the segment `MouseArea`), `FlSectionHeader.qml` (the header `MouseArea`) and `inspector/FlScope.qml` (its `MouseArea`), make the click handler release a slider's keyboard focus before its existing action. Example for `FlSwitch.qml`:

```qml
    MouseArea {
        anchors.fill: parent
        enabled: sw.enabled
        onClicked: {
            // A slider keeps the arrows only until something else is clicked.
            const w = sw.Window.window;
            if (w && w.activeFocusItem && w.activeFocusItem.keepsArrowKeys === true && w.returnFocus) w.returnFocus();
            sw.toggled();
        }
    }
```

Same three lines (with that file's root id: `seg`, `h`, `scope`) in the other three handlers, followed by their existing calls (`seg.activated(index)`, `h.toggled()`, `scope.switchRequested()`).

- [ ] **Step 4: Pickers stay in the window; View menu; zoom centre**

`desktop/qml/v2/controls/FlListPopup.qml`: after `padding: 6` add `margins: 8`.

`desktop/qml/v2/shell/FlToolbar.qml`, `viewMenu`: replace the whole menu body with:

```qml
        Action { text: "Edited"; enabled: engine.hasImage; onTriggered: bar.compareChosen(0) }
        Action { text: "Split"; enabled: engine.hasImage && engine.hasBefore; onTriggered: bar.compareChosen(1) }
        Action { text: "Side by Side"; enabled: engine.hasImage && engine.hasBefore; onTriggered: bar.compareChosen(2) }
        MenuSeparator {}
        Action { objectName: "zoomFitAction"; text: "Fit"; property string keys: "Ctrl+0"; enabled: engine.hasImage; onTriggered: bar.fitRequested() }
        Action { objectName: "zoomActualAction"; text: "Zoom to 200%"; property string keys: "Ctrl+1"; enabled: engine.hasImage; onTriggered: bar.actualSizeRequested() }
        MenuSeparator {}
        Action { text: "Show or Hide Sidebar"; enabled: !engine.lightroomRoundTrip; onTriggered: bar.sidebarToggled() }
```

and the `zoomMenu` second action's text becomes `"Zoom to 200%"`.

In `desktop/qml/v2/canvas/FlCanvas.qml` add after `function setZoom(z, fx, fy) { … }`:

```qml
    // Zoom about the photo's centre (imageArea coordinates, not the canvas's).
    function zoomCentered(z) { imageArea.setZoom(z, imageArea.width / 2, imageArea.height / 2); }
```

In `desktop/qml/v2/MainV2.qml` replace both `canvas.setZoom(2.0, canvas.width / 2, canvas.height / 2)` calls (the `Ctrl+1` Shortcut and the toolbar's `onActualSizeRequested`) with `canvas.zoomCentered(2.0)`.

- [ ] **Step 5: Mark Phase 1 done in the spec**

In `docs/superpowers/specs/2026-09-27-ui-redesign-design.md`, change the Phasing line `1. New shell on the existing API, …` so it starts with `1. (Done 2026-10-07: plans 1A–1C; v1 Main.qml retired.) New shell on the existing API, …`.

- [ ] **Step 6: Build and run the whole suite on the default window**

Run: `cmake --build desktop/out/build --config Release`, then every script **without `-Ui`** (except `gallery.script`, which keeps `-Ui gallery`): `v2_boot`, `v2_shell`, `v2_canvas`, `v2_keys`, `v2_crop`, `v2_groups`, `v2_inspector`, `v2_inspector_color`, `v2_coverage`, `v2_tray`, `v2_looks`, `v2_crop_mode`, `v2_sheets`, `v2_polish`, `smoke`, `memory`, `info`; `v2_minsize` with `-AppArgs @('--min-size')`; the persistence pair with one `-UiSettings` ini; `v2_inspector_lr`, `v2_lightroom` and `lightroom_bypass` via `-Command` with `-AppArgs @('--lightroom-edit','${SAMPLE_TIFF}')`; unit suites `desktop_tests` (11), `image_info_tests` (6), `stock_catalog_tests` (5).
Expected: every script `failures=0`; unit totals 11/6/5 passed.

- [ ] **Step 7: Commit, installer, on-screen check**

```bash
git add -A desktop/qml desktop/src/main.cpp desktop/CMakeLists.txt docs/superpowers/specs/2026-09-27-ui-redesign-design.md desktop/tests/ui/v2_polish.script
git commit -m "feat(desktop): v2 is the app; v1 window retired; focus, zoom and menu polish"
```

Then `desktop/packaging/deploy.ps1` and `"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" desktop\packaging\FilmLab.iss`. Run the deployed `FilmLab.exe` once on screen (no input injection; a script with `open`, `wait`, `shot`, `quit` and `QT_QPA_PLATFORM` unset) to confirm icons, the tray and the dark title bar.
