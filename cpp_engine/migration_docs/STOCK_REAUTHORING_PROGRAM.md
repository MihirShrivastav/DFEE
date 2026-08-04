# Stock Reauthoring Program

## Purpose

DFEE must not present a stock variant merely because a real product had a
different ISO. A variant remains available only when its native response has a
visible, reproducible reason to exist beyond a slightly different grain amount.

This program reauthors the active profiles from sensitometric and material
behaviour, then decides whether a variant remains a primary recipe, becomes a
family variant, or is retired from the product catalog.

## Audit Baseline

The 2026-08-04 native assay used `9339116563.rw2`, `filmic_v3`, As Shot,
`adaptive=false`, grain Off, halation Off, bloom 0, and no print stock. It
isolates the static image-forming response from material finish and automatic
scene placement.

| Pair | Mean absolute RGB difference | Assessment |
| --- | ---: | --- |
| Cinestill 50D / 400D | 0.0059 | Not distinct enough |
| T-Max 100 / 400 | 0.0044 | Not distinct enough |
| Delta 100 / 400 | 0.0081 | Borderline, not distinct enough |
| Delta 400 / 3200 | 0.0206 | Materially distinct |

The active C++ renderer consumes the documented profile fields. The weakness is
not ignored YAML; it is shallow calibration and an incomplete model of
speed-dependent sensitometry. `adaptation.base_iso` currently drives mostly
grain adaptation, while family-level Auto placement uses the same stock bias
for several materially different stocks.

## Calibration Contract

Each stock must be calibrated against documented technical data and reference
scans for these independent behaviours:

1. **Sensitivity and latitude**: nominal EI, practical over/under exposure
   tolerance, and speed-dependent shadow failure.
2. **Characteristic response**: toe position and length, midtone gamma/density,
   shoulder onset, shoulder slope, and minimum density.
3. **Colour response**: balance, dye bias by tonal region, hue-family movement,
   saturation, and highlight desaturation.
4. **Spectral response**: monochrome panchromatic balance and filter-like colour
   separation; this is a primary look dimension, not a minor profile detail.
5. **Material response**: grain amplitude, particle scale, clumping,
   shadow/highlight dependency, sharpness/edge softness, halation, and bloom.
6. **Process response**: push/pull behaviour belongs to the later M7-004
   Process model and must not be faked with Film Exposure.

Auto Balanced provides a restrained scene-development starting point. It must
not erase a stock's calibrated sensitometry; Film Exposure remains an explicit
pre-emulation placement choice, not a generic brightness overlay.

## Required Engine Work

Current `tone_response` fields are sufficient for broad family looks but not
for calibrated speed variants. Add a versioned, profile-driven sensitometry
stage only after the benchmark fixtures are in place. The proposed parameters
are intentionally exposure-domain terms rather than more arbitrary display
curves:

- `sensitometry.nominal_ei`
- `sensitometry.toe_start_ev`, `toe_latitude_ev`, `toe_gamma`
- `sensitometry.midtone_gamma`
- `sensitometry.shoulder_start_ev`, `shoulder_latitude_ev`, `shoulder_gamma`
- `sensitometry.shadow_separation`
- `sensitometry.overexposure_smoothing` and `underexposure_breakup`

These values must operate before display encoding, have a no-op baseline for
existing `parity_v1`, appear in report JSON, and be tested with exposure ramps.
They are not approved YAML fields until that implementation exists.

## Family Priorities

### Cinestill

- **50D**: daylight-balanced ISO 50, exceptionally fine grain, maximum edge
  definition, broad highlight/shadow latitude, and remjet-free halation.
- **400D**: daylight-balanced ISO 400, softer palette, natural saturation,
  warm skin response, broad practical EI range, and a clearly different
  grain/latitude tradeoff.
- **800T**: tungsten-balanced high-speed stock, cool daylight crossover,
  stronger point-light halation, and substantially different shadow/grain
  behaviour.

50D and 400D must receive clearly different characteristic curves and material
response before both remain first-class selections. Current values make them
near-duplicates.

### Monochrome

Retain the meaningful families provisionally, but calibrate them by response
rather than ISO labels:

- Slow/fine: Pan F Plus 50, FP4 Plus 125, Delta 100, T-Max 100, Acros 100.
- General-purpose/classic: HP5 Plus, Tri-X 400, Double-X, Delta 400, T-Max 400.
- High-speed: Delta 3200.

Each requires a distinct panchromatic mix plus characteristic curve. Fine-grain
and tabular emulsions should not differ only by subtle toe/shoulder changes;
their highlight transition, local acutance, shadow separation, and grain
placement need distinct calibration.

### Candidate Coverage Gaps

Do not add a stock merely for catalog size. Candidates are accepted only after
reference material and a distinct response hypothesis exist:

- Kodak VISION3 50D and 200T: fill meaningful motion-picture daylight and
  tungsten speed gaps between the existing 250D and 500T.
- CineStill BwXX: only if calibrated separately from the existing Double-X
  profile for its still-photography process and scan behaviour.
- Harman Phoenix 200: a genuinely different modern colour-negative candidate
  if its colour instability, contrast, and halation can be represented without
  turning defects into generic effects.
- Current Fujifilm 400: evaluate separately from legacy Superia X-TRA 400;
  do not claim they are the same emulsion without evidence.

## Source Set

Use manufacturer technical documents and product guidance as the first layer of
evidence. Scanner, developer, and reference-scan variation must be recorded as
calibration context rather than attributed blindly to the emulsion.

- CineStill 50D: daylight balance, ISO 50, fine grain, latitude, and remjet-free
  halation: <https://help.cinestillfilm.com/hc/en-us/articles/360028918672-What-is-different-about-CineStill-50Daylight-film>
- CineStill 400D: daylight balance, wide practical EI range, palette, and skin
  response: <https://cinestillfilm.com/products/a-new-color-film-400dynamic>
- CineStill 800T: tungsten balance, high-speed use, and point-light halation:
  <https://cinestillfilm.com/blogs/news/cinestill-800t-in-your-toolbox>
- Kodak technical education on sensitometry and stock choice:
  <https://www.kodak.com/en/motion/page/filmmaker-resources/>
- Kodak reference on low-speed texture and low-contrast latitude:
  <https://www.kodak.com/content/products-brochures/Film/kodak-essential-reference-guide-for-filmmakers.pdf>
- Fujifilm data-sheet index for C200, Superia, Pro 400H, Velvia, and Provia:
  <https://www.fujifilm.com/uk/en/consumer/support/films/negative-and-reversal>
- ILFORD product information and technical documents:
  <https://www.ilfordphoto.com/>

## Delivery Order

1. Run `stock_response_benchmark.py` on the reference fixture set and record
   near-duplicate pairs.
2. Add synthetic exposure ramps, neutral colour chart, skin-like warm colours,
   foliage-like yellow/green colours, blue/cyan highlights, and monochrome
   spectral patches to the native fixture suite.
3. Implement the sensitometry stage with a no-op compatibility path.
4. Reauthor Cinestill first, then monochrome, then the colour-negative and
   reversal families.
5. Require material-off separation plus human visual approval before retaining
   sibling speeds as primary stocks.
6. Group the desktop catalog by family and show a concise stock character, not
   a flat list of loosely differentiated ISO labels.

## Benchmark Usage

```powershell
python cpp_engine/tools/stock_response_benchmark.py 9339116563.rw2
python cpp_engine/tools/stock_response_benchmark.py 9339116563.rw2 --stocks cinestill_50d,cinestill_400d,cinestill_800t
```

The JSON artifact is written under `cpp_engine/out/benchmarks/` by default and
is intentionally not source-controlled.
