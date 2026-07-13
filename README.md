# Deterministic Film Emulation Engine (DFEE)

DFEE is a high-fidelity, scene-adaptive film stock emulation engine. Instead of relying on neural networks or generic lookup tables (LUTs), DFEE parses RAW sensor data, extracts statistical exposure and spatial metrics, and applies a deterministic chemical and optical emulation pipeline in the perceptually uniform **OKLab** color space.

## Key Features

1. **Scene-Adaptive Solver**: Dynamically scales tone curves, highlight desaturation, grain density, and halation width from the image's dynamic range and clipping characteristics.
2. **7-Zone Exposure Weight System**: Partitions exposures into continuous zones (`Z0` to `Z6`) using a softmax-gaussian partition of unity to drive region-specific film behavior.
3. **Neutral-Axis Estimation**: Detects input color cast from low-chroma, unclipped midtone pixels and computes correction vectors weighted by confidence.
4. **Organic Grain and Halation Emulation**: Simulates silver halide grain structure and specular highlight red/orange halos.
5. **Real-Time Web UI (FastAPI + React)**: Provides interactive before/after previewing, Lightroom-style controls, optional print-stock finishing, and 16-bit export.

## Directory Layout

```text
DFEE/
|-- dfee/                   # Core Python library
|   |-- __init__.py
|   |-- analyzer.py         # 7-zone masks and spatial texture metrics
|   |-- bias.py             # Neutral-axis color cast detection
|   |-- color_spaces.py     # Linear RGB <-> OKLab <-> OKLCH transforms
|   |-- engine.py           # Single-image orchestrator
|   |-- ingest.py           # rawpy-based scene-linear ingestion
|   |-- profile.py          # YAML schema validation
|   |-- renderer.py         # Ordered emulation modules
|   |-- report.py           # Sidecar JSON reporter
|   `-- solver.py           # Adaptive render-plan solver
|-- profiles/
|   |-- stocks/             # Film stock profiles
|   `-- print_stocks/       # Optional print-finish profiles
|-- raw_files/              # RAW inputs and exported files
|-- frontend/               # Vite + React UI
|-- tests/                  # Pytest suite
|-- server.py               # FastAPI backend and React host
|-- cli.py                  # Command-line interface
|-- README.md
|-- STARTUP.md
`-- start.bat
```

## Technical Notes

### Color Conversions
Rendering happens in **OKLab** and **OKLCH** so DFEE can manipulate lightness, chroma, and hue with fewer unwanted color shifts.

### Continuous Zone Softmax
Exposure zone masks sum exactly to `1.0` across the image:

```text
w_i(EV) = exp(-(EV - c_i)^2 / 2sigma^2) / sum_j exp(-(EV - c_j)^2 / 2sigma^2)
```

`Z0` and `Z6` use boundary envelopes so the weighting remains stable at extreme exposures.

## Development and Testing

```powershell
$env:PYTHONPATH="."
pytest tests/test_dfee.py
```

By default, DFEE now prefers the native backend across every migrated FastAPI route.
To force that preference explicitly:

```powershell
$env:DFEE_USE_NATIVE_ENGINE="1"
python server.py
```

The native-default path covers:
- `/api/profiles`
- `/api/select`
- `/api/raw-image`
- `/api/preview`
- `/api/export`

To force the legacy Python backend globally for debugging:

```powershell
$env:DFEE_USE_NATIVE_ENGINE="0"
python server.py
```

Per-route flags still exist as debug overrides. If a per-route flag is set, it
takes precedence over `DFEE_USE_NATIVE_ENGINE`.

To exercise native profile discovery through the existing FastAPI route while
keeping the response shape unchanged:

```powershell
$env:DFEE_USE_NATIVE_PROFILES="1"
python server.py
```

With that flag enabled, `/api/profiles` uses the C++ bridge for stock and print
profile listing and falls back to the Python loader if the native path fails.

To route `/api/raw-image` through the native C++ preview path:

```powershell
$env:DFEE_USE_NATIVE_RAW_IMAGE="1"
python server.py
```

With that flag enabled, `/api/raw-image` asks the native engine to build and
serve the RAW preview JPEG and falls back to the Python cache if the native
preview path fails.

To route `/api/preview` through the native C++ render path:

```powershell
$env:DFEE_USE_NATIVE_PREVIEW="1"
python server.py
```

With that flag enabled, `/api/preview` asks the native engine to render the
current stock preview JPEG and falls back to the Python preview pipeline if the
native render path fails.

To route `/api/export` through the native C++ export path:

```powershell
$env:DFEE_USE_NATIVE_EXPORT="1"
python server.py
```

With that flag enabled, `/api/export` asks the native engine to write the final
image and report outputs and falls back to the Python export pipeline if the
native export path fails. The exception is native memory-pressure rejection:
when the native engine estimates that the export would exceed the current safe
memory budget, the route returns HTTP `507 Insufficient Storage` instead of
falling back to Python.

To route `/api/select` through the native C++ session path:

```powershell
$env:DFEE_USE_NATIVE_SELECT="1"
python server.py
```

With that flag enabled, `/api/select` now runs through the native session path,
warms draft decode, preview JPEG, and preview analysis state, and returns the
same compact metadata and diagnostics response shape the frontend already uses.
If a later native route falls back, the Python draft state is prepared lazily on
demand instead of being eagerly computed during select.

The preview/export request model now also carries `effect_pipeline_version`.
Supported values are `parity_v1` and `filmic_v2`. `parity_v1` preserves the
CPU parity baseline. `filmic_v2` enables the native filmic redesign path:
highlight bloom/halation uses bounded multiscale diffusion with source-region
shoulder compression, and grain uses deterministic density-aware modulation so
lower mids/shadows carry more texture than smooth highlights. Unsupported
values are rejected instead of silently rendering through the wrong effect
implementation. The export request model also carries photographer-facing export options:
`jpeg_quality`, `export_dpi`, `embed_metadata`, and `export_color_space`.

Film Lab preview and export controls use additional stock-relative fields:

- `exposure_placement`: `auto_balanced` asks the solver for a stock-aware
  scene placement; `as_shot` retains the captured RAW placement. Missing API
  values default to `as_shot` to preserve existing clients.
- `film_exposure_ev`: a bounded `-3.0` to `+3.0` offset applied before the
  selected stock's tone and colour response. It is intentionally separate from
  the advanced digital `exposure` correction.

Unsupported placement values and out-of-range film exposure offsets are
rejected with HTTP `400`. The React Film Lab explicitly sends
`exposure_placement=auto_balanced` as its default starting point.

Four **Color Character** controls are available under `filmic_v2`. Each is
bipolar `−100` to `+100`, with `0` as the neutral no-op default:

| Field | Effect |
| --- | --- |
| `highlight_color_hold` | Scales the stock's highlight chroma rolloff and highlight desaturation so colour survives into bright zones. High-zone only; never touches lightness. |
| `shadow_color_retention` | Scales the stock's shadow chroma rolloff. Shadow-gated; no black lift. |
| `palette_separation` | Per-pixel hue-anchor attraction in OKLCh; neutrals preserved; hue-wrap stable. |
| `emulsion_color_density` | Scales the stock's dye chroma body (`chroma_boost`), distinct from the legacy `film_color` multiplier. |

Sending any non-zero Color Character value under `parity_v1` is rejected with
HTTP `400` — never a silent render. `parity_v1` remains bit-for-bit
reproducible. Report JSON records both the four requested inputs and the four
resolved per-stock sensitivities.

Current native export support:
- `jpeg` / `jpg`: native final export supports `jpeg_quality`, writes `.jpg`,
  and embeds JFIF DPI metadata from `export_dpi` when `embed_metadata=true`.
- `png8`: native final export writes `.png`, preserves the current sRGB 8-bit
  output path, and embeds PNG `pHYs` DPI metadata from `export_dpi` when
  `embed_metadata=true`.
- `png16`: native final export writes `.png`, preserves the current 16-bit sRGB
  output path, and embeds PNG `pHYs` DPI metadata from `export_dpi` when
  `embed_metadata=true`.
- `tiff`: native final export writes `.tif`, preserves the current 16-bit sRGB
  output path, and writes inch-based TIFF DPI metadata from `export_dpi`.

Native exports that render a stock profile also write the sidecar JSON report
with the existing DFEE contract sections: `image_diagnosis`,
`feature_summary`, `render_plan`, and `warnings`. Reports include
`effect_pipeline_version` so future redesigned effects remain reproducible and
auditable.

If an export request asks for an option combination the native path does not
fully honor yet, the server deliberately routes that request through the Python
export path instead of silently degrading behavior.

Native export memory safety:
- Before a full native export, the engine drops nonessential preview caches and
  estimates full-resolution peak memory demand.
- On Windows, the engine checks current physical-memory availability and the
  low-memory notification API before rendering.
- If the system is already under memory pressure, `/api/export` rejects the
  request with HTTP `507` rather than risking a fallback into another
  high-memory export path.
- For testing or conservative local tuning, set
  `DFEE_NATIVE_EXPORT_MEMORY_BUDGET_MB` to force a lower native export budget.

At backend startup, DFEE now also logs the native engine capability snapshot:
engine version, LibRaw availability, CUDA mode, device details, and any CUDA
fallback reason.

When you run `python server.py`, the backend now uses DFEE's own compact console
logger and disables Uvicorn's noisy access-log lines. Route-level DFEE logs stay
visible, but the full query-string request spam from slider-driven preview calls
is suppressed.
