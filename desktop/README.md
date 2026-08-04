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
Material Finish. Film Tone provides adaptive scene tone, highlight rolloff, film
contrast, and Shadow Lift. Film Profile Strength is stock-aware: 100 is the
profile's authored baseline, while lower and higher values soften or reinforce
its toe, midtone, and shoulder response within safe profile/family limits.
Color Character provides the live supported controls:
Color Density, Color Boost, Highlight Saturation, and Shadow Saturation. These
color controls automatically disable for monochrome stocks.

Material Finish keeps grain matched to the selected film speed by default.
Turning that option off resolves the active stock and image through the native
solver, then seeds editable Strength, Size, and Roughness controls with the
matching Custom values. Preview and TIFF export use the same snapshot, so an
export cannot accidentally omit a recent Film Lab adjustment.

## Export

Exports are written beside the source image. Choose 8-bit PNG, 16-bit PNG,
16-bit TIFF, or JPEG from the Film Lab inspector. JPEG exposes a quality
setting and TIFF exposes its output DPI. The app blocks duplicate requests and
shows an indeterminate full-resolution export state until the native engine
reports either the resulting path or an error.

Current TIFF output is 16-bit RGB and intentionally uncompressed for
compatibility. Lossless TIFF compression is a separate export-engine task.

## Lightroom Classic Round-Trip

Configure Lightroom Classic's **Additional External Editor** to launch `DFEE.exe`, then
use `Photo > Edit In > DFEE`. The application receives Lightroom's rendered working
TIFF as a standard positional argument:

```text
DFEE.exe "C:\\path\\to\\working-file.tif"
```

In this mode, the regular export controls are intentionally replaced with **Save &
Return to Lightroom**. DFEE encodes a temporary sibling and atomically replaces only
the working TIFF on success, never the original RAW/DNG, then closes so Lightroom can
refresh its external-edit session. Failed exports keep DFEE open.

Phase A requires Lightroom to create an **uncompressed 16-bit sRGB TIFF**. The engine
does not yet retain embedded ICC profiles, so ProPhoto RGB, Adobe RGB, HDR, and custom
profiles are not supported for this handoff yet. Full setup, safety behavior, and the
wide-gamut follow-up are in
[`docs/superpowers/specs/2026-07-31-lightroom-classic-roundtrip.md`](../docs/superpowers/specs/2026-07-31-lightroom-classic-roundtrip.md).
