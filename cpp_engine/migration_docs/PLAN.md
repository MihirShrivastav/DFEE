# DFEE C++ Engine Migration With Optional CUDA

## Summary

Build a native C++ engine first while keeping the existing React/FastAPI UI for testing. The first milestone prioritizes pixel parity with the current Python engine, moves RAW decoding into C++ early, and exposes the engine to FastAPI through a native Python extension/DLL. CUDA is optional acceleration: the CPU path remains complete and authoritative, while CUDA accelerates selected stages when available.

## Architecture

- Create a CMake-based native workspace with three targets: `dfee_core` for portable C++ engine code, `dfee_cuda` for optional CUDA kernels, and `dfee_native` as the Python-loadable `.pyd` bridge used by FastAPI.
- Keep React and the existing FastAPI route shape initially: `/api/files`, `/api/profiles`, `/api/select`, `/api/raw-image`, `/api/preview`, and `/api/export` stay stable while their internals move to native calls.
- Move RAW decode, metadata extraction, preview cache, full-res cache, render-plan solving, rendering, and export into C++ using LibRaw, OpenCV C++, yaml-cpp, nlohmann-json, and libtiff/png/jpeg codecs as needed.
- Preserve the existing YAML profile format and report JSON format so current film stocks, print stocks, presets, and frontend controls remain usable.
- Use `float32` linear RGB internally, planar or tightly packed aligned buffers, explicit image dimensions/stride, and cache preview/full-res buffers in the C++ session object instead of Python globals.

## Implementation Plan

- Phase 1: Native scaffold and bridge
  - Add CMake presets for Windows/MSVC, vcpkg manifest dependencies, and a minimal `dfee_native` module callable from Python.
  - Implement native session lifecycle: `create_session`, `select_file`, `get_raw_preview_jpeg`, `render_preview_jpeg`, `export_image`, `list_profiles`.
  - Update FastAPI internals to call the native module while keeping request/response behavior unchanged.

- Phase 2: CPU engine parity
  - Port core math in this order: color spaces, profile parsing, analyzer, bias estimator, solver, pre-film sliders, renderer, post effects, report writer.
  - Match Python behavior first, including OKLab/OKLCH math, zone masks, tone response, print finish, HSL, curves, clarity, texture, dehaze, bloom, halation, and grain.
  - Use golden-output tests against selected RAW files and synthetic arrays; compare tolerances per stage, not only final JPEGs.

- Phase 3: RAW and export ownership
  - Use LibRaw for ARW/DNG ingestion and metadata extraction, matching current `rawpy` settings as closely as practical: camera WB, no auto bright, scene-linear output, sRGB primaries, 16-bit source conversion.
  - Implement native JPEG preview encoding and 8-bit PNG, 16-bit PNG, and 16-bit TIFF export.
  - Keep Python-side RAW fallback only as a temporary debug switch, disabled by default.

- Phase 4: Optional CUDA acceleration
  - Add runtime CUDA detection and engine mode reporting: `cpu`, `cuda_available`, `cuda_active`, device name, VRAM estimate, and fallback reason.
  - Start with CUDA kernels for high-payoff stages: RGB/OKLab transforms, zone mask generation, tone curves, per-pixel color response, HSL masks, curves LUT application, halation/bloom blurs, grain, and print finish.
  - Keep CPU and CUDA outputs numerically close under defined tolerances; if CUDA fails or memory is insufficient, fall back to CPU without breaking the UI.
  - Use tiled processing for full-res export to avoid excessive VRAM use on large RAW files.

- Phase 5: Redesign after parity
  - After CPU/CUDA parity is stable, redesign slow or visually weak effects behind explicit versioned effect implementations.
  - Bloom and halation should move from fixed large Gaussian passes to separable/multiscale pyramids with thresholded highlight sources, energy controls, and warm film response.
  - Grain should move toward deterministic tileable procedural noise or precomputed blue-noise-like fields for responsiveness while preserving per-stock character.
  - Any improved effect changes the render-plan/report version so results are explainable and reproducible.

## Interfaces

- FastAPI request parameters remain unchanged for the React UI during migration.
- Native bridge input for preview/export is a structured parameter object equivalent to `PreviewRequest`, plus selected file/session ID.
- Native bridge output for preview is encoded JPEG bytes; export returns `{status, output_path, report_path, format, engine_mode, timings}`.
- Reports include existing diagnosis/render-plan fields plus native engine metadata: engine version, CPU/CUDA mode, CUDA device if used, and per-stage timing.
- Profiles remain YAML in `profiles/stocks` and `profiles/print_stocks`.

## Test Plan

- Unit tests for C++ color conversions, profile validation, analyzer masks, solver output schema, and deterministic grain seed behavior.
- Golden parity tests using current Python outputs for synthetic images and a small RAW fixture set.
- API integration tests through FastAPI confirming React-facing behavior stays stable.
- CPU vs CUDA comparison tests with tolerances per stage and final image delta metrics.
- Performance tests measuring select, preview render, full-res export, peak RAM, peak VRAM, and cache reuse.
- Failure tests for missing CUDA, invalid profile YAML, unsupported RAW, insufficient VRAM, corrupted files, and export path errors.

## Assumptions

- First implementation target is Windows 11 with Visual Studio/MSVC and CMake.
- Build/dependency management uses vcpkg unless there is a strong repo constraint against it.
- LibRaw is the RAW decode foundation because it is the direct C++ equivalent for this app class: https://www.libraw.org/docs
- CUDA support follows NVIDIA’s Windows toolchain requirements and remains optional: https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/
- React remains the UI until the native engine is validated; WinUI/Qt decisions are deferred.
- Pixel parity is required for the first native engine milestone; visual improvements come only after parity through versioned redesigned effects.
