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

## Current Slice

The first implementation adds the exposure contract and reorganizes the React
workflow around Film Recipe, Film Exposure, Color Character, and Material
Finish. Existing print controls and advanced digital controls remain available
while later slices add independent stock-relative controls for highlight colour
hold, shadow colour retention, and palette separation.
