# DFEE Native C++ Engine

This directory is the root CMake project for the native DFEE engine. It intentionally does not contain a nested `cpp_engine/` project.

## Current Milestone

The current native scaffold builds:

- `dfee_core`: portable C++20 CPU baseline library.
- `dfee_native`: CPython extension module for FastAPI integration experiments.
- `dfee_cli`: small local debug executable.
- `dfee_tests`: native unit/parity foundation tests.
- `dfee_cuda`: optional CUDA target when configured with `DFEE_ENABLE_CUDA=ON`.

The first implemented surface covers core image containers, OKLab/OKLCH transforms, tonal/zone analysis, color analysis, yaml-cpp-backed stock and print-profile discovery with schema validation, native session setup, native RAW decode, native cached RAW preview JPEG generation, and CUDA status reporting. Full spatial analysis, preview render, full export, and FastAPI route delegation are the next migration slices.

## Build

```powershell
cd d:\Codebases\DFEE\cpp_engine
cmake --preset windows-msvc
cmake --build --preset windows-msvc --config Debug
ctest --preset windows-msvc
```

The `ninja-dev` preset is also available when Ninja is installed and on `PATH`.

For the first real RAW-decode milestone, configure with vcpkg after setting `VCPKG_ROOT`:

```powershell
cd d:\Codebases\DFEE\cpp_engine
$env:VCPKG_ROOT = "D:\path\to\vcpkg"
cmake --preset windows-msvc-vcpkg
cmake --build --preset windows-msvc-vcpkg --config Debug
```

If LibRaw is not discoverable, the plain `windows-msvc` preset still builds the scaffold but prints a configure warning and leaves native RAW decode disabled. `yaml-cpp` is now required for native profile loading, so native builds should use the vcpkg-backed preset on this machine.

## Smoke Checks

```powershell
cd d:\Codebases\DFEE\cpp_engine
out\build\windows-msvc\Debug\dfee_cli.exe --list-profiles
out\build\windows-msvc\Debug\dfee_cli.exe --cuda-status
out\build\windows-msvc\Debug\dfee_cli.exe --select DSC00246.ARW
```

Python bridge smoke check:

```powershell
cd d:\Codebases\DFEE\cpp_engine
$env:PYTHONPATH = "out\build\windows-msvc\Debug"
python -c "import dfee_native; s=dfee_native.create_session('D:/Codebases/DFEE'); print(dfee_native.engine_version()); print(dfee_native.cuda_status()); print(len(dfee_native.list_profiles(s)['stocks']))"
```

## Python Integration

Low-level extension module:

- `engine_version() -> str`
- `cuda_status() -> dict`
- `create_session(project_root: str) -> capsule`
- `list_profiles(session) -> dict`
- `select_file(session, filename: str) -> dict`

Stable Python wrapper:

- [dfee_native_bridge.py](d:/Codebases/DFEE/dfee_native_bridge.py)
- `create_session(project_root) -> NativeEngineSession`
- `engine_version() -> str`
- `cuda_status() -> NativeCudaStatus`
- `NativeEngineSession.list_profiles() -> NativeProfiles`
- `NativeEngineSession.select_file(request_or_filename) -> NativeSelectResult`
- `NativeEngineSession.read_raw_metadata(filename) -> NativeRawMetadata`
- `NativeEngineSession.decode_raw(filename, draft_mode=True) -> (NativeRawDecodeSummary, NativeRawMetadata)`
- `NativeEngineSession.cache_state() -> NativeSessionCacheState`
- `NativeEngineSession.raw_preview(filename="", max_edge=1024) -> NativeRawPreview`

`dfee_native_bridge.py` is the intended Python-side integration surface for future FastAPI delegation. It hides the raw CPython capsule handle and normalizes native responses into typed Python objects.

`select_file` currently validates and records the RAW file path only. LibRaw decode and native preview/export bytes are intentionally not claimed as complete in this milestone.

`read_raw_metadata` is implemented behind LibRaw-aware build wiring. In scaffold-only builds without LibRaw discovery, it raises a structured native operation error with code `LIBRAW_UNAVAILABLE`.

`decode_raw` uses the current native LibRaw path with the same core decode knobs as the Python ingest path: camera white balance, no auto bright, scene-linear gamma, sRGB output primaries, 16-bit output, and optional half-size draft mode.

`EngineSession` now owns draft decode, preview-scale, and full-resolution cache state internally. Re-selecting the same file preserves those caches; selecting a different file invalidates them.

`raw_preview` returns cached JPEG bytes for the preview-scale RAW image. In the vcpkg-backed build it uses OpenCV for area downsampling and JPEG encoding so the native preview path matches the current Python `/api/raw-image` behavior closely.

Unsupported or corrupt RAW inputs are covered in both native and FastAPI-side tests. Native decode and preview paths now fail with structured errors and leave no stale preview/decode caches behind.

Profile discovery now uses `yaml-cpp` instead of the temporary indentation parser. Direct profile loads fail on missing required sections or invalid `stock_type` values, while directory listing skips invalid YAML files so profile enumeration stays resilient.

The native color analyzer now computes the same high-level color metrics the Python solver expects, including zonal saturation summaries, hue entropy, dominant hue bins, warm/cool balance, and neon-risk style saturation pressure. The implementation is intentionally single-pass over pixels plus a fixed-size hue histogram so it avoids building full-frame temporary OKLab/OKLCH buffers.
