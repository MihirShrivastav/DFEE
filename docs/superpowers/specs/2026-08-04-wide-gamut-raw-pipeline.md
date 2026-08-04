# Wide-Gamut RAW Pipeline And Input Contract

**Status:** proposed implementation architecture

**Date:** 2026-08-04

**Scope:** `cpp_engine` and the Qt desktop application

## Decision Context

DFEE currently asks LibRaw for camera-white-balanced, linear sRGB (`output_color =
LIBRAW_COLORSPACE_sRGB`, gamma `(1, 1)`, `no_auto_bright = 1`). The renderer then
uses a neutral RAW baseline before Film Lab stages. This is deterministic, but it is
not a complete RAW developer: it constrains the working gamut to sRGB before the
film profile, does not apply a DFEE camera-family colour transform, and cannot
faithfully match a Lightroom-developed TIFF merely by changing exposure.

The industry pattern is consistent:

- Lightroom/Camera Raw applies a camera profile as the foundation for RAW edits and
  uses ProPhoto RGB for Develop previews. Its exact internal pipeline is proprietary.
- Capture One assigns a camera profile, edits in a very large internal colour space,
  then converts through ICC profiles for output.
- darktable applies an input profile then uses scene-referred linear Rec.2020 by
  default.
- DxO applies a camera/DCP rendering in its wide-gamut workflow and separately
  protects saturated colours.

DFEE must adopt the same separation of concerns without copying any vendor's
implementation.

## Non-Goals

- Do not change a stock YAML to compensate for camera colour, TIFF interpretation,
  or a missing input transform.
- Do not use LibRaw auto-bright to make a raw preview look acceptable. It would hide
  exposure-model defects and make a film recipe less deterministic.
- Do not silently reinterpret a file just because it has a `.dng` or `.tiff`
  extension.
- Do not switch LibRaw from sRGB to a wide-gamut output until all RGB-dependent
  engine math has an explicit colour-space contract.

## Colour-Space Model

### Proposed canonical working space

`DFEE Working RGB v1` is **linear Rec.2020 RGB, D65 white point, float32**.

Why this first:

- It is substantially wider than sRGB while retaining D65, which aligns with most
  display/output paths and simplifies chromatic adaptation.
- It is a proven practical choice for a scene-referred photo pipeline.
- It has simple, well-known RGB <-> XYZ matrices and is available from LibRaw and
  LittleCMS.
- It avoids prematurely coupling DFEE to a complete ACES/VFX workflow.

This is a working-space decision, not an output default. JPEG remains sRGB by
default. Wider output is explicit, tagged, and selected at export.

### Required conversions

```text
camera mosaic RAW -- camera input transform --> linear DFEE Working RGB
ICC-tagged rendered RGB -- ICC + transfer decode --> linear DFEE Working RGB
linear DNG RGB -- DNG colourimetric transform --> linear DFEE Working RGB
DFEE Working RGB -- display ICC transform --> monitor preview
DFEE Working RGB -- output ICC + transfer encode --> JPEG/PNG/TIFF
```

The engine must keep colour-space metadata with every decoded image. An unlabelled
`Image` whose RGB primaries are inferred from call order is not acceptable after this
migration.

## Input Classification Contract

Extensions are only hints. The decoder produces an `InputClassification` from file
content, DNG/TIFF tags, embedded profiles, and decoder capabilities:

| Classification | Examples | Default pipeline | Auto placement | Camera bias / RAW baseline |
| --- | --- | --- | --- | --- |
| `MosaicRaw` | ARW, NEF, RAF, CR3, mosaic DNG | Full RAW develop | Enabled | Enabled |
| `LinearSceneRgb` | Linear/demosaiced DNG, linear HDR scan | Colour-managed linear scene path | Enabled, highlight-safe | No sensor-specific processing |
| `RenderedRgb` | Lightroom TIFF, JPEG, PNG, rendered TIFF | Colour-managed rendered path | Disabled by default | Disabled |
| `Unknown` | Invalid/untagged/ambiguous file | Require user choice or conservative rendered path | Disabled | Disabled |

### DNG rules

DNG is a container, not a synonym for mosaic camera RAW. Adobe's DNG Converter can
write either the default mosaic form or a linear, demosaiced form; the conversion is
one-way. A mosaic DNG receives the full RAW pipeline. A linear DNG must not be
demosaiced again or receive camera sensor clipping/bias logic. It enters as
`LinearSceneRgb` using its DNG colour tags/embedded profile.

If a linear DNG has been intentionally rendered with a display curve, auto-detection
cannot reliably infer artistic intent. The UI exposes an **Input treatment** override:

- `Camera RAW` (available only for a verified mosaic input)
- `Linear scene-referred`
- `Already developed`

The default and override are saved with the edit recipe and written to the render
report. The UI should show a small input badge and an explanatory tooltip, not an
opaque hidden heuristic.

### TIFF, PNG, JPEG, and scanner files

Most TIFF, PNG, and JPEG files are already developed RGB images. DFEE must honour
their embedded ICC profile and apply the rendered-input policy, preserving their
established exposure/tone while still allowing Film Lab colour and material effects.

Some 16-bit TIFFs are scene-linear scans or interchange files. They can be marked
`Linear scene-referred` through the same override. An untagged TIFF is never assumed
to be sRGB without a visible warning in the report; the conservative default is
rendered sRGB only until the user supplies a profile or confirms the treatment.

## Pipeline Architecture

```text
                  +-- MosaicRaw: decode, demosaic, camera WB, black/white normalization
Input classifier --+-- LinearSceneRgb: decode RGB + source colour metadata
                  +-- RenderedRgb: decode RGB + ICC/transfer metadata
                                     |
                                     v
                         transform to linear DFEE Working RGB
                                     |
             +-----------------------+------------------------+
             |                        |                        |
        RAW-only camera          scene analysis          rendered-input
        colour calibration       / clipping metrics      policy, no re-metering
             |                        |                        |
             +------------------- developed working image ------+
                                     |
                     Film Lab tone, colour, material and print stages
                                     |
                         display transform / export transform
```

### Stage contracts

1. **Decode:** preserve values above display white where the source supports them;
   record black/white levels, saturation masks, raw clipping ratios, CFA layout, WB,
   source ICC/DNG colour metadata, and decode warnings.
2. **Input transform:** use camera calibration or embedded DNG/ICC transforms to
   obtain `DFEE Working RGB`. No stock look belongs here.
3. **RAW develop:** only `MosaicRaw` receives camera-family baseline colour
   calibration, highlight reconstruction where justified, and neutral scene-to-display
   tone development. The baseline shoulder prevents pipeline-created clipping.
4. **Scene analysis:** measure tone and clipping in linear working space. Separate
   sensor-clipped, channel-near-limit, diffuse highlight, and specular highlight
   metrics. Positive Auto Balanced placement is bounded by post-develop headroom.
5. **Rendered policy:** `RenderedRgb` does not receive RAW baseline, camera bias, or
   automatic scene re-exposure. Manual controls remain fully available; Film Lab
   profile tone remains attenuated according to the rendered-input control.
6. **Film Lab:** profile colour operations use RGB -> XYZ -> OKLab/OKLCH transforms
   valid for the active working RGB primaries. Operations that are physically linear
   remain in linear working RGB.
7. **Display/export:** transform and gamut-map only at the output boundary. Embed the
   chosen ICC profile for TIFF/PNG and always tag JPEG sRGB by default.

## Existing-Code Gaps

The migration must deliberately replace these assumptions:

- `raw_decode.cpp` requests LibRaw linear sRGB for RAW inputs.
- `decode_tiff_image_from_file` assigns all TIFFs a synthetic `Rendered` identity and
  does not currently invoke LittleCMS to inspect and transform embedded profiles.
- `is_tiff_filename()` is used as the rendered-input discriminator. This makes DNG
  classification extension-driven and cannot distinguish linear DNG from mosaic DNG.
- RGB luminance coefficients, OKLab conversion, HSL, film hue curves, compression,
  and export encoders assume sRGB implicitly.
- `raw_development.cpp` is a neutral tone baseline only. It is not a camera colour
  profile and must stay independent of stock authoring.

## Compatibility And Recipe Semantics

Existing edits must reproduce under a named legacy contract:

- `color_pipeline_version = "srgb_legacy_v1"` remains available for old recipes and
  baseline regression fixtures.
- New edits use `color_pipeline_version = "wide_gamut_v1"` only after the complete
  input-transform and output-transform path is implemented.
- Existing TIFF behavior stays rendered by default. Existing mosaic DNG behavior stays
  RAW by default. Ambiguous linear DNGs receive the legacy RAW path only under the
  legacy version; the new path classifies and reports them correctly.
- The profile YAML schema gains an `authoring_working_space` field for validation, but
  stock profile values are not mechanically transformed. Each profile family is
  visually revalidated after the working-space migration.

## Performance And Memory Requirements

- Store a single float32 working image and reuse buffers in-place where safe. Do not
  retain source RGB, working RGB, preview RGB, and display RGB at full resolution
  simultaneously.
- Build/cache an ICC transform per source-profile -> working-profile -> destination
  tuple, never per pixel or per render request.
- Analyse a proxy, but preserve source clipping statistics from full decode.
- Use tiled export for all full-resolution colour transforms and report peak working
  memory in the export report.
- GPU implementation is deferred until CPU transforms have pixel-contract tests; GPU
  and CPU must use the same matrices, transfer functions, and gamut-map parameters.

## Acceptance Criteria

- A saturated RAW patch that is outside sRGB but inside Rec.2020 is not clipped before
  Film Lab and remains stable through a no-stock render.
- A tagged ProPhoto RGB Lightroom TIFF and an equivalent tagged sRGB TIFF are
  transformed to materially equivalent DFEE working values.
- Mosaic DNG, linear DNG, and rendered TIFF fixtures take their intended route and
  report the classification and source profile.
- Rendered TIFFs never receive raw auto-balance or camera cast correction unless the
  user explicitly changes Input treatment.
- Auto Balanced cannot create a new clipped diffuse-highlight region in RAW tests.
- `srgb_legacy_v1` parity fixtures remain within current tolerance.
- Wide-gamut exports embed the selected ICC profile; sRGB JPEG exports remain
  interoperable.

## Sources

- Adobe: Camera Raw profiles and RAW colour rendering, 2025.
- Adobe: Lightroom Classic colour FAQ and external editing recommendations, 2024-2025.
- Adobe: DNG Converter documentation confirming mosaic and linear/demosaiced DNG.
- Capture One: Colors in Capture One, 2025.
- darktable: input colour profile and scene-referred workflow documentation.
- DxO PhotoLab: Color Rendering and DxO Wide Gamut documentation.
