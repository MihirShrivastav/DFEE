# Film Lab export — design

## Context

Today's export sheet offers format, JPEG quality and TIFF dpi, and always saves next to the
original (`<stem>_<stock>_dfee.jpg`), silently overwriting. It also writes a
`_report.json` beside the source. Nothing is remembered between launches: format, quality
and dpi reset every time. The user wants export to be the best part of the app, covering
what Lightroom can't do and what photographers currently finish in other apps.

What the user asked for:
- A default **Film Lab Exports** folder.
- If they change the folder, it is **remembered**; folders can be saved as **favourites**.
- Above all, **borders**: placing the photo on a **white canvas of a standard size**
  (print and social sizes).
- File names, resizing "etc etc".

Lightroom Edit-In stays as is: Ctrl+S saves the TIFF straight back.

What the code gives us (surveyed):
- **Engine** (`cpp_engine/src/session.cpp:3104-3722`, `EngineSession::export_image`):
  - renders float linear sRGB at full size;
  - applies geometry last (`:3516`);
  - quantises with `linear_to_srgb_channel` (`:3596-3633`) and writes with `cv::imwrite`;
  - the temp file is moved into place atomically (`replace_file_atomically`, `:1084`);
  - DPI is written for PNG (pHYs) and JPEG (JFIF), at `:380-527`;
  - `output_path` (absolute, parent must exist, extension must match) already lets the
    caller pick the file;
  - `resize_image_to_max_edge` (`:530`, INTER_AREA, linear light) is reusable.
- **Engine gaps:**
  - `export_color_space` is ignored (always sRGB);
  - no EXIF is copied;
  - no text rendering (only Hershey `cv::putText`);
  - the temp file is left behind on encode failure;
  - the report is always written beside the source.
- **Desktop:**
  - `EngineController::exportImage/buildExportRequest` (`EngineController.cpp:878-968`);
  - `RenderWorker::exportImage` (`RenderWorker.cpp:161`);
  - `FlExportSheet.qml` / `FlSheet.qml`;
  - a `FolderDialog` pattern in `MainV2.qml:139`;
  - a QtCore `Settings` pattern (`location: uiSettingsLocation`, isolated in tests);
  - `engine.imageInfo` has camera/lens/iso/shutter/aperture/focal but **no capture date**;
  - the Roll has **no multi-select** (that is Phase 5).

## Design

### The export sheet (standalone mode)
- **Layout:**
  - one wide sheet (about 880 px), with a **live preview** of the exact output on the left:
    canvas, border and photo;
  - settings on the right in short sections: **Preset · Destination · File name · Size ·
    Border · Format**;
  - a summary line above Export, e.g. `DSC0421_Portra400.jpg · 2400×3000 px · 8×10 in @ 300 dpi`.
- **One screen, not a step-by-step wizard:** repeat exports are a single click.
- **Warnings** appear in the summary: "Enlarged 140%", "Below 200 dpi at this print size", "Will
  replace an existing file".

### Destination
- **Default folder:** `Pictures\Film Lab Exports`, created on first use.
- **Folder list:** recent and favourite folders in a dropdown (☆ to pin), "Choose…" (FolderDialog)
  and "Next to the original".
- **Remembered:** the last folder used.
- **Optional subfolder:** none / by film / by date.
- **After export:** "Show in folder" in the status line (opens Explorer with the file selected).

### File name
- **Template** built from tokens (`{look}` arrives with Phase 3, `{size}` with E2): `{name}` (original stem), `{film}`, `{look}`, `{date}`
  (capture date, falling back to the file date), `{seq}` (zero-padded counter), `{camera}`,
  `{size}` (e.g. 8x10) and free text.
- **Default:** `{name}_{film}`.
- **Live example** shown under the field.
- **Characters Windows forbids** are replaced with `-`.
- **If the name already exists:** *Add number* (default, `-2`, `-3`…), *Replace*, or *Skip*.

### Size
- **Modes:**
  - **Original**;
  - **Long edge** (px);
  - **Short edge**;
  - **Width × height** (fit inside);
  - **Megapixels**;
  - **Percent**;
  - **Print size**: W × H in inches or cm at a chosen dpi.
- **Don't enlarge:** on by default.
- **Resampling** happens in linear light: INTER_AREA when shrinking, Lanczos when enlarging.
- **Sharpen for:** Off / Screen / Matte paper / Glossy paper. This is a small unsharp mask
  after resizing, with its radius scaled to the output pixels.

### Border (the priority)
- **None.**
- **Even border:** a uniform border as a % of the short side (or px / mm). The canvas grows to
  fit it.
- **Canvas:** the photo is placed on a canvas of a standard size.
  - **Print sizes:**
    - 4×6, 5×7, 8×10, 11×14, 12×18, 13×19 in;
    - A5, A4, A3;
    - square 8×8 and 12×12.
  - **Social sizes:** Instagram 4:5 (1080×1350), Square (1080×1080), Story 9:16 (1080×1920).
  - **Custom:** W × H.
  - The canvas follows the photo's orientation automatically (a portrait photo gets a portrait
    canvas), with a manual override.
  - **Minimum border:** the gap on the tightest side, as a % of the short side (default 5%).
    The photo fits inside it, centred.
  - **Placement:** *Centred* or *Gallery* (a deeper bottom margin, like a mat or instant print).
- **Colour:** White / Paper (warm off-white) / Black / custom.
- The **print size and dpi** of the canvas set the output pixels and the written DPI.

### Format
- JPEG (quality, plus an optional "limit file size to N MB", which finds the quality by binary search).
- 8-bit PNG, 16-bit PNG, 16-bit TIFF.
- **Colour:** sRGB only for now (the engine has no other output space yet).

### Presets
- **Built-in presets:**
  - "Instagram 4:5 white border";
  - "Instagram square";
  - "Print 8×10 white";
  - "Full size JPEG";
  - "Full size TIFF".
- **User presets:** "Save as preset…", plus rename and delete.
- **Each preset stores:** size, border, format and naming. The destination is not stored.

### Later (listed, not in this plan)
- Captions (camera · lens · f/2 · 1/250 · ISO · film) and a signature or watermark: Qt-rendered
  text composited into the border.
- A film-edge frame (a negative-carrier border with the stock's edge markings).
- Batch export of selected Roll photos (comes with Phase 5's multi-select; built on the same
  per-photo request builder).
- One click making several outputs.
- Copying EXIF (needs exiv2).
- Display P3 or Adobe RGB output.

## Architecture

Split the work so each side does what it is good at.

### Desktop decides the layout: pure, unit-tested C++, no engine
- **`desktop/src/ExportOptions.{h,cpp}`:**
  - a struct with `fromVariant` / `toVariant` (QML passes a QVariantMap);
  - it holds size mode, border, format, naming, collision rule, sharpen and preset name.
- **`desktop/src/ExportLayout.{h,cpp}`:**
  - `compute(sourceW, sourceH, options) → {canvasW, canvasH, imageRect, dpi, upscale,
    warnings}`;
  - the source size is the post-crop size (`imageInfo` width/height × the crop rect,
    90° rotation swaps them);
  - the same function drives the sheet preview and the engine request, so the preview and
    the file always match.
- **`desktop/src/ExportNaming.{h,cpp}`:**
  - `resolve(template, tokens) → filename`;
  - `sanitize`;
  - `uniquePath(dir, name, rule)` for the add-number / replace / skip rule.
- **`EngineController`:**
  - `Q_INVOKABLE QVariantMap planExport(QVariantMap options)` gives the sheet its live summary,
    example name and warnings;
  - `Q_INVOKABLE void exportImage(QVariantMap options)`: the sheet's call;
  - `exportImage()` with no arguments keeps the Lightroom path unchanged.
  - `buildExportRequest(options)`:
    - sets `output_path` from the folder plus the resolved name;
    - creates the folder (`QDir::mkpath`);
    - adds the layout, and sets `write_report=false`.
- **`onExportDone`** keeps the path of the last exported file for "Show in folder"
  (`QProcess` `explorer /select,`).

### Engine places pixels: one new stage, the preview untouched
- **Request:** `NativeExportRequest` gains a `NativeExportLayout`:

  ```
  bool enabled; int canvas_w, canvas_h; int image_x, image_y, image_w, image_h;
  std::array<float,3> canvas_rgb_srgb; bool allow_upscale;
  float sharpen_amount; float sharpen_radius_px;
  ```

  plus `bool write_report = true` (the Python and server callers keep today's behaviour).
- **Where it runs:** in `export_image` after `apply_geometry` (`session.cpp:3516`) and before
  quantisation, in a new `apply_export_layout(Image, layout)`.
  - Resize the photo to fit `image_w×image_h` with its aspect kept: INTER_AREA when shrinking,
    Lanczos4 when enlarging, all in linear light.
  - Optional unsharp mask.
  - Fill a canvas with the linear version of the canvas colour, then copy the photo in at
    (`image_x`, `image_y`).
  - Disabled layout: today's bytes exactly.
- **Memory preflight:** `estimate_export_peak_bytes` adds a canvas term.
- **Fixes while we're here:**
  - delete the temp file on an encode or metadata failure;
  - report path is honoured or skipped via `write_report`.
- **Capture date:** add `capture_time` (ISO-8601 string) to `NativeRawMetadata`, from LibRaw
  `imgdata.other.timestamp`, and pass it into `imageInfo.date` for `{date}`.

### Persistence
- **Where settings live:** a C++ `ExportPrefs` object (context property `exportPrefs`), saved
  with `QSettings` group `export`. It writes to the `DFEE_UI_SETTINGS` ini when that is set
  (tests), else the app's settings. *Amended 2026-10-07 (E1 plan): C++ rather than a QML
  `Settings`, because the export request is built in C++ and needs the folder, name rule and
  format without a QML round trip.*
- **What it holds:** folder (or next-to-original), name template, collision rule, format,
  JPEG quality, dpi, favourite folders, recent folders (most recent first, up to 6), the
  `{seq}` counter, and later the size/border options and user presets.
- Format, quality and dpi move from `EngineController` members into `ExportPrefs`. The
  Lightroom path keeps forcing 16-bit TIFF.
- `DFEE_EXPORT_DIR` overrides the default folder (tests never touch the real Pictures folder).

## Phases (each shipped, tested, and the installer rebuilt)

**E0 — Spec + plan docs.**
- Write `docs/superpowers/specs/2026-10-07-export-design.md` from this plan.
- Then an implementation plan (writing-plans); commit both.

**E1 — Destination, naming, remembering.**
- `ExportOptions` and `ExportNaming` with a unit-test target (`export_tests`).
- Default folder, last folder, favourites and recents.
- File-name template with a live example.
- Collision rule.
- "Show in folder".
- Format, quality and dpi remembered.
- Engine: `write_report`, and temp-file cleanup.
- Capture date into `imageInfo`.

**E2 — Size.**
- `ExportLayout` (size modes, print size and dpi, never enlarge).
- Engine `apply_export_layout` (resize plus output sharpen).
- Summary line and warnings.

**E3 — Border and canvas.**
- Even border, canvas presets (print and social), auto orientation, minimum border,
  centred or gallery placement, colour.
- The live preview pane in the sheet (QML draws the canvas plus the current preview image
  using `planExport`'s rects).

**E4 — Presets and polish.**
- Built-in and user presets.
- JPEG file-size limit.
- Sheet layout and keyboard flow.
- DESIGN.md update.

## Critical files
- `cpp_engine/include/dfee/bridge_types.hpp`: `NativeExportRequest`, `NativeRawMetadata`.
- `cpp_engine/src/session.cpp`: `export_image`, the new `apply_export_layout`, preflight,
  temp cleanup, report.
- `cpp_engine/src/raw_decode.cpp`: capture time.
- `cpp_engine/tests/test_session.cpp`: export tests, plus a new `tests/golden/export/`.
- `desktop/src/EngineController.{h,cpp}`, and `RenderWorker.cpp` if needed.
- `desktop/src/ImageInfo.cpp`.
- New: `desktop/src/Export{Options,Layout,Naming}.{h,cpp}` and `desktop/tests/export_test.cpp`.
- `desktop/qml/v2/dialogs/FlExportSheet.qml`, rewritten. New pieces as needed, for example
  `FlExportPreview.qml` and `FlFolderPicker.qml`. A dropdown is built on `FlListPopup`.
- `desktop/CMakeLists.txt`, `desktop/DESIGN.md`.

## Verification
- **Engine:** `dfee_session_tests` gets new throwing checks.
  - **Disabled layout:** the bytes are identical to today's output.
  - **Long-edge resize:** gives the exact dimensions.
  - **Canvas 8×10 at 300 dpi:** output 2400×3000 (or 3000×2400 for a landscape canvas), the
    border pixels are exactly the canvas colour, the photo sits in the requested rect, and the
    DPI chunk reads 300.
  - **Never enlarge:** holds.
  - **Encode failure:** leaves no temp file.
  - **Report:** none is written when `write_report=false`.
  - **Preview goldens:** still 24/24.
  - Only `dfee_session_tests` / `dfee_tests` are built.
- **Desktop unit tests (`export_tests`):**
  - naming tokens, sanitising, and the add-number / replace / skip rules in a temp folder;
  - layout maths across every canvas preset × portrait/landscape × minimum border × gallery
    placement, including rounding and never-enlarge.
- **UI scripts (offscreen):**
  - **Remembering:** a pair of runs; folder, favourite and options survive a restart.
  - **Sheet:** the summary text matches the layout, and the preview objects' sizes match the
    rects.
  - **Exports:** none in tests. Only a preflight-refused export is used
    (`DFEE_NATIVE_EXPORT_MEMORY_BUDGET_MB=1`), to check the resolved path is passed through.
    Real exports are run only with the user's OK.
- **Release and installer:** rebuild Release and regenerate the installer after each phase.
  Close Film Lab first.

## Decisions (approved 2026-10-07)
- One screen, not a step-by-step wizard.
- The default folder is in Pictures, not Documents.
- The default minimum border is 5% of the short side, centred, in white.
- Captions, the film-edge frame, batch export, EXIF and wide gamut are deferred.
- Export settings live in UI Settings, not in the catalog.
