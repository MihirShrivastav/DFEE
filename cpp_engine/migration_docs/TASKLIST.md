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
| M0-006 | done | Port tonal analysis and 7-zone masks | `dfee_tests` partition check | Spatial/color analyzer stages remain Python-only. |
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
| M1-005 | planned | Wire FastAPI `/api/profiles` to native profile listing behind a feature flag | FastAPI integration test | Start with low-risk endpoint before moving image bytes. |
| M1-006 | planned | Add native engine capability endpoint or internal startup log | Server startup log review | Should log engine version, mode, CUDA status, and fallback reason. |

## Milestone M2 - RAW Decode And Session Ownership

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M2-001 | done | Add LibRaw dependency wiring through vcpkg/CMake | `cmake --preset windows-msvc` and `cmake -S . -B out/build/libraw-required-check -DDFEE_REQUIRE_LIBRAW=ON` | Plain builds now warn clearly when LibRaw is absent, and `windows-msvc-vcpkg` / `DFEE_REQUIRE_LIBRAW=ON` provide an actionable required path. |
| M2-002 | done | Implement native RAW metadata extraction | `ctest --preset windows-msvc` and `pytest tests/test_native_bridge.py -q` | Native metadata structs/session/wrapper are implemented. Full LibRaw-vs-`rawpy` fixture parity on this machine still depends on a vcpkg-backed LibRaw install. |
| M2-003 | next | Implement scene-linear RGB decode matching current `rawpy` settings | RAW fixture parity test | Camera WB, no auto bright, deterministic output. |
| M2-004 | planned | Add session-owned full-res and preview caches | Memory/reselect tests | Avoid Python global image buffers for migrated paths. |
| M2-005 | planned | Implement native cached RAW preview JPEG for `/api/raw-image` | FastAPI integration test | Preserve current response bytes/content type behavior. |
| M2-006 | planned | Add failure coverage for unsupported/corrupt RAW files | Native and FastAPI tests | Must not crash server process. |

## Milestone M3 - CPU Pipeline Parity

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M3-001 | next | Port profile model fully to yaml-cpp and validate required schema | Invalid profile tests | The current parser is a bootstrap parser, not the final validator. |
| M3-002 | planned | Port color analyzer stages | Synthetic parity tests | Hue/chroma metrics and dominant hue bins. |
| M3-003 | planned | Port spatial analyzer stages | Synthetic parity tests | Texture, edge, specular, halation masks. |
| M3-004 | planned | Port camera bias estimator | Python-vs-C++ report diff | Preserve current report fields. |
| M3-005 | planned | Port solver and render-plan schema | Solver schema completeness test | Report shape must remain compatible. |
| M3-006 | planned | Port pre-film controls | Synthetic image parity tests | Exposure, WB, tint, highlights, shadows. |
| M3-007 | planned | Port film tone response and monochrome path | Synthetic and profile parity tests | Include color negative, reversal, monochrome stocks. |
| M3-008 | planned | Port color response and luminance-chroma coupling | Synthetic parity tests | OKLab/OKLCH behavior must match first. |
| M3-009 | planned | Port acutance, clarity, texture, dehaze | Synthetic parity tests | Redesign later, not during parity. |
| M3-010 | planned | Port bloom and halation baseline behavior | Fixture parity tests | Current implementation may be slow; match first. |
| M3-011 | planned | Port deterministic grain baseline | Deterministic grain test | Seed from session/image/profile/params. |
| M3-012 | planned | Port print finish and output transforms | Fixture parity tests | Preserve print stock controls. |
| M3-013 | planned | Native preview render returns JPEG bytes | FastAPI `/api/preview` integration test | Keep current frontend untouched. |
| M3-014 | planned | Native full export returns current response shape plus timing metadata | FastAPI `/api/export` integration test | Output path/report path compatibility required. |

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
| M6-005 | planned | Add performance dashboard or benchmark script | Benchmark output artifact | Track select, preview, export, RAM, VRAM. |

## Standing Engineering Tasks

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| S-001 | active | Keep API specs/docs updated when route behavior changes | Docs diff in same patch | Required for endpoint/service changes. |
| S-002 | active | Keep FastAPI route logging intact when delegating to native code | Log review/integration tests | New endpoints/services need proper logging. |
| S-003 | active | Preserve Python-vs-C++ parity tests before redesigning effects | CI/local test evidence | Quality improvements are versioned after parity. |
| S-004 | active | Keep generated build outputs ignored | `git status --short` | `cpp_engine/out/` must remain untracked. |
| S-005 | active | Record significant migration decisions in APAM | APAM update after meaningful slices | Maintains project continuity across sessions. |
