## Task 5 Report: Export to TIFF

### Output-location branch chosen: no-path `exportImage()`

**Why:** `NativeExportRequest` (in `cpp_engine/include/dfee/bridge_types.hpp`) inherits
`NativePreviewRenderRequest` and adds only `export_format`, `jpeg_quality`, `export_dpi`,
`embed_metadata`, and `export_color_space`. There is **no output-path field**.

Confirmed in `cpp_engine/src/session.cpp` at the `export_image_write_output` stage (line 2999):

```cpp
// Write exports next to the source file.
const std::filesystem::path output_dir = raw_path.parent_path();
response.output_path = output_dir / (basename + "_" + stock_label + "_dfee.tif");
```

The engine always writes beside the source file and returns the path in
`NativeExportResponse::output_path`. No `FileDialog` is needed — the user gets the
exported path back in the status line.

### Implementation

**Files changed:**

- `desktop/src/EngineController.h` — added `Q_INVOKABLE void exportImage()` and
  `Q_INVOKABLE void onExportDone(const QString& msg)`.
- `desktop/src/EngineController.cpp` — implemented both: `exportImage()` queues
  `worker_->exportImage(...)` via `QMetaObject::invokeMethod` (Qt::QueuedConnection) so
  the call always runs on the worker thread; `onExportDone` sets `status_` and emits
  `statusChanged()`.
- `desktop/src/RenderWorker.h` — added `exportImage(file, stock, ev, lift)` slot.
- `desktop/src/RenderWorker.cpp` — implemented `exportImage`: builds
  `NativeExportRequest`, calls `session_->export_image(req)`, marshals result back to
  GUI thread via `onExportDone`.
- `desktop/qml/Main.qml` — added "Export TIFF" button (enabled when `engine.hasImage`)
  that calls `engine.exportImage()`. No save FileDialog — the engine writes beside the
  source. Exported path appears in the existing status line.
- `desktop/src/main.cpp` — extended the `DFEE_SELFTEST_EXPORT` env gate to trigger
  `exportImage()` at T+7 s and write the result to a log file (path = value of the env
  var) so headless verification doesn't depend on `OutputDebugString`.

### Headless verification

```
DFEE_SELFTEST  = d:/Codebases/DFEE/comparision/2316908974.tif
DFEE_SELFTEST_EXPORT = d:/tmp/dfee_export_test.log
QT_QPA_PLATFORM = offscreen
```

Log file contents after run:
```
Exported: d:/Codebases/DFEE/comparision\2316908974_none_dfee.tif
```

Exported file: `d:/Codebases/DFEE/comparision/2316908974_none_dfee.tif`
Size: **146,337,618 bytes (139.6 MB)** — 16-bit TIFF, full-resolution.

File confirmed written and cleaned up (removed from repo working tree before commit).
