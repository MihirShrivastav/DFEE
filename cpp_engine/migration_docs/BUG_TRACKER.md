# DFEE C++ Migration Bug Tracker

Track native migration defects and migration risks here. Keep IDs stable and reference task IDs from `TASKLIST.md` when a bug blocks a planned task.

Severity values:
- `critical`: Data loss, crash, broken build, or route unusable.
- `high`: Major correctness/performance issue or parity blocker.
- `medium`: Important issue with a workaround.
- `low`: Cleanup, tooling, or documentation issue.

Status values:
- `open`: Not fixed.
- `active`: Being investigated or fixed.
- `blocked`: Waiting on a dependency or decision.
- `fixed`: Fixed and verified.
- `watch`: Known risk, no immediate fix needed.

## Open Bugs And Risks

| ID | Severity | Status | Area | Related Tasks | Summary | Repro / Evidence | Next Action |
| --- | --- | --- | --- | --- | --- | --- | --- |
| BUG-001 | medium | open | Tooling | M0-003 | `ninja-dev` preset cannot configure when Ninja is not installed/on PATH. | `cmake --preset ninja-dev` fails with missing Ninja build program. | Either install Ninja, document Visual Studio as default, or add a local generator fallback. |
| BUG-002 | medium | watch | Python bridge | M1-002 | MSVC Debug builds require Python release ABI handling. | Initial Debug build tried linking `python313_d.lib`; fixed by temporarily undefining `_DEBUG` around `Python.h`. | Keep this bridge-specific workaround unless a proper debug Python install is introduced. |
| BUG-003 | medium | watch | Session warmup | M2-003/M2-004/M2-005 | Native `select_file` still only validates and records the RAW filename; it does not proactively warm the draft decode or preview caches. | The native engine now exposes separate `decode_raw(...)` and `raw_preview(...)` calls, while `select_file` remains a lightweight validation step. | Decide during FastAPI cutover whether `/api/select` should continue warming caches eagerly or switch to lazy decode on first preview request. |
| BUG-004 | low | fixed | Profile parsing | M3-001 | The bootstrap indentation parser was not a complete YAML implementation and could accept malformed profile shapes silently. | Native profile loading now uses yaml-cpp plus required-section validation, and tests cover invalid stock/print YAML cases. | Directory listing skips invalid YAML files, while direct loads fail with schema errors. |
| BUG-005 | high | open | API migration | M4-006 | FastAPI routes still use Python engine for image processing. | Native module is only smoke-tested; `server.py` is not delegated to C++ yet. | Migrate endpoints one at a time after native decode/render/export surfaces exist. |
| BUG-006 | medium | open | CUDA | M5-001 | CUDA status is build-time/fallback plumbing, not real runtime detection. | `dfee_cli --cuda-status` reports CPU with CUDA disabled; CUDA target only contains stub kernel. | Add CUDA runtime probing and actual kernel dispatch after CPU parity. |
| BUG-007 | medium | watch | Repository hygiene | S-004 | `raw_files/` appears untracked and contains large local fixtures/outputs. | `git status --short` shows `?? raw_files/`. | Decide whether fixtures should be ignored, moved to a test fixture subset, or tracked via another storage mechanism. |
| BUG-016 | low | fixed | Color analysis | M3-002 | Native analyzer coverage only handled tonal and zone outputs, leaving color-state metrics Python-only. | Native analyzer now computes color metrics directly from linear RGB, and tests cover hue-bin, entropy, and zonal saturation behavior. | The implementation uses a single pass and fixed histogram rather than materializing full-frame temporary OKLab/OKLCH images. |
| BUG-017 | low | fixed | Spatial analysis | M3-003 | Native analyzer coverage still lacked texture, edge, specular, and halation mask outputs, so solver and renderer support remained blocked on Python-only spatial analysis. | Native analyzer now computes spatial aggregate metrics plus grain receptivity and halation masks, and tests cover the specular/highlight mask behavior on a synthetic fixture. | The implementation uses downsampled local-variance statistics and bounded morphology work to keep the stage efficient before later CUDA work. |
| BUG-018 | low | fixed | Camera bias estimation | M3-004 | Camera input bias remained Python-only, which blocked native solver/report parity once analyzer stages were ported. | Native `CameraBiasEstimator` now computes neutral-axis confidence, zonal OKLab casts, and solver-facing bias indices; native and Python-side synthetic fixtures cover the expected cool-shadow behavior. | The estimator reuses zone masks and evaluates OKLab values per pixel directly, avoiding extra full-frame transform buffers. |
| BUG-019 | low | fixed | Solver parity | M3-005 | Render-plan generation still lived entirely in Python, leaving the native path without the solver-facing contract that later report, preview, and export stages depend on. | Native `RenderPlanSolver` now reproduces the current Python plan shape and heuristics for warnings, pre-film normalization, film response, material effects, and optional print finish; native and Python-side synthetic fixtures cover the expected contract. | The native test intentionally preserves the current Python `shadow_cast` heuristic even though it overlaps awkwardly with `SHADOW_NOISE_RISK`, because this milestone is parity-first. |
| BUG-020 | low | fixed | Pre-film normalization | M3-006 | Even after solver parity, the native path still lacked the first renderer stage that consumes the solved normalization controls, leaving preview/export migration blocked on Python-only exposure and cast correction. | Native `FilmRenderer::apply_pre_film_normalization` now implements exposure lift, highlight neutral repair, and zone-weighted OKLab cast correction; native and Python-side synthetic fixtures confirm brighter midtones, reduced highlight channel spread, and shadow blue adjustment. | The implementation keeps the stage as a compact explicit pass so later renderer milestones can build on it without pulling in the full Python renderer at once. |
| BUG-021 | low | fixed | Tone response parity | M3-007 | The native renderer still lacked the core film-emulsion tone curve and monochrome conversion stages, so the migration could not yet model stock-specific tonal character in C++. | Native `FilmRenderer` now implements the panchromatic monochrome mix and the current per-channel film tone-response stage; native and Python-side fixtures cover neutral channel collapse, toe lift, and shoulder compression of overrange highlights. | Tests preserve the current Python behavior where a very bright overrange input can compress below a midtone sample because of the shoulder curve, since this milestone is parity-first. |
| BUG-022 | low | fixed | Color response parity | M3-008 | The native renderer still lacked the stock color-shaping stages that bend hue and chroma after the base tone curve, so parity remained blocked on Python-only color response logic. | Native `FilmRenderer` now implements zone-weighted color bias, red/orange and blue/cyan chroma compression, neon taming, highlight desaturation, and luminance-driven chroma/hue coupling; native and Python fixtures cover the expected chroma rolloff and highlight hue convergence behavior. | The implementation keeps the stage memory-efficient by transforming pixels directly through OKLab/OKLCH helpers instead of allocating whole-frame temporary color-space images. |
| BUG-010 | low | watch | Dependency setup | M2-001 | LibRaw is wired in CMake, but this machine does not currently have `VCPKG_ROOT` configured. | `cmake --preset windows-msvc` warns that LibRaw was not found; `DFEE_REQUIRE_LIBRAW=ON` fails with the new actionable configure error. | Set `VCPKG_ROOT`, install manifest dependencies, and use `windows-msvc-vcpkg` before starting native decode work. |
| BUG-011 | low | fixed | Metadata parity | M2-002/M2-003 | Native RAW metadata and draft decode metadata needed fixture-level parity against the current Python `RawIngestor` output. | `pytest tests/test_native_bridge.py -q` now compares decoded draft metadata and clipping ratios against the Python ingest path on a real RAW fixture. | Metadata capture now happens before LibRaw postprocess mutates draft dimensions, preserving Python-compatible image width/height fields. |
| BUG-012 | low | fixed | Test runtime | M2-003 | The `windows-msvc-vcpkg` CTest run initially failed with missing runtime DLL resolution. | Added the vcpkg runtime `bin` directory to the `windows-msvc-vcpkg` test preset `PATH`. | `ctest --preset windows-msvc-vcpkg` now has the required runtime search path. |
| BUG-013 | medium | fixed | Native decode stability | M2-004 | The first cache-owning decode implementation reopened LibRaw for metadata while a processed image buffer was still active, which crashed the native test binary with a stack overflow in Debug. | `dfee_tests.exe` failed with `0xc00000fd` during draft decode until metadata extraction was moved onto the already-open LibRaw handle. | Decode metadata is now captured from the original `LibRaw` instance before postprocess state mutates the reported dimensions. |
| BUG-014 | low | fixed | RAW preview parity | M2-005 | Native RAW preview JPEGs initially differed too much from the current Python `/api/raw-image` path because the preview cache used a simpler resize path than Python's `cv2.INTER_AREA` preview build. | Bridge parity test failed until preview cache generation was switched onto OpenCV area resizing and compared against the Python JPEG pipeline. | Native preview caching now uses the same resize/encode family as the current Python path and is validated through decoded-image parity tests. |
| BUG-015 | medium | fixed | Bad-input safety | M2-006 | Unsupported and corrupt `.ARW` inputs previously had little explicit coverage, making it easy for LibRaw error-path regressions or server-side crash regressions to slip through. | Added wrapper, native, and FastAPI tests that create fake and truncated `.ARW` files, assert structured native failures, and verify `/api/select` returns an HTTP error without killing the server process. | Bad-input coverage now exercises unsupported and corrupt RAW cases through decode, preview-cache, and endpoint paths. |

## Fixed Bugs

| ID | Severity | Status | Area | Related Tasks | Summary | Fix | Verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| BUG-000 | medium | fixed | Build outputs | S-004 | Native CMake build generated many files under `cpp_engine/out/`. | Added `cpp_engine/out/` and `*.pyd` to `.gitignore`. | `git status --short` no longer lists generated native build outputs individually. |
| BUG-008 | medium | fixed | Python bridge | M1-002 | Python callers had to handle a raw session capsule directly. | Added `dfee_native_bridge.py` typed wrapper and native bridge contract structs. | `pytest tests/test_native_bridge.py -q` passes and no test code touches a capsule. |
| BUG-009 | medium | fixed | Native error handling | M1-003 | Native failures surfaced as generic runtime errors or ambiguous result payloads. | Added `NativeError`/`NativeException` in C++, serialized response errors, exposed `dfee_native.NativeError`, and mapped wrapper failures to `NativeBridgeError`/`NativeOperationError`. | `pytest tests/test_native_bridge.py -q` covers invalid project root and missing RAW file errors. |

## Bug Intake Template

```markdown
| BUG-XXX | severity | open | area | related-task-id | One-line summary. | Repro command, route, or fixture. | Concrete next action. |
```

For image-quality or parity bugs, include:
- Source RAW or synthetic fixture.
- Film stock, print stock, and full control parameters.
- Python output artifact and native output artifact.
- Numeric delta if available.
- Whether the issue is CPU-only, CUDA-only, or shared.
