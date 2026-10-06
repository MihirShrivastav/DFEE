# UI v2 — 1B Inspector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fill the v2 window's right panel with the inspector — pinned scope, then Film, Exposure, Tone, Color, Grain & light, Print and Fine-tune sections in one scrolling column — so every film control from v1 is reachable in v2.

**Architecture:** Section-level state lives in the engine: `EngineController` gains v2 control groups (one-step section Reset with friendly history labels), an `editedGroups` map that drives the edited dots, and `setGradeColor` (hue + saturation as one history step). The QML side is a thin view: `FlFilmSlider` binds one engine key to an `FlSliderRow`; each section is an `FlInspectorSection` (controlled open state, persisted via QtCore `Settings`); pickers share one `FlListPopup`. Labels and tooltips are copied from v1 `Main.qml` so wording stays effect-named.

**Tech Stack:** Qt 6.8.3 Quick/Controls (Basic style), QtCore `Settings` (Qt ≥ 6.5), C++20, CMake.

**Spec:** `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` (Inspector, Phase 1) and `desktop/DESIGN.md` v2; mockup https://claude.ai/artifact/595M73HfHvmwkhUFEiJQpB; builds on plan `docs/superpowers/plans/2026-10-06-ui-v2-1a-shell.md`.

## Global Constraints

- Tokens, type and geometry exactly as `desktop/DESIGN.md` v2 (`Theme.qml`); inspector width 300, section header row 40, section body padding 16 horizontal / 14 vertical, 14px between rows; never below 11px type; sentence case; US spelling ("Color").
- Every v2 QML file except `Theme.qml` has `import DFEE`; component files never reference MainV2 ids (they use `engine`, `library`, `uiSettingsLocation` context properties and emit signals).
- Controlled components: a component that shows state it doesn't own (section `open`, switch `checked`, segment index) only emits a signal on click; the owner changes the state. Never assign to a property that is bound from outside.
- QML pitfalls hit in 1A: no property names `on<Upper>…` (parsed as handlers), no redeclaring FINAL/existing members (`Button.icon`, `Slider.moved`), clicks via `MouseArea` (not `TapHandler`), popups hand focus back with `Window.window.returnFocus()`.
- Control labels and tooltips copied verbatim from v1 `desktop/qml/Main.qml` unless this plan gives new wording; controls named for the effect they produce.
- Every `defaultFilmControls()` key except geometry (`crop_*`, `straighten_deg`, `rotate_quadrant`, `flip_h`, `flip_v` — crop mode, plan 1C) is reachable in the v2 inspector (Task 5 coverage script).
- v1 `Main.qml` stays working; the only shared code touched is C++ (`EngineController`, `UiScript`, `main.cpp`) and must keep v1 scripts green.
- Verify with `DFEE_UI_SCRIPT` via `desktop/tests/run_ui.ps1` (offscreen); never inject system-wide input. Rebuild Release after each task; commits carry no Co-Authored-By lines.

## Review Focus

1. **Undo / redo / history jump after inspector edits**: every slider, switch, segment and wheel must show the restored value (no knob stuck where it was dragged). Task 3 script: drag + nudge Film contrast, Ctrl+Z, slider reads 100.
2. **Monochrome stock**: film-color controls dim and stop responding, white balance stays live, and switching back to a color stock restores them. Task 4 script.
3. **Keyboard focus after a slider**: arrows nudge the focused slider; Esc or clicking the photo gives the arrows back to photo navigation. Task 3 script.
4. **Lightroom Edit-In mode**: the inspector works the same (film picker, sliders), edits never reach the catalog. Task 3 Lightroom script.
5. **Section state across launches, and isolated in tests**: collapsed/expanded sections and scope mode survive a restart; UI tests never read or write the user's real settings. Task 3 two-run persistence check.

---

### Task 1: Engine — v2 control groups, edited flags, grade color

**Files:**
- Modify: `desktop/src/EngineController.h`, `desktop/src/EngineController.cpp`
- Modify: `desktop/src/UiScript.cpp` (`call:` step)
- Create: `desktop/tests/ui/v2_groups.script`

**Interfaces:**
- Produces:
  - `Q_PROPERTY(QVariantMap editedGroups READ editedGroups NOTIFY filmControlsChanged)` — `{group: bool}` for every group below; grain amount keys are ignored while `grain_auto` is on.
  - `resetControlGroup(group)` accepts v2 groups `film`, `exposure`, `tone`, `color`, `grain_light`, `print`, `fine_tune` (v1 names `film_tone`, `color_character`, `material`, `light`, `color_balance`, `grade` still work); history label `"Reset <Label>"` (e.g. `Reset Tone`, `Reset Grain & light`).
  - `Q_INVOKABLE void setGradeColor(const QString& zone, double hue, double sat)` — zone ∈ `shadow|midtone|highlight|global`; hue wrapped to [0,360), sat clamped [0,100]; one history step, coalesced per zone (`grade_<zone>`), label `Shadow tint` / `Midtone tint` / `Highlight tint` / `Global tint`.
  - UiScript step `call:<method>=<string>` — invokes `engine.<method>(QString)`.

- [ ] **Step 1: Add the `call:` step to `desktop/src/UiScript.cpp`**

After the `crop` branch in `runNext`:

```cpp
    } else if (verb == QLatin1String("call")) {  // call:<method>=<string arg>
        QMetaObject::invokeMethod(engine, arg.section('=', 0, 0).toUtf8().constData(),
                                  Q_ARG(QString, arg.section('=', 1)));
```

- [ ] **Step 2: Write the failing script `desktop/tests/ui/v2_groups.script`**

```
# v2 section groups: edited flags follow the controls, Reset is one step with a
# readable label, grade color is one step per zone.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:engine.editedGroups.tone=false
expect:engine.editedGroups.grain_light=false
expect:engine.editedGroups.fine_tune=false
control:film_contrast=150
expect:engine.editedGroups.tone=true
expect:engine.editedGroups.film_tone=true
expect:engine.editedGroups.color=false
control:hsl_red_s=20
expect:engine.editedGroups.fine_tune=true
call:resetControlGroup=tone
expect:engine.filmControls.film_contrast=100
expect:engine.editedGroups.tone=false
expect:engine.history.0.label=Reset Tone
call:resetControlGroup=fine_tune
expect:engine.filmControls.hsl_red_s=0
expect:engine.history.0.label=Reset Fine-tune
quit
```

- [ ] **Step 3: Build and run it to verify it fails**

Run: `cmake --build desktop/out/build --config Release` then
`powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_groups.script -Ui v2`
Expected: `UISCRIPT FAIL expect:engine.editedGroups.tone=false (got '<missing>')` and more; `exit=3`.

- [ ] **Step 4: Declare the new API in `desktop/src/EngineController.h`**

After the `presetGroups` Q_PROPERTY (line 67):

```cpp
    // Per-group "has edits" flags for the v2 inspector's section dots:
    // {film, exposure, tone, color, grain_light, print, fine_tune, + v1 group names} -> bool.
    Q_PROPERTY(QVariantMap editedGroups READ editedGroups NOTIFY filmControlsChanged)
```

After `Q_INVOKABLE void resetControlGroup(const QString& group);`:

```cpp
    QVariantMap editedGroups() const;
    // Set one color-grading zone's hue (degrees) and saturation (0..100) as a single
    // history step; zone is shadow, midtone, highlight or global.
    Q_INVOKABLE void setGradeColor(const QString& zone, double hue, double sat);
```

- [ ] **Step 5: Implement in `desktop/src/EngineController.cpp`**

Add `#include <cmath>` with the other includes. Replace the whole `resetControlGroup` function (currently lines 469–522, from `void EngineController::resetControlGroup` through its closing brace) with:

```cpp
namespace {

// Control groups for section Reset and the edited dots. The v2 inspector's sections
// come first; the v1 names stay until the v1 window is retired.
const QHash<QString, QStringList>& controlGroups()
{
    static const QStringList light = {"exposure", "contrast", "highlights", "shadows",
        "whites", "blacks", "midtones", "texture", "clarity", "dehaze", "sharpness",
        "sharpness_mask"};
    static const QStringList grade = {"cg_shadow_hue", "cg_shadow_sat", "cg_shadow_lum",
        "cg_midtone_hue", "cg_midtone_sat", "cg_midtone_lum", "cg_highlight_hue",
        "cg_highlight_sat", "cg_highlight_lum", "cg_global_hue", "cg_global_sat",
        "cg_global_lum", "cg_balance", "cg_blending"};
    static const QStringList hsl = [] {
        QStringList keys;
        for (const char* band : {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"})
            for (const char* part : {"h", "s", "l"})
                keys << QStringLiteral("hsl_%1_%2").arg(QLatin1String(band), QLatin1String(part));
        return keys;
    }();
    static const QStringList grainLight = {"grain_auto", "grain_strength", "grain_size",
        "grain_roughness", "halation_strength", "halation_threshold", "bloom"};
    static const QStringList print = {"print_stock", "print_strength", "print_c", "print_m",
        "print_y", "print_contrast", "print_black_point"};
    static const QHash<QString, QStringList> groups = {
        // v2 inspector sections
        {"film", {"profile_strength"}},
        {"exposure", {"exposure_placement", "film_exposure_ev"}},
        {"tone", {"rendered_input", "adaptive", "highlight_rolloff", "film_contrast", "shadow_lift"}},
        {"color", {"film_color_density", "emulsion_color_density", "highlight_color_hold",
                   "shadow_color_retention", "crossover", "cg_crossbalance", "temp", "tint",
                   "vibrance", "saturation"}},
        {"grain_light", grainLight},
        {"print", print},
        {"fine_tune", light + grade + hsl},
        // v1 groups
        {"film_tone", {"rendered_input", "adaptive", "profile_strength", "highlight_rolloff",
                       "film_contrast", "shadow_lift"}},
        {"color_character", {"film_color_density", "emulsion_color_density",
                             "highlight_color_hold", "shadow_color_retention", "crossover",
                             "cg_crossbalance"}},
        {"material", grainLight},
        {"light", light},
        {"color_balance", {"temp", "tint", "vibrance", "saturation"}},
        {"grade", grade},
    };
    return groups;
}

QString groupLabel(const QString& group)
{
    static const QHash<QString, QString> labels = {
        {"film", "Film"}, {"exposure", "Exposure"}, {"tone", "Tone"}, {"color", "Color"},
        {"grain_light", "Grain & light"}, {"print", "Print"}, {"fine_tune", "Fine-tune"},
        {"film_tone", "Film tone"}, {"color_character", "Color character"},
        {"material", "Material finish"}, {"light", "Light"}, {"color_balance", "Color balance"},
        {"grade", "Color grading"},
    };
    return labels.value(group, group);
}

// Controls hold doubles, ints, bools and strings; numbers compare by value.
bool sameControlValue(const QVariant& a, const QVariant& b)
{
    const auto numeric = [](const QVariant& v) {
        const int id = v.metaType().id();
        return id == QMetaType::Double || id == QMetaType::Int || id == QMetaType::LongLong
            || id == QMetaType::UInt || id == QMetaType::Float;
    };
    if (numeric(a) && numeric(b)) return qAbs(a.toDouble() - b.toDouble()) < 1e-6;
    return a == b;
}

} // namespace

void EngineController::resetControlGroup(const QString& group)
{
    const auto& groups = controlGroups();
    const auto it = groups.constFind(group);
    if (it == groups.cend()) return;

    const QVariantMap defaults = defaultFilmControls();
    bool changed = false;
    for (const QString& key : *it) {
        const QVariant value = defaults.value(key);
        if (filmControls_.value(key) != value) {
            filmControls_.insert(key, value);
            changed = true;
        }
    }
    if (!changed) return;
    if (group == QStringLiteral("material") || group == QStringLiteral("grain_light")) {
        grainResolving_ = false;
        emit grainResolvingChanged();
    }
    emit filmControlsChanged();
    recordHistory(QStringLiteral("Reset %1").arg(groupLabel(group)));
    scheduleRender();
}

QVariantMap EngineController::editedGroups() const
{
    const QVariantMap defaults = defaultFilmControls();
    // While Auto grain is on, the amount keys are the engine's, not the user's.
    const bool autoGrain = filmControls_.value("grain_auto").toBool();
    QVariantMap result;
    const auto& groups = controlGroups();
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        bool edited = false;
        for (const QString& key : it.value()) {
            if (autoGrain && key.startsWith(QLatin1String("grain_")) && key != QLatin1String("grain_auto"))
                continue;
            if (!sameControlValue(filmControls_.value(key), defaults.value(key))) {
                edited = true;
                break;
            }
        }
        result.insert(it.key(), edited);
    }
    return result;
}

void EngineController::setGradeColor(const QString& zone, double hue, double sat)
{
    static const QHash<QString, QString> labels = {
        {"shadow", "Shadow tint"}, {"midtone", "Midtone tint"},
        {"highlight", "Highlight tint"}, {"global", "Global tint"}};
    if (!labels.contains(zone)) return;
    const QString hueKey = QStringLiteral("cg_%1_hue").arg(zone);
    const QString satKey = QStringLiteral("cg_%1_sat").arg(zone);
    const double h = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    const double s = std::clamp(sat, 0.0, 100.0);
    if (sameControlValue(filmControls_.value(hueKey), h) && sameControlValue(filmControls_.value(satKey), s))
        return;
    filmControls_.insert(hueKey, h);
    filmControls_.insert(satKey, s);
    emit filmControlsChanged();
    recordHistory(labels.value(zone), QStringLiteral("grade_") + zone);
    scheduleRender();
}
```

- [ ] **Step 6: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 3 script command, then v1 `smoke.script` and `memory.script` (no `-Ui`).
Expected: all `failures=0`.

- [ ] **Step 7: Commit**

```bash
git add desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/src/UiScript.cpp desktop/tests/ui/v2_groups.script
git commit -m "feat(desktop): v2 control groups with edited flags, readable reset labels, one-step grade color"
```

---

### Task 2: Inspector building blocks (switch, engine slider, section, group label)

**Files:**
- Create in `desktop/qml/v2/controls/`: `FlSwitch.qml`, `FlFilmSlider.qml`, `FlGroupLabel.qml`, `FlInspectorSection.qml`
- Modify: `desktop/qml/v2/controls/FlSectionHeader.qml` (controlled; optional hairline), `FlSliderRow.qml` (tooltip, label elide), `FlSlider.qml` (`keepsArrowKeys`, `preventStealing`, Esc)
- Modify: `desktop/qml/v2/MainV2.qml` (`arrowKeysFree`), `desktop/qml/v2/ControlsGallery.qml`, `desktop/tests/ui/gallery.script`, `desktop/CMakeLists.txt`, `desktop/DESIGN.md`

**Interfaces:**
- Consumes: `engine.filmControls`, `engine.setFilmControl`, `engine.editedGroups`, `engine.resetControlGroup` (Task 1); `FlSliderRow`, `FlSectionHeader`, `FlTip`, `FlHairline` (1A).
- Produces:
  - `FlSwitch { label; checked; tip; signal toggled() }` — controlled; 26×16 track, accent when on.
  - `FlFilmSlider` (an `FlSliderRow`) `{ controlKey; autoValue: bool }` — objectName `ctl_<key>`, slider objectName `slider_<key>`; value from `engine.filmControls[key]`; drags call `engine.setFilmControl(key, v)`; `autoValue` shows "Auto" with the knob at `neutral`.
  - `FlSliderRow` gains `tip: string` (tooltip on the label line).
  - `FlSlider` gains `readonly property bool keepsArrowKeys: true`; Esc hands focus back to the window; drags never let a parent Flickable steal the mouse.
  - `FlSectionHeader` is controlled (click only emits `toggled()`) and gains `showHairline: bool` (default true).
  - `FlInspectorSection { title; group; open; summary; signal toggled(); readonly edited; default content }` — header objectName `<section objectName>Header`; body column 16px side padding, 14px top/bottom, 14px spacing; hairline under the whole section.
  - `FlGroupLabel` — 11/600 caption for subgroups ("Grain", "Halation").
  - MainV2 `arrowKeysFree` keys off `activeFocusItem.keepsArrowKeys`.

- [ ] **Step 1: Extend the gallery script (failing)**

Append before `shot:${TEMP}/gallery.png` in `desktop/tests/ui/gallery.script`:

```
# Section header is controlled: an outside change to its open state still shows.
click:galleryOpenHeader@0.5,0.5
wait:150
expect:galleryHeader.open=true
# Switch is controlled by its owner.
click:gallerySwitch@0.9,0.5
wait:150
expect:gallerySwitch.checked=true
# An engine-bound slider writes the control and follows outside changes.
click:slider_film_contrast@0.75,0.5
wait:150
expect:engine.filmControls.film_contrast=150
control:film_contrast=100
wait:150
expect:slider_film_contrast.value=100
expect:ctl_film_contrast.edited=false
```

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/gallery.script -Ui gallery -TimeoutSec 30`
Expected: FAIL at `click:galleryOpenHeader` (`missing`) and the later steps.

- [ ] **Step 2: Make `FlSectionHeader.qml` controlled, with an optional hairline**

Replace line 17 (`MouseArea { anchors.fill: parent; onClicked: { h.open = !h.open; h.toggled(); } }`) with:

```qml
    // Controlled: the owner flips `open` in response to toggled().
    MouseArea { anchors.fill: parent; onClicked: h.toggled() }
```

Add `property bool showHairline: true` after `property string summary: ""`, and change the last child to:

```qml
    FlHairline { anchors.bottom: parent.bottom; visible: h.showHairline }
```

Update the header comment's last line to: `// Reset on hover when edited, chevron (down = open, right = closed). Controlled.`

- [ ] **Step 3: Update `FlSlider.qml`**

Replace `    objectName: "inspectorSlider"   // MainV2.arrowKeysFree: a focused slider keeps the arrows` with:

```qml
    // MainV2.arrowKeysFree: a focused slider keeps Left/Right for nudging.
    readonly property bool keepsArrowKeys: true
```

In its `MouseArea`, add `preventStealing: true` after `anchors.bottomMargin: -6` (a drag inside the scrolling inspector must never turn into a scroll). After the `Keys.onRightPressed` line add:

```qml
    // Esc gives the arrow keys back to photo navigation.
    Keys.onEscapePressed: (e) => {
        const w = control.Window.window;
        if (w && w.returnFocus) w.returnFocus();
        e.accepted = true;
    }
```

- [ ] **Step 4: Update `FlSliderRow.qml` (tooltip, elide)**

Add `property string tip: ""` after `property var trackPalette: null`. Replace the label `Text` (the first child of the label `Item`) with:

```qml
        Text {
            anchors.left: parent.left
            anchors.right: valueText.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
            elide: Text.ElideRight
        }
```

give the value `Text` `id: valueText`, and add inside the label `Item`, after the value text:

```qml
        HoverHandler { id: labelHover }
        FlTip { visible: labelHover.hovered && row.tip.length > 0 && !s.pressed; text: row.tip }
```

- [ ] **Step 5: Create the new controls**

`desktop/qml/v2/controls/FlSwitch.qml`:

```qml
import QtQuick
import DFEE

// Switch row: label left, 26×16 switch right (accent when on). Controlled: a click
// only emits toggled(); the owner binds `checked` to its model.
Item {
    id: sw
    property string label: ""
    property bool checked: false
    property string tip: ""
    signal toggled()
    width: parent ? parent.width : 240
    height: 24
    opacity: enabled ? 1.0 : 0.4
    Text {
        anchors.left: parent.left
        anchors.right: track.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: sw.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
        elide: Text.ElideRight
    }
    Rectangle {
        id: track
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 26
        height: 16
        radius: 8
        color: sw.checked ? Theme.accent : Theme.control
        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        Rectangle {
            width: 12
            height: 12
            radius: 6
            y: 2
            x: sw.checked ? parent.width - width - 2 : 2
            color: Theme.knob
            Behavior on x { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic } }
        }
    }
    HoverHandler { id: hover }
    MouseArea { anchors.fill: parent; enabled: sw.enabled; onClicked: sw.toggled() }
    FlTip { visible: hover.hovered && sw.tip.length > 0; text: sw.tip }
}
```

`desktop/qml/v2/controls/FlFilmSlider.qml`:

```qml
import QtQuick
import DFEE

// One engine control as a slider row: shows engine.filmControls[controlKey] and
// writes drags through engine.setFilmControl. Row objectName "ctl_<key>", slider
// "slider_<key>". autoValue (Auto grain) shows "Auto" with the knob at neutral.
FlSliderRow {
    id: fs
    property string controlKey: ""
    property bool autoValue: false
    objectName: "ctl_" + controlKey
    slider.objectName: "slider_" + controlKey
    readonly property real engineValue: Number(engine.filmControls[controlKey])
    value: autoValue ? neutral : engineValue
    autoText: autoValue ? "Auto" : ""
    trackPalette: Theme.colorTrackPalette(controlKey)
    onMoved: (v) => engine.setFilmControl(fs.controlKey, v)
}
```

`desktop/qml/v2/controls/FlGroupLabel.qml`:

```qml
import QtQuick
import DFEE

// Subgroup caption inside a section ("Grain", "Halation"): 11/600, caption color.
Text {
    width: parent ? parent.width : 0
    topPadding: 4
    color: Theme.textCaption
    font.pixelSize: Theme.fontCaption
    font.weight: Font.DemiBold
}
```

`desktop/qml/v2/controls/FlInspectorSection.qml`:

```qml
import QtQuick
import DFEE

// One inspector section: 40px header (title, edited dot, Reset on hover, collapse)
// over a padded body, hairline under the whole section. Controlled: `open` comes
// from the owner (persisted settings). `group` names the engine control group that
// drives Reset and the edited dot.
Column {
    id: section
    property string title: ""
    property string group: ""
    property bool open: true
    property string summary: ""
    default property alias content: body.data
    signal toggled()
    readonly property bool edited: group.length > 0 && engine.editedGroups[group] === true
    width: parent ? parent.width : Theme.inspectorWidth
    FlSectionHeader {
        objectName: section.objectName.length > 0 ? section.objectName + "Header" : ""
        width: parent.width
        title: section.title
        open: section.open
        edited: section.edited
        summary: section.summary
        showHairline: false
        onToggled: section.toggled()
        onResetRequested: engine.resetControlGroup(section.group)
    }
    Item {
        width: parent.width
        height: section.open ? body.implicitHeight + 28 : 0
        visible: section.open
        Column {
            id: body
            x: 16
            y: 14
            width: parent.width - 32
            spacing: 14
        }
    }
    FlHairline {}
}
```

- [ ] **Step 6: MainV2 `arrowKeysFree`**

In `desktop/qml/v2/MainV2.qml` replace:

```qml
        && !(activeFocusItem && activeFocusItem.objectName === "inspectorSlider")
```

with:

```qml
        && !(activeFocusItem && activeFocusItem.keepsArrowKeys === true)
```

- [ ] **Step 7: Gallery additions**

In `desktop/qml/v2/ControlsGallery.qml`: add `property bool galleryHeaderOpen: true` and `property bool gallerySwitchOn: false` after `function returnFocus()…`; in the first button `Row` add

```qml
            FlButton { objectName: "galleryOpenHeader"; kind: "quiet"; text: "Open"; onClicked: root.galleryHeaderOpen = true }
```

change the header to

```qml
            FlSectionHeader { objectName: "galleryHeader"; title: "Tone"; edited: true; summary: "Off"; open: root.galleryHeaderOpen; onToggled: root.galleryHeaderOpen = !root.galleryHeaderOpen }
```

and append inside the slider `Column` (after the Temperature row):

```qml
                FlSwitch { objectName: "gallerySwitch"; label: "Adaptive scene tone"; checked: root.gallerySwitchOn; onToggled: root.gallerySwitchOn = !root.gallerySwitchOn }
                FlFilmSlider { controlKey: "film_contrast"; label: "Film contrast (engine)"; from: 0; to: 200; neutral: 100; tip: "Bound to engine.filmControls.film_contrast." }
```

Register the four new files in `desktop/CMakeLists.txt` `QML_FILES` (after `qml/v2/controls/FlMenu.qml`):

```cmake
        qml/v2/controls/FlSwitch.qml
        qml/v2/controls/FlFilmSlider.qml
        qml/v2/controls/FlGroupLabel.qml
        qml/v2/controls/FlInspectorSection.qml
```

- [ ] **Step 8: Document the new components in `desktop/DESIGN.md`**

In `## Components`, after the **Slider row** bullet, add:

```markdown
- **Switch:** label left (12, `textSecondary`), 26×16 track right — `control`
  off, `accent` on, 12px `knob`. For on/off film behaviours (Adaptive scene tone,
  Match grain to film speed).
- **Inspector section:** section header (above) over a body padded 16 / 14,
  rows 14 apart, hairline under the whole section. Subgroups inside a section get
  an 11/600 caption ("Grain", "Halation"), never a nested header.
- **Picker popover:** popover surface, rows 28 (40 with box art), group captions
  11/600, current row bold with an accent check; hover `rowSelected`.
- **Scope:** `inset` well, radius 6, pinned above the inspector sections;
  histogram 64 tall (R/G/B fills at ~0.3 alpha), click switches to the
  vectorscope (152 tall).
- **Color wheel:** hue ring fading to `inset` at the centre, 13px handle with a
  `knob` ring; drag from the centre to tint, double-click clears.
```

- [ ] **Step 9: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 1 command, and the 1A v2 scripts `v2_keys.script`, `v2_shell.script` with `-Ui v2`.
Expected: all `failures=0`.

- [ ] **Step 10: Commit**

```bash
git add desktop/qml/v2 desktop/CMakeLists.txt desktop/tests/ui/gallery.script desktop/DESIGN.md
git commit -m "feat(desktop): v2 inspector building blocks (switch, engine slider, section)"
```

---

### Task 3: Inspector shell — scope, Film, Exposure, Tone, persisted sections

**Files:**
- Create: `desktop/qml/v2/inspector/FlInspector.qml`, `FlScope.qml`, `FlFilmSection.qml`, `FlExposureSection.qml`, `FlToneSection.qml`
- Create: `desktop/qml/v2/controls/FlListPopup.qml`
- Create: `desktop/tests/ui/v2_inspector.script`, `v2_inspector_persist_a.script`, `v2_inspector_persist_b.script`, `v2_inspector_lr.script`
- Modify: `desktop/qml/v2/MainV2.qml`, `desktop/qml/v2/canvas/FlCanvas.qml`, `desktop/src/main.cpp`, `desktop/tests/run_ui.ps1`, `desktop/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 groups; Task 2 controls; `engine.stockModel` rows `{id, name, typeLabel, groupLabel, iso, blurb}` (row 0 is `{id: "none", name: "None"}`), `engine.stock`, `engine.histogramR/G/B`, `engine.vectorscope` (4096 Cb/Cr cells).
- Produces:
  - Context property `uiSettingsLocation` (QUrl; empty = the app's QSettings) from env `DFEE_UI_SETTINGS`; `run_ui.ps1 -UiSettings <ini>` (default: a throwaway ini in the work dir).
  - `FlListPopup { rows: [{id, name, detail, group, art}]; currentId; rowPrefix; signal picked(string id) }` — row objectName `<rowPrefix><id>`.
  - `FlInspector` (objectName `inspector`) with sections objectNames `filmSection`, `exposureSection`, `toneSection` (headers `<name>Header`), scope `scope` (`mode: "histogram"|"vectorscope"`), Flickable `inspectorFlick`; `Settings { category: "inspector" }` keys `filmOpen, exposureOpen, toneOpen, colorOpen, grainOpen, printOpen, fineTuneOpen` (defaults true, true, true, false, false, false, false) and `scopeMode`.
  - Film section: card `filmCard`, name text `filmCardName`, picker `stockPicker` (rows `stock_<id>`), slider `ctl_profile_strength`.
  - Exposure section: `placementSegmented` (0 = As shot, 1 = Auto balanced), `ctl_film_exposure_ev`.
  - Tone section: `ctl_highlight_rolloff`, `ctl_film_contrast`, `ctl_shadow_lift`, `ctl_rendered_input`, `adaptiveSwitch`.
  - `FlCanvas` gains `signal backgroundPressed()`; MainV2 returns focus on it.
  - Tasks 4–5 add their sections inside `FlInspector`'s `Column { id: sections }`.

- [ ] **Step 1: Settings isolation for tests**

`desktop/tests/run_ui.ps1`: add the parameter `[string]$UiSettings = ""` after `[string]$Ui = ""` (with a comma on the previous line), and after `$env:FILMLAB_UI = $Ui`:

```powershell
# v2 inspector state goes to a throwaway ini unless a test shares one across runs.
$env:DFEE_UI_SETTINGS = if ($UiSettings) { $UiSettings } else { Join-Path $work "ui-settings.ini" }
```

`desktop/src/main.cpp`: add `#include <QUrl>`; after `engine.rootContext()->setContextProperty("editStore", &editStore);`:

```cpp
    // v2 inspector state (QtCore Settings) lives in the app's QSettings; UI tests
    // point it at a throwaway ini with DFEE_UI_SETTINGS.
    const QString uiSettings = qEnvironmentVariable("DFEE_UI_SETTINGS");
    engine.rootContext()->setContextProperty("uiSettingsLocation",
        uiSettings.isEmpty() ? QUrl() : QUrl::fromLocalFile(uiSettings));
```

- [ ] **Step 2: Write the failing scripts**

`desktop/tests/ui/v2_inspector.script`:

```
# Film picker, exposure placement, a tone slider with keyboard + undo, the adaptive
# switch, the scope switch and a section collapse.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:filmSection.open=true
expect:filmCardName.text=No film
click:filmCard@0.5,0.5
wait:300
expect:stockPicker.visible=true
click:stock_cinestill_400d@0.5,0.5
wait:300
expect:engine.stock=cinestill_400d
expect:stockPicker.visible=false
expect:filmCardName.text=CineStill 400D
click:placementSegmented@0.85,0.5
wait:200
expect:engine.filmControls.exposure_placement=auto_balanced
# Collapse Film so every Tone row sits well inside the panel.
click:filmSectionHeader@0.2,0.5
wait:200
expect:filmSection.open=false
click:slider_film_contrast@0.75,0.5
wait:200
expect:engine.filmControls.film_contrast=150
expect:toneSection.edited=true
expect:v2Root.arrowKeysFree=false
key:16777234
wait:150
expect:engine.filmControls.film_contrast=149
key:16777216
wait:150
expect:v2Root.arrowKeysFree=true
key:90+ctrl
wait:300
expect:engine.filmControls.film_contrast=100
expect:slider_film_contrast.value=100
click:slider_shadow_lift@0.5,0.5
wait:150
expect:v2Root.arrowKeysFree=false
click:photoCanvas@0.5,0.5
wait:150
expect:v2Root.arrowKeysFree=true
click:adaptiveSwitch@0.95,0.5
wait:200
expect:engine.filmControls.adaptive=false
click:scope@0.5,0.5
wait:300
expect:scope.mode=vectorscope
click:exposureSectionHeader@0.2,0.5
wait:200
expect:exposureSection.open=false
shot:${TEMP}/v2_inspector.png
quit
```

`desktop/tests/ui/v2_inspector_persist_a.script`:

```
# First launch: collapse Tone, switch the scope. Settings are saved on quit.
expect:toneSection.open=true
click:toneSectionHeader@0.2,0.5
wait:200
expect:toneSection.open=false
click:scope@0.5,0.5
wait:200
expect:scope.mode=vectorscope
quit
```

`desktop/tests/ui/v2_inspector_persist_b.script`:

```
# Second launch with the same settings file: the state came back.
expect:toneSection.open=false
expect:scope.mode=vectorscope
expect:filmSection.open=true
quit
```

`desktop/tests/ui/v2_inspector_lr.script`:

```
# Lightroom Edit-In: the film picker and sliders work; nothing reaches the catalog.
waitfor:engine.hasImage=true,30000
expect:engine.lightroomRoundTrip=true
click:filmCard@0.5,0.5
wait:300
click:stock_cinestill_400d@0.5,0.5
wait:300
expect:engine.stock=cinestill_400d
click:slider_film_contrast@0.75,0.5
wait:300
expect:engine.filmControls.film_contrast=150
wait:1200
expect:editStore.recordCount=0
quit
```

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_inspector.script -Ui v2`
Expected: FAIL from `expect:filmSection.open=true` (`<missing>`) onward.

- [ ] **Step 3: Create `desktop/qml/v2/controls/FlListPopup.qml`**

```qml
import QtQuick
import QtQuick.Controls
import DFEE

// Picker popover: rows of {id, name, detail, group, art}. A row whose group differs
// from the row above gets a group caption. Row objectName "<rowPrefix><id>". Picking
// emits picked(id) and closes; closing hands focus back to the window.
Popup {
    id: pop
    property var rows: []
    property string currentId: ""
    property string rowPrefix: "row_"
    signal picked(string id)
    width: 268
    height: Math.min(list.contentHeight + 12, 420)
    padding: 6
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function indexOf(id) {
        for (let i = 0; i < rows.length; ++i) if (rows[i].id === id) return i;
        return -1;
    }
    onOpened: list.positionViewAtIndex(Math.max(0, indexOf(currentId)), ListView.Contain)
    onClosed: Qt.callLater(function() {
        const w = pop.parent ? pop.parent.Window.window : null;
        if (w && w.returnFocus) w.returnFocus();
    })
    background: Rectangle {
        color: Theme.popover
        radius: Theme.radiusPopover
        border.width: 1
        border.color: Theme.hairline
    }
    contentItem: ListView {
        id: list
        clip: true
        model: pop.rows
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Column {
            id: rowItem
            width: list.width
            readonly property bool hasArt: (modelData.art || "").length > 0
            readonly property bool firstOfGroup: index === 0 || pop.rows[index - 1].group !== modelData.group
            readonly property bool current: modelData.id === pop.currentId
            Text {
                visible: rowItem.firstOfGroup && (modelData.group || "").length > 0
                height: visible ? 26 : 0
                leftPadding: 8
                bottomPadding: 4
                verticalAlignment: Text.AlignBottom
                text: modelData.group || ""
                color: Theme.textCaption
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
            }
            Rectangle {
                objectName: pop.rowPrefix + modelData.id
                width: parent.width
                height: rowItem.hasArt ? 40 : 28
                radius: Theme.radiusControl
                color: rowHover.hovered ? Theme.rowSelected : "transparent"
                HoverHandler { id: rowHover }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { pop.picked(modelData.id); pop.close(); }
                }
                Image {
                    id: art
                    visible: rowItem.hasArt
                    x: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: 28
                    source: modelData.art || ""
                    sourceSize: Qt.size(56, 56)
                    fillMode: Image.PreserveAspectCrop
                }
                Column {
                    anchors.left: rowItem.hasArt ? art.right : parent.left
                    anchors.leftMargin: rowItem.hasArt ? 10 : 8
                    anchors.right: check.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        text: modelData.name
                        elide: Text.ElideRight
                        color: rowItem.current ? Theme.text : Theme.textBody
                        font.pixelSize: Theme.fontBody
                        font.weight: rowItem.current ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        visible: (modelData.detail || "").length > 0
                        width: parent.width
                        text: modelData.detail || ""
                        elide: Text.ElideRight
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontCaption
                    }
                }
                FlIcon {
                    id: check
                    visible: rowItem.current
                    name: "check"
                    size: 14
                    color: Theme.accent
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
```

- [ ] **Step 4: Create `desktop/qml/v2/inspector/FlScope.qml`**

```qml
import QtQuick
import DFEE

// The inspector's pinned scope: histogram (default) or vectorscope; a click asks the
// owner to switch. Drawing ported from v1 Main.qml (Histogram, Vectorscope).
Rectangle {
    id: scope
    property string mode: "histogram"     // "histogram" | "vectorscope"
    signal switchRequested()
    width: parent ? parent.width : 268
    height: mode === "histogram" ? 64 : 152
    radius: Theme.radiusTile
    color: Theme.inset
    clip: true
    Behavior on height { NumberAnimation { duration: Theme.motionNormal; easing.type: Easing.OutCubic } }

    Canvas {
        id: hist
        anchors.fill: parent
        anchors.margins: 4
        visible: scope.mode === "histogram"
        property var hr: engine.histogramR
        property var hg: engine.histogramG
        property var hb: engine.histogramB
        onHrChanged: requestPaint()
        onHgChanged: requestPaint()
        onHbChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const w = width, h = height;
            if (!hr || hr.length < 2) return;
            const n = hr.length;
            let mx = 1;
            for (let i = 1; i < n - 1; ++i) mx = Math.max(mx, hr[i], hg[i], hb[i]);
            function channel(arr, style) {
                ctx.beginPath();
                ctx.moveTo(0, h);
                for (let i = 0; i < n; ++i) {
                    const v = Math.min(1, Math.sqrt(arr[i] / mx));
                    ctx.lineTo(i / (n - 1) * w, h - v * h);
                }
                ctx.lineTo(w, h);
                ctx.closePath();
                ctx.fillStyle = style;
                ctx.fill();
            }
            ctx.globalCompositeOperation = "lighter";
            channel(hb, "rgba(90,150,255,0.32)");
            channel(hg, "rgba(90,210,120,0.30)");
            channel(hr, "rgba(255,90,80,0.32)");
            ctx.globalCompositeOperation = "source-over";
        }
    }

    // Rec.709 Cb/Cr density from the worker (64×64 cells), colored by each cell's hue.
    Canvas {
        id: vec
        anchors.fill: parent
        anchors.margins: 8
        visible: scope.mode === "vectorscope"
        property var samples: engine.vectorscope
        onSamplesChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (!samples || samples.length !== 4096) return;
            const side = Math.min(width, height);
            const ox = width * 0.5;
            const oy = height * 0.5;
            const radius = side * 0.455;
            ctx.strokeStyle = "rgba(220,220,226,0.12)";
            ctx.lineWidth = 1;
            ctx.beginPath(); ctx.arc(ox, oy, radius, 0, Math.PI * 2); ctx.stroke();
            ctx.beginPath(); ctx.arc(ox, oy, radius * 0.5, 0, Math.PI * 2); ctx.stroke();
            ctx.beginPath();
            ctx.moveTo(ox - radius, oy); ctx.lineTo(ox + radius, oy);
            ctx.moveTo(ox, oy - radius); ctx.lineTo(ox, oy + radius);
            ctx.stroke();
            let maxCount = 1;
            for (let i = 0; i < samples.length; ++i) maxCount = Math.max(maxCount, samples[i]);
            const logMax = Math.log(1 + maxCount);
            const cells = 64;
            const cell = (radius * 2) / cells;
            ctx.globalCompositeOperation = "lighter";
            for (let yy = 0; yy < cells; ++yy) {
                for (let xx = 0; xx < cells; ++xx) {
                    const count = samples[yy * cells + xx];
                    if (count <= 0) continue;
                    const dx = (xx + 0.5) / cells * 2 - 1;
                    const dy = (yy + 0.5) / cells * 2 - 1;
                    if (dx * dx + dy * dy > 1) continue;
                    const density = Math.log(1 + count) / logMax;
                    const cb = dx * 0.5, cr = -dy * 0.5;
                    let r = 0.5 + 1.5748 * cr, b = 0.5 + 1.8556 * cb;
                    let g = (0.5 - 0.2126 * r - 0.0722 * b) / 0.7152;
                    r = Math.max(0, Math.min(1, r)); g = Math.max(0, Math.min(1, g)); b = Math.max(0, Math.min(1, b));
                    ctx.fillStyle = "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + ","
                        + Math.round(b * 255) + "," + Math.min(0.5, 0.05 + 0.42 * density) + ")";
                    ctx.beginPath();
                    ctx.arc(ox - radius + (xx + 0.5) * cell, oy - radius + (yy + 0.5) * cell,
                            Math.max(1.1, cell * (0.8 + density * 0.45)), 0, Math.PI * 2);
                    ctx.fill();
                }
            }
            ctx.globalCompositeOperation = "source-over";
        }
    }

    Text {
        anchors.centerIn: parent
        visible: !engine.hasImage
        text: scope.mode === "histogram" ? "Histogram" : "Vectorscope"
        color: Theme.textTertiary
        font.pixelSize: Theme.fontCaption
    }
    HoverHandler { id: hover }
    MouseArea { anchors.fill: parent; onClicked: scope.switchRequested() }
    FlTip {
        visible: hover.hovered
        text: scope.mode === "histogram" ? "Click for the vectorscope" : "Click for the histogram"
    }
}
```

- [ ] **Step 5: Create the three sections**

`desktop/qml/v2/inspector/FlFilmSection.qml`:

```qml
import QtQuick
import DFEE

// Film: the stock card (box art, name, "type · ISO", chevron) that opens the stock
// picker, the stock's one-line look, and the strength of its tone curve.
FlInspectorSection {
    id: sec
    title: "Film"
    group: "film"
    readonly property var stockInfo: {
        const m = engine.stockModel;
        for (let i = 0; i < m.length; ++i) if (m[i].id === engine.stock) return m[i];
        return { id: "none", name: "None", typeLabel: "", iso: 0, blurb: "" };
    }
    readonly property bool hasFilm: stockInfo.id !== "none"
    summary: hasFilm ? stockInfo.name : "No film"

    Rectangle {
        id: card
        objectName: "filmCard"
        width: parent.width
        height: 60
        radius: Theme.radiusCard
        color: cardHover.hovered ? "#303033" : Theme.card
        border.width: 1
        border.color: "#0dffffff"
        HoverHandler { id: cardHover }
        MouseArea { anchors.fill: parent; onClicked: picker.open() }
        Rectangle {
            id: art
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            height: 40
            radius: 5
            color: Theme.inset
            clip: true
            Image {
                anchors.fill: parent
                visible: sec.hasFilm
                source: sec.hasFilm ? "qrc:/boxart/" + sec.stockInfo.id + ".svg" : ""
                sourceSize: Qt.size(80, 80)
                fillMode: Image.PreserveAspectCrop
            }
            FlIcon {
                anchors.centerIn: parent
                visible: !sec.hasFilm
                name: "film-strip"
                size: 18
                color: Theme.textTertiary
            }
        }
        Column {
            anchors.left: art.right
            anchors.leftMargin: 12
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                objectName: "filmCardName"
                width: parent.width
                text: sec.hasFilm ? sec.stockInfo.name : "No film"
                color: Theme.text
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: sec.hasFilm
                    ? sec.stockInfo.typeLabel + (sec.stockInfo.iso > 0 ? " · ISO " + sec.stockInfo.iso : "")
                    : "Choose a film stock"
                color: Theme.textCaption
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
        FlIcon {
            id: chevron
            name: "caret-right"
            size: 12
            color: Theme.textTertiary
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
        }
        FlListPopup {
            id: picker
            objectName: "stockPicker"
            rowPrefix: "stock_"
            y: card.height + 4
            currentId: engine.stock
            rows: engine.stockModel.map(s => ({
                id: s.id,
                name: s.name,
                group: s.id === "none" ? "" : (s.groupLabel || ""),
                detail: s.id === "none" ? "No film look"
                    : ((s.iso > 0 ? "ISO " + s.iso : "") + (s.iso > 0 && s.blurb ? " · " : "") + (s.blurb || "")),
                art: s.id === "none" ? "" : "qrc:/boxart/" + s.id + ".svg"
            }))
            onPicked: (id) => engine.stock = id
        }
    }
    Text {
        visible: sec.hasFilm && (sec.stockInfo.blurb || "").length > 0
        width: parent.width
        text: sec.stockInfo.blurb || ""
        wrapMode: Text.WordWrap
        color: Theme.textCaption
        font.pixelSize: Theme.fontCaption
        lineHeight: 1.3
    }
    FlFilmSlider {
        controlKey: "profile_strength"
        label: "Strength"
        from: 0; to: 200; neutral: 100
        suffix: "%"
        tip: "Master strength of this stock's authored tone curve. At 100, you get the stock's intended baseline; lower softens its toe, midtones, and shoulder together, while higher reinforces them within that stock's safe limits."
    }
}
```

`desktop/qml/v2/inspector/FlExposureSection.qml`:

```qml
import QtQuick
import DFEE

// Exposure: where the scene sits before the film (As shot / Auto balanced) and the
// film's own exposure.
FlInspectorSection {
    id: sec
    title: "Exposure"
    group: "exposure"
    readonly property bool asShot: engine.filmControls.exposure_placement === "as_shot"
    summary: asShot ? "As shot" : "Auto balanced"

    Item {
        width: parent.width
        height: Theme.segmentHeight + 4
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Scene"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        FlSegmented {
            objectName: "placementSegmented"
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            model: ["As shot", "Auto balanced"]
            currentIndex: sec.asShot ? 0 : 1
            onActivated: (i) => engine.setFilmControl("exposure_placement", i === 0 ? "as_shot" : "auto_balanced")
        }
        HoverHandler { id: sceneHover }
        FlTip {
            visible: sceneHover.hovered
            text: "Auto balanced sets a stock-aware starting exposure for the scene. As shot preserves the RAW's own exposure placement before the film response."
        }
    }
    FlFilmSlider {
        controlKey: "film_exposure_ev"
        label: "Film exposure"
        from: -3; to: 3; stepSize: 0.05; decimals: 2; bipolar: true
        suffix: " EV"
        tip: "Virtual exposure (in stops) reaching the film before its tone and color response — like rating the stock faster or slower."
    }
}
```

`desktop/qml/v2/inspector/FlToneSection.qml`:

```qml
import QtQuick
import DFEE

// Tone: the film's tone curve — rolloff, contrast, shadow lift — and how much of the
// photo's developed tone it keeps.
FlInspectorSection {
    title: "Tone"
    group: "tone"
    FlFilmSlider {
        controlKey: "highlight_rolloff"; label: "Highlight rolloff"
        from: 0; to: 200; neutral: 100
        tip: "How gently the brightest tones roll off instead of clipping — higher for softer, glowier film highlights."
    }
    FlFilmSlider {
        controlKey: "film_contrast"; label: "Film contrast"
        from: 0; to: 200; neutral: 100
        tip: "The punch of the film's tone curve — higher for a deeper, more contrasty look; lower for flatter."
    }
    FlFilmSlider {
        controlKey: "shadow_lift"; label: "Shadow lift"
        from: -100; to: 100; bipolar: true
        tip: "Base-fog fade in the deepest shadows, the way negative film never quite reaches pure black. Forward lifts shadows into a soft matte; back deepens them toward true black."
    }
    FlFilmSlider {
        controlKey: "rendered_input"; label: "Preserve rendered tone"
        from: 0; to: 100; neutral: 80
        tip: "Higher keeps the photo's developed exposure and tone and applies the film look gently, protecting skies and bright highlights from being pushed again. Lower lets the film's full tone curve through."
    }
    FlSwitch {
        objectName: "adaptiveSwitch"
        label: "Adaptive scene tone"
        checked: engine.filmControls.adaptive === true
        onToggled: engine.setFilmControl("adaptive", !checked)
        tip: "Lets the film read the scene and auto-adjust its tone for flat, high-dynamic-range files. Turn off for a fixed, predictable response."
    }
}
```

- [ ] **Step 6: Create `desktop/qml/v2/inspector/FlInspector.qml`**

```qml
import QtQuick
import QtQuick.Controls
import QtCore
import DFEE

// Right-hand inspector: the pinned scope, then the sections in darkroom order in one
// scrolling column. Open/closed sections and the scope mode persist (QtCore Settings,
// category "inspector"; UI tests point `location` at a throwaway ini).
Rectangle {
    id: insp
    objectName: "inspector"
    color: Theme.panel

    Settings {
        id: prefs
        category: "inspector"
        location: uiSettingsLocation
        property bool filmOpen: true
        property bool exposureOpen: true
        property bool toneOpen: true
        property bool colorOpen: false
        property bool grainOpen: false
        property bool printOpen: false
        property bool fineTuneOpen: false
        property string scopeMode: "histogram"
    }

    Item {
        id: scopeBox
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: scope.height + 28
        FlScope {
            id: scope
            objectName: "scope"
            x: 16
            y: 14
            width: parent.width - 32
            mode: prefs.scopeMode
            onSwitchRequested: prefs.scopeMode = prefs.scopeMode === "histogram" ? "vectorscope" : "histogram"
        }
        FlHairline { anchors.bottom: parent.bottom }
    }

    Flickable {
        id: flick
        objectName: "inspectorFlick"
        anchors.top: scopeBox.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: sections.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Column {
            id: sections
            width: flick.width
            FlFilmSection {
                objectName: "filmSection"
                open: prefs.filmOpen
                onToggled: prefs.filmOpen = !prefs.filmOpen
            }
            FlExposureSection {
                objectName: "exposureSection"
                open: prefs.exposureOpen
                onToggled: prefs.exposureOpen = !prefs.exposureOpen
            }
            FlToneSection {
                objectName: "toneSection"
                open: prefs.toneOpen
                onToggled: prefs.toneOpen = !prefs.toneOpen
            }
        }
    }
    Rectangle { anchors.left: parent.left; width: 1; height: parent.height; color: Theme.hairline }
}
```

- [ ] **Step 7: Put the inspector in MainV2; canvas clicks return focus**

In `desktop/qml/v2/MainV2.qml` replace the whole `Rectangle { id: inspectorSlot … }` block with:

```qml
    FlInspector {
        id: inspector
        anchors.right: parent.right
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        width: Theme.inspectorWidth
    }
```

change the canvas's `anchors.right: inspectorSlot.left` to `anchors.right: inspector.left`, and add to the `FlCanvas` block:

```qml
        onBackgroundPressed: root.returnFocus()
```

In `desktop/qml/v2/canvas/FlCanvas.qml` add `signal backgroundPressed()` after `signal compareModeRequested(int mode)`, and make the first line of the zoom/pan `MouseArea`'s `onPressed` handler (the one with `if (!imageArea.zoomed) { mouse.accepted = false; return; }`):

```qml
                canvas.backgroundPressed();
```

Register in `desktop/CMakeLists.txt` `QML_FILES`:

```cmake
        qml/v2/controls/FlListPopup.qml
        qml/v2/inspector/FlInspector.qml
        qml/v2/inspector/FlScope.qml
        qml/v2/inspector/FlFilmSection.qml
        qml/v2/inspector/FlExposureSection.qml
        qml/v2/inspector/FlToneSection.qml
```

- [ ] **Step 8: Build and run**

Run: `cmake --build desktop/out/build --config Release`, then:
- `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_inspector.script -Ui v2`
- persistence (two launches, one settings file):
  ```powershell
  $ini = Join-Path $env:TEMP "filmlab-persist-test.ini"; Remove-Item $ini -ErrorAction SilentlyContinue
  powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_inspector_persist_a.script -Ui v2 -UiSettings $ini
  powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_inspector_persist_b.script -Ui v2 -UiSettings $ini
  Remove-Item $ini
  ```
- Lightroom: `powershell -ExecutionPolicy Bypass -Command "& ./desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_inspector_lr.script -Ui v2 -AppArgs @('--lightroom-edit','`${SAMPLE_TIFF}')"`
- the 1A v2 scripts (`v2_keys`, `v2_canvas`, `v2_crop`) with `-Ui v2`.

Expected: every run `failures=0`. Check `v2_inspector.png`: vectorscope well on top, Film collapsed with summary "CineStill 400D", Exposure collapsed with summary "Auto balanced", Tone open with an edited dot.

- [ ] **Step 9: Commit**

```bash
git add desktop/qml/v2 desktop/src/main.cpp desktop/CMakeLists.txt desktop/tests/run_ui.ps1 desktop/tests/ui/v2_inspector.script desktop/tests/ui/v2_inspector_persist_a.script desktop/tests/ui/v2_inspector_persist_b.script desktop/tests/ui/v2_inspector_lr.script
git commit -m "feat(desktop): v2 inspector with scope, film card and picker, exposure and tone"
```

---

### Task 4: Color, Grain & light, Print

**Files:**
- Create: `desktop/qml/v2/controls/FlColorWheel.qml`, `desktop/qml/v2/controls/FlToneSwatch.qml`
- Create: `desktop/qml/v2/inspector/FlColorSection.qml`, `FlGrainLightSection.qml`, `FlPrintSection.qml`
- Create: `desktop/tests/ui/v2_inspector_color.script`
- Modify: `desktop/qml/v2/inspector/FlInspector.qml`, `desktop/CMakeLists.txt`

**Interfaces:**
- Consumes: `engine.setGradeColor` (Task 1); `FlInspectorSection`, `FlFilmSlider`, `FlSwitch`, `FlGroupLabel` (Task 2); `FlListPopup`, `FlInspector` prefs (Task 3); `engine.currentStockMonochrome`, `engine.grainResolving`, `engine.setAutoGrain(bool)`, `engine.printStockNames`, `engine.printStockIdAt(i)`.
- Produces:
  - `FlColorWheel { zone; label; diameter }` — disc objectName `<wheel objectName>Disc`; drags call `engine.setGradeColor(zone, hue, sat)`; double-click clears.
  - `FlToneSwatch { zone; label }` — swatch circle + label; click opens a popover (objectName `swatchPopup_<zone>`) holding an `FlColorWheel` named `swatchWheel_<zone>`.
  - Sections `colorSection`, `grainSection`, `printSection` (headers `<name>Header`), switch `grainAutoSwitch`, print selector `printField`, picker `printPicker` (rows `print_<id>`), swatches `swatch_shadow`, `swatch_highlight`.

- [ ] **Step 1: Write the failing script `desktop/tests/ui/v2_inspector_color.script`**

```
# B&W stocks dim film color (white balance stays live); Auto grain; print picker;
# the shadow tint swatch opens a wheel that edits in one history step.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
stock:tri_x_400
wait:200
expect:engine.currentStockMonochrome=true
expect:ctl_film_color_density.available=false
expect:ctl_temp.available=true
stock:portra_400
wait:200
expect:ctl_film_color_density.available=true
expect:grainAutoSwitch.checked=true
expect:ctl_grain_strength.autoText=Auto
expect:ctl_grain_strength.available=false
expect:ctl_print_strength.available=false
expect:engine.history.length=2
click:filmSectionHeader@0.2,0.5
click:exposureSectionHeader@0.2,0.5
click:toneSectionHeader@0.2,0.5
click:printSectionHeader@0.2,0.5
wait:300
click:printField@0.5,0.5
wait:300
expect:printPicker.visible=true
click:print_kodak_2383@0.5,0.5
wait:300
expect:engine.filmControls.print_stock=kodak_2383
expect:ctl_print_strength.available=true
expect:printSection.summary=Kodak Vision 2383
click:printSectionHeader@0.2,0.5
click:colorSectionHeader@0.2,0.5
wait:300
click:swatch_shadow@0.2,0.5
wait:300
expect:swatchPopup_shadow.visible=true
click:swatchWheel_shadowDisc@0.9,0.5
wait:200
expect:engine.filmControls.cg_shadow_hue=0
expect:engine.filmControls.cg_shadow_sat=80
expect:engine.history.length=4
click:swatchWheel_shadowDisc@0.5,0.1
wait:200
expect:engine.filmControls.cg_shadow_hue=90
expect:engine.history.length=4
key:16777216
wait:200
expect:swatchPopup_shadow.visible=false
shot:${TEMP}/v2_inspector_color.png
quit
```

(History: Import, film (the two stock changes coalesce), print stock, shadow tint (both wheel clicks coalesce) = 4. `Kodak Vision 2383` is `print_stock_name` in `profiles/print_stocks/kodak_2383.yaml`.)

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_inspector_color.script -Ui v2`
Expected: FAIL from `expect:ctl_film_color_density.available=false` (`<missing>`).

- [ ] **Step 2: Create `desktop/qml/v2/controls/FlColorWheel.qml`**

```qml
import QtQuick
import DFEE

// Hue/saturation wheel for one grading zone (cg_<zone>_hue / _sat), written as one
// step through engine.setGradeColor. Drag from the centre to tint; double-click
// clears. Ported from v1 Main.qml ColorWheel.
Item {
    id: wheel
    property string zone: ""
    property string label: ""
    property int diameter: 96
    width: diameter
    height: diameter + 20
    readonly property real hueVal: Number(engine.filmControls["cg_" + zone + "_hue"])
    readonly property real satVal: Number(engine.filmControls["cg_" + zone + "_sat"])

    function pick(mx, my) {
        const c = wheel.diameter / 2;
        const dx = mx - c, dy = my - c;
        let ang = Math.atan2(-dy, dx) * 180 / Math.PI;
        if (ang < 0) ang += 360;
        const rad = Math.min(Math.sqrt(dx * dx + dy * dy) / c, 1.0);
        engine.setGradeColor(wheel.zone, Math.round(ang), Math.round(rad * 100));
    }

    Rectangle {
        id: disc
        objectName: wheel.objectName.length > 0 ? wheel.objectName + "Disc" : ""
        width: wheel.diameter
        height: wheel.diameter
        radius: wheel.diameter / 2
        anchors.horizontalCenter: parent.horizontalCenter
        color: Theme.inset
        border.width: 1
        border.color: Theme.hairline
        clip: true
        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const w = width, cx = w / 2, cy = w / 2, r = w / 2;
                for (let a = 0; a < 360; a += 3) {
                    ctx.beginPath();
                    ctx.moveTo(cx, cy);
                    ctx.arc(cx, cy, r, (-(a + 3)) * Math.PI / 180, (-a) * Math.PI / 180, false);
                    ctx.closePath();
                    ctx.fillStyle = "hsl(" + a + ",60%,50%)";
                    ctx.fill();
                }
                const g = ctx.createRadialGradient(cx, cy, 0, cx, cy, r);
                g.addColorStop(0.0, "rgba(22,22,24,1.0)");
                g.addColorStop(0.35, "rgba(22,22,24,0.4)");
                g.addColorStop(1.0, "rgba(22,22,24,0.0)");
                ctx.fillStyle = g;
                ctx.beginPath();
                ctx.arc(cx, cy, r, 0, 2 * Math.PI);
                ctx.fill();
            }
        }
        Rectangle {
            width: 13
            height: 13
            radius: 7
            border.width: 2
            border.color: Theme.knob
            x: disc.width / 2 + Math.cos(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.width / 2 - 9) - width / 2
            y: disc.height / 2 - Math.sin(wheel.hueVal * Math.PI / 180) * (wheel.satVal / 100) * (disc.height / 2 - 9) - height / 2
            color: wheel.satVal > 0
                ? Qt.hsla((((wheel.hueVal % 360) + 360) % 360) / 360, Math.min(wheel.satVal / 100, 1.0), 0.55, 1.0)
                : Theme.control
        }
        MouseArea {
            id: drag
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: (mouse) => wheel.pick(mouse.x, mouse.y)
            onPositionChanged: (mouse) => { if (pressed) wheel.pick(mouse.x, mouse.y); }
            onDoubleClicked: engine.setGradeColor(wheel.zone, 0, 0)
        }
        HoverHandler { id: wheelHover }
        FlTip {
            visible: wheelHover.hovered && !drag.pressed
            text: "Tints the " + wheel.label.toLowerCase() + " — drag from the centre to add color, double-click to reset."
        }
    }
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: wheel.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontCaption
    }
}
```

- [ ] **Step 3: Create `desktop/qml/v2/controls/FlToneSwatch.qml`**

```qml
import QtQuick
import QtQuick.Controls
import DFEE

// A grading zone's tint as a swatch; a click opens that zone's wheel in a popover.
// A second view of the Fine-tune color grading wheels (same controls).
Item {
    id: sw
    property string zone: ""
    property string label: ""
    readonly property real hueVal: Number(engine.filmControls["cg_" + zone + "_hue"])
    readonly property real satVal: Number(engine.filmControls["cg_" + zone + "_sat"])
    width: 120
    height: 28
    Rectangle {
        id: dot
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20
        radius: 10
        color: sw.satVal > 0
            ? Qt.hsla((((sw.hueVal % 360) + 360) % 360) / 360, Math.min(sw.satVal / 100, 1.0), 0.55, 1.0)
            : Theme.control
        border.width: 1
        border.color: Theme.hairline
    }
    Text {
        anchors.left: dot.right
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: sw.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontLabel
    }
    MouseArea { anchors.fill: parent; onClicked: pop.open() }
    Popup {
        id: pop
        objectName: "swatchPopup_" + sw.zone
        y: sw.height + 6
        padding: 14
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: Qt.callLater(function() {
            const w = sw.Window.window;
            if (w && w.returnFocus) w.returnFocus();
        })
        background: Rectangle {
            color: Theme.popover
            radius: Theme.radiusPopover
            border.width: 1
            border.color: Theme.hairline
        }
        contentItem: FlColorWheel {
            objectName: "swatchWheel_" + sw.zone
            zone: sw.zone
            label: sw.label
            diameter: 120
        }
    }
}
```

- [ ] **Step 4: Create the three sections**

`desktop/qml/v2/inspector/FlColorSection.qml`:

```qml
import QtQuick
import DFEE

// Color: the film's color character (dimmed on B&W stocks), split toning, then white
// balance and saturation.
FlInspectorSection {
    id: sec
    title: "Color"
    group: "color"
    readonly property bool mono: engine.currentStockMonochrome
    summary: mono ? "B&W film" : ""

    FlFilmSlider {
        controlKey: "film_color_density"; label: "Color density"
        from: 0; to: 200; neutral: 100; available: !sec.mono
        tip: "How dense and cohesive the film's colors are — forward for richer, deeper, more film-like color; back for a thinner, more digital look."
    }
    FlFilmSlider {
        controlKey: "emulsion_color_density"; label: "Color boost"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "Overall saturation of the stock's color dyes — forward for punchier color, back for a muted look."
    }
    FlFilmSlider {
        controlKey: "crossover"; label: "Crossover"
        from: 0; to: 200; neutral: 100; available: !sec.mono
        tip: "Strength of the film's natural color crossover — the way its dye layers render cool shadows and warm highlights (and shift greens/blues). 100 is the stock's authentic amount; higher exaggerates it, 0 removes it."
    }
    FlFilmSlider {
        controlKey: "highlight_color_hold"; label: "Highlight saturation"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "How much color survives in the highlights — back bleaches bright areas toward clean white (rescues blown, over-warm highlights)."
    }
    FlFilmSlider {
        controlKey: "shadow_color_retention"; label: "Shadow saturation"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "How much color survives in the shadows — forward keeps darks colorful, back mutes them toward neutral."
    }
    FlFilmSlider {
        controlKey: "cg_crossbalance"; label: "Split toning"
        from: -100; to: 100; bipolar: true; available: !sec.mono
        tip: "Adds your own split-tone on top of the film — forward for teal shadows and warm highlights, back for the inverse."
    }
    Row {
        width: parent.width
        spacing: 12
        opacity: sec.mono ? 0.4 : 1.0
        enabled: !sec.mono
        FlToneSwatch { objectName: "swatch_shadow"; zone: "shadow"; label: "Shadow tint" }
        FlToneSwatch { objectName: "swatch_highlight"; zone: "highlight"; label: "Highlight tint" }
    }
    FlGroupLabel { text: "White balance" }
    FlFilmSlider {
        controlKey: "temp"; label: "Temperature"
        from: -100; to: 100; bipolar: true
        tip: "White balance warmth — forward warms (more amber), back cools (more blue)."
    }
    FlFilmSlider {
        controlKey: "tint"; label: "Tint"
        from: -100; to: 100; bipolar: true
        tip: "White balance green/magenta — forward toward magenta, back toward green."
    }
    FlFilmSlider {
        controlKey: "vibrance"; label: "Vibrance"
        from: -100; to: 100; bipolar: true
        tip: "Smart saturation that protects skin tones and already-saturated colors."
    }
    FlFilmSlider {
        controlKey: "saturation"; label: "Saturation"
        from: -100; to: 100; bipolar: true
        tip: "Overall color intensity, applied evenly to all hues."
    }
}
```

`desktop/qml/v2/inspector/FlGrainLightSection.qml`:

```qml
import QtQuick
import DFEE

// Grain & light: grain (matched to film speed, or by hand), halation and bloom.
FlInspectorSection {
    id: sec
    title: "Grain & light"
    group: "grain_light"
    readonly property bool autoGrain: engine.filmControls.grain_auto === true
    summary: autoGrain ? "Auto grain" : ""

    FlGroupLabel { text: "Grain" }
    FlSwitch {
        objectName: "grainAutoSwitch"
        label: engine.grainResolving ? "Resolving stock grain…" : "Match grain to film speed"
        checked: sec.autoGrain
        enabled: !engine.grainResolving
        onToggled: engine.setAutoGrain(!checked)
        tip: "Automatically matches grain to the film speed (ISO) and stock. Turn off to seed and edit Strength, Size and Roughness manually."
    }
    FlFilmSlider {
        controlKey: "grain_strength"; label: "Strength"
        from: 0; to: 2; stepSize: 0.05; decimals: 2
        available: !sec.autoGrain; autoValue: sec.autoGrain
        tip: "How visible the grain is — the apparent film speed."
    }
    FlFilmSlider {
        controlKey: "grain_size"; label: "Size"
        from: 0.1; to: 2; stepSize: 0.05; decimals: 2; neutral: 0.6
        available: !sec.autoGrain; autoValue: sec.autoGrain
        tip: "Particle size — larger reads as a coarser, higher-ISO stock."
    }
    FlFilmSlider {
        controlKey: "grain_roughness"; label: "Roughness"
        from: 0; to: 1; stepSize: 0.05; decimals: 2; neutral: 0.5
        available: !sec.autoGrain; autoValue: sec.autoGrain
        tip: "Irregularity of the grain clumping — higher is grittier and more organic, lower is finer and more even."
    }
    FlGroupLabel { text: "Halation" }
    FlFilmSlider {
        controlKey: "halation_strength"; label: "Strength"
        from: 0; to: 200; neutral: 100
        tip: "Strength of the warm red-orange glow that bleeds around bright edges against dark backgrounds."
    }
    FlFilmSlider {
        controlKey: "halation_threshold"; label: "Threshold"
        from: 0; to: 100; neutral: 50
        tip: "How bright an area must be before it starts to halate — higher restricts the glow to the brightest highlights."
    }
    FlGroupLabel { text: "Bloom" }
    FlFilmSlider {
        controlKey: "bloom"; label: "Amount"
        from: 0; to: 100
        tip: "Soft optical glow spreading from the highlights, like light diffusing in the lens."
    }
}
```

`desktop/qml/v2/inspector/FlPrintSection.qml`:

```qml
import QtQuick
import DFEE

// Print: an optional print stock (the paper the negative is printed on), its
// strength, color head and contrast. Controls dim until a print stock is chosen.
FlInspectorSection {
    id: sec
    title: "Print"
    group: "print"
    readonly property string printId: engine.filmControls.print_stock || "none"
    readonly property bool active: printId !== "none"
    readonly property string printName: {
        const names = engine.printStockNames;
        for (let i = 0; i < names.length; ++i) if (engine.printStockIdAt(i) === sec.printId) return names[i];
        return "None";
    }
    summary: active ? printName : "Off"

    Item {
        width: parent.width
        height: Theme.controlHeight
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: "Print stock"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontLabel
        }
        Rectangle {
            id: field
            objectName: "printField"
            anchors.right: parent.right
            width: 172
            height: parent.height
            radius: Theme.radiusControl
            color: fieldHover.hovered ? Theme.controlHover : Theme.control
            HoverHandler { id: fieldHover }
            MouseArea { anchors.fill: parent; onClicked: printPicker.open() }
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: caret.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: sec.printName
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                elide: Text.ElideRight
            }
            FlIcon {
                id: caret
                name: "caret-up-down"
                size: 12
                color: Theme.textCaption
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
            }
            FlListPopup {
                id: printPicker
                objectName: "printPicker"
                rowPrefix: "print_"
                width: 240
                x: field.width - width
                y: field.height + 4
                currentId: sec.printId
                rows: engine.printStockNames.map((name, i) => ({ id: engine.printStockIdAt(i), name: name, group: "", detail: "", art: "" }))
                onPicked: (id) => engine.setFilmControl("print_stock", id)
            }
        }
    }
    FlFilmSlider {
        controlKey: "print_strength"; label: "Print strength"
        from: 0; to: 1; stepSize: 0.05; decimals: 2; neutral: 1.0; available: sec.active
        tip: "Blend between the source and the selected print material. One is the calibrated stock response."
    }
    FlFilmSlider {
        controlKey: "print_c"; label: "Color head: cyan"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "A bounded cyan printer-timing correction. It cools the printable mid-scale without clipping red."
    }
    FlFilmSlider {
        controlKey: "print_m"; label: "Color head: magenta"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "A bounded magenta printer-timing correction. It shifts the printable mid-scale without clipping green."
    }
    FlFilmSlider {
        controlKey: "print_y"; label: "Color head: yellow"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "A bounded yellow printer-timing correction. It warms the printable mid-scale without clipping blue."
    }
    FlFilmSlider {
        controlKey: "print_contrast"; label: "Print contrast"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "Adjust the middle slope of the selected print material while retaining its toe and shoulder."
    }
    FlFilmSlider {
        controlKey: "print_black_point"; label: "Paper black"
        from: -100; to: 100; bipolar: true; available: sec.active
        tip: "Positive opens the paper base and lower shadows; negative produces a denser, tighter print toe. It never globally lifts or clips the image."
    }
}
```

- [ ] **Step 5: Add the sections to `FlInspector.qml`**

Inside `Column { id: sections … }`, after the `FlToneSection` block:

```qml
            FlColorSection {
                objectName: "colorSection"
                open: prefs.colorOpen
                onToggled: prefs.colorOpen = !prefs.colorOpen
            }
            FlGrainLightSection {
                objectName: "grainSection"
                open: prefs.grainOpen
                onToggled: prefs.grainOpen = !prefs.grainOpen
            }
            FlPrintSection {
                objectName: "printSection"
                open: prefs.printOpen
                onToggled: prefs.printOpen = !prefs.printOpen
            }
```

Register in `desktop/CMakeLists.txt` `QML_FILES`:

```cmake
        qml/v2/controls/FlColorWheel.qml
        qml/v2/controls/FlToneSwatch.qml
        qml/v2/inspector/FlColorSection.qml
        qml/v2/inspector/FlGrainLightSection.qml
        qml/v2/inspector/FlPrintSection.qml
```

- [ ] **Step 6: Build and run**

Run: `cmake --build desktop/out/build --config Release`, the Step 1 command, then `v2_inspector.script` and `v2_groups.script` with `-Ui v2`.
Expected: `failures=0` for all. Check `v2_inspector_color.png`: Color open with the shadow swatch tinted, film-color sliders bright on Portra.

- [ ] **Step 7: Commit**

```bash
git add desktop/qml/v2 desktop/CMakeLists.txt desktop/tests/ui/v2_inspector_color.script
git commit -m "feat(desktop): v2 inspector color, grain and light, and print sections"
```

---

### Task 5: Fine-tune and the full-coverage check

**Files:**
- Create: `desktop/qml/v2/inspector/FlFineTuneSection.qml`
- Create: `desktop/tests/ui/v2_coverage.script`
- Modify: `desktop/qml/v2/inspector/FlInspector.qml`, `desktop/src/UiScript.cpp` (`set:` step), `desktop/CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 2–4 controls; `FlColorWheel`.
- Produces: section `fineTuneSection` with `property int hslIndex` (0 Hue, 1 Saturation, 2 Luminance), `hslSegmented`, rows `ctl_hsl_<band>_<h|s|l>`, wheels `wheel_shadow|midtone|highlight|global`; UiScript step `set:<object>.<property>=<value>` (sets a QObject property; value parsed like `control:`).

- [ ] **Step 1: Add the `set:` step to `desktop/src/UiScript.cpp`**

After the `call` branch:

```cpp
    } else if (verb == QLatin1String("set")) {  // set:<object>.<property>=<value>
        const QString target = arg.section('=', 0, 0);
        QObject* obj = resolveRoot(*s, target.section('.', 0, 0));
        if (!obj || !obj->setProperty(target.section('.', 1).toUtf8().constData(),
                                      parseValue(arg.section('=', 1)))) {
            fail(*s, step, "cannot set");
        }
```

- [ ] **Step 2: Write the failing coverage script `desktop/tests/ui/v2_coverage.script`**

```
# Every film control (geometry aside: crop mode, plan 1C) has a home in the v2
# inspector.
waitfor:inspector.visible=true,5000
expect:placementSegmented.currentIndex=0
expect:adaptiveSwitch.checked=true
expect:grainAutoSwitch.checked=true
expect:printField.objectName=printField
expect:ctl_profile_strength.controlKey=profile_strength
expect:ctl_film_exposure_ev.controlKey=film_exposure_ev
expect:ctl_rendered_input.controlKey=rendered_input
expect:ctl_highlight_rolloff.controlKey=highlight_rolloff
expect:ctl_film_contrast.controlKey=film_contrast
expect:ctl_shadow_lift.controlKey=shadow_lift
expect:ctl_crossover.controlKey=crossover
expect:ctl_film_color_density.controlKey=film_color_density
expect:ctl_emulsion_color_density.controlKey=emulsion_color_density
expect:ctl_highlight_color_hold.controlKey=highlight_color_hold
expect:ctl_shadow_color_retention.controlKey=shadow_color_retention
expect:ctl_cg_crossbalance.controlKey=cg_crossbalance
expect:ctl_temp.controlKey=temp
expect:ctl_tint.controlKey=tint
expect:ctl_vibrance.controlKey=vibrance
expect:ctl_saturation.controlKey=saturation
expect:ctl_grain_strength.controlKey=grain_strength
expect:ctl_grain_size.controlKey=grain_size
expect:ctl_grain_roughness.controlKey=grain_roughness
expect:ctl_halation_strength.controlKey=halation_strength
expect:ctl_halation_threshold.controlKey=halation_threshold
expect:ctl_bloom.controlKey=bloom
expect:ctl_print_strength.controlKey=print_strength
expect:ctl_print_c.controlKey=print_c
expect:ctl_print_m.controlKey=print_m
expect:ctl_print_y.controlKey=print_y
expect:ctl_print_contrast.controlKey=print_contrast
expect:ctl_print_black_point.controlKey=print_black_point
expect:ctl_exposure.controlKey=exposure
expect:ctl_contrast.controlKey=contrast
expect:ctl_highlights.controlKey=highlights
expect:ctl_shadows.controlKey=shadows
expect:ctl_whites.controlKey=whites
expect:ctl_blacks.controlKey=blacks
expect:ctl_midtones.controlKey=midtones
expect:ctl_texture.controlKey=texture
expect:ctl_clarity.controlKey=clarity
expect:ctl_dehaze.controlKey=dehaze
expect:ctl_sharpness.controlKey=sharpness
expect:ctl_sharpness_mask.controlKey=sharpness_mask
expect:ctl_cg_shadow_lum.controlKey=cg_shadow_lum
expect:ctl_cg_midtone_lum.controlKey=cg_midtone_lum
expect:ctl_cg_highlight_lum.controlKey=cg_highlight_lum
expect:ctl_cg_global_lum.controlKey=cg_global_lum
expect:ctl_cg_balance.controlKey=cg_balance
expect:ctl_cg_blending.controlKey=cg_blending
expect:wheel_shadow.zone=shadow
expect:wheel_midtone.zone=midtone
expect:wheel_highlight.zone=highlight
expect:wheel_global.zone=global
expect:swatch_shadow.zone=shadow
expect:swatch_highlight.zone=highlight
expect:ctl_hsl_red_h.controlKey=hsl_red_h
expect:ctl_hsl_orange_h.controlKey=hsl_orange_h
expect:ctl_hsl_yellow_h.controlKey=hsl_yellow_h
expect:ctl_hsl_green_h.controlKey=hsl_green_h
expect:ctl_hsl_aqua_h.controlKey=hsl_aqua_h
expect:ctl_hsl_blue_h.controlKey=hsl_blue_h
expect:ctl_hsl_purple_h.controlKey=hsl_purple_h
expect:ctl_hsl_magenta_h.controlKey=hsl_magenta_h
set:fineTuneSection.hslIndex=1
wait:150
expect:hslSegmented.currentIndex=1
expect:ctl_hsl_red_s.controlKey=hsl_red_s
expect:ctl_hsl_orange_s.controlKey=hsl_orange_s
expect:ctl_hsl_yellow_s.controlKey=hsl_yellow_s
expect:ctl_hsl_green_s.controlKey=hsl_green_s
expect:ctl_hsl_aqua_s.controlKey=hsl_aqua_s
expect:ctl_hsl_blue_s.controlKey=hsl_blue_s
expect:ctl_hsl_purple_s.controlKey=hsl_purple_s
expect:ctl_hsl_magenta_s.controlKey=hsl_magenta_s
set:fineTuneSection.hslIndex=2
wait:150
expect:ctl_hsl_red_l.controlKey=hsl_red_l
expect:ctl_hsl_orange_l.controlKey=hsl_orange_l
expect:ctl_hsl_yellow_l.controlKey=hsl_yellow_l
expect:ctl_hsl_green_l.controlKey=hsl_green_l
expect:ctl_hsl_aqua_l.controlKey=hsl_aqua_l
expect:ctl_hsl_blue_l.controlKey=hsl_blue_l
expect:ctl_hsl_purple_l.controlKey=hsl_purple_l
expect:ctl_hsl_magenta_l.controlKey=hsl_magenta_l
# Fine-tune edits light the section's dot and reset as one step.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
control:clarity=30
expect:fineTuneSection.edited=true
call:resetControlGroup=fine_tune
expect:fineTuneSection.edited=false
quit
```

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_coverage.script -Ui v2`
Expected: FAIL from `expect:ctl_exposure.controlKey=exposure` (`<missing>`) — the earlier sections already pass.

- [ ] **Step 3: Create `desktop/qml/v2/inspector/FlFineTuneSection.qml`**

```qml
import QtQuick
import DFEE

// Fine-tune (collapsed by default): the generic grade on top of the film — basic
// tone, detail, the HSL color mixer and the color grading wheels.
FlInspectorSection {
    id: sec
    title: "Fine-tune"
    group: "fine_tune"
    property int hslIndex: 0                 // 0 hue, 1 saturation, 2 luminance
    readonly property string hslSuffix: ["h", "s", "l"][hslIndex]

    FlGroupLabel { text: "Basic tone" }
    FlFilmSlider {
        controlKey: "exposure"; label: "Exposure"
        from: -3; to: 3; stepSize: 0.05; decimals: 2; bipolar: true; suffix: " EV"
        tip: "Overall brightness of the finished image, in stops — a grade applied after the film response. For the film's own exposure (which drives its tone and rolloff), use Film exposure in the Exposure section."
    }
    FlFilmSlider { controlKey: "contrast"; label: "Contrast"; from: -100; to: 100; bipolar: true; tip: "Global contrast — spreads or compresses the tonal range around the midtones." }
    FlFilmSlider { controlKey: "highlights"; label: "Highlights"; from: -100; to: 100; bipolar: true; tip: "Recovers or brightens the brighter tones without moving whites." }
    FlFilmSlider { controlKey: "shadows"; label: "Shadows"; from: -100; to: 100; bipolar: true; tip: "Opens or deepens the darker tones without moving blacks." }
    FlFilmSlider { controlKey: "whites"; label: "Whites"; from: -100; to: 100; bipolar: true; tip: "Sets the white clipping point — how bright the brightest tones become." }
    FlFilmSlider { controlKey: "blacks"; label: "Blacks"; from: -100; to: 100; bipolar: true; tip: "Sets the black clipping point — how deep the darkest tones become." }
    FlFilmSlider { controlKey: "midtones"; label: "Midtones"; from: -100; to: 100; bipolar: true; tip: "Brightness of the mid-tones, leaving the extremes anchored." }

    FlGroupLabel { text: "Detail" }
    FlFilmSlider { controlKey: "texture"; label: "Texture"; from: -100; to: 100; bipolar: true; tip: "Medium-scale detail like skin and foliage — forward enhances, back smooths." }
    FlFilmSlider { controlKey: "clarity"; label: "Clarity"; from: -100; to: 100; bipolar: true; tip: "Midtone local contrast — forward adds punch and presence, back softens." }
    FlFilmSlider { controlKey: "dehaze"; label: "Dehaze"; from: -100; to: 100; bipolar: true; tip: "Cuts or adds atmospheric haze and low-contrast veiling." }
    FlFilmSlider { controlKey: "sharpness"; label: "Sharpening"; from: 0; to: 2; stepSize: 0.05; decimals: 2; tip: "Edge sharpening amount." }
    FlFilmSlider { controlKey: "sharpness_mask"; label: "Sharpening mask"; from: 0; to: 1; stepSize: 0.05; decimals: 2; neutral: 0.5; tip: "Limits sharpening to edges, protecting smooth areas (like skies) from being sharpened into noise." }

    FlGroupLabel { text: "Color mixer" }
    FlSegmented {
        objectName: "hslSegmented"
        model: ["Hue", "Saturation", "Luminance"]
        currentIndex: sec.hslIndex
        onActivated: (i) => sec.hslIndex = i
    }
    Repeater {
        model: [
            { key: "red", label: "Red" }, { key: "orange", label: "Orange" },
            { key: "yellow", label: "Yellow" }, { key: "green", label: "Green" },
            { key: "aqua", label: "Aqua" }, { key: "blue", label: "Blue" },
            { key: "purple", label: "Purple" }, { key: "magenta", label: "Magenta" }
        ]
        delegate: FlFilmSlider {
            width: parent.width
            controlKey: "hsl_" + modelData.key + "_" + sec.hslSuffix
            label: modelData.label
            from: -100; to: 100; bipolar: true
        }
    }

    FlGroupLabel { text: "Color grading" }
    Grid {
        anchors.horizontalCenter: parent.horizontalCenter
        columns: 2
        columnSpacing: 28
        rowSpacing: 12
        FlColorWheel { objectName: "wheel_shadow"; zone: "shadow"; label: "Shadows"; diameter: 104 }
        FlColorWheel { objectName: "wheel_midtone"; zone: "midtone"; label: "Midtones"; diameter: 104 }
        FlColorWheel { objectName: "wheel_highlight"; zone: "highlight"; label: "Highlights"; diameter: 104 }
        FlColorWheel { objectName: "wheel_global"; zone: "global"; label: "Global"; diameter: 104 }
    }
    FlFilmSlider { controlKey: "cg_shadow_lum"; label: "Shadow luminance"; from: -100; to: 100; bipolar: true; tip: "Brightness of the shadow zone only." }
    FlFilmSlider { controlKey: "cg_midtone_lum"; label: "Midtone luminance"; from: -100; to: 100; bipolar: true; tip: "Brightness of the midtone zone only." }
    FlFilmSlider { controlKey: "cg_highlight_lum"; label: "Highlight luminance"; from: -100; to: 100; bipolar: true; tip: "Brightness of the highlight zone only." }
    FlFilmSlider { controlKey: "cg_global_lum"; label: "Global luminance"; from: -100; to: 100; bipolar: true; tip: "Overall brightness applied by the grade." }
    FlFilmSlider { controlKey: "cg_balance"; label: "Balance"; from: -100; to: 100; bipolar: true; tip: "Shifts where shadows end and highlights begin, weighting the grade toward darks or lights." }
    FlFilmSlider { controlKey: "cg_blending"; label: "Blending"; from: 0; to: 100; tip: "How softly the shadow, midtone and highlight zones overlap." }
}
```

- [ ] **Step 4: Add the section to `FlInspector.qml`**

After the `FlPrintSection` block:

```qml
            FlFineTuneSection {
                objectName: "fineTuneSection"
                open: prefs.fineTuneOpen
                onToggled: prefs.fineTuneOpen = !prefs.fineTuneOpen
            }
```

Register `qml/v2/inspector/FlFineTuneSection.qml` in `desktop/CMakeLists.txt` `QML_FILES`.

- [ ] **Step 5: Build, run the whole suite, screenshot**

Run: `cmake --build desktop/out/build --config Release`, the Step 2 command, then every UI script:
v2 (`-Ui v2`): `v2_boot`, `v2_shell`, `v2_canvas`, `v2_keys`, `v2_crop`, `v2_groups`, `v2_inspector`, `v2_inspector_color`, `v2_coverage`; `v2_minsize` with `-AppArgs @('--min-size')`; the persistence pair and `v2_inspector_lr` as in Task 3 Step 8; `gallery` with `-Ui gallery`; v1 (no `-Ui`): `smoke`, `memory`, `info`, and `lightroom_bypass` via `-Command` with `-AppArgs @('--lightroom-edit','${SAMPLE_TIFF}')`.
Unit suites: `desktop_tests`, `image_info_tests`, `stock_catalog_tests` (Qt `bin` on PATH, `-o <file>,txt`).
Expected: every script `failures=0`; unit totals 10/6/5 passed.

- [ ] **Step 6: Commit and rebuild the installer**

```bash
git add desktop/qml/v2 desktop/src/UiScript.cpp desktop/CMakeLists.txt desktop/tests/ui/v2_coverage.script
git commit -m "feat(desktop): v2 fine-tune section; every film control reachable in the inspector"
```

Then run `desktop/packaging/deploy.ps1` and `"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" desktop\packaging\FilmLab.iss` (v2 stays opt-in via `FILMLAB_UI=v2`).
