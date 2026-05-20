# Deterministic Film Emulation Engine (DFEE)

DFEE is a high-fidelity, scene-adaptive film stock emulation engine. Instead of relying on neural networks or generic lookup tables (LUTs), DFEE parses raw sensor data, extracts statistical exposure and spatial metrics, and applies a deterministic 14-stage chemical and optical emulation pipeline in the perceptually uniform **OKLab** color space.

---

## Key Features

1. **Scene-Adaptive Solver**: Dynamically scales tone curves, highlight desaturation, organic grain density, and halation width based on the image's dynamic range and clipping characteristics.
2. **7-Zone Exposure Weight System**: Partitions image exposures into continuous zones ($Z0$ to $Z6$) using a softmax-gaussian partition of unity to apply region-specific film chemistry response.
3. **Neutral-Axis Estimation**: Auto-detects input color cast using low-chroma, unclipped midtone pixels to compute correction vectors weighted by a confidence score.
4. **Organic Grain & Halation Emulation**: Simulates physical silver halide grain sizes across density layers and specular highlight light-leak red/orange halos (halation).
5. **Real-Time Web UI (FastAPI + React)**: A polished, macOS Neo-Brutalist dashboard featuring an interactive before/after split slider, Lightroom-style tuning, and a 16-bit linear TIFF exporter.

---

## Directory Layout

```
DFEE/
├── dfee/                   # Core Python Library
│   ├── __init__.py
│   ├── analyzer.py         # 7-Zone masks & spatial texture metrics
│   ├── bias.py             # Neutral-axis color cast detection
│   ├── color_spaces.py     # Linear RGB <-> OKLab <-> OKLCH transforms
│   ├── engine.py           # Single-image orchestrator
│   ├── ingest.py           # LibRaw/rawpy 32-bit linear ingestion
│   ├── profile.py          # YAML schema validation
│   ├── renderer.py         # 14 ordered emulation modules
│   ├── report.py           # Sidecar JSON reporter
│   └── solver.py           # Adaptively solves render plans
├── profiles/               # Emulation Profiles
│   ├── stocks/             # Kodachrome 64, Portra 400, Tri-X, Superia
│   └── scanners/           # Noritsu, Frontier, Darkroom Print
├── raw_files/              # RAW input (.ARW) and Export directory
├── frontend/               # Vite + React UI source code
├── tests/                  # Pytest Unit Test Suite
├── server.py               # FastAPI backend & React hosting server
├── cli.py                  # Command-Line Interface
├── README.md               # Main documentation
├── STARTUP.md              # Startup instructions
└── start.bat               # Double-click startup script (Windows)
```

---

## Technical Specifications

### Color Conversions
We perform rendering in the **OKLab** and **OKLCH** color spaces to isolate lightness ($L$), chroma/saturation ($C$), and hue ($h$). This prevents unwanted color shifts when modifying density curves and exposure.

### Continuous Zone Softmax
Exposure zone masks sum exactly to $1.0$ (partition of unity) across the image:
$$w_i(EV) = \frac{\exp(-(EV - c_i)^2 / 2\sigma^2)}{\sum_j \exp(-(EV - c_j)^2 / 2\sigma^2)}$$
*   **$Z0$ (Deep Shadow)** and **$Z6$ (Specular Highlight)** use one-sided flat boundary envelopes to support infinite exposure scales without sum divergence.

---

## Development & Testing
To run the python test suite:
```powershell
$env:PYTHONPATH="."
pytest tests/test_dfee.py
```
