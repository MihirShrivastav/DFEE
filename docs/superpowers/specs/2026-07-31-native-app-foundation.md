# DFEE Native Desktop — Phase 1 Foundation (design spec)

Date: 2026-07-31 · Branch: `qt-desktop` · Status: approved for planning

## Context & motivation

DFEE today is a C++ engine (`cpp_engine` → `dfee_core`) wrapped by a Python/FastAPI
middleware and a React UI. For the shipped product we're pivoting to a **native Qt 6
desktop app** that links the C++ engine directly — dropping the Python HTTP layer and
the React UI for speed and to match how pro plugins (Dehancer et al.) ship. Going
native also *simplifies* Lightroom integration: LR Classic's external-editor round-trip
expects a native executable, which is exactly what we'll build.

This is a multi-phase product. **This spec covers Phase 1 only**: a thin vertical slice
that proves the Qt ↔ engine architecture end-to-end. Later phases (full UI, Lightroom
round-trip, packaging, GPU) get their own specs.

## Goals (Phase 1)

A native `DFEE` executable that:
1. Opens a TIFF or RAW file.
2. Lists the film stocks from the engine and lets the user pick one.
3. Exposes two controls (Film Exposure, Shadow Lift) wired to the engine.
4. Shows a **live proxy preview** that updates on any change, off the UI thread.
5. Exports the result to a TIFF.
6. Uses the dark "Graphite" look (design language ported to QML).

**Success:** it feels instant/native, and proves Qt links `dfee_core` and drives the
`EngineSession` pipeline with no Python involved.

## Non-goals (Phase 1)

Lightroom round-trip; the full control set; GPU compute; packaging/installer;
licensing/activation; macOS; and retiring the existing Python/React stack (it remains
untouched as a dev harness).

## Architecture

- **`dfee_core`** (existing static lib) is reused as-is. Public API already suits a
  non-Python caller: `list_profiles()`, `select_file()`, `decode_raw()`,
  `render_preview()`, `export_image()` (`cpp_engine/include/dfee/session.hpp`).
  `dfee_cli` already links it, proving a native C++ frontend works.
- **`desktop/`** — new, self-contained Qt 6 app:
  - **`CMakeLists.txt`** — defines the `DFEE` Qt Quick executable, pulls in the engine
    (`add_subdirectory(../cpp_engine)` for `dfee_core`), links `Qt6::Quick`/`Qt6::Gui`,
    dynamically (LGPLv3). Uses the same vcpkg toolchain as the engine.
  - **`src/EngineController`** — a `QObject` bridge owning one `EngineSession`. Exposed
    to QML with: `openFile(url)`, `stocks` (model from `list_profiles`), `stock` (set →
    re-render), `filmExposure`/`shadowLift` (set → re-render), `exportImage(url)`, and a
    `previewReady`/preview-image mechanism.
  - **`src/PreviewImageProvider`** — a `QQuickImageProvider` serving the latest rendered
    preview to the QML `Image`.
  - **`src/main.cpp`** — bootstraps the QML engine, registers the controller + provider.
  - **`qml/Main.qml`** + components — the Graphite UI (canvas + stock dropdown + two
    sliders + open/export buttons).

### Data flow
open file → `EngineController` runs `select_file` + `decode_raw` on a **worker thread**
→ on control/stock change, debounce → `render_preview` (proxy size) on the worker thread
→ result handed to `PreviewImageProvider` → QML `Image` refreshes. Export runs
`export_image` on the worker thread and writes a TIFF. The UI thread never blocks.

### Threading
A single serialized worker (Qt `QThread`/`QtConcurrent` + a pending-request coalescer)
so rapid slider drags collapse to the latest render. Engine calls are not called
concurrently with themselves.

### Preview transport
`render_preview` currently returns encoded bytes (JPEG). Phase 1 decodes those into a
`QImage` for display — simplest path. (A raw-buffer fast path is a later optimization.)

## Folder structure (created on this branch)
```
desktop/
  README.md
  CMakeLists.txt      # (added in implementation)
  src/                # main.cpp, EngineController.{h,cpp}, PreviewImageProvider.{h,cpp}
  qml/                # Main.qml, components/
  resources/          # app.qrc, icons, fonts
```
Kept fully separate from `cpp_engine/`, `frontend/`, and the Python code.

## Build & prerequisites
- **Qt 6** via the Online Installer: **MSVC 2022 64-bit** kit + **Qt Quick**, matching
  the engine's MSVC toolchain. Dynamically linked (LGPLv3 compliance: ship Qt DLLs +
  license notices; users can replace the Qt libs).
- CMake ≥ 3.21; the engine's existing **vcpkg** toolchain (OpenCV/LibRaw/yaml-cpp).
- Configure: `cmake -S desktop -B desktop/out -DCMAKE_PREFIX_PATH=<Qt6> -DCMAKE_TOOLCHAIN_FILE=<vcpkg>` then build target `DFEE`.
- **Step 0 (user):** confirm/install Qt 6 and provide its path. The agent cannot install Qt.

## Error handling
- Unsupported/failed decode → non-blocking error banner in the UI; app stays usable.
- Export failure (path/permissions) → error dialog with the message from the engine.
- Missing Qt at configure time → clear CMake error pointing at `CMAKE_PREFIX_PATH`.

## Testing
- Engine correctness stays covered by the existing `ctest` (unchanged).
- Phase 1 app: a smoke path — launch, open a bundled sample, pick a stock, nudge a
  control (preview changes), export (file written). Automated UI tests are deferred; a
  documented manual smoke checklist ships with this phase.

## Risks
- **Qt toolchain/setup** on Windows (MSVC kit must match the engine build). Mitigate with
  explicit prereqs + a known-good CMake preset.
- **QML ramp-up** for richer components later (fine for this slice).
- **Preview latency** if JPEG-encode/decode per frame is slow → raw-buffer path later.

## Phasing (product roadmap; each its own spec)
1. **Foundation (this spec)** — Qt↔engine vertical slice.
2. **Native UI MVP** — full control set + stock picker/boxart, Graphite parity.
3. **Lightroom round-trip** — external editor: open handed-off TIFF, write back, restack.
4. **Packaging** — installer, codesigning/notarization, licensing/activation; then other
   hosts.
