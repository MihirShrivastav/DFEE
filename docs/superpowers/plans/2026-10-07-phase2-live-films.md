# Phase 2 — Live Films Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Films tray shows the open photo rendered in every stock (live tiles, refreshed as you edit), and hovering a tile previews that film on the canvas; a click applies it, Esc or moving away restores.

**Architecture:** Tiles come from `EngineSession::render_look_proxy` (plan 0C). The controller adds tile jobs to its existing single-worker flow as the lowest priority: one tile in flight, dispatched only when the worker is idle and no preview, open, peek, auto-grain or export is waiting; a look *epoch* (bumped on every adjustment, cleared on a new photo) discards stale results and re-queues tiles. Finished tiles are QImages served by an `image://look/` provider. Peek is a controller state (`peekStock`) that only changes the preview request — no history, no catalog write. Tile fidelity: the one fixed-pixel effect (acutance) scales by the proxy's size factor; the preview passes 1.0 and stays bit-exact.

**Tech Stack:** Qt 6.8.3 Quick/Controls, C++20, the DFEE engine (`cpp_engine`), CMake.

**Spec:** `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` (Bottom tray — Films; Engine additions; Phasing step 2). Builds on `docs/superpowers/plans/2026-10-07-engine-0c-proxy-render.md` and the v2 UI plans 1A–1C.

## Global Constraints

- Scheduling priority (spec): Open > Preview/peek > Auto-grain > Export > Proxy; one proxy in flight; proxies only when idle; cancelled by image epoch.
- Films tiles: "the current photo rendered through every stock (with the current adjustments, so a tile is exactly what a click gives)"; hover (≈120 ms) previews the stock on the canvas; click applies (one history step); Esc restores; `[` `]` still cycle stocks.
- Peek never records history and never writes the catalog; exports always use the applied stock, never a peeked one.
- Preview rendering maths must not change: the engine golden test stays 24/24 bit-exact.
- Tile target p50 ≤ 60 ms (engine measured 29 ms); tile max edge 256.
- QML rules from 1A–1C: `import DFEE`, controlled components, no cross-file ids, `MouseArea` clicks, tokens from `Theme`.
- Build only `dfee_session_tests` / `dfee_tests` in `cpp_engine/out/build/windows-msvc-vcpkg` (never `dfee_native`); don't touch `cpp_engine/bindings/python`, `dfee_native_bridge.py`, `server.py`. No export runs in tests. Never inject system-wide input.
- Rebuild Release after each task; regenerate the installer at the end (close any running Film Lab first).

## Review Focus

1. **Editing while tiles render**: a slider drag must stay as responsive as before — at most one ~30 ms tile ahead of a preview, and tiles never render while the debounce timer is pending. Task 2 test checks a preview edit lands while tiles are queued.
2. **Switching photos** mid-render never shows the previous photo's tiles. Task 2 script opens B and checks tiles were cleared before refilling.
3. **Peek and history/catalog**: hover then leave, hover then Esc, hover then click — only the click adds a history step; the catalog is untouched by peeks. Task 3 script.
4. **Export while peeking** saves the applied film, not the hovered one. Task 3 makes `exportImage()` end the peek first; reviewer checks every export path.
5. **Lightroom Edit-In**: Films tiles and peek work in the round-trip window. Task 3 runs its peek check under `--lightroom-edit` too.

---

### Task 1: Tile fidelity — scale acutance for small renders

**Files:**
- Modify: `cpp_engine/include/dfee/renderer.hpp`, `cpp_engine/src/renderer.cpp` (`apply_acutance_shaping`)
- Modify: `cpp_engine/include/dfee/session.hpp` (`PipelineOptions::pixel_scale`), `cpp_engine/include/dfee/bridge_types.hpp` (`NativeLookProxyRequest::scale_pixel_effects`), `cpp_engine/src/session.cpp`
- Modify: `cpp_engine/tests/test_session.cpp`

**Interfaces:**
- Produces: `Image FilmRenderer::apply_acutance_shaping(const Image&, const MaterialEffectsPlan&, float pixel_scale = 1.0F) const`; `PipelineOptions::pixel_scale` (float, default 1.0); `NativeLookProxyRequest::scale_pixel_effects` (bool, default true); proxies pass `pixel_scale = proxy width / preview width`.

- [ ] **Step 1: Failing test**

In `cpp_engine/tests/test_session.cpp`, in `make_scene`, add a scene before the final `else` (low key):

```cpp
            } else if (kind == 3) {
                // Fine detail where acutance matters: 3 px stripes above, a 12 px
                // checker below, light hash noise everywhere, on a mid grey.
                const unsigned hash = (static_cast<unsigned>(x) * 73856093u) ^ (static_cast<unsigned>(y) * 19349663u);
                const double noise = static_cast<double>((hash >> 8) & 0xFFu) / 255.0 - 0.5;
                const double stripes = ((x / 3) % 2 == 0) ? 0.08 : -0.08;
                const double checker = (((x / 12) + (y / 12)) % 2 == 0) ? 0.10 : -0.10;
                r = g = b = 0.45 + (v < 0.5 ? stripes : checker) + 0.06 * noise;
                r *= 1.05;
                b *= 0.95;
```

(the existing `} else {` low-key branch follows unchanged), and add the test inside the anonymous namespace:

```cpp
// A proxy is ~1/4 of the preview's size: fixed-pixel detail shaping must scale with
// it, or tiles over-state sharpening on textured photos.
void test_proxy_fidelity_textured() {
    dfee::EngineSession session(kRepoRoot);
    const auto file = write_scene("proxy_texture", 3, 1600, 1066);
    auto look = grain_free(base_request(file, "portra_400"));
    look.sharpness = 1.0F;
    auto scaled = proxy_request(look);
    auto unscaled = proxy_request(look);
    unscaled.scale_pixel_effects = false;
    const auto proxy_scaled = session.render_look_proxy(scaled);
    const auto proxy_unscaled = session.render_look_proxy(unscaled);
    expect(proxy_scaled.ok && proxy_unscaled.ok, "textured proxies render");
    const cv::Mat preview = preview_at(session, look, {proxy_scaled.width, proxy_scaled.height});
    const auto d_scaled = compare_images(proxy_to_bgr(proxy_scaled), preview);
    const auto d_unscaled = compare_images(proxy_to_bgr(proxy_unscaled), preview);
    std::cout << "  textured proxy vs preview: scaled mean " << d_scaled.mean_abs
              << ", unscaled mean " << d_unscaled.mean_abs << "\n";
    expect(d_scaled.mean_abs < d_unscaled.mean_abs, "scaling pixel effects brings tiles closer to the preview");
    expect(d_scaled.mean_abs <= 6.0, "textured proxy looks like the preview");
    std::filesystem::remove(file);
}
```

and call it in `main` after `test_proxy_matches_preview();`:

```cpp
        test_proxy_fidelity_textured();
```

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests`
Expected: compile error — `scale_pixel_effects` is not a member of `NativeLookProxyRequest`.

- [ ] **Step 2: Implement**

`cpp_engine/include/dfee/bridge_types.hpp`, in `NativeLookProxyRequest` after `bool apply_geometry = true;`:

```cpp
    // Scale fixed-pixel effects (acutance kernels) by the proxy's size so a tile
    // shows the same detail shaping as the preview. Off only for comparisons.
    bool scale_pixel_effects = true;
```

`cpp_engine/include/dfee/renderer.hpp`: change the `apply_acutance_shaping` declaration's parameters to

```cpp
    [[nodiscard]] Image apply_acutance_shaping(
        const Image& rgb_linear,
        const MaterialEffectsPlan& effects,
        float pixel_scale = 1.0F) const;
```

`cpp_engine/src/renderer.cpp`, `apply_acutance_shaping`: change the signature to match (`const MaterialEffectsPlan& effects, const float pixel_scale) const {`), then replace

```cpp
    const int k_low = odd_kernel_size(19, 3, short_edge);
    cv::Mat low_blur;
    cv::GaussianBlur(lightness, low_blur, cv::Size(k_low, k_low), 0.0);

    cv::Mat mid_blur;
    cv::GaussianBlur(lightness, mid_blur, cv::Size(5, 5), 0.0);
```

with

```cpp
    // Kernels are tuned for the ~1024 px preview; a smaller render (a look proxy)
    // passes pixel_scale < 1 so the same detail band is shaped. 1.0 is unchanged.
    const int k_low = odd_kernel_size(static_cast<int>(std::lround(19.0F * pixel_scale)), 3, short_edge);
    cv::Mat low_blur;
    cv::GaussianBlur(lightness, low_blur, cv::Size(k_low, k_low), 0.0);

    const int k_mid = static_cast<int>(std::lround(5.0F * pixel_scale)) | 1;
    cv::Mat mid_blur;
    if (k_mid >= 3) {
        cv::GaussianBlur(lightness, mid_blur, cv::Size(k_mid, k_mid), 0.0);
    } else {
        mid_blur = lightness;               // below 3 px the mid band vanishes
    }
```

and in the sharpening block replace `const float sharp_val = clampf(effects.sharpness, 0.0F, 1.0F);` with

```cpp
        const float sharpness = effects.sharpness * std::min(1.0F, pixel_scale);
        const float sharp_val = clampf(sharpness, 0.0F, 1.0F);
```

and the two later uses of `effects.sharpness` in that block (`if (effects.sharpness > 0.0F)` stays as the guard; the blend `* effects.sharpness` at the end of the loop) → `* sharpness`.

`cpp_engine/include/dfee/session.hpp`, `struct PipelineOptions`, after `bool dump_stages = true;`:

```cpp
        float pixel_scale = 1.0F;                      // render size / preview size
```

`cpp_engine/src/session.cpp`, in `run_film_pipeline`, the acutance call becomes:

```cpp
            rendered = renderer.apply_acutance_shaping(rendered, render_plan.material_effects, options.pixel_scale);
```

and in `render_look_proxy`, after `options.dump_stages = false;`:

```cpp
        if (request.scale_pixel_effects && preview_cache_->rgb_linear.width > 0) {
            options.pixel_scale = static_cast<float>(cached.rgb_linear.width) /
                                  static_cast<float>(preview_cache_->rgb_linear.width);
        }
```

- [ ] **Step 3: Build and run**

Run:
```bash
cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests dfee_tests
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_tests.exe
```
Expected: `golden previews: 24/24 bit-exact` (twice), the `textured proxy vs preview` line with scaled < unscaled (record both in the ledger), `all passed`; `dfee_tests` exit 0. If scaled is not lower, the acutance difference is not what separates tiles from previews: revert this task's code, keep the test's measurement as a ledger note, and rule.

- [ ] **Step 4: Commit**

```bash
git add cpp_engine/include/dfee cpp_engine/src/renderer.cpp cpp_engine/src/session.cpp cpp_engine/tests/test_session.cpp
git commit -m "feat(engine): look proxies scale acutance kernels to their size (preview unchanged)"
```

---

### Task 2: Live tiles — scheduling, provider, Films strip

**Files:**
- Create: `desktop/src/LookPreviewProvider.h`
- Modify: `desktop/src/RenderWorker.h`, `desktop/src/RenderWorker.cpp`, `desktop/src/EngineController.h`, `desktop/src/EngineController.cpp`, `desktop/src/main.cpp`, `desktop/CMakeLists.txt`
- Modify: `desktop/qml/v2/tray/FlFilmsStrip.qml`
- Create: `desktop/tests/ui/v2_films_live.script`

**Interfaces:**
- Consumes: `EngineSession::render_look_proxy`, `NativeLookProxyRequest/Response` (0C).
- Produces:
  - `LookPreviewProvider` (`image://look/<stockId>?e=<epoch>`): `setImage(QString key, QImage)`, `clear()`; thread-safe.
  - `RenderWorker::renderLookProxy(const dfee::NativeLookProxyRequest&, const QString& stockId, qulonglong epoch)` → controller `onLookProxyReady(QString stockId, qulonglong epoch, QImage image)` then `onWorkerBusyChanged(false)`.
  - Controller: `Q_PROPERTY(QVariantMap lookTiles NOTIFY lookTilesChanged)` (stock id → epoch of its ready tile), `Q_PROPERTY(int lookEpoch NOTIFY lookTilesChanged)`, `Q_PROPERTY(int lookTilesPending NOTIFY lookTilesChanged)` (queued + in flight), `Q_INVOKABLE void requestLookTiles(const QStringList& stockIds)`, `void setLookProvider(LookPreviewProvider*)`.
  - Films tiles: delegate properties `live` (a tile image exists), `fresh` (it matches the current look); box art stays as the placeholder and becomes an 18 px badge on live tiles.

- [ ] **Step 1: Failing script `desktop/tests/ui/v2_films_live.script`**

```
# Films tiles render the open photo per stock, go stale on an edit (old tiles stay
# up) and refresh; previews still win the worker; a new photo starts empty.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:tray.activeTab=films
waitfor:engine.lookTilesPending=0,60000
expect:filmTile_none.live=true
expect:filmTile_cinestill_400d.live=true
expect:filmTile_cinestill_400d.fresh=true
control:film_contrast=150
expect:filmTile_cinestill_400d.live=true
expect:filmTile_cinestill_400d.fresh=false
waitfor:engine.previewRevision=2,15000
waitfor:engine.lookTilesPending=0,60000
expect:filmTile_cinestill_400d.fresh=true
open:${SAMPLE_B}
waitfor:engine.currentFile=${SAMPLE_B},15000
expect:filmTile_cinestill_400d.live=false
waitfor:engine.lookTilesPending=0,60000
expect:filmTile_cinestill_400d.live=true
shot:${TEMP}/v2_films_live.png
quit
```

(`previewRevision=2`: the edit's preview lands — revision 1 was the open. If the open renders twice on this machine, read the actual revision after open in the first run and add one; ledger it.)

Run: `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_films_live.script`
Expected: FAIL from `waitfor:engine.lookTilesPending=0` (`<missing>`).

- [ ] **Step 2: Provider and worker slot**

`desktop/src/LookPreviewProvider.h`:

```cpp
#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Serves Films / Looks tiles: "image://look/<key>?e=<epoch>". The query only
// busts QML's cache; the key picks the image. Written on the GUI thread by
// EngineController, read on QML's image threads.
class LookPreviewProvider : public QQuickImageProvider {
public:
    LookPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    void setImage(const QString& key, const QImage& image) {
        QMutexLocker lock(&mutex_);
        images_.insert(key, image);
    }

    void clear() {
        QMutexLocker lock(&mutex_);
        images_.clear();
    }

    QImage requestImage(const QString& id, QSize* size, const QSize&) override {
        const QString key = id.section(QLatin1Char('?'), 0, 0);
        QMutexLocker lock(&mutex_);
        const QImage image = images_.value(key);
        if (size) *size = image.size();
        return image;
    }

private:
    QMutex mutex_;
    QHash<QString, QImage> images_;
};
```

`desktop/src/RenderWorker.h`, public slots, after `resolveAutoGrain`:

```cpp
    // A small render of the open photo in another stock (a Films tile).
    void renderLookProxy(const dfee::NativeLookProxyRequest& request, const QString& stockId, qulonglong epoch);
```

`desktop/src/RenderWorker.cpp`, at the end of the file:

```cpp
void RenderWorker::renderLookProxy(const dfee::NativeLookProxyRequest& request,
                                   const QString& stockId,
                                   qulonglong epoch)
{
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, true));
    QImage image;
    try {
        const dfee::NativeLookProxyResponse proxy = session_->render_look_proxy(request);
        if (proxy.ok && proxy.width > 0 && proxy.height > 0) {
            image = QImage(proxy.rgb8.data(), proxy.width, proxy.height, proxy.width * 3,
                           QImage::Format_RGB888).copy();   // own the pixels
        } else {
            qWarning().noquote() << "DFEE look tile failed" << stockId
                                 << QString::fromStdString(proxy.error.code)
                                 << QString::fromStdString(proxy.error.detail);
        }
    } catch (const std::exception& e) {
        qWarning().noquote() << "DFEE look tile failed" << stockId << e.what();
    }
    QMetaObject::invokeMethod(controller_, "onLookProxyReady", Qt::QueuedConnection,
                              Q_ARG(QString, stockId), Q_ARG(qulonglong, epoch), Q_ARG(QImage, image));
    QMetaObject::invokeMethod(controller_, "onWorkerBusyChanged",
                              Qt::QueuedConnection, Q_ARG(bool, false));
}
```

- [ ] **Step 3: Controller scheduling**

`desktop/src/EngineController.h`:
- forward-declare `class LookPreviewProvider;` next to `class PreviewImageProvider;`, and `#include <QImage>`, `#include <QHash>`, `#include <QSet>`;
- after the `presetGroups` Q_PROPERTY:

```cpp
    // Films tiles: stock id -> epoch of its ready tile image (image://look/<id>?e=<epoch>);
    // lookEpoch changes with every adjustment; pending = queued + in flight.
    Q_PROPERTY(QVariantMap lookTiles READ lookTiles NOTIFY lookTilesChanged)
    Q_PROPERTY(int lookEpoch READ lookEpoch NOTIFY lookTilesChanged)
    Q_PROPERTY(int lookTilesPending READ lookTilesPending NOTIFY lookTilesChanged)
```

- public (next to the other getters):

```cpp
    QVariantMap lookTiles() const;
    int lookEpoch() const { return static_cast<int>(lookEpoch_); }
    int lookTilesPending() const { return int(tileQueue_.size()) + (tileInFlight_.isEmpty() ? 0 : 1); }
    void setLookProvider(LookPreviewProvider* provider) { lookProvider_ = provider; }
    // The tiles the tray shows right now (visible group, in order). Empty stops tiles.
    Q_INVOKABLE void requestLookTiles(const QStringList& stockIds);
    Q_INVOKABLE void onLookProxyReady(const QString& stockId, qulonglong epoch, const QImage& image);
```

- signals: `void lookTilesChanged();`
- private:

```cpp
    void bumpLookEpoch();            // adjustments changed: tiles stale, re-queue
    void resetLookTiles();           // a new photo: drop every tile
    void rebuildTileQueue();
    void pumpLookTiles();            // dispatch one tile when the worker is idle
    LookPreviewProvider* lookProvider_ = nullptr;
    quint64 lookEpoch_ = 1;
    QHash<QString, quint64> tileEpochs_;   // stock -> epoch of its ready image
    QSet<QString> failedTiles_;            // failed this epoch: don't retry
    QStringList wantedTiles_;
    QStringList tileQueue_;
    QString tileInFlight_;
```

`desktop/src/EngineController.cpp`:
- `#include "LookPreviewProvider.h"`;
- in the constructor, after `refreshPresets();`:

```cpp
    // Every adjustment changes what a tile shows (tiles are "what a click gives").
    connect(this, &EngineController::filmControlsChanged, this, &EngineController::bumpLookEpoch);
```

- add the functions (after `onWorkerBusyChanged`):

```cpp
// ── Films tiles ─────────────────────────────────────────────────────────

namespace { constexpr int kLookTileEdge = 256; }

QVariantMap EngineController::lookTiles() const
{
    QVariantMap tiles;
    for (auto it = tileEpochs_.cbegin(); it != tileEpochs_.cend(); ++it) {
        tiles.insert(it.key(), static_cast<int>(it.value()));
    }
    return tiles;
}

void EngineController::requestLookTiles(const QStringList& stockIds)
{
    wantedTiles_ = stockIds;
    rebuildTileQueue();
    pumpLookTiles();
}

void EngineController::bumpLookEpoch()
{
    ++lookEpoch_;
    failedTiles_.clear();
    rebuildTileQueue();                  // old tiles stay visible until replaced
    pumpLookTiles();
}

void EngineController::resetLookTiles()
{
    ++lookEpoch_;
    tileEpochs_.clear();
    failedTiles_.clear();
    if (lookProvider_) lookProvider_->clear();
    rebuildTileQueue();
}

void EngineController::rebuildTileQueue()
{
    tileQueue_.clear();
    for (const QString& id : std::as_const(wantedTiles_)) {
        if (id == tileInFlight_ || failedTiles_.contains(id)) continue;
        if (tileEpochs_.value(id, 0) != lookEpoch_) tileQueue_.append(id);
    }
    emit lookTilesChanged();
}

// Tiles are the lowest priority: only when the worker is idle and nothing else
// waits — a pending open/preview (dirty_), a slider debounce, an export or an
// auto-grain resolve all go first. One tile in flight at a time.
void EngineController::pumpLookTiles()
{
    if (workerBusy_ || dirty_ || exporting_ || grainResolving_ || !hasImage_ || !lookProvider_ ||
        currentFile_.isEmpty() || previewDebounceTimer_.isActive() || tileQueue_.isEmpty()) {
        return;
    }
    const QString stock = tileQueue_.takeFirst();
    tileInFlight_ = stock;
    workerBusy_ = true;
    dfee::NativeLookProxyRequest request;
    request.look = buildPreviewRequest();
    request.look.stock = stock.toStdString();
    request.max_edge = kLookTileEdge;
    const qulonglong epoch = lookEpoch_;
    QMetaObject::invokeMethod(worker_, [worker = worker_, request, stock, epoch]() {
        worker->renderLookProxy(request, stock, epoch);
    }, Qt::QueuedConnection);
    emit lookTilesChanged();
}

void EngineController::onLookProxyReady(const QString& stockId, qulonglong epoch, const QImage& image)
{
    tileInFlight_.clear();
    if (epoch == lookEpoch_) {
        if (image.isNull()) {
            failedTiles_.insert(stockId);
        } else {
            if (lookProvider_) lookProvider_->setImage(stockId, image);
            tileEpochs_.insert(stockId, epoch);
        }
    } else if (wantedTiles_.contains(stockId) && !tileQueue_.contains(stockId)) {
        tileQueue_.append(stockId);      // rendered for an older look: do it again
    }
    emit lookTilesChanged();
}
```

- in `onWorkerBusyChanged`, the `if (!busy && dirty_) { … }` block gets an `else` branch so an idle worker picks up tiles:

```cpp
    } else if (!busy) {
        pumpLookTiles();
    }
```

(i.e. the function becomes `workerBusy_ = busy; if (!busy && dirty_) { …existing… } else if (!busy) { pumpLookTiles(); }`).

- photo changes drop tiles: in `openFile` immediately before `loadEditsFor(file);` and in `onWorkerBusyChanged`'s deferred-open branch immediately before `loadEditsFor(currentFile_);` add:

```cpp
            resetLookTiles();
```

- when the preview of a new photo lands the worker goes idle through `onWorkerBusyChanged(false)`, which pumps; no other hook is needed.

`desktop/src/main.cpp`: `#include "LookPreviewProvider.h"`; after `engine.addImageProvider("thumb", …);`:

```cpp
    auto* lookProvider = new LookPreviewProvider();
    engine.addImageProvider("look", lookProvider);   // engine takes ownership
    controller.setLookProvider(lookProvider);
```

`desktop/CMakeLists.txt`: add `src/LookPreviewProvider.h` to the `DFEE` sources (after `src/PreviewImageProvider.h`).

- [ ] **Step 4: Films strip shows live tiles and asks for them**

In `desktop/qml/v2/tray/FlFilmsStrip.qml`:
- after `readonly property string activeGroup: …` add:

```qml
    // Ask the engine for exactly the tiles on show; none while the strip is hidden.
    readonly property var wantedIds: (model || []).map(r => r.id)
    function requestTiles() { engine.requestLookTiles(visible ? wantedIds : []); }
    onWantedIdsChanged: requestTiles()
    onVisibleChanged: requestTiles()
    Component.onCompleted: requestTiles()
```

- in the delegate, after `readonly property bool current: …` add:

```qml
        readonly property int tileEpoch: engine.lookTiles[modelData.id] || 0
        readonly property bool live: tileEpoch > 0
        readonly property bool fresh: live && tileEpoch === engine.lookEpoch
```

- inside the `art` Rectangle, before the existing box-art `Image`, add the live tile:

```qml
            Image {
                anchors.fill: parent
                visible: tile.live
                source: tile.live ? "image://look/" + modelData.id + "?e=" + tile.tileEpoch : ""
                fillMode: Image.PreserveAspectCrop
                cache: false
                asynchronous: true
            }
```

  change the existing box-art `Image`'s `visible: modelData.id !== "none"` to `visible: modelData.id !== "none" && !tile.live`, and the "No film" `Text`'s `visible` to `modelData.id === "none" && !tile.live`;
- after the `art` Rectangle add the badge:

```qml
        Image {                                   // box-art badge on a live tile
            visible: tile.live && modelData.id !== "none"
            x: 6
            y: art.height - height - 6
            width: 18
            height: 18
            source: visible ? "qrc:/boxart/" + modelData.id + ".svg" : ""
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectCrop
        }
```

- [ ] **Step 5: Build and run**

Run: `cmake --build desktop/out/build --config Release`, then the Step 1 command, `v2_tray.script`, `v2_inspector.script`, `smoke.script`.
Expected: all `failures=0`. Check `v2_films_live.png`: Films tiles show the photo in each stock with small box-art badges.

- [ ] **Step 6: Commit**

```bash
git add desktop/src desktop/CMakeLists.txt desktop/qml/v2/tray/FlFilmsStrip.qml desktop/tests/ui/v2_films_live.script
git commit -m "feat(desktop): live Films tiles — the photo in every stock, rendered when idle"
```

---

### Task 3: Hover peek

**Files:**
- Modify: `desktop/src/EngineController.h`, `desktop/src/EngineController.cpp`
- Modify: `desktop/src/UiScript.cpp` (`hover:` step)
- Modify: `desktop/qml/v2/tray/FlFilmsStrip.qml`, `desktop/qml/v2/canvas/FlCanvas.qml`, `desktop/qml/v2/MainV2.qml`
- Create: `desktop/tests/ui/v2_peek.script`, `desktop/tests/ui/v2_peek_lr.script`

**Interfaces:**
- Consumes: Task 2 tiles.
- Produces: controller `Q_PROPERTY(QString peekStock READ peekStock NOTIFY peekChanged)`, `Q_INVOKABLE void beginPeek(const QString& stockId)`, `Q_INVOKABLE void endPeek()`; previews render `peekStock` when set; `setStock`, opening a photo and `exportImage` end any peek; UiScript `hover:<object>[@fx,fy]` (a mouse move); canvas chip `peekChip`.

- [ ] **Step 1: Add the `hover:` step to `desktop/src/UiScript.cpp`**

After `postClick` add:

```cpp
// A mouse move with no button: drives HoverHandlers (enter on the target, leave
// on whatever was hovered before).
void postHover(ScriptState& s, const QString& spec)
{
    const QString name = spec.section('@', 0, 0);
    const QString frac = spec.section('@', 1, 1);
    auto* item = qobject_cast<QQuickItem*>(resolveRoot(s, name));
    if (!item) { fail(s, "hover:" + spec, "missing"); return; }
    const double fx = frac.isEmpty() ? 0.5 : frac.section(',', 0, 0).toDouble();
    const double fy = frac.isEmpty() ? 0.5 : frac.section(',', 1, 1).toDouble();
    const QPointF p = item->mapToScene(QPointF(item->width() * fx, item->height() * fy));
    static ulong timestamp = 500000;
    QMouseEvent event(QEvent::MouseMove, p, p, s.window->mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    event.setTimestamp(timestamp += 20);
    QCoreApplication::sendEvent(s.window, &event);
}
```

and in `runNext`, after the `dclick` branch:

```cpp
    } else if (verb == QLatin1String("hover")) {
        postHover(*s, arg);
```

- [ ] **Step 2: Failing scripts**

`desktop/tests/ui/v2_peek.script`:

```
# Hover a film for ~120 ms: the canvas previews it, nothing is recorded; moving away
# or Esc restores; a click applies it as one history step. The catalog is untouched.
open:${SAMPLE_A}
waitfor:engine.hasImage=true,30000
expect:engine.history.length=1
hover:filmTile_cinestill_400d@0.5,0.3
waitfor:engine.peekStock=cinestill_400d,3000
expect:peekChip.visible=true
expect:engine.stock=none
waitfor:engine.previewRevision=2,15000
expect:engine.history.length=1
hover:photoCanvas@0.5,0.5
waitfor:engine.peekStock=,3000
expect:peekChip.visible=false
hover:filmTile_cinestill_50d@0.5,0.3
waitfor:engine.peekStock=cinestill_50d,3000
key:16777216
wait:200
expect:engine.peekStock=
expect:engine.stock=none
expect:engine.history.length=1
wait:600
expect:editStore.recordCount=0
hover:filmTile_cinestill_400d@0.5,0.3
waitfor:engine.peekStock=cinestill_400d,3000
click:filmTile_cinestill_400d@0.5,0.3
wait:300
expect:engine.stock=cinestill_400d
expect:engine.peekStock=
expect:engine.history.length=2
quit
```

`desktop/tests/ui/v2_peek_lr.script`:

```
# Lightroom Edit-In: tiles render and peek works in the round-trip window too.
waitfor:engine.hasImage=true,30000
expect:engine.lightroomRoundTrip=true
expect:tray.activeTab=films
waitfor:engine.lookTilesPending=0,60000
expect:filmTile_cinestill_400d.live=true
hover:filmTile_cinestill_400d@0.5,0.3
waitfor:engine.peekStock=cinestill_400d,3000
key:16777216
wait:200
expect:engine.peekStock=
expect:engine.stock=none
quit
```

Run: `cmake --build desktop/out/build --config Release` then `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/v2_peek.script`
Expected: FAIL at `waitfor:engine.peekStock=cinestill_400d` (`<missing>`).

- [ ] **Step 3: Controller peek**

`desktop/src/EngineController.h`: after the look Q_PROPERTYs:

```cpp
    // A film shown on the canvas while hovering its tile: changes only the preview
    // request — no history, no catalog write — until it is applied with setStock.
    Q_PROPERTY(QString peekStock READ peekStock NOTIFY peekChanged)
```

public: `QString peekStock() const { return peekStock_; }`, `Q_INVOKABLE void beginPeek(const QString& stockId);`, `Q_INVOKABLE void endPeek();`; signal `void peekChanged();`; private `QString peekStock_;`.

`desktop/src/EngineController.cpp`:

```cpp
void EngineController::beginPeek(const QString& stockId)
{
    if (stockId == stockId_) { endPeek(); return; }   // hovering the applied film
    if (peekStock_ == stockId || currentFile_.isEmpty()) return;
    peekStock_ = stockId;
    emit peekChanged();
    scheduleRender();
}

void EngineController::endPeek()
{
    if (peekStock_.isEmpty()) return;
    peekStock_.clear();
    emit peekChanged();
    scheduleRender();
}
```

- in `buildPreviewRequest()`, replace `request.stock = stockId_.toStdString();` with

```cpp
    request.stock = (peekStock_.isEmpty() ? stockId_ : peekStock_).toStdString();
```

  (`buildExportRequest` starts from `buildPreviewRequest`, hence the export rule below).
- at the top of `setStock(const QString& id)`, before `if (stockId_ == id) return;`:

```cpp
    if (!peekStock_.isEmpty()) {          // applying a film ends its peek
        peekStock_.clear();
        emit peekChanged();
        if (stockId_ == id) { scheduleRender(); return; }
    }
```

- at the top of `exportImage()`, after `if (exporting_) return;`: `endPeek();   // export the applied film, never a hovered one`
- in `openFile` and the deferred-open branch, next to `resetLookTiles();`:

```cpp
            if (!peekStock_.isEmpty()) { peekStock_.clear(); emit peekChanged(); }
```

- [ ] **Step 4: QML — hover timing, Esc, canvas chip**

`desktop/qml/v2/tray/FlFilmsStrip.qml`, after the `Component.onCompleted` line:

```qml
    // Hover ~120 ms to preview a film on the canvas; leaving the tiles restores.
    property string hoverId: ""
    Timer {
        id: peekTimer
        interval: 120
        onTriggered: strip.hoverId.length > 0 ? engine.beginPeek(strip.hoverId) : engine.endPeek()
    }
    onHoverIdChanged: peekTimer.restart()
```

and change `onVisibleChanged: requestTiles()` to

```qml
    onVisibleChanged: { requestTiles(); if (!visible) { hoverId = ""; engine.endPeek(); } }
```

in the delegate's `HoverHandler { id: tileHover }` add a handler:

```qml
        HoverHandler {
            id: tileHover
            onHoveredChanged: {
                if (hovered) strip.hoverId = modelData.id;
                else if (strip.hoverId === modelData.id) strip.hoverId = "";
            }
        }
```

`desktop/qml/v2/MainV2.qml`, with the other Shortcuts:

```qml
    Shortcut { sequence: "Esc"; enabled: engine.peekStock.length > 0 && !canvas.cropMode; onActivated: engine.endPeek() }
```

`desktop/qml/v2/canvas/FlCanvas.qml`, before the crop toolbar:

```qml
    // While a film tile is hovered: say what the canvas shows and how to keep it.
    Rectangle {
        objectName: "peekChip"
        visible: engine.peekStock.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        width: peekText.implicitWidth + 24
        height: 26
        radius: 13
        color: "#e6242426"
        border.width: 1
        border.color: Theme.hairline
        readonly property string filmName: {
            const m = engine.stockModel;
            for (let i = 0; i < m.length; ++i) if (m[i].id === engine.peekStock) return m[i].name;
            return engine.peekStock;
        }
        Text {
            id: peekText
            anchors.centerIn: parent
            text: "Previewing " + parent.filmName + " · click to apply · Esc to cancel"
            color: Theme.text
            font.pixelSize: Theme.fontCaption
        }
    }
```

- [ ] **Step 5: Build and run**

Run: `cmake --build desktop/out/build --config Release`, then `v2_peek.script`, `v2_peek_lr.script` (via `-Command … -AppArgs @('--lightroom-edit','${SAMPLE_TIFF}')`), `v2_films_live.script`, `v2_tray.script`, `v2_keys.script`, `v2_crop_mode.script`.
Expected: all `failures=0`.

- [ ] **Step 6: Commit**

```bash
git add desktop/src desktop/qml/v2 desktop/tests/ui/v2_peek.script desktop/tests/ui/v2_peek_lr.script
git commit -m "feat(desktop): hover a film tile to preview it on the canvas; click applies, Esc restores"
```

---

### Task 4: Whole-suite run, installer, on-screen check

**Files:**
- Modify: `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` (status)

- [ ] **Step 1: Mark Phase 2 in the spec**

In the Phasing list change `2. Live Films browser (scheduler, proxy tiles, hover peek).` to `2. (Done 2026-10-07: plan 2026-10-07-phase2-live-films.) Live Films browser (scheduler, proxy tiles, hover peek).`

- [ ] **Step 2: Everything green**

Run the engine tests (`dfee_session_tests`, `dfee_tests`), every desktop UI script (no `-Ui`; `gallery` with `-Ui gallery`; the Lightroom ones and `v2_peek_lr` via `-AppArgs @('--lightroom-edit','${SAMPLE_TIFF}')`; the persistence pair with one `-UiSettings` ini; `v2_minsize` with `--min-size`) and the three Qt unit suites.
Expected: every script `failures=0`; engine `all passed` with goldens 24/24; unit totals 11/6/5.

- [ ] **Step 3: Commit, installer, look at it**

```bash
git add docs/superpowers/specs/2026-09-27-ui-redesign-design.md
git commit -m "docs: phase 2 (live Films browser) done"
```

Close any running Film Lab, then `desktop/packaging/deploy.ps1` and `"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" desktop\packaging\FilmLab.iss`. Run the deployed app once on screen (a script with `open`, `waitfor:engine.lookTilesPending=0`, `shot`, `quit`; `QT_QPA_PLATFORM` unset) and look at the Films tiles.
