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
| M2-007 | done | Add neutral RAW development baseline for no-stock editing | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` plus native RAW probe | `stock=none` now runs the same scene-placement, pre-film correction, and post-correction path as a real render without inheriting any stock look. `Auto Balanced` meters the scene; `As Shot` stays unadjusted. Preview and export share the behavior. |

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
| M3-013 | done | Native preview render returns JPEG bytes | `pytest tests/test_native_bridge.py -q` and `ctest --preset windows-msvc-vcpkg -V` | Native session now renders solver-driven film previews to JPEG bytes, including pre-film sliders and post-film color/effects; no-stock renders use the neutral development baseline rather than bypassing the pipeline. |
| M3-014 | active | Native full export returns current response shape plus timing metadata | FastAPI `/api/export` integration test | Native export path now writes image/report outputs and exposes detailed render timings. Current warm stable probe is about `12.1s` total / `10.4s` render, and the remaining dominant renderer hotspot is full-res color response. |
| M3-015 | done | Meter Auto Balanced placement from robust highlights | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` | `filmic_v3` caps upward Auto Balanced exposure from `p98`. This preserves meaningful skies and daylight highlight areas while leaving only isolated top-1% speculars and dappled light to the tone shoulder. |

## Milestone M4 - Encoders, Reports, And API Cutover

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M4-001 | done | Add native JPEG encoder for previews and final exports | `pytest tests/test_native_bridge.py tests/test_server_errors.py -q` and `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` | Native preview rendering already returned JPEG bytes, and native final export now supports JPEG output with photographer-facing `jpeg_quality` control plus embedded JFIF DPI metadata. The FastAPI export gate now keeps supported JPEG requests on the native path and still falls back cleanly for richer non-JPEG combinations that have not been ported yet. |
| M4-002 | done | Add native 8-bit PNG export | `pytest tests/test_native_bridge.py tests/test_server_errors.py -q` and `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` | Native final export now supports 8-bit PNG output as a first-class native route target, preserves the existing sRGB transfer path, and writes PNG `pHYs` DPI metadata when `embed_metadata=true`. The FastAPI export gate now keeps supported `png8` requests native while richer `png16` and other still-unported option sets continue to fall back deliberately. |
| M4-003 | done | Add native 16-bit PNG export | `pytest tests/test_native_bridge.py tests/test_server_errors.py -q` and `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` | Native final export now supports 16-bit PNG output as a first-class native route target, keeps the existing 16-bit sRGB write path on native pixel buffers, and writes PNG `pHYs` DPI metadata when `embed_metadata=true`. The FastAPI export gate now keeps supported `png16` requests native while TIFF-specific richer metadata work remains a separate milestone. |
| M4-004 | done | Add native 16-bit TIFF export | `pytest tests/test_native_bridge.py tests/test_server_errors.py -q` and `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` | Native final export now supports 16-bit TIFF as a first-class native route target, uses OpenCV TIFF write flags to preserve no-compression export plus inch-based DPI metadata, and keeps supported TIFF requests on the native path. Richer TIFF metadata and compression controls can build on this without reopening the route gate. |
| M4-005 | done | Add native report JSON writer | `pytest tests/test_native_bridge.py tests/test_server_errors.py -q` | Native export already writes the sidecar report JSON through `serialize_feature_report_json(...)`; this milestone closes the contract by asserting top-level compatibility (`engine_version`, input/output files, stock/print ids, image diagnosis, feature summary, render plan, warnings) and the expected nested section layout. |
| M4-006 | done | Move `/api/select`, `/api/raw-image`, `/api/preview`, `/api/export` internals to native engine | Full FastAPI integration suite | All core FastAPI image routes are now native-first, `/api/select` returns native metadata/diagnostics, and the native backend is the default route path with Python fallback still preserved for resilience. |
| M4-007 | active | Add Python fallback/debug switch during cutover | Manual and automated fallback test | `DFEE_USE_NATIVE_ENGINE=0` and route-level overrides remain available for debugging; keep them until the richer export-option and encoder milestones are fully native. |
| M4-008 | done | Add native export memory-pressure preflight and fail-fast route behavior | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` and `pytest tests/test_native_bridge.py tests/test_server_errors.py -q` | Native export now drops nonessential preview caches before full-res render, estimates peak memory, checks Windows memory pressure, and rejects unsafe jobs with HTTP `507` instead of falling back to Python. |

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
| M6-001 | done | Add versioned effect pipeline flagging | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` and targeted pytest unsupported-version tests | Preview/export requests now carry `effect_pipeline_version`; `parity_v1` is the default CPU parity implementation and native reports record the requested version for reproducibility. |
| M6-002 | done | Replace bloom/halation with separable or pyramid highlight diffusion | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure`, `pytest tests/test_native_bridge.py tests/test_server_errors.py -q --tb=short`, and no-server preview timing probe | `filmic_v2` now routes native film-stage halation/bloom and the explicit Bloom post effect through shoulder-aware multiscale diffusion while preserving `parity_v1`. Probe on `credit @ryanbreitkreutz _DSC0027.ARW` with halation High and bloom 25: parity median preview `606ms`, v2 median preview `687ms`. |
| M6-003 | in_progress | Replace grain with deterministic procedural/precomputed fields | Visual QA plus determinism test | `filmic_v2` now has native grain dispatch, density-aware modulation, curated rich grain fields across all stock profiles, and deterministic field caching. Final visual/performance QA remains. |
| M6-004 | planned | Redesign dehaze/local contrast after parity | Visual QA plus regression tests | Avoid changing current look accidentally. |
| M6-005 | done | Add performance dashboard or benchmark script | Benchmark output artifact | `cpp_engine/tools/export_benchmark.py` now emits a stable native export JSON artifact for the documented cold/warm probe. Dashboarding can build on that later. |
| M6-006 | planned | Rework large-image export around tiled render and row-streamed encoders | Large RAW export stress test | Current preflight guards crashes, but true production-grade memory scaling still requires tiled processing and scanline/row output for PNG/TIFF. |
| M6-007 | done | Document native editing-flow architecture principles | Manual doc review | `migration_docs/EDITING_FLOW_ARCHITECTURE.md` now defines DFEE guidance for tiled export, ROI preview, graph invalidation, cache budgets, async preview jobs, and CUDA transfer discipline. |
| M6-008 | done | Add native session cache byte accounting and budget pruning | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` and `pytest tests/test_native_bridge.py -q` | `cache_state()` now reports estimated bytes for draft decode, preview, raw preview JPEG, preview analysis, full decode, and export analysis caches. `DFEE_NATIVE_CACHE_BUDGET_MB` enables conservative pruning of lower-priority caches when a session exceeds the configured budget. |
| M6-009 | done | Audit and enforce native stock-profile field consumption | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` | Native profile loading now rejects unknown/non-finite fields, every active stock is resolved through the C++ solver in tests, and previously unconsumed colour, grain, adaptation, halation, and monochrome fields are mapped into native render plans/stages. See `STOCK_PROFILE_CONTRACT.md`. |

| M6-010 | active | Calibrate neutral RAW baseline development against matched rendered references | Native baseline unit test plus `raw_rendered_pair_benchmark.py` corpus artifacts | `filmic_v3` now applies the RAW baseline through luminance-preserving, gamut-safe scaling rather than a per-channel power curve. The first 20-pair Adobe Standard corpus selected 1.28 midtone power; remaining camera-colour separation work is tracked separately from tone. |
| M6-011 | planned | Add camera-family baseline colour calibration | Camera-grouped RAW/TIFF corpus tests plus visual QA | Design a bounded, profile-driven camera look transform after RAW decode and before film. Calibrate hue/chroma separation from edit-free Lightroom Adobe Standard references; do not approximate it with a global saturation slider or stock-specific compensation. |
| M6-012 | active | Reauthor colour-negative stock families from manufacturer evidence | Native role tests, `dfee_cli --stock-assay`, material-off stock assay, and visual QA | Completed slices: Vision3 250D/500T as distinct latitude-first daylight/tungsten camera negatives; Kodak Portra 160/400/800 plus Ektar 100 as differentiated professional still negatives; and UltraMax 400 as the first Kodak consumer-negative profile. `COLOR_NEGATIVE_CALIBRATION.md` defines the native hue/chroma assay; the `9339116563.rw2` material-off check reports no completed-slice pair at or below `0.0100` RGB MAE. Next slices: Gold 200, remaining consumer stocks, and Fujifilm negative families, then portrait/reference-scan visual QA. |

## Milestone M7 - Film Lab Product Flow

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M7-001 | done | Add native film-exposure placement contract | Native CTest plus bridge/server contract tests | Preview/export now accept `exposure_placement` and `film_exposure_ev`. The React Film Lab defaults to Auto Balanced while omitted API fields preserve legacy As Shot behavior. |
| M7-002 | done | Reorganize React controls into film-lab primary flow | `npm run build` and `npm run lint` | The right panel now leads with Film Recipe, Film Exposure, Color Character, and Material Finish; generic tools remain available as advanced correction. |
| M7-003 | next | Add independent stock-relative colour-character controls | Native renderer tests, bridge/route contract tests, and visual QA | Build Highlight Color Hold, Shadow Color Retention, and Palette Separation as bounded native controls; do not map them to generic HSL sliders. See `FILM_LAB_IMPLEMENTATION_PLAN.md`. |
| M7-004 | planned | Add stock-specific push/pull process model | Stock response fixtures and visual QA | Model process response separately from source/film exposure after stock calibration data is available. |
| M7-005 | planned | Separate and harden the Print stage | Native print fixtures plus UI visual QA | Move print controls out of Film Recipe while preserving request behavior and validate supported stock/print combinations. |
| M7-006 | planned | Close Material Finish visual acceptance | Representative RAW visual/performance acceptance matrix | Complete grain and bloom/halation review before adding more material controls. M7-006D is done: disabling Auto grain now materializes native stock/ISO-derived Custom slider values. |
| M7-007 | planned | Add versioned recipes and A/B comparison | Recipe round-trip and cache-aware UI tests | Recipes must capture pipeline version and reproduce native render inputs without duplicating full-resolution source buffers. |

## Desktop / Host Integration

| ID | Status | Task | Acceptance | Notes |
| --- | --- | --- | --- | --- |
| DSK-LR-001 | done | Lightroom Classic external-editor save-back | `DFEE.exe --lightroom-edit <absolute-tiff>` renders and atomically replaces the handed-off TIFF | Lightroom owns derivative creation/catalog stacking; DFEE never touches source RAW/DNG. |
| DSK-LR-002 | planned | ICC-aware wide-gamut round-trip | Detect embedded profile; preserve/emit tagged TIFF; ProPhoto 16-bit Lightroom smoke pass | Release blocker before documenting a ProPhoto preset. |
| DSK-LR-003 | planned | Lightroom convenience plug-in | Plugin launches the same external-editor contract and provides setup diagnostics | Must not take ownership of raw conversion/import from Lightroom. |
| DSK-LR-004 | planned | Installer + registered host integration | Installed, signed executable selectable in Lightroom preferences | Include upgrade/uninstall behavior and per-user preset instructions. |
| M7-008 | planned | Add guided diagnostics and workflow accessibility | Route/report tests plus keyboard/narrow-layout QA | Guidance is factual and reversible; accessibility work must not increase redundant preview rendering. |
| M7-009 | planned | Film Lab release hardening | Contract matrix, quality regression set, and performance/memory probes | Validate native bridge, preview/export agreement, visual quality, and large-RAW behavior before calling the Film Lab complete. |

## Standing Engineering Tasks

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| S-001 | active | Keep API specs/docs updated when route behavior changes | Docs diff in same patch | Required for endpoint/service changes. |
| S-002 | active | Keep FastAPI route logging intact when delegating to native code | Log review/integration tests | New endpoints/services need proper logging. |
| S-003 | active | Preserve Python-vs-C++ parity tests before redesigning effects | CI/local test evidence | Quality improvements are versioned after parity. |
| S-004 | active | Keep generated build outputs ignored | `git status --short` | `cpp_engine/out/` must remain untracked. |
| S-005 | active | Record significant migration decisions in APAM | APAM update after meaningful slices | Maintains project continuity across sessions. |
| S-006 | active | Follow the documented native performance method for optimization work | Baseline/probe notes in same slice | Use `migration_docs/PERFORMANCE_METHOD.md` for stable probe discipline, warm-vs-cold interpretation, and accept/revert decisions. |

## M6-003 Grain Redesign Tasklist

| ID | Status | Task | Verification | Notes |
| --- | --- | --- | --- | --- |
| M6-003A | done | Document the filmic grain model before implementation | Manual doc review | `migration_docs/GRAIN_MODEL.md` defines the research-grounded behavior, exposure response, stock families, profile schema direction, and acceptance tests. |
| M6-003B | done | Add native `filmic_v2` grain dispatch while preserving `parity_v1` | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` | Preview/export calls `apply_filmic_grain` only when `effect_pipeline_version=filmic_v2`; `parity_v1` keeps `apply_film_grain`. |
| M6-003C | done | Implement density-domain signal-dependent grain | Native synthetic exposure test | `filmic_v2`/`filmic_v3` now add deterministic grain in optical density with a log-normal mean correction, restrained dye-layer separation, smooth tonal response, and no neighbourhood/detail mask. |
| M6-003D | done | Add stock-family grain differentiation | Native synthetic stock test | `filmic_v2` now uses explicit/inferred grain families and rich plan fields to distinguish fine color negative, high-speed color negative, consumer color negative, color reversal, cubic B&W, and tabular B&W behavior. |
| M6-003E | done | Add richer optional grain profile fields | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` and targeted native bridge profile/report tests | Existing profiles remain valid, and all 27 active stock YAML files now include optional `family`, `target_pgi_35mm_4x6`, `clumpiness`, `micro_grit`, `layer_correlation`, `shadow_response`, `midtone_response`, `highlight_response`, `underexposure_coarsening`, and `overexposure_smoothing`. |
| M6-003F | done | Add deterministic field caching for `filmic_v2` | `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` and no-server preview timing probe | Native thread-local field cache is implemented; deterministic tests pass. Probe on `credit @ryanbreitkreutz _DSC0027.ARW` with grain Auto, halation Off, bloom 0: parity median grain `60.7ms`, v2 median grain `66.9ms`. |
| M6-003G | done | Add bridge/server coverage for `filmic_v2` grain behavior | `pytest tests/test_native_bridge.py tests/test_server_errors.py -q --tb=short` | Existing `filmic_v2` bridge/server route-contract tests cover preview/export request propagation without changing public route contracts. |
| M6-003H | in_progress | Run visual/performance acceptance probe | No-server preview timing plus image QA | On `0146371903.ARW` (62 MB), Portra 400, `filmic_v3`, Auto grain, halation Off, bloom 0, warm grain was `31.2 ms` then `26.6 ms`; total preview was `488.8 ms` then `456.4 ms`. The density-domain rewrite removes the gamma Soft-Light overlay model while preserving bounded cached fields and anti-blotch constraints. Visual QA across representative RAWs and stock families remains before marking M6-003 complete. |
