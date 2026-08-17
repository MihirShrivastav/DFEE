# Print Emulation V2 Design

## Problem

The legacy print stage treats a print stock as a stack of unrelated global grades:
channel multiplication, a per-channel S curve, black lift, global contrast, highlight
scaling, Lab bias, RGB multipliers, saturation, and synthetic grain. Those operations
are applied independently in linear RGB. They can fight each other, clip channels, and
make a print finish simultaneously bleach highlights and collapse shadow separation.

That is not how a colour print is made. A timed camera-negative print is a single
exposure-to-dye-density process with bounded cyan, magenta, and yellow layer
differences. The material response determines the print's usable contrast and its toe
and shoulder. Printer timing is a small exposure/filter correction, not a destructive
RGB multiplier.

## Evidence

Kodak describes VISION Color Print Film 2383/3383 as producing rich blacks and neutral
highlights. Its H-1-2383 technical information publishes separate cyan, magenta, and
yellow sensitometric/dye-density curves for the ECP-2D process. Kodak's processing
documentation describes 2393 as having a higher upper tone-scale density than 2383,
with deeper shadows and more vivid colour, while its toe curves are more closely
matched for neutral projected highlights. Kodak's Laboratory Aim Density workflow also
uses a neutral control patch within the normal printing range rather than an arbitrary
global RGB grade.

Sources:

- Kodak, [VISION Color Print Film 2383/3383 technical information](https://www.kodak.com/content/products-brochures/motion-picture/KODAK-VISION-Color-Print-Film-2383-3383-technical-information.pdf).
- Kodak, [Motion-picture film processing module 9A](https://www.kodak.com/content/products-brochures/Film/Processing-KODAK-Motion-Picture-Films-Module-9A.pdf).
- Kodak, [Laboratory Aim Density documentation](https://www.kodak.com/en/motion/page/laboratory-tools-and-techniques/).

Published curves are material measurements, not a complete digital LUT. DFEE uses them
to constrain the role of each stock: 2383 is neutral through highlights with rich but
separated blacks; 2393 is a denser, higher-contrast alternative rather than a generic
orange-and-teal grade. Individual profile values remain a display-referred calibration
which must be verified against representative images.

## V2 Contract

1. **Tone response is one print characteristic curve.** The curve is evaluated from
   linear luminance relative to an 18-percent print aim. It has an authored middle
   slope, usable latitude, toe and shoulder joins, and paper black/white limits.
2. **Strength blends a tone delta.** At zero strength the stage is exact identity. At
   partial strength it blends source and print tone in perceptual tone, then returns to
   linear RGB with a luminance-preserving scale. It never linearly mixes two images.
3. **Dye behavior is restrained and tone-zone aware.** Neutral balance and optional
   zone biases operate in OKLab after tone mapping. Chroma changes are proportional to
   existing chroma, so neutral patches remain neutral unless the profile deliberately
   calls for a small balance bias.
4. **Colour-head controls are bounded timing corrections.** Cyan, magenta, and yellow
   controls map to small OKLab opponent-axis offsets. They cannot zero an RGB channel,
   and their influence is strongest through the printable mid-scale rather than at
   paper black or white.
5. **Paper black is a material endpoint, not a global lift.** Profile d_min is part
   of the curve. The legacy Black Point control becomes a bounded paper-base adjustment
   around that endpoint, preserving exact black at its neutral setting.
6. **No print grain is added by this stage.** Camera-film grain is the material texture
   model. Print grain is not a reliable generic overlay and was a source of repeated
   noise passes. A future scanned-print texture effect, if needed, belongs to Material
   Finish and must be opt-in.

## V2 Profile Schema

    print_pipeline: print_v2
    tone:
      gamma: 1.02
      latitude_stops: 6.4
      toe_onset: 2.0
      toe_hardness: 0.85
      shoulder_onset: 1.7
      shoulder_hardness: 1.05
      d_min: 0.012
      d_max: 0.985
    color:
      neutral_balance_lab: [0.0, 0.0, 0.0]
      v2_shadow_bias_lab: [0.0, 0.0, -0.15]
      v2_midtone_bias_lab: [0.0, 0.10, 0.20]
      v2_highlight_bias_lab: [0.0, 0.0, 0.05]
      chroma_scale: 1.03
      shadow_chroma_scale: 0.98
      highlight_chroma_scale: 0.94

The loader remains permissive. A print profile without print_pipeline: print_v2
continues through the legacy path until it is individually reauthored. This prevents
quietly changing a saved recipe's appearance.

## Validation

- strength = 0 must be byte-identical.
- A neutral ramp remains neutral for a neutral profile.
- Curve output is monotonic, bounded by profile endpoints, and retains distinct values
  in both the toe and shoulder.
- Printer-head extremes remain finite, bounded, and preserve printable highlight and
  shadow separation.
- V2 stock fixtures establish 2393 as denser/more contrast-forward than 2383 without
  clipping a representative luminance ramp.
- Profile migration is per stock; it requires a native test and visual review.

## Rollout

1. Introduce the V2 plan and renderer while retaining legacy profiles.
2. Reauthor Kodak 2383 and 2393 from Kodak material documentation.
3. Reauthor Fujifilm Eterna CP 3510, archival Eastman, and editorial matte as separate
   material/paper roles.
4. Replace the desktop's destructive legacy wording with print-material controls and
   preserve legacy recipe keys as aliases where practical.
5. Create representative RAW and developed-TIFF visual acceptance fixtures before
   declaring the print stage complete.
