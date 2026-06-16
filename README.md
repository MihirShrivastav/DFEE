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
