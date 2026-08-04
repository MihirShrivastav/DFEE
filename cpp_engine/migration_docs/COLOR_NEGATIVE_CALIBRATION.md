# Colour-Negative Calibration Assay

## Purpose

Colour-negative profile work must be tied to the active native renderer, not to
generic statements such as "warm", "cinematic", or "more saturated". This
assay provides a stable set of synthetic linear-RGB patches that reveal how a
profile changes neutral lightness, chroma, and hue after the actual `filmic_v3`
tone, dye, colour-response, density, hue-gain, and compression stages.

It is a diagnostic instrument, not a claim to reproduce a particular scanner,
paper, DI grade, lab process, or rem-jet-removal derivative.

## Command

Build the native CLI, then run one stock at a time:

```powershell
cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_cli
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_cli.exe --stock-assay vision3_250d
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_cli.exe --stock-assay vision3_500t
```

The command prints a tab-separated table for these named patches:

- `neutral_shadow`, `neutral_mid`, `neutral_highlight`
- `skin_like`
- `red`, `orange`, `yellow`, `green`, `cyan`, `blue`, `magenta`
- `cyan_highlight`

Columns are measured in OKLab after the native color stages:

- `L_delta`: change in perceptual lightness. Large hue-patch-specific movement
  should be justified by dye density or the stock's characteristic curve.
- `chroma_ratio`: output chroma divided by input chroma. Neutral patches report
  `1.0` by definition because hue/chroma is not meaningful near neutral.
- `hue_delta_deg`: shortest signed hue rotation. Treat broad rotations with
  suspicion unless supported by spectral/dye evidence. It is `n/a` for neutral
  patches because hue is undefined there.

## Calibration Rules

1. Start with published technical data: balance, nominal speed, latitude,
   grain/image structure, and stated colour intent.
2. Use the assay to map each intended statement to a small number of active
   profile fields. Do not alter all colour fields at once.
3. Keep near-neutral patches close to neutral. A stock should not become a
   global white-balance preset merely because it was daylight- or
   tungsten-balanced at capture.
4. Model hue-family compression as a limiter, not as a hue-replacement tool:
   red/orange and cyan/blue compression should protect dense/highlight colours;
   yellow/green muting should only be used when there is clear evidence.
5. Verify a real material-off RAW assay after every profile family pass. The
   synthetic assay establishes direction; the RAW assay catches interactions
   with tonal analysis and real scene colours.
6. Record source links and the resulting measurement in
   `STOCK_REAUTHORING_PROGRAM.md`; preserve the profile role in a native test.

## Interpretation Boundaries

The stock's capture balance is not the same thing as a permanent output cast.
Correct white balance belongs to RAW development and user controls. The profile
may still have a measured zonal colour response, crossover, hue gain, or chroma
shoulder, but those must be narrow and explainable.

For motion-picture negatives, do not model CineStill/rem-jet-removal halation as
the base stock. That is a derivative process behavior and is calibrated
separately.
