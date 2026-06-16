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
| BUG-003 | high | open | RAW decode | M2-003 | Native `select_file` does not decode RAW yet. | Python smoke test returns selected file with message that LibRaw decode is scheduled. | Implement LibRaw ingest and replace placeholder selection behavior. |
| BUG-004 | high | open | Profile parsing | M3-001 | Bootstrap YAML parser is not a complete YAML implementation. | It supports current simple profile patterns but not full YAML semantics. | Replace with yaml-cpp validation before treating profile parsing as production native behavior. |
| BUG-005 | high | open | API migration | M4-006 | FastAPI routes still use Python engine for image processing. | Native module is only smoke-tested; `server.py` is not delegated to C++ yet. | Migrate endpoints one at a time after native decode/render/export surfaces exist. |
| BUG-006 | medium | open | CUDA | M5-001 | CUDA status is build-time/fallback plumbing, not real runtime detection. | `dfee_cli --cuda-status` reports CPU with CUDA disabled; CUDA target only contains stub kernel. | Add CUDA runtime probing and actual kernel dispatch after CPU parity. |
| BUG-007 | medium | watch | Repository hygiene | S-004 | `raw_files/` appears untracked and contains large local fixtures/outputs. | `git status --short` shows `?? raw_files/`. | Decide whether fixtures should be ignored, moved to a test fixture subset, or tracked via another storage mechanism. |
| BUG-010 | low | watch | Dependency setup | M2-001 | LibRaw is wired in CMake, but this machine does not currently have `VCPKG_ROOT` configured. | `cmake --preset windows-msvc` warns that LibRaw was not found; `DFEE_REQUIRE_LIBRAW=ON` fails with the new actionable configure error. | Set `VCPKG_ROOT`, install manifest dependencies, and use `windows-msvc-vcpkg` before starting native decode work. |
| BUG-011 | low | fixed | Metadata parity | M2-002/M2-003 | Native RAW metadata and draft decode metadata needed fixture-level parity against the current Python `RawIngestor` output. | `pytest tests/test_native_bridge.py -q` now compares decoded draft metadata and clipping ratios against the Python ingest path on a real RAW fixture. | Metadata capture now happens before LibRaw postprocess mutates draft dimensions, preserving Python-compatible image width/height fields. |
| BUG-012 | low | fixed | Test runtime | M2-003 | The `windows-msvc-vcpkg` CTest run initially failed with missing runtime DLL resolution. | Added the vcpkg runtime `bin` directory to the `windows-msvc-vcpkg` test preset `PATH`. | `ctest --preset windows-msvc-vcpkg` now has the required runtime search path. |
| BUG-013 | medium | fixed | Native decode stability | M2-004 | The first cache-owning decode implementation reopened LibRaw for metadata while a processed image buffer was still active, which crashed the native test binary with a stack overflow in Debug. | `dfee_tests.exe` failed with `0xc00000fd` during draft decode until metadata extraction was moved onto the already-open LibRaw handle. | Decode metadata is now captured from the original `LibRaw` instance before postprocess state mutates the reported dimensions. |
| BUG-014 | low | fixed | RAW preview parity | M2-005 | Native RAW preview JPEGs initially differed too much from the current Python `/api/raw-image` path because the preview cache used a simpler resize path than Python's `cv2.INTER_AREA` preview build. | Bridge parity test failed until preview cache generation was switched onto OpenCV area resizing and compared against the Python JPEG pipeline. | Native preview caching now uses the same resize/encode family as the current Python path and is validated through decoded-image parity tests. |

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
