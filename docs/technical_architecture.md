# DFEE (Digital Film Emulation Engine)
## Technical Architecture Documentation

### 1. High-Level Architecture
DFEE is a perceptually accurate, physics-inspired film emulation engine. It is designed around a decoupled client-server architecture:
- **Frontend (React / Vite):** Handles user interactions, manages real-time parameter state, and fetches high-performance previews and full-resolution exports.
- **Backend (FastAPI / Python):** Exposes an API over HTTP, caching heavy RAW files in memory, and currently executing the production emulation pipeline using NumPy and OpenCV.
- **Native Engine Migration (`cpp_engine/`):** A root-level CMake/C++20 project now provides the first native engine foundation through `dfee_core`, `dfee_native`, `dfee_cli`, and `dfee_tests`. The React/FastAPI route contract remains stable while heavy image processing is migrated behind the Python extension in slices.

### 2. Core Paradigms
The core image processing backend is split into a **Solver -> Renderer** pattern.

#### A. The Solver (`dfee/solver.py`)
The solver's job is to read the image's inherent characteristics and map them against the selected film and print profiles to create a **Render Plan**.
- **ImageStateAnalyzer:** Scans the input image to determine its tonal distribution, hue/saturation state, spatial frequency, and channel behaviors.
- **Profile Injection:** Takes the user parameters (Exposure, Halation, Print Strength, etc.) and the YAML configurations (`FilmStockProfile`, `PrintStockProfile`).
- **Output (`render_plan`):** A dictionary detailing the exact mathematical operations required to process the image, completely abstracted away from the raw pixels.

#### B. The Renderer (`dfee/renderer.py`)
The renderer's job is strictly execution. It takes the linear RGB image array and the `render_plan` to apply sequential transformations.
The pipeline follows a physical order of operations:
1. **Pre-Film Normalization:** Corrects base exposure, white balance, tint, and recovers highlights.
2. **Panchromatic Conversion (Monochrome):** Mixes RGB channels based on the film stock's spectral sensitivity (if applicable).
3. **Film Tone Response:** Applies non-linear algebraic S-curves (toe, midtone, shoulder) individually to RGB layers.
4. **Dye Contamination:** Simulates cross-talk between the cyan, magenta, and yellow dye layers (e.g., Red layer leaking into Green).
5. **Color Response:** Warps the OKLCH / OKLab color space based on Zone (Shadow/Midtone/Highlight) × Hue × Saturation biases defined by the stock.
6. **Luminance-Chroma Coupling:** Replicates the physical constraint where highlights and deep shadows lose saturation naturally as dye density approaches limits.
7. **Acutance Shaping:** Local contrast and micro-contrast adjustments (clarity, texture, dehaze).
8. **Halation & Bloom:** Physics-based simulation of light scattering past the emulsion into the anti-halation backing. Uses thresholding and large-kernel convolution.
9. **Procedural Film Grain:** A Boolean mathematical model (sparse point convolution) to generate razor-sharp, resolution-independent silver halide clumps rather than simple Gaussian noise.
10. **Theatrical Print Finish:** A final subtractive grading stage that mimics printing a camera negative onto positive projection stock, complete with CMY optical light heads and deep OLED-like black anchors.

### 3. State Management & API
- **In-Memory Caching:** When a file is selected, the server extracts the linear RGB data from the RAW file (using `rawpy`), downscales it for preview caching, and holds it in a `session` variable to allow real-time sliders without re-decoding.
- **Native Bridge Status:** The native module currently exposes `engine_version`, `cuda_status`, `create_session`, `list_profiles`, and `select_file`. `select_file` validates session/file plumbing only; LibRaw decode, native JPEG preview, and native full-resolution export are the next migration slices.
- **Endpoints:**
  - `GET /api/profiles`: Lists available camera negative and print stock profiles.
  - `POST /api/select`: Instructs the backend to load and cache a specific image.
  - `GET /api/preview`: Renders the cached image rapidly using the requested query parameters.
  - `POST /api/export`: Runs the solver and renderer on the full-resolution un-downscaled image arrays and saves to disk.

### 4. Native Engine Build
```powershell
cd d:\Codebases\DFEE\cpp_engine
cmake --preset windows-msvc
cmake --build --preset windows-msvc --config Debug
ctest --preset windows-msvc
```

### 5. Profiles Configuration
Profiles are defined in declarative YAML to decouple emulation aesthetics from engine code.
- **Film Stocks (`profiles/stocks/*.yaml`):** Contains algebraic tone response constraints, discrete color biases, and spatial settings.
- **Print Stocks (`profiles/print_stocks/*.yaml`):** Contains black/white point anchoring, secondary contrast logic, and theatrical subtractive color offsets.
