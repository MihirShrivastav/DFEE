# DFEE Native Performance Method

This document defines how native-engine performance work must be done for DFEE.

The goal is not to collect random fast-looking numbers. The goal is to make the
engine measurably faster for a live image-editing workflow without silently
changing image character or confusing cold-cache cost with interactive latency.

## Principles

- Measure first, then optimize.
- Change one thing at a time.
- Keep the probe scenario fixed while comparing revisions.
- Prefer warm-cache numbers for live-editing decisions.
- Keep parity-safe math unless a later versioned quality redesign is explicit.
- Revert any optimization that regresses the stable probe, even if the idea
  looked reasonable in theory.

## Primary Use Cases

Performance work must be evaluated against the actual product modes:

- `interactive preview`: repeated slider changes on the same selected RAW
- `full export`: final full-resolution render to disk

Preview and export are related but not identical problems. Cold RAW decode time
matters for first-open behavior, while warm-cache repeated-render time matters
more for editing feel.

## Measurement Loop

Every optimization slice should follow this loop:

1. Pick one stable probe scenario.
2. Record the baseline timings.
3. Change one implementation variable.
4. Rebuild in `Release`.
5. Re-run the exact same probe.
6. Keep the change only if:
   - timing improves on the target metric, and
   - native tests still pass, and
   - no known parity regression is introduced.

Do not bundle multiple unrelated optimizations into one measurement step. If two
changes land together, it becomes unclear which one helped or harmed the result.

## Stable Probe Rules

While a task is being evaluated, keep these constant:

- same RAW fixture
- same stock
- same print stock
- same slider set
- same build config: `Release`
- same machine
- same bridge path
- same timing extraction path from native metadata

If any of those change, the result is a new benchmark scenario and must be
treated separately.

## Current Export Probe

The repository benchmark entrypoint for this probe is:

- `cpp_engine/tools/export_benchmark.py`

Run it from the repo root:

```powershell
cd d:\Codebases\DFEE
python cpp_engine\tools\export_benchmark.py
```

By default it writes:

```text
cpp_engine/out/benchmarks/native_export_benchmark.json
```

Useful options:

- `--label <name>`: store a short scenario or revision label in the artifact
- `--baseline <path>`: compare the current run against a previous artifact
- `--cold-runs <n>` / `--warm-runs <n>`: control phase sample counts

The artifact now stores:

- per-run raw timings
- per-phase summary statistics
- optional baseline deltas when `--baseline` is provided

The working export probe is:

- select one stable local RAW fixture from `raw_files/`
- run native export through `dfee_native_bridge.py`
- use the same request payload each time
- capture both cold and warm-cache timings

Current low-noise warm-cache export request shape:

- `stock='portra_400'`
- `print_stock='none'`
- `export_format='png8'`
- `grain='Off'`
- `halation='Off'`
- `bloom=0.0`
- `clarity=0.0`
- `texture=0.0`
- `dehaze=0.0`

This probe is not artistically interesting. It is intentionally narrow so the
engine hotspot is easier to isolate.

## Current Preview Probe

Use this probe for live-editing latency work on the native preview path:

- stable local RAW fixture: `credit @ryanbreitkreutz _DSC0027.ARW`
- `stock='ultramax_400'`
- `print_stock='none'`
- `grain='Auto'`
- `halation='Auto'`
- `bloom=0.0`
- `clarity=0.0`
- `texture=0.0`
- `dehaze=0.0`
- `film_color=100.0`

Execution rules:

- call `select_file(...)` once before timing
- record one cold render plus at least three warm renders
- extract timings from `NativeRenderedPreview.engine.timings`
- compare warm averages, not just the fastest single run

Current useful preview metrics:

- `render_preview_total`
- `render_preview_analyze`
- `render_preview_film_pipeline`
- `render_preview_film_stage_color_response`
- `render_preview_film_stage_grain`
- `render_preview_film_stage_halation_bloom`

This probe is intentionally closer to a real editing pass than the export probe,
so grain and halation remain enabled.

## Metrics To Record

For native export work, record these timings from native metadata:

- `export_image_total`
- `export_image_render`
- `export_image_render_prefilm_fullres`
- `export_image_render_film_fullres`
- `export_image_render_stage_tone_response`
- `export_image_render_stage_color_response`
- `export_image_write_output`
- `export_image_ensure_full_cache`

When a new substage is introduced, include it in the log only if it helps local
decision-making. Avoid instrumentation sprawl that no one uses.

## Cold vs Warm Cache

Always distinguish:

- `cold`: includes decode/cache warmup costs
- `warm-cache`: repeat render on the same selected RAW with the session already
  holding the needed caches

Interpretation:

- cold timings matter for first-open UX and export-from-fresh-session behavior
- warm-cache timings matter more for slider responsiveness and repeated edits

For live-editor work, warm-cache numbers should drive most decisions.

## Acceptance Rules

An optimization is acceptable only when all of these are true:

- target timing improves on the stable probe
- `ctest` still passes on the native build
- no known parity behavior is broken
- the code remains readable enough to debug later

If a change improves one stage but regresses the top-level warm-cache render
time, reject it.

If a change improves cold numbers only because it changes cache behavior, do not
claim it as an interactive-rendering win.

## Optimization Priorities

Prefer optimizations in this order:

1. remove redundant full-image passes
2. remove redundant color-space conversions
3. move repeated per-pixel setup into per-render precomputation
4. reduce temporary allocations and copies
5. improve cache locality and pass structure
6. use approximations only when measurement proves they help and parity remains acceptable

This ordering matters. Many theoretically clever approximations lose in practice
because memory access and interpolation overhead erase the gain.

## Preview-Specific Guidance

For live editing, the right target is not only raw throughput. It is latency.

Preview work should bias toward:

- cached decode reuse
- cached analysis reuse
- stage invalidation by control group
- bounded working resolution
- progressive refinement when useful

That means a future preview optimization may be valid even if it does not help
full export.

## Export-Specific Guidance

For export work:

- full quality remains authoritative
- analysis may run on a bounded working image if final full-resolution render
  quality is unchanged
- final output still renders full resolution

Export optimizations should improve final throughput without introducing hidden
quality mode changes.

## Regression Handling

If an experiment regresses the stable probe:

1. measure twice to rule out obvious noise
2. if the regression is still real, revert it
3. record the result in commit history or task notes if it taught something useful

Do not keep a bad optimization in-tree just because it was expensive to write.

## Current Direction

As of the current migration state:

- the tone-response stage has already been reduced substantially with a per-render LUT
- the pre-film wrapper has improved by fusing pixel-local tonal passes
- the remaining dominant warm-cache export hotspot is the combined color-response stage

That means the next performance work should focus on the color-response stage,
using the same measurement discipline defined here.
