# Native Stock Profile Contract

This document defines the YAML fields accepted by the native C++ engine for
`profiles/stocks/*.yaml` and `profiles/print_stocks/*.yaml`.

The C++ profile loader rejects unknown leaf fields, non-finite numbers, and
non-three-component colour arrays. This is intentional: a profile must never
silently carry configuration that the native renderer does not consume.

## Camera Film Stocks

| YAML section | Native use |
| --- | --- |
| `adaptation` | Controls base ISO grain adaptation, stock adaptation strength, cast correction sensitivity, highlight recovery sensitivity, and shadow-noise grain suppression. |
| `tone_response` | Drives the film toe, midtone density, shoulder, rolloff, black density, and per-layer tone differences. |
| `color_response` | Supplies zonal OKLab colour bias, stock-specific cast stabilization, and monochrome panchromatic weights. |
| `hue_saturation_response` | Supplies overall chroma response plus red/orange, yellow/green, cyan/blue, neon, and highlight colour handling. |
| `grain` | Drives baseline/v2 grain strength, scale, chroma, roughness, family, PGI, clumping, grit, layer correlation, tonal response, peak placement, and texture masking. |
| `halation` | Drives strength, trigger interpretation, resolution-scaled inner/outer diffusion radii, warm core, and red fringe. Rich geometry and colour apply through `effect_pipeline_version=filmic_v2`; `parity_v1` remains the compatibility baseline. |
| `chroma_coupling` | Drives highlight/shadow chroma compression and highlight hue convergence. |
| `dye_contamination` | Drives cross-channel dye-layer interaction before the main colour-response stage. |

## Print Stocks

| YAML section | Native use |
| --- | --- |
| `tone` | Drives print shadow lift, contrast, shoulder rolloff, and toe depth. |
| `color` | Drives zonal print bias, blue suppression, red boost, green shift, and saturation scale. |
| `grain` | Drives the subtle final print-grain layer. |

## Guardrails

- Use only fields listed in the current active profile schema. Adding a new
  leaf requires native loader, solver, renderer, report, and test coverage in
  the same change.
- Keep `parity_v1` behavior stable. New profile-driven material behaviour that
  intentionally changes the look belongs to `filmic_v2`.
- Profile tests load every active stock and resolve it through the native
  solver. Behavioural tests cover yellow/green muting and v2 grain placement
  and texture masking.
