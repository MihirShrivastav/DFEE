# DFEE Native C++ Engine

This directory is the root CMake project for the native DFEE engine. It intentionally does not contain a nested `cpp_engine/` project.

## Current Milestone

The current native scaffold builds:

- `dfee_core`: portable C++20 CPU baseline library.
- `dfee_native`: CPython extension module for FastAPI integration experiments.
- `dfee_cli`: small local debug executable.
- `dfee_tests`: native unit/parity foundation tests.
- `dfee_cuda`: optional CUDA target when configured with `DFEE_ENABLE_CUDA=ON`.

The first implemented surface covers core image containers, OKLab/OKLCH transforms, tonal/zone analysis, YAML stock and print-profile discovery, native session setup, and CUDA status reporting. RAW decode, preview JPEG rendering, full export, and FastAPI route delegation are the next migration slices.

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

If LibRaw is not discoverable, the plain `windows-msvc` preset still builds the scaffold but prints a configure warning and leaves native RAW decode disabled.

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

`dfee_native_bridge.py` is the intended Python-side integration surface for future FastAPI delegation. It hides the raw CPython capsule handle and normalizes native responses into typed Python objects.

`select_file` currently validates and records the RAW file path only. LibRaw decode and native preview/export bytes are intentionally not claimed as complete in this milestone.
