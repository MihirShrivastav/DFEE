# Wide-Gamut RAW Pipeline Implementation Plan

**Status:** planned; no renderer colour-space switch has been made

**Depends on:** M6-010, M6-011, M6-013

## Delivery Rule

Each phase lands as a separately testable commit. `wide_gamut_v1` is opt-in until
all CPU parity, RAW/TIFF corpus, and desktop visual tests pass. Do not alter stock
profiles merely to make an incomplete phase look closer to Lightroom.

## Phase A - Contracts And Diagnostics

1. Add `ColorSpaceDescriptor`, `TransferFunction`, `InputClassification`, and
   `InputTreatmentOverride` to native bridge/session types.
2. Replace `is_tiff_filename()` decisions in session code with decoded
   classification. Keep the old helper only as a decoder-routing convenience.
3. Expand render reports with source profile, input class, input-treatment source
   (`auto` or `user_override`), working space, display space, output space, and
   clipping metrics before/after development.
4. Add fixtures for mosaic DNG, linear DNG, tagged TIFF (sRGB, Adobe RGB, ProPhoto),
   untagged TIFF, and existing JPEG/PNG cases.

**Gate:** no behavior change under `srgb_legacy_v1`; every fixture classification is
explicitly asserted.

## Phase B - Colour-Managed Input And Core Math

1. Add LittleCMS-backed ICC inspection and transform-cache ownership to the session.
2. Implement matrix/transfer conversion helpers for sRGB, Adobe RGB, Display P3,
   Rec.2020, ProPhoto RGB, and linear forms.
3. Convert all decoders into linear Rec.2020 `DFEE Working RGB`; use DNG tags or
   camera calibration rather than assuming sRGB.
4. Replace sRGB-only luminance, RGB <-> XYZ, and OKLab entry points with
   colour-space-aware versions.
5. Add unit tests with known matrices, D65/D50 chromatic adaptation, transfer
   round-trips, and ICC conversion fixtures.

**Gate:** a tagged ProPhoto TIFF and equivalent tagged sRGB reference converge in
working RGB; no per-pixel ICC allocation; peak preview memory budget unchanged by
more than the documented descriptor/cache overhead.

## Phase C - RAW Developer Calibration

1. Extend LibRaw decode diagnostics to retain black/white levels, source clipping,
   camera make/model, and colour metadata needed by calibration.
2. Build camera-family calibration descriptors with a bounded matrix/LUT-like
   correction in working space. Apply them after input transform and before Film Lab.
3. Replace single-number baseline calibration with corpus measurements for toe,
   midtone, diffuse highlights, saturated highlights, hue error, saturation, and
   neutral balance.
4. Update Auto Balanced to use post-develop headroom prediction and explicit
   sensor/diffuse/specular distinction. Preserve As Shot as no automatic placement.
5. Evaluate highlight reconstruction separately by camera family; do not enable a
   generic LibRaw recovery mode without fixture evidence.

**Gate:** camera-grouped edit-free RAW/TIFF references improve globally, with no
increase in clipping or hue error. Film profiles are evaluated only after baseline
acceptance.

## Phase D - Film Lab And Display

1. Make HSL, hue compression, film hue curves, crossover, grain colour, halation,
   bloom, and print stages declare whether they operate in working RGB or perceptual
   XYZ/OKLab.
2. Revalidate stock family response tests and visual assays under `wide_gamut_v1`.
3. Add monitor ICC/display-transform support to the Qt preview path. The UI renders
   display-referred pixels only; the engine cache remains linear working RGB.
4. Add soft proof/gamut warning infrastructure as a later UI slice, but keep the
   transform architecture ready for it.

**Gate:** stock identity remains intentional across RAW and developed TIFF inputs;
no operation uses RGB coefficients from the wrong primaries.

## Phase E - Export And Migration

1. Add export colour-space options: sRGB default, Display P3, Adobe RGB, Rec.2020,
   and ProPhoto RGB for suitable 16-bit TIFF/PNG formats.
2. Embed output ICC profiles and record the transform/gamut-map choice in export
   reports.
3. Keep JPEG limited to practical SDR output profiles; warn before exporting
   wide-gamut 8-bit files where banding/interoperability risk is high.
4. Migrate new desktop recipes to `wide_gamut_v1`; preserve legacy recipe playback.
5. After broad corpus acceptance, make `wide_gamut_v1` the default for new edits.

**Gate:** export fixtures validate embedded ICC, bit depth, expected pixels, and
round-trip interoperability in Windows Photos/Lightroom/Photoshop test workflows.

## Explicit Developed-Image Behavior

| Input | Default | Why |
| --- | --- | --- |
| Lightroom 16-bit ProPhoto TIFF | `RenderedRgb` | It already has a camera profile and display tone; transform ICC to working RGB, do not raw-develop again. |
| Lightroom-converted mosaic DNG | `MosaicRaw` | Its pixels remain sensor mosaic; edits/metadata do not turn it into a TIFF. |
| Linear DNG | `LinearSceneRgb` | It is demosaiced RGB, so do not apply CFA/sensor-specific processing. It may still need scene-to-display development. |
| PNG/JPEG | `RenderedRgb` | They are display-referred by default; retain their established look. |
| Linear/scanned TIFF | User-selected `LinearSceneRgb` | TIFF alone cannot prove intent. The override prevents double development. |

## Test Corpus Requirements

- At least three camera families, each with daylight, overcast, tungsten, saturated
  subject, high dynamic range, and skin/neutral scenes.
- Matched Lightroom/ACR TIFF references exported as tagged 16-bit sRGB and ProPhoto.
- Mosaic and linear DNG fixtures from the same source where licensing permits.
- Synthetic primaries, near-neutral ramps, channel-clipped highlights, and out-of-sRGB
  but in-Rec.2020 colour fixtures.
- Golden reports for input classification and export ICC metadata.
