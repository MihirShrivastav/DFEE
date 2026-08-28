# Film Lab Beta Hardening

## Scope

This plan covers the shipped Lightroom Classic external-editor workflow only:

```text
Lightroom 16-bit TIFF -> Film Lab desktop app -> dfee_core -> atomic TIFF replacement -> Lightroom refresh
```

The legacy React/FastAPI harness is not a beta release dependency.

## Non-Negotiable Rules

- Every interactive edit runs off the GUI thread and the UI remains paintable.
- The worker receives immutable request snapshots. Intermediate slider values may
  be discarded; the last settled value must always render.
- A Lightroom working TIFF is written to a unique sibling temporary file, then
  atomically replaces the original working TIFF only after encoding succeeds.
- Export safety is more important than completing an export. Insufficient memory
  must produce a structured error, never an operating-system-wide memory crisis.
- No export failure may silently overwrite or delete the Lightroom working file.

## Baseline Findings (2026-08-28)

| Area | Observation | Action |
| --- | --- | --- |
| Preview scheduling | The worker already coalesced edits while busy, but the first value of a slider drag could start a costly render immediately. | Add a 90 ms quiet-period debounce; retain only the newest request. |
| Preview failures | Structured native render errors could be reported as an empty successful preview. | Propagate the native error and retain the last good preview. |
| Export memory | Export preflight calculated a peak estimate, but only enforced it below 2 GB free RAM or after a Windows low-memory signal. | Enforce the safe commit-headroom budget whenever Windows reports memory state. |
| Export integrity | Full-resolution export writes a sibling temporary file and atomically replaces the target. | Keep and test this contract for every export type. |
| Diagnostics | Qt messages were console-only and stage timing was not surfaced to desktop logs. | Persist rotated local logs and record preview/export wall time plus native total time. |
| Test cleanliness | Desktop self-test emitted invalid QML enum and stale-control warnings. | Remove both warnings before using logs as release evidence. |

## Instrumentation

Application diagnostics are written to:

```text
%LOCALAPPDATA%\Film Lab\Film Lab\Logs\film-lab-<timestamp>-<pid>.log
```

The newest eight files are retained. The log records Qt warnings/errors, startup,
preview wall time/native total time, and export wall time/native total time. It
contains paths and diagnostic details, so beta testers should attach it only to
the project team.

Native export tracing remains opt-in for engineering investigations:

```powershell
$env:DFEE_TRACE_EXPORT = "1"
```

It records stage boundaries and working-set high-water data in
`cpp_engine/out/export_trace.log`; it is not enabled for normal users.

## Memory Model And Safety

The engine uses float32 linear RGB and intentionally bounds its long-lived state:

- preview/draft/full decode caches are session-owned and evicted under
  `DFEE_NATIVE_CACHE_BUDGET_MB` when explicitly configured;
- export drops draft preview caches before full-resolution work;
- full-resolution analysis masks remain bounded proxy masks rather than being
  upsampled to image resolution;
- large-film grain uses bounded periodic tiles instead of full-frame fields;
- full decode data is released once pre-film RGB is materialized;
- output encoding uses a single output `cv::Mat`, followed by atomic replacement.

Before rendering, export estimates the peak concurrent allocation, compares it
with current Windows commit headroom minus a reserved margin, and fails with
`EXPORT_MEMORY_BUDGET_EXCEEDED` when unsafe. QA can force a stricter limit:

```powershell
$env:DFEE_NATIVE_EXPORT_MEMORY_BUDGET_MB = "2048"
```

That setting is for testing only, not a shipped default.

## Beta Acceptance Gates

Run each case on a clean launch and record the desktop log plus Windows Task
Manager working set/CPU observations.

| Gate | Pass condition |
| --- | --- |
| TIFF open | Supported Lightroom TIFF produces a preview; corrupt/missing input gives a clear failure with no crash. |
| Slider drag | Dragging a Film Lab slider does not freeze QML input; only the settled value begins a new native render after the debounce interval. |
| Latest wins | Multiple changes during an active render produce one eventual preview reflecting the final state. |
| Preview baseline | On the standard beta TIFF corpus, record p50/p95 wall times by stock/effect family. No regression greater than 10% versus the approved baseline without an explicit visual-quality decision. |
| Export integrity | Successful Save & Return atomically replaces Lightroom's working TIFF; a forced failure leaves the prior TIFF byte-for-byte intact. |
| Memory refusal | A budget-forced large export fails before rendering with `EXPORT_MEMORY_BUDGET_EXCEEDED`; the application remains responsive and can continue editing. |
| Export baseline | Record wall time, native total time, peak working set, output dimensions, and byte size for TIFF, PNG, and JPEG where exposed. |
| Reliability soak | Repeated open/edit/export cycles do not show unbounded working-set growth, zombie `DFEE.exe` processes, or QML warnings. |

## Remaining Hardening Work

1. Add a repeatable desktop benchmark runner that collects preview p50/p95,
   export timing, and process working set across the approved TIFF corpus.
2. Add explicit native tests for unconditional export-memory rejection and
   temporary-output cleanup on every error exit.
3. Audit all full-resolution effect stages with the benchmark traces; optimize
   the measured top allocator/time consumer rather than speculative rewrites.
4. Add a crash-reporting helper process for beta builds. Microsoft documents
   that `MiniDumpWriteDump` is safest from a separate process; do not call it
   directly from an unstable exception thread.
5. Package symbols, version metadata, and a user-facing diagnostic collection
   procedure before external distribution.

## Validation Commands

```powershell
cmake --build desktop\out\build --config Release --target DFEE -- /m:1
cmake --build cpp_engine\out\build\windows-msvc-vcpkg --config Release --target dfee_tests -- /m:1
ctest --test-dir cpp_engine\out\build\windows-msvc-vcpkg -C Release --output-on-failure
```
