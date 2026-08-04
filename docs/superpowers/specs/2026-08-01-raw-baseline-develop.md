# RAW Baseline Develop And Input Contract

**Status:** active calibration work
**Date:** 2026-08-01
**Area:** `cpp_engine` RAW decode and session render flow

## Goal

Make a RAW and an edit-free Lightroom-rendered TIFF of the same capture converge
credibly after the same film recipe, without weakening stock profiles or applying a
global exposure lift that destroys bright scenes.

## Input Contract

RAW and rendered files do not start from the same photographic state:

- RAW is scene-linear camera data. DFEE develops it to a neutral working baseline,
  then applies the stock's full tone and colour response.
- TIFF is already developed by Lightroom or another renderer. DFEE preserves its
  input tone more carefully and uses `rendered_input` to attenuate stock tone only.

`rendered_input` is strictly a rendered-file control. It must never attenuate RAW
stock tone. RAW has no baked display curve to protect, so its chosen stock keeps the
complete authored tone response.

## Pipeline

```
RAW  -> scene placement -> neutral RAW baseline -> full stock tone/colour/material -> output
TIFF -> rendered-input adjustment ----------------> attenuated stock tone/colour/material -> output
```

Scene analysis and Auto Balanced placement are baseline operations. They position a
scene for development; they are not a stock-specific exposure boost.

## Neutral RAW Baseline

The baseline must be neutral. It may change luminance and should protect gamut, but
it must not invent a stock's saturation, hue bias, highlight colour, grain, or
halation.

The current `filmic_v3` implementation uses luminance-only scaling:

- It preserves the 0.18 middle-gray anchor.
- It applies a restrained monotonic midtone curve. The current production power is
  `1.28`, selected against 20 edit-free Lightroom Adobe Standard TIFF references:
  mean preview MAE improved from `0.0788` at `1.12` to `0.0712`, while mean p95
  luminance error remained near zero (`+0.0052`).
- It scales RGB channels together, preserving hue and chroma proportions.
- It reduces gain before an individual channel would clip, avoiding the hue shifts
  caused by the former independent red/green/blue power curve.

This is a structural correction, not a claim that the current curve is final. Any
future toe or shoulder modification must be measured against matched source pairs.
It must not be used to compensate for a wrong camera matrix, embedded profile, or
creative Lightroom edit.

## Calibration Method

Use `cpp_engine/tools/raw_rendered_pair_benchmark.py` with an edit-free capture pair:

```powershell
python cpp_engine/tools/raw_rendered_pair_benchmark.py raw_files/2316908974.nef comparision/2316908974.tif --stock none
python cpp_engine/tools/raw_rendered_pair_benchmark.py raw_files/2316908974.nef comparision/2316908974.tif --stock kodachrome_64
```

The tool renders both inputs through the same native `filmic_v3` request:

- Auto Balanced placement
- adaptive adjustments disabled
- grain, halation, bloom, and print finish disabled

It records display luminance percentiles, contrast, saturation, preview timing, and
mean absolute RGB difference in `cpp_engine/out/benchmarks/raw_rendered_pair.json`.

Evaluate `stock=none` first. If it does not converge, the defect belongs to neutral
RAW development or input colour management, not the stock YAML. Then compare the
same pair with a stock applied. Do not use a single scalar MAE as the decision: inspect
toe, middle gray, upper tones, saturation, and visual evidence together.

## Acceptance

- Native tests prove the baseline retains middle gray, chromatic RGB ratios, and
  gamut bounds.
- A representative corpus includes low contrast, normal daylight, high dynamic range,
  and saturated scenes with known edit-free TIFF references.
- Baseline-only and stock-applied output improve across the corpus before a curve
  parameter changes.
- RAW remains full-stock-tone; TIFF attenuation remains TIFF-only.
- `parity_v1` and `filmic_v2` remain unchanged.

The corpus still shows a mean RAW saturation deficit of about `0.094` after this
tone correction. That is a separate camera-colour rendering problem, not a reason to
increase the baseline power further.

## Risks And Deferred Work

- Lightroom TIFFs may carry different ICC profiles or hidden develop edits. The pair
  is invalid for calibration unless those conditions are controlled.
- LibRaw highlight reconstruction must be changed only when source evidence supports
  it; it is not a substitute for tone calibration.
- Wide-gamut working-space support remains a later phase. It may be required for
  saturated-source accuracy, but should not be mixed into this baseline experiment.
