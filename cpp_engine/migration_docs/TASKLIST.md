# DFEE C++ Migration Tasklist

This is the working task board for migrating DFEE's Python image engine into the native C++ engine under `cpp_engine/`. Keep task IDs stable so commits, bugs, tests, and APAM notes can reference them.

Status values:
- `done`: Implemented and verified.
- `active`: Currently being implemented.
- `next`: Ready to start.
- `blocked`: Cannot proceed without a dependency or decision.
- `planned`: Not ready yet.

## Milestone M0 - Native Project Foundation

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M0-001 | done | Create root CMake project directly in `cpp_engine/` | `cmake --preset windows-msvc` | No nested native root. |
| M0-002 | done | Add `dfee_core`, `dfee_native`, `dfee_cli`, `dfee_tests`, optional `dfee_cuda` target | `cmake --build --preset windows-msvc --config Debug` | CUDA target is a compile-time stub until kernels are added. |
| M0-003 | done | Add CMake presets and vcpkg manifest | Configure/build smoke test | `ninja-dev` requires Ninja on PATH; `windows-msvc` works here. |
| M0-004 | done | Add core image and luminance containers | `dfee_tests` | Current layout is packed interleaved float32 RGB plus separate luminance. |
| M0-005 | done | Port OKLab/OKLCH color transforms | `dfee_tests` round trip | Matches current Python math and clamps RGB outputs. |
| M0-006 | done | Port tonal analysis and 7-zone masks | `dfee_tests` partition check | Initial analyzer foundation; later color/spatial stages are tracked under M3. |
| M0-007 | done | Add dependency-light YAML profile discovery | CLI lists current stock/print profiles | Temporary parser preserves buildability before yaml-cpp wiring. |
| M0-008 | done | Add native session/profile/file-selection bridge | Python import smoke test | `select_file` validates file presence only; no RAW decode yet. |
| M0-009 | done | Add native engine README and architecture doc note | Manual doc review | Docs explicitly state what is and is not migrated. |

## Milestone M1 - Native Bridge Contract For FastAPI

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M1-001 | done | Define native request/response structs for select, raw preview, preview render, export, and report metadata | Header review plus Python smoke tests | Added `include/dfee/bridge_types.hpp` as the contract model layer for bridge work. |
| M1-002 | done | Replace ad hoc CPython capsule API with a thin stable Python wrapper module | `pytest tests/test_native_bridge.py -q` | Wrapper lives in `dfee_native_bridge.py` and keeps capsule handles away from future `server.py` integration. |
| M1-003 | done | Add structured native error type and Python exception mapping | `pytest tests/test_native_bridge.py -q` | Native errors now carry `code`, `user_message`, and `detail` through the C++ layer, `.pyd`, and Python wrapper exceptions. |
| M1-004 | done | Add per-stage timing model and JSON serialization helpers | `ctest --preset windows-msvc` and `pytest tests/test_native_bridge.py -q` | Native metadata now carries stage timings plus serialized `metadata_json` through the `.pyd` and Python wrapper. |
| M1-005 | done | Wire FastAPI `/api/profiles` to native profile listing behind a feature flag | `pytest tests/test_server_errors.py -q` | `DFEE_USE_NATIVE_PROFILES=1` switches `/api/profiles` onto the native bridge, preserves the existing response shape, and falls back to the Python loader if the native path fails. |
| M1-006 | done | Add native engine capability endpoint or internal startup log | `pytest tests/test_server_errors.py -q` | FastAPI startup now logs native engine version, LibRaw availability, CUDA mode, device details, and fallback reason through a lifespan startup probe without changing the HTTP API surface. |

## Milestone M2 - RAW Decode And Session Ownership

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M2-001 | done | Add LibRaw dependency wiring through vcpkg/CMake | `cmake --preset windows-msvc` and `cmake -S . -B out/build/libraw-required-check -DDFEE_REQUIRE_LIBRAW=ON` | Plain builds now warn clearly when LibRaw is absent, and `windows-msvc-vcpkg` / `DFEE_REQUIRE_LIBRAW=ON` provide an actionable required path. |
| M2-002 | done | Implement native RAW metadata extraction | `ctest --preset windows-msvc` and `pytest tests/test_native_bridge.py -q` | Native metadata structs/session/wrapper are implemented. Full LibRaw-vs-`rawpy` fixture parity on this machine still depends on a vcpkg-backed LibRaw install. |
| M2-003 | done | Implement scene-linear RGB decode matching current `rawpy` settings | `pytest tests/test_native_bridge.py -q` against `windows-msvc-vcpkg` | Native decode now uses LibRaw with camera WB, no auto bright, `gamma=(1,1)`, `sRGB`, `output_bps=16`, and draft/full mode support. Current parity test checks decoded draft dimensions and clipping ratios against Python `RawIngestor`. |
| M2-004 | done | Add session-owned full-res and preview caches | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg` | `EngineSession` now owns draft decode, preview-scale, and full-res decode caches, exposes cache-state inspection for tests, preserves caches on same-file reselect, and clears them on file changes. |
| M2-005 | done | Implement native cached RAW preview JPEG for `/api/raw-image` | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg` | Native session now caches preview JPEG bytes behind `raw_preview(...)`, using OpenCV-backed area resize and JPEG encode to match the current Python preview path closely. |
| M2-006 | done | Add failure coverage for unsupported/corrupt RAW files | `pytest tests/test_native_bridge.py tests/test_server_errors.py -q` and `ctest --preset windows-msvc-vcpkg` | Native decode/preview now return structured errors for bad inputs, cache ownership stays clean after failure, and FastAPI `/api/select` tests confirm unsupported/corrupt inputs do not crash the server process. |

## Milestone M3 - CPU Pipeline Parity

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M3-001 | done | Port profile model fully to yaml-cpp and validate required schema | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg` | Native profile loading now uses yaml-cpp, enforces required stock/print sections, rejects invalid `stock_type` values, and skips invalid YAML files during directory listing. |
| M3-002 | done | Port color analyzer stages | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg` | Native analyzer now computes hue/chroma metrics, zonal saturation summaries, dominant hue bins, hue entropy, warm/cool ratios, and neon-risk-style saturation pressure in a single pass plus histogram. |
| M3-003 | done | Port spatial analyzer stages | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg` | Native analyzer now computes texture variance, edge density, specular/highlight ratios, grain receptivity, and halation source/receiver masks with downsampled statistics plus bounded mask operations. |
| M3-004 | done | Port camera bias estimator | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg` | Native estimator now computes neutral confidence plus global/shadow/midtone/highlight OKLab casts and solver-facing blue, green-magenta, and warm-cool bias indices. |
| M3-005 | done | Port solver and render-plan schema | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native solver now reproduces the current render-plan contract, including warnings, pre-film normalization, film response, material effects, and optional print-finish payloads. |
| M3-006 | done | Port pre-film controls | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native renderer now implements the current pre-film normalization stage: exposure compensation, highlight neutral repair, and zone-weighted shadow blue plus green-magenta cast correction. |
| M3-007 | done | Port film tone response and monochrome path | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native renderer now implements monochrome panchromatic conversion and the current per-channel film S-curve stage, including shoulder compression for overrange inputs and toe-only shadow lift. |
| M3-008 | done | Port color response and luminance-chroma coupling | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native renderer now applies zone-weighted color bias, hue-targeted chroma compression, neon taming, highlight desaturation, and luminance-driven chroma/hue coupling with per-pixel OKLab/OKLCH math instead of full-frame temporary color-space buffers. |
| M3-009 | done | Port acutance, clarity, texture, dehaze | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native renderer now includes the Python local-contrast/acutance stage plus parity-baseline clarity, texture, and dehaze helpers, with clarity/texture sharing one gamma-space local-contrast core to avoid duplicated blur/mask logic. |
| M3-010 | done | Port bloom and halation baseline behavior | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native renderer now ports the current film halation/bloom stage and keeps it efficient by using a resolution-aware downsample/blur/upsample bloom path instead of a literal full-resolution giant-kernel blur for large frames. |
| M3-011 | done | Port deterministic grain baseline | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native renderer now ports the current deterministic grain baseline with content-derived seeding, shared master-field generation, and exact repeatability across identical inputs. |
| M3-012 | done | Port print finish and output transforms | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native renderer now ports the full print-finish stage, including CMY printer-light shifts, print contrast/shoulder shaping, zonal color bias, dye scaling, and subtle print grain. Output encoding transforms remain route/export-layer work for the next milestone. |
| M3-013 | done | Native preview render returns JPEG bytes | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native session now renders solver-driven film previews to JPEG bytes, including pre-film sliders, post-film color/effects, passthrough RAW preview fallback, and bridge exposure for later FastAPI cutover. |
| M3-014 | active | Native full export returns current response shape plus timing metadata | FastAPI `/api/export` integration test | Native export path now writes image/report outputs and exposes detailed render timings; full route cutover and remaining export-performance work are still in progress. |

## Milestone M4 - Encoders, Reports, And API Cutover

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M4-001 | planned | Add native JPEG encoder for previews | Byte decode test | Prefer libjpeg-turbo or OpenCV based on final dependency choice. |
| M4-002 | planned | Add native 8-bit PNG export | Export test | Preserve color management expectations. |
| M4-003 | planned | Add native 16-bit PNG export | Export test | Ensure scaling and endianness are correct. |
| M4-004 | planned | Add native 16-bit TIFF export | Export test | Main photographer-grade export path. |
| M4-005 | planned | Add native report JSON writer | Report compatibility test | Existing diagnosis/render-plan fields plus native metadata. |
| M4-006 | planned | Move `/api/select`, `/api/raw-image`, `/api/preview`, `/api/export` internals to native engine | Full FastAPI integration suite | Keep route definitions, logging, validation in Python. |
| M4-007 | planned | Add Python fallback/debug switch during cutover | Manual and automated fallback test | Temporary only; native should be default after parity. |

## Milestone M5 - CUDA Acceleration

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M5-001 | planned | Add CUDA runtime probing | CUDA/no-CUDA tests | Report `cpu`, `cuda_available`, `cuda_active`, or `cuda_fallback`. |
| M5-002 | planned | Add GPU memory budget and tiled render scheduler | Large image test | Full-res export must survive limited VRAM. |
| M5-003 | planned | Accelerate RGB/OKLab/OKLCH transforms | CPU-vs-CUDA tolerance tests | High-value per-pixel stage. |
| M5-004 | planned | Accelerate zone masks and tone curves | CPU-vs-CUDA tolerance tests | Must preserve partition behavior. |
| M5-005 | planned | Accelerate HSL masks, curves LUTs, and print finish | CPU-vs-CUDA tolerance tests | Keep CPU fallback complete. |
| M5-006 | planned | Accelerate bloom/halation blur baseline | CPU-vs-CUDA tolerance tests | Use baseline first; redesign later. |
| M5-007 | planned | Accelerate deterministic grain | Determinism test across CPU/CUDA | Seed compatibility required. |
| M5-008 | planned | Add CUDA fallback reporting to native report JSON | API export test | Include device name and fallback reason. |

## Milestone M6 - Post-Parity Quality And Performance Redesign

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M6-001 | planned | Add versioned effect pipeline flagging | Report version test | Improved effects must be explainable/reproducible. |
| M6-002 | planned | Replace bloom/halation with separable or pyramid highlight diffusion | Visual QA plus performance benchmark | Only after baseline parity. |
| M6-003 | planned | Replace grain with deterministic procedural/precomputed fields | Visual QA plus determinism test | Must preserve stock character. |
| M6-004 | planned | Redesign dehaze/local contrast after parity | Visual QA plus regression tests | Avoid changing current look accidentally. |
| M6-005 | done | Add performance dashboard or benchmark script | Benchmark output artifact | `cpp_engine/tools/export_benchmark.py` now emits a stable native export JSON artifact for the documented cold/warm probe. Dashboarding can build on that later. |

## Standing Engineering Tasks

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| S-001 | active | Keep API specs/docs updated when route behavior changes | Docs diff in same patch | Required for endpoint/service changes. |
| S-002 | active | Keep FastAPI route logging intact when delegating to native code | Log review/integration tests | New endpoints/services need proper logging. |
| S-003 | active | Preserve Python-vs-C++ parity tests before redesigning effects | CI/local test evidence | Quality improvements are versioned after parity. |
| S-004 | active | Keep generated build outputs ignored | `git status --short` | `cpp_engine/out/` must remain untracked. |
| S-005 | active | Record significant migration decisions in APAM | APAM update after meaningful slices | Maintains project continuity across sessions. |
| S-006 | active | Follow the documented native performance method for optimization work | Baseline/probe notes in same slice | Use `migration_docs/PERFORMANCE_METHOD.md` for stable probe discipline, warm-vs-cold interpretation, and accept/revert decisions. |
