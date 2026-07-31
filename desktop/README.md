# DFEE Desktop (Qt 6)

Native desktop DFEE — a Qt 6 (C++/QML) application that links the C++ engine
(`../cpp_engine` → `dfee_core`) directly and is invoked by Lightroom via the
external-editor round-trip. This replaces the React/Python stack for the shipped
product; that stack stays around only as a dev harness during the transition.

This folder is intentionally **self-contained** and separate from `cpp_engine/`,
`frontend/`, and the Python code, so the two worlds don't tangle.

## Layout
```
desktop/
  CMakeLists.txt     # Qt app target; links dfee_core from ../cpp_engine
  src/               # C++: main.cpp, EngineController (QObject bridge to EngineSession)
  qml/               # QML UI (Graphite look), components
  resources/         # icons, .qrc, boxart, fonts
```

## Prerequisites
- **Qt 6** (Online Installer): the **MSVC 2022 64-bit** kit + **Qt Quick**, matching
  the engine's MSVC toolchain. Qt is used under **LGPLv3, dynamically linked**.
- CMake ≥ 3.21 and the same vcpkg toolchain the engine uses (for OpenCV/LibRaw/yaml-cpp).

## Build (once wired up in Phase 1)
Configured via CMake with `-DCMAKE_PREFIX_PATH=<path/to/Qt6>` and the vcpkg toolchain;
builds `dfee_core` (static) + `DFEE` (Qt executable).

See the design spec: `docs/superpowers/specs/2026-07-31-native-app-foundation.md`.

## Current Film Lab Controls

The desktop app drives `filmic_v3` directly through immutable native request
snapshots. The primary workflow currently includes Film Recipe, Film Exposure
(scene placement and stock-relative exposure), Film Tone, Color Character, and
Material Finish. Color controls automatically disable for monochrome stocks.

Material Finish keeps grain matched to the selected film speed by default.
Turning that option off resolves the active stock and image through the native
solver, then seeds editable Strength, Size, and Roughness controls with the
matching Custom values. Preview and TIFF export use the same snapshot, so an
export cannot accidentally omit a recent Film Lab adjustment.
