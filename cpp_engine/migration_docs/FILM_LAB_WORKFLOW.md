# Film Lab Workflow

DFEE is a film-emulation lab for digital RAW files. Its primary editing flow
must follow the photographic path into and out of a film system rather than a
generic digital-editor panel order.

## Primary Flow

1. **Film Recipe**: choose camera stock and optional print medium.
2. **Film Exposure**: choose scene placement and a stock-relative exposure
   offset before the emulation response.
3. **Color Character**: alter the selected stock's colour personality through
   bounded, stock-relative controls.
4. **Material Finish**: grain, halation, bloom, and other optical/emulsion
   response.
5. **Print**: print medium, CMY colour head, print density, and print contrast.
6. **Advanced Correction**: conventional exposure, curves, HSL, local detail,
   and other digital correction tools.

The app may expose advanced correction at all times for expert workflow, but it
must not be presented as the default way to obtain a film look.

## Exposure Contract

Native preview and export requests accept:

- `exposure_placement`: `auto_balanced` or `as_shot`
- `film_exposure_ev`: `-3.0` to `+3.0`

`auto_balanced` solves a stock-aware scene placement before applying the film
response. `as_shot` preserves the captured RAW placement. `film_exposure_ev`
is added before the emulation stage and is therefore different from the legacy
advanced `exposure` correction.

For API compatibility, omitted `exposure_placement` defaults to `as_shot`.
The Film Lab UI explicitly sends `auto_balanced` by default.

## Naming Rule

Primary labels must describe a general visual or photographic behaviour:

- `Color Character`, not a subject-specific label
- `Emulsion Density`, not a global `Saturation` substitute
- `Scene Placement`, not an opaque adaptation percentage
- `Material Finish`, not a catch-all effect panel

Tooltips may explain likely subject outcomes, such as foliage, skin, skies, or
neon signage, but those examples must not be part of the control name because a
photograph may not contain them.

## Color Character Contract

The Color Character group exposes four bipolar controls, each in the range
`−100` to `+100`. The neutral value `0` is always a no-op: the stock renders
exactly as calibrated with no modification. All four controls are
`filmic_v2`-only. Sending any non-zero value under `parity_v1` is rejected
with an explicit error (HTTP 400 on the server, a native error on the C++
path) — it is never a silent render. This keeps `parity_v1` bit-for-bit
reproducible.

| Field | What it alters |
| --- | --- |
| `highlight_color_hold` | Scales the stock's resolved highlight chroma rolloff and highlight desaturation so colour survives further into bright zones. High-zone only; never touches lightness. |
| `shadow_color_retention` | Scales the stock's resolved shadow chroma rolloff. Shadow-gated; no black lift. Chroma only. |
| `palette_range` | Per-pixel hue-anchor attraction in OKLCh. Chroma-gated so near-neutral pixels are preserved. Hue-wrap is stable via `sin(delta)`. Default 6 perceptual anchors (radians) unless the stock YAML supplies a `palette.anchors` override. |
| `emulsion_color_density` | Scales the stock's dye chroma body (`chroma_boost`). Distinct from the legacy `film_color` multiplier, which also scaled compressions and biases. `film_color` is retained as a legacy / Advanced control. |

**Request contract.** All four fields are accepted on preview (query params)
and export (POST body). Omitting a field is equivalent to `0`. Values outside
`−100..100` are rejected with a range-validation error.

**Report JSON.** The report records both the four requested input values and
the four resolved per-stock sensitivities (`highlight_hold_sensitivity`,
`shadow_retention_sensitivity`, `emulsion_density_sensitivity`,
`palette.range_sensitivity`).

**Native application.** Color Character is applied only inside
`apply_color_response_and_coupling_pipeline`, the sole live render path.
Monochrome stocks resolve all four sensitivities to `0` and skip the colour
stage entirely. The legacy Python engine path is not extended; the controls
are filmic_v2 native-only and are a documented no-op on the legacy fallback.

## Completed Slices

- **M7-001 / M7-002**: Exposure contract, React workflow reorganisation around
  Film Recipe / Film Exposure / Color Character / Material Finish.
- **M7-003**: Color Character — all four controls shipped
  (`highlight_color_hold`, `shadow_color_retention`, `palette_range`,
  `emulsion_color_density`), including YAML `color_character:` group, native
  solver, renderer, report, request contract, parity guard, and UI.

## Remaining Implementation

The detailed delivery order, contracts, dependencies, and acceptance criteria
are maintained in [FILM_LAB_IMPLEMENTATION_PLAN.md](FILM_LAB_IMPLEMENTATION_PLAN.md).
The next implementation slice is M7-004: Process (calibrated push/pull
development). The following M7-003F human steps remain outstanding:

- Visual acceptance pass on representative RAWs (reversal, negative, B&W).
- Preview / export timing probe under the Color Character code path.
