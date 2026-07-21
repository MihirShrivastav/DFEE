# Tone Controls Rebuild — Scene-Referred, EV-Masked (filmic_v3)

## Problem

The basic tone controls (Exposure, Contrast, Highlights, Shadows, Whites, Blacks,
Midtones) feel inconsistent: some barely move the image, some swing hard on a small
nudge, and Highlights/Whites (and Shadows/Blacks) feel redundant.

Root causes in the current stage (`apply_pre_film_preview_sliders`, gamma-2.2 space):

1. **Overlapping zones.** Highlights pivots at 0.30 (acts 0.30→1.0); Shadows pivots at
   0.65 (acts 0→0.65). Both hammer the midtones; Whites (0.60+) overlaps Highlights,
   Blacks (<0.35) overlaps Shadows.
2. **Additive shifts in gamma space with inconsistent maxima** (HL ±0.45, SH ±0.50,
   WH ±0.30, BL ±0.25). Every slider feels different; additive-on-all-channels clips
   and desaturates near the ends.
3. **Asymmetric midtones gamma** `1/(1+m·0.01)`: gentle positive, explosive negative
   (−100 → gamma 5.0).
4. **Contrast** tanh driven to k=±2.5 — strong, saturating, not perceptually linear.

## Reference model (Lightroom, darktable, DaVinci Resolve, Snapseed)

- Adjust in a **perceptual domain** — EV/stops (darktable tone equalizer), multiplicative
  Lift/Gamma/Gain (Resolve). Never additive in a gamma buffer.
- Each control **owns a luminance zone via a smooth mask** that tapers to zero outside it
  (Lightroom "builds a mask"). Overlap is deliberately small (Resolve *Log* wheels).
- **Highlights ≠ Whites, Shadows ≠ Blacks:** Whites/Blacks set the clipping endpoints;
  Highlights/Shadows are broad region recovery.
- **Chroma preserved** — tonal moves scale luminance/ratios, they don't shift channels
  additively.

## New design (scene-referred linear, gated to filmic_v3)

Runs on linear (scene-referred) RGB before the film tone curve. parity_v1/filmic_v2 keep
the existing gamma-additive stage byte-identical.

| Control    | Mechanism                         | Zone / behaviour                                  |
|------------|-----------------------------------|---------------------------------------------------|
| Exposure   | global EV multiply                | whole image (unchanged)                           |
| Blacks     | black-point lift (offset, bottom) | sets black point (Resolve Lift / LR Blacks)       |
| Whites     | white-point gain (multiply, top)  | sets white point (Resolve Gain / LR Whites)       |
| Shadows    | EV dodge/burn, mask centered low  | Gaussian mask, tapers to 0 by mid                 |
| Midtones   | EV dodge/burn, mid bell           | centered at mid-grey                              |
| Highlights | EV dodge/burn, mask centered high | tapers to 0 by mid AND before pure white (≠ Whites)|
| Contrast   | pivot S-curve around mid-grey     | perceptually-linear amount                        |

**Zone masks.** Computed from a perceptual luminance `Lp = luminance^(1/2.2)` so the bands
line up with how tones read. Smooth Gaussian-ish weights on distinct centers:

- Shadows center `Lp ≈ 0.22`, Midtones `≈ 0.50`, Highlights `≈ 0.78`, each with a width
  that gives gentle (not muddy) adjacent overlap.
- Blacks weight rises toward `Lp = 0`; Whites weight rises toward `Lp = 1`.

**Application.** Per pixel, `total_EV = Σ control_EV_c · mask_c(Lp)` for Shadows/Midtones/
Highlights; linear RGB is scaled by `2^total_EV` (uniform multiply → hue/saturation-ratio
preserving). Blacks apply a bottom-weighted lift; Whites a top-weighted gain; Contrast a
pivot S-curve around mid-grey (in perceptual space).

**Slider calibration.** Each −100…+100 maps symmetrically to a consistent max:
Shadows/Midtones/Highlights ≈ ±1.15 EV; Whites/Blacks endpoint stretch; a mild ease so
small moves stay small across the whole range.

**Deferred:** guided-filter mask (local-contrast preservation) — v1 uses per-pixel
luminance masks, already a large improvement. Guided filter is a follow-up once the
calibration is dialed in visually.

## Tests (native)

- Each region control brightens/darkens its own band and leaves distant bands ~unchanged
  (zones are distinct).
- Symmetric response: +N and −N are mirror-ish in magnitude.
- Chroma preserved: a saturated mid pixel keeps hue and relative chroma under a dodge/burn.
- Whites/Blacks move the endpoints (near-white / near-black) more than the mids.
- filmic_v2/parity path is byte-identical to before (regression guard).
