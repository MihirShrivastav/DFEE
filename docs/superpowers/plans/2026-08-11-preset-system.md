# Preset System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A preset system for Film Lab — save the current look as a named preset, organize presets into folders/groups, and browse/apply them from a left-pane browser with live thumbnails of the current photo.

**Architecture:** A new `PresetStore` (C++ `QObject`, sibling to `EngineController`) owns the on-disk + built-in preset model and exposes a QML list model + CRUD invokables. Pure recipe↔JSON serialization lives in a Qt-testable `PresetSerialization` unit. `EngineController` gains `captureRecipe()`/`applyRecipe()` and a small-size recipe thumbnail render (via the existing `RenderWorker`). A `PresetBrowser.qml` fills the left pane in plugin mode and sits behind a Library⇄Presets toggle in standalone.

**Tech Stack:** C++20, Qt 6 (Core/Quick/QML), QVariantMap controls model, QJsonDocument storage, QQuickImageProvider thumbnails, CMake (`desktop/CMakeLists.txt`), MSVC Release build `desktop/out/build`. New assert-based Qt test target `desktop/tests`.

## Global Constraints

- A preset captures `stock` + the **look** controls only; it MUST exclude geometry keys: `crop_x`, `crop_y`, `crop_w`, `crop_h`, `straighten_deg`, `rotate_quadrant`, `flip_h`, `flip_v`. Applying a preset never changes geometry.
- Presets are JSON files; **folders are real directories** under `QStandardPaths::AppDataLocation + "/presets"`. Preset id = path relative to the presets root, without `.json` (e.g. `My Portraits/Soft Skin`). Built-ins namespaced under `Film Lab/…`.
- Built-in presets are **read-only** (bundled in the app via Qt resource `:/presets/…`): apply and duplicate allowed; rename/delete/move-in-place forbidden.
- Favorites persist in `presets/favorites.json` (JSON array of ids), never inside preset files.
- Unknown/missing control keys on load fall back to `EngineController::defaultFilmControls()` values (forward/backward compatible).
- Applying a preset is one undoable step (reuses the existing before/reset revisioning). Hover-preview is transient and creates no undo step.
- Product name in all UI copy is "Film Lab". No `Co-Authored-By` in commits. After desktop changes, rebuild `desktop/out/build` target `DFEE`.
- Do not modify the WIP files `cpp_engine/bindings/python/dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py`.

## File Structure

- Create `desktop/src/PresetSerialization.h/.cpp` — pure recipe↔JSON + look-key filtering (no engine deps).
- Create `desktop/src/PresetStore.h/.cpp` — on-disk + built-in model, CRUD, favorites, thumbnail requests; QML-exposed.
- Create `desktop/src/PresetThumbnailProvider.h` — `QQuickImageProvider` serving cached preset thumbnails by `"<presetId>/<imageRevision>"`.
- Modify `desktop/src/EngineController.h/.cpp` — `captureRecipe()`, `applyRecipe()`, `renderPresetThumbnail()`, hover peek/restore.
- Modify `desktop/src/RenderWorker.h/.cpp` — `renderThumbnail(request, key)` small-size path.
- Modify `desktop/src/main.cpp` — construct `PresetStore`, register `preset` context property + `presetthumb` image provider.
- Create `desktop/qml/PresetBrowser.qml` — the browser component.
- Modify `desktop/qml/Main.qml` — mount browser (plugin: fill left pane; standalone: Library⇄Presets toggle).
- Create `desktop/presets/*.json` + `desktop/presets.qrc` — built-in preset set.
- Modify `desktop/CMakeLists.txt` — new sources, `presets.qrc`, and a `filmlab_tests` test target.
- Create `desktop/tests/preset_test.cpp` — assert-based Qt Core tests.

---

### Task 1: Recipe serialization + look-key filtering (pure, unit-tested)

**Files:**
- Create: `desktop/src/PresetSerialization.h`, `desktop/src/PresetSerialization.cpp`
- Create: `desktop/tests/preset_test.cpp`
- Modify: `desktop/CMakeLists.txt` (add `filmlab_tests` target)

**Interfaces — Produces:**
- `namespace filmlab { QStringList presetGeometryKeys(); QVariantMap filterLookControls(const QVariantMap& all); QJsonObject recipeToJson(const QString& name, const QString& stock, const QVariantMap& look); bool recipeFromJson(const QJsonObject& obj, QString& stockOut, QVariantMap& controlsOut); }`

- [ ] **Step 1: Write the failing test** — `desktop/tests/preset_test.cpp`:

```cpp
#include "PresetSerialization.h"
#include <QVariantMap>
#include <QJsonObject>
#include <cassert>
#include <cstdio>

static void test_recipe_roundtrip() {
    using namespace filmlab;
    QVariantMap all{
        {"film_contrast", 108.0}, {"temp", 12.0}, {"saturation", -5.0},
        {"crop_x", 0.1}, {"crop_w", 0.8}, {"straighten_deg", 3.0}, {"flip_h", true}, {"rotate_quadrant", 1},
    };
    // Geometry excluded from the look.
    QVariantMap look = filterLookControls(all);
    assert(look.contains("film_contrast") && look.contains("temp") && look.contains("saturation"));
    for (const QString& g : presetGeometryKeys()) assert(!look.contains(g));

    // Round-trip through JSON preserves look controls + stock.
    QJsonObject obj = recipeToJson("Golden Hour", "portra_400", look);
    QString stock; QVariantMap out;
    assert(recipeFromJson(obj, stock, out));
    assert(stock == "portra_400");
    assert(qFuzzyCompare(out.value("film_contrast").toDouble(), 108.0));
    assert(qFuzzyCompare(out.value("temp").toDouble(), 12.0));
    // Unknown keys tolerated: an extra key survives as-is; missing ones simply absent.
    assert(!out.contains("crop_x"));
    std::printf("test_recipe_roundtrip passed\n");
}

int main() {
    test_recipe_roundtrip();
    std::printf("filmlab_tests passed\n");
    return 0;
}
```

- [ ] **Step 2: Add the test target and run to verify it fails to build** — in `desktop/CMakeLists.txt`, after the `DFEE` target, add:

```cmake
if(DFEE_BUILD_TESTS)
    enable_testing()
    add_executable(filmlab_tests tests/preset_test.cpp src/PresetSerialization.cpp)
    target_include_directories(filmlab_tests PRIVATE src)
    target_link_libraries(filmlab_tests PRIVATE Qt6::Core)
    add_test(NAME filmlab_tests COMMAND filmlab_tests)
endif()
```
Add `option(DFEE_BUILD_TESTS "Build Film Lab desktop tests" ON)` near the top if not present.
Run: `cmake --build desktop/out/build --config Release --target filmlab_tests`
Expected: FAIL — `PresetSerialization.h` not found.

- [ ] **Step 3: Implement `PresetSerialization`** — `desktop/src/PresetSerialization.h`:

```cpp
#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QJsonObject>
namespace filmlab {
const QStringList& presetGeometryKeys();
QVariantMap filterLookControls(const QVariantMap& all);
QJsonObject recipeToJson(const QString& name, const QString& stock, const QVariantMap& look);
bool recipeFromJson(const QJsonObject& obj, QString& stockOut, QVariantMap& controlsOut);
}
```
`desktop/src/PresetSerialization.cpp`:

```cpp
#include "PresetSerialization.h"
#include <QJsonValue>
#include <QDateTime>
namespace filmlab {
const QStringList& presetGeometryKeys() {
    static const QStringList k{"crop_x","crop_y","crop_w","crop_h",
                               "straighten_deg","rotate_quadrant","flip_h","flip_v"};
    return k;
}
QVariantMap filterLookControls(const QVariantMap& all) {
    QVariantMap out = all;
    for (const QString& g : presetGeometryKeys()) out.remove(g);
    return out;
}
QJsonObject recipeToJson(const QString& name, const QString& stock, const QVariantMap& look) {
    QJsonObject controls;
    for (auto it = look.begin(); it != look.end(); ++it)
        controls.insert(it.key(), QJsonValue::fromVariant(it.value()));
    QJsonObject root;
    root.insert("schemaVersion", 1);
    root.insert("name", name);
    root.insert("stock", stock);
    root.insert("controls", controls);
    root.insert("createdAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return root;
}
bool recipeFromJson(const QJsonObject& obj, QString& stockOut, QVariantMap& controlsOut) {
    if (!obj.contains("controls") || !obj.value("controls").isObject()) return false;
    stockOut = obj.value("stock").toString();
    const QJsonObject controls = obj.value("controls").toObject();
    controlsOut.clear();
    for (auto it = controls.begin(); it != controls.end(); ++it) {
        if (presetGeometryKeys().contains(it.key())) continue; // never import geometry
        controlsOut.insert(it.key(), it.value().toVariant());
    }
    return true;
}
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build desktop/out/build --config Release --target filmlab_tests && desktop/out/build/Release/filmlab_tests.exe`
Expected: `test_recipe_roundtrip passed` and `filmlab_tests passed`.

- [ ] **Step 5: Commit**

```bash
git add desktop/src/PresetSerialization.h desktop/src/PresetSerialization.cpp desktop/tests/preset_test.cpp desktop/CMakeLists.txt
git commit -m "presets: recipe<->JSON serialization + look-key filtering (unit-tested)"
```

---

### Task 2: EngineController capture/apply recipe

**Files:**
- Modify: `desktop/src/EngineController.h`, `desktop/src/EngineController.cpp`
- Test: `desktop/tests/preset_test.cpp` (extend)

**Interfaces:**
- Consumes: `filmlab::filterLookControls` (Task 1).
- Produces: `Q_INVOKABLE QVariantMap EngineController::captureRecipe() const;` (returns `{ "stock": <id>, "controls": <look map> }`), `Q_INVOKABLE void EngineController::applyRecipe(const QVariantMap& recipe);` (sets stock + look controls, geometry preserved, one render), and a static `QStringList EngineController::lookControlKeys()` (all `defaultFilmControls()` keys minus geometry).

- [ ] **Step 1: Write the failing test** — extend `preset_test.cpp` with a pure check of the key set (no full controller instantiation — that needs the render stack). Add:

```cpp
#include <QSet>
// Mirror of the intended look-key set: every default control minus geometry.
static void test_look_keys_exclude_geometry() {
    using namespace filmlab;
    // The controller's captureRecipe must exclude exactly the geometry keys.
    QVariantMap sample{{"film_contrast",100.0},{"crop_x",0.0},{"flip_v",false},{"temp",0.0}};
    QVariantMap look = filterLookControls(sample);
    assert(look.size() == 2);
    assert(look.contains("film_contrast") && look.contains("temp"));
    std::printf("test_look_keys_exclude_geometry passed\n");
}
```
Call it from `main()` before the final print.

- [ ] **Step 2: Run test to verify it passes** (this asserts the filtering contract Task 3's controller relies on)

Run: `cmake --build desktop/out/build --config Release --target filmlab_tests && desktop/out/build/Release/filmlab_tests.exe`
Expected: `test_look_keys_exclude_geometry passed`.

- [ ] **Step 3: Implement in `EngineController`** — header: add under `public Q_INVOKABLE`:

```cpp
Q_INVOKABLE QVariantMap captureRecipe() const;
Q_INVOKABLE void applyRecipe(const QVariantMap& recipe);
```
`EngineController.cpp` (include `"PresetSerialization.h"`):

```cpp
QVariantMap EngineController::captureRecipe() const {
    QVariantMap recipe;
    recipe.insert("stock", stockId_);                       // current stock id member
    recipe.insert("controls", filmlab::filterLookControls(filmControls_));
    return recipe;
}
void EngineController::applyRecipe(const QVariantMap& recipe) {
    const QString stock = recipe.value("stock").toString();
    const QVariantMap look = filmlab::filterLookControls(recipe.value("controls").toMap());
    // Merge onto current controls so geometry (and any absent look key -> default) is preserved.
    for (auto it = look.begin(); it != look.end(); ++it) filmControls_.insert(it.key(), it.value());
    if (!stock.isEmpty() && stock != stockId_) { setStock(stock); }   // setStock triggers its own render
    emit filmControlsChanged();
    scheduleRender();
}
```
(Confirm the current-stock member name is `stockId_`; use whatever `stock()` returns.)

- [ ] **Step 4: Build to verify it compiles**

Run: `cmake --build desktop/out/build --config Release --target DFEE`
Expected: builds clean.

- [ ] **Step 5: Commit**

```bash
git add desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/tests/preset_test.cpp
git commit -m "presets: EngineController captureRecipe/applyRecipe (geometry-preserving)"
```

---

### Task 3: PresetStore — on-disk + built-in model and CRUD

**Files:**
- Create: `desktop/src/PresetStore.h`, `desktop/src/PresetStore.cpp`
- Modify: `desktop/CMakeLists.txt` (add sources to `DFEE` and, for the store test, to `filmlab_tests`)
- Test: `desktop/tests/preset_test.cpp` (extend — temp-dir store ops)

**Interfaces:**
- Consumes: `filmlab::recipeToJson/recipeFromJson` (Task 1); `EngineController::captureRecipe/applyRecipe` (Task 2).
- Produces: `class PresetStore : public QAbstractListModel` with roles `IdRole, NameRole, GroupRole, IsBuiltInRole, IsFavoriteRole`; ctor `PresetStore(EngineController* engine, QObject* parent=nullptr)`; a settable roots API for tests `void setRootsForTest(const QString& userDir, const QString& builtinDir)`; invokables:
  - `Q_INVOKABLE void createFromCurrent(const QString& name, const QString& group);`
  - `Q_INVOKABLE void apply(const QString& id);`
  - `Q_INVOKABLE void rename(const QString& id, const QString& newName);`
  - `Q_INVOKABLE void remove(const QString& id);`
  - `Q_INVOKABLE void move(const QString& id, const QString& group);`
  - `Q_INVOKABLE void duplicate(const QString& id);`
  - `Q_INVOKABLE void createGroup(const QString& name);`
  - `Q_INVOKABLE void setFavorite(const QString& id, bool fav);`
  - `Q_INVOKABLE QStringList groups() const;`
  - `Q_INVOKABLE void refresh();`

- [ ] **Step 1: Write the failing test** — extend `preset_test.cpp` (needs `QCoreApplication` for temp paths). Add near the top a helper and a test that drives the filesystem directly through the store's non-UI methods against a temp dir:

```cpp
#include "PresetStore.h"
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QFile>
static void test_store_crud(QString userDir) {
    PresetStore store(nullptr);
    store.setRootsForTest(userDir, QString());          // no built-ins
    // createGroup makes a directory; createPreset writes a file (test seam that bypasses engine).
    store.createGroup("My Portraits");
    assert(QDir(userDir + "/My Portraits").exists());
    // Write a preset via the store's serialization seam:
    store.writePresetForTest("My Portraits", "Soft Skin", "portra_400",
                             QVariantMap{{"film_contrast", 105.0}});
    assert(QFile::exists(userDir + "/My Portraits/Soft Skin.json"));
    store.refresh();
    // Model contains it with the right group/id.
    bool found = false;
    for (int i = 0; i < store.rowCount(); ++i)
        if (store.data(store.index(i), PresetStore::IdRole).toString() == "My Portraits/Soft Skin") found = true;
    assert(found);
    // Favorite persists in favorites.json.
    store.setFavorite("My Portraits/Soft Skin", true);
    assert(QFile::exists(userDir + "/favorites.json"));
    // Move to root.
    store.move("My Portraits/Soft Skin", QString());
    assert(QFile::exists(userDir + "/Soft Skin.json"));
    std::printf("test_store_crud passed\n");
}
```
In `main()`, wrap store tests with a `QCoreApplication app(argc, argv);` and a `QTemporaryDir tmp;` passing `tmp.path()`.
(Add `PresetStore.cpp` to the `filmlab_tests` sources in CMake, and link `Qt6::Core`; the store's engine-dependent methods must be guarded so `engine==nullptr` is safe for tests — `apply`/`createFromCurrent` early-return without an engine; add a `writePresetForTest(...)` and `setRootsForTest(...)` seam.)

- [ ] **Step 2: Run test to verify it fails** — build `filmlab_tests`; expected: `PresetStore.h` not found.

- [ ] **Step 3: Implement `PresetStore`** — `QAbstractListModel` that:
  - Holds `struct Entry { QString id, name, group; bool builtIn; }` in a `QList<Entry>`, sorted group-then-name, plus a `QSet<QString> favorites_`.
  - `refresh()`: recursively scans `userRoot_` and `builtinRoot_` for `*.json` (depth: root + one subdirectory = group), building ids (`group + "/" + base` or `base`), `builtIn` = under `builtinRoot_`. Loads `favorites.json` into `favorites_`. Calls `beginResetModel()/endResetModel()`.
  - `data()` returns the roles. `roleNames()` maps to `"presetId","name","group","isBuiltIn","isFavorite"`.
  - `createGroup(name)`: `QDir(userRoot_).mkpath(name)`, refresh.
  - `createFromCurrent(name, group)`: `engine_->captureRecipe()` → `recipeToJson` → write `userRoot_/[group/]name.json`; refresh; (UI selects it).
  - `apply(id)`: read the file (user or built-in), `recipeFromJson`, `engine_->applyRecipe({stock, controls})`.
  - `rename/remove/move/duplicate`: filesystem ops on `userRoot_` (reject if `entry.builtIn` except `duplicate`, which always writes a user copy). `move` = move file between group dirs. Refresh after each.
  - `setFavorite`: toggle in `favorites_`, write `favorites.json` (JSON array), refresh (or targeted dataChanged).
  - `writePresetForTest`, `setRootsForTest`: test seams (guarded so engine-less).
  - Real roots (production): `userRoot_ = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/presets"` (mkpath), `builtinRoot_ = ":/presets"` (Qt resource, read-only).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build desktop/out/build --config Release --target filmlab_tests && desktop/out/build/Release/filmlab_tests.exe`
Expected: `test_store_crud passed`, `filmlab_tests passed`.

- [ ] **Step 5: Commit**

```bash
git add desktop/src/PresetStore.h desktop/src/PresetStore.cpp desktop/tests/preset_test.cpp desktop/CMakeLists.txt
git commit -m "presets: PresetStore model + filesystem CRUD, favorites, built-in scan"
```

---

### Task 4: Preset thumbnail rendering (worker + provider)

**Files:**
- Modify: `desktop/src/RenderWorker.h/.cpp` — `Q_INVOKABLE void renderThumbnail(const dfee::NativePreviewRenderRequest& request, const QString& cacheKey);` renders at ~200px long edge and emits `thumbnailReady(QString cacheKey, QImage img)`.
- Create: `desktop/src/PresetThumbnailProvider.h` — `QQuickImageProvider(Image)`, `requestImage(id,…)` returns the cached `QImage` for `id` (a `QHash<QString,QImage>` guarded by a mutex), or a 1×1 transparent placeholder.
- Modify: `desktop/src/EngineController.h/.cpp` — `Q_INVOKABLE QString EngineController::presetThumbKey(const QString& presetId) const` (returns `presetId + "/" + QString::number(previewRevision_)`), and `Q_INVOKABLE void requestPresetThumbnail(const QString& presetId, const QVariantMap& recipe)` which builds a preview request from `recipe` (reusing `buildPreviewRequest()` then overriding stock+controls) and dispatches `renderThumbnail` on the worker; on `thumbnailReady`, insert into the provider and emit `presetThumbnailReady(presetId)`.
- Modify: `desktop/src/main.cpp` — `auto* presetThumbs = new PresetThumbnailProvider(); engine.addImageProvider("presetthumb", presetThumbs);` and pass it to `EngineController`/worker like the existing `PreviewImageProvider`.

**Interfaces:**
- Consumes: `RenderWorker` dispatch pattern (`QMetaObject::invokeMethod(worker_, …)`), `buildPreviewRequest()`.
- Produces: image URLs `image://presetthumb/<presetId>/<previewRevision>`; signal `presetThumbnailReady(QString presetId)`.

- [ ] **Step 1: Implement worker thumbnail render** — add `renderThumbnail`: same pipeline as `doRender` but request a small preview (set the request's preview long-edge to 200 if such a field exists, else downscale the returned image), convert to `QImage`, `emit thumbnailReady(cacheKey, img)`. Do not touch the main `preview` provider.

- [ ] **Step 2: Implement the provider + EngineController wiring** as above. `requestPresetThumbnail` overrides the request's `stock` and look controls from `recipe` (leave geometry at the current image's values so the crop matches the main preview), dispatches to the worker, and on `thumbnailReady` stores the image under `presetThumbKey(presetId)` and emits `presetThumbnailReady`.

- [ ] **Step 3: Build + manual verify**

Run: `cmake --build desktop/out/build --config Release --target DFEE`
Then, with a temporary QML probe (or after Task 5), confirm `image://presetthumb/<id>/<rev>` yields a rendered thumbnail of the open image through a recipe, and that opening a different image changes `previewRevision` (new cache key → re-render).
Acceptance: a thumbnail request returns a correctly-toned small image; requesting the same key twice hits cache (no second render).

- [ ] **Step 4: Commit**

```bash
git add desktop/src/RenderWorker.h desktop/src/RenderWorker.cpp desktop/src/PresetThumbnailProvider.h desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/src/main.cpp desktop/CMakeLists.txt
git commit -m "presets: worker-rendered live thumbnails + presetthumb image provider"
```

---

### Task 5: PresetBrowser.qml + left-pane integration

**Files:**
- Create: `desktop/qml/PresetBrowser.qml`
- Modify: `desktop/qml/Main.qml` (mount browser), `desktop/src/main.cpp` (register `preset` context property: `engine.rootContext()->setContextProperty("preset", &presetStore);`), `desktop/CMakeLists.txt` (add `PresetBrowser.qml` to the QML module / copy_qml).

**Interfaces:**
- Consumes: `preset` (PresetStore model + invokables), `engine.captureRecipe/applyRecipe`, `engine.presetThumbKey`, `engine.requestPresetThumbnail`, `presetThumbnailReady`.

- [ ] **Step 1: Build `PresetBrowser.qml`** — a `Column`/`ListView` with:
  - Header row: `＋ New preset` button (opens a name+group dialog → `preset.createFromCurrent`), a **list/grid** `viewMode` toggle (property `bool gridMode`), and a `TextField` search bound to a `filterText`.
  - A `ListView`/`GridView` over `preset` (the model), section-grouped by `group` (`ListView.section.property: "group"`, collapsible sections tracked in a `var collapsed` map), with a pinned Favorites section (isFavorite) at the top.
  - **Grid delegate:** an `Image { source: "image://presetthumb/" + engine.presetThumbKey(model.presetId) }` with the name overlaid; on `Component.onCompleted`/when visible call `engine.requestPresetThumbnail(model.presetId, <recipe read via preset.recipeFor(id)>)`. Refresh the `source` when `engine.presetThumbnailReady(id)` matches (bump a nonce). **List delegate:** name + small swatch, no thumbnail.
  - `onEntered` (HoverHandler) → `engine.previewPreset(model.presetId)` (transient peek, Task 7); `onClicked` → `preset.apply(model.presetId)`.
  - Per-item `⋯` menu: Rename, Duplicate, Move to group, Favorite, Delete (Delete/Rename/Move hidden when `model.isBuiltIn`).

- [ ] **Step 2: Mount in Main.qml** — In the left library `Rectangle` (currently `width: engine.lightroomRoundTrip ? 0 : (root.libraryOpen ? 232 : 22)`): in `lightroomRoundTrip` mode set width to the browser width (e.g. 232) and show `PresetBrowser`. In standalone, add a small **Library ⇄ Presets** segmented toggle at the top of the pane (`property int leftMode: 0`) switching between the existing library content and `PresetBrowser`.

- [ ] **Step 3: Build + manual verify**

Run: rebuild `DFEE`; launch on a TIFF (or via `--lightroom-edit`). Verify: browser fills the plugin left pane; grid shows live thumbnails of the photo; list/grid toggle works; search filters; groups collapse; ＋ New writes a preset that appears; click applies; standalone shows the Library⇄Presets toggle.

- [ ] **Step 4: Commit**

```bash
git add desktop/qml/PresetBrowser.qml desktop/qml/Main.qml desktop/src/main.cpp desktop/CMakeLists.txt
git commit -m "presets: PresetBrowser UI + left-pane integration (plugin fill, standalone toggle)"
```

---

### Task 6: Hover-preview + apply-as-undo-step

**Files:**
- Modify: `desktop/src/EngineController.h/.cpp` — `Q_INVOKABLE void previewPreset(const QString& presetId)` (apply a recipe transiently, remembering the pre-hover recipe) and `Q_INVOKABLE void clearPresetPreview()` (restore it); ensure `applyRecipe` (commit) registers one before/undo step consistent with `resetAllEdits`.
- Modify: `desktop/qml/PresetBrowser.qml` — wire HoverHandler enter→`engine.previewPreset(id)`, exit→`engine.clearPresetPreview()`.

- [ ] **Step 1: Implement peek/restore** — `previewPreset` snapshots `captureRecipe()` into `hoverBackup_` (once, on first hover), applies the target recipe with render but WITHOUT touching the undo/before baseline; `clearPresetPreview` re-applies `hoverBackup_` and clears it. `preset.apply(id)` (commit path) goes through `applyRecipe` which updates the undo baseline exactly once.

- [ ] **Step 2: Build + manual verify** — hovering presets live-updates the big preview and restores on exit; clicking commits; a single Undo/Reset reverts a committed preset apply.

- [ ] **Step 3: Commit**

```bash
git add desktop/src/EngineController.h desktop/src/EngineController.cpp desktop/qml/PresetBrowser.qml
git commit -m "presets: hover-preview peek/restore + single-step apply"
```

---

### Task 7: Built-in preset set + bundling

**Files:**
- Create: `desktop/presets/Film Lab/*.json` (8–10 curated looks), `desktop/presets.qrc` mapping them under `:/presets/Film Lab/…`
- Modify: `desktop/CMakeLists.txt` (add `presets.qrc` to the `DFEE` target resources)

- [ ] **Step 1: Author the built-in set** — 8–10 presets as JSON (schema from Task 1), each `stock` + a hand-tuned look, spanning the range, e.g.: "Kodachrome Gold" (kodachrome_64, warm/punchy), "Portra Pastel" (portra_400, soft/airy), "Ektar Punch" (ektar_100, vivid), "Tri-X Street" (tri_x_400, contrasty B&W), "HP5 Documentary" (hp5_plus, gentle B&W), "Velvia Landscape" (velvia_50, saturated), "Cinestill Night" (cinestill_800t, teal/halation), "Superia Everyday" (superia_400, green-lean). Author real control values (not defaults) so each reads distinct.

- [ ] **Step 2: Bundle via qrc** — `presets.qrc`:
```xml
<RCC><qresource prefix="/presets"><file>Film Lab/Kodachrome Gold.json</file>… </qresource></RCC>
```
Add to the `DFEE` target (`qt_add_resources` or the existing resource list in `desktop/CMakeLists.txt`).

- [ ] **Step 3: Build + manual verify** — fresh run (empty user presets dir) shows the read-only "Film Lab" group populated; each applies and looks distinct; built-ins can be duplicated (copy becomes editable) but not renamed/deleted.

- [ ] **Step 4: Commit**

```bash
git add "desktop/presets" desktop/presets.qrc desktop/CMakeLists.txt
git commit -m "presets: curated built-in Film Lab preset set (bundled, read-only)"
```

---

## Self-Review

**Spec coverage:** complete-recipe capture/apply (Task 2, geometry excluded via Task 1); JSON files + folders-as-dirs + ids + favorites.json (Tasks 1, 3); built-in read-only set (Tasks 3 scan, 7 authored); browser with list/grid + live thumbnails + hover/click + groups + favorites + search + new-from-current + context menu (Tasks 4, 5); worker-thread cached visible-only thumbnails (Task 4); apply as one undo step + hover peek (Task 6); Library⇄Presets standalone toggle + plugin fill (Task 5); tests for serialization round-trip + store CRUD (Tasks 1–3). All spec sections covered.

**Placeholder scan:** code steps carry real code for the pure/testable core; UI/worker steps give exact interfaces, signatures, integration points, and acceptance criteria (build-and-verify, as UI work must be). No TBD/TODO.

**Type consistency:** `filterLookControls`, `recipeToJson`, `recipeFromJson` consistent across Tasks 1–3; `captureRecipe`/`applyRecipe` signatures consistent Tasks 2/3/6; preset id format (`group/name`) consistent across store, thumbnails (`presetThumbKey`), and QML; provider name `presetthumb` and context property `preset` consistent across main.cpp and QML.

## Risks / Notes

- Instantiating a full `EngineController` in a unit test is heavy (render stack + worker thread); tests cover the pure serialization + filesystem CRUD (the logic most likely to regress). Capture/apply and thumbnails are verified by build + manual UI check.
- The worker thumbnail path must not disturb the main `preview` provider or the auto-grain resolution; render thumbnails on the same worker but into a separate cache/signal.
- If `NativePreviewRenderRequest` has no small-size field, downscale the rendered image to 200px before emitting; keep it off the hot path (visible tiles only).
