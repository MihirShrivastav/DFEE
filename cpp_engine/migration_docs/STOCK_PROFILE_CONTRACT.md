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
| `color_character` | Optional. Per-stock sensitivity calibration and palette anchor configuration for the Color Character controls. See section below. |

## `color_character` YAML Group (Camera Stocks)

This group is optional. When absent, all sensitivities are inferred from the
stock family in the native solver. Monochrome stocks always resolve every
sensitivity to `0` regardless of what is written here.

### Leaf fields

| Leaf | Range | Meaning |
| --- | --- | --- |
| `highlight_hold_sensitivity` | `0.0`–`1.0` | How strongly the stock responds to `highlight_color_hold`. `1.0` = full intended range; `0.0` = effectively disabled. |
| `shadow_retention_sensitivity` | `0.0`–`1.0` | How strongly the stock responds to `shadow_color_retention`. |
| `emulsion_density_sensitivity` | `0.0`–`1.0` | How strongly the stock responds to `emulsion_color_density`. |
| `palette.range_sensitivity` | `0.0`–`1.0` | How strongly the stock responds to `palette_range`. |
| `palette.anchors` | variable-length array of radians | Hue anchors (OKLCh h, in radians) that `palette_range` merges toward (negative) or separates away from (positive). Overrides the engine default of 6 evenly spaced anchors. Fewer/tuned anchors give a stronger, more stock-specific merge signature. |
| `palette.anchor_weights` | variable-length array, same length as `palette.anchors` | Per-anchor strength weights. Omit to use uniform weights. |

### Radian anchor convention

Hue anchors are expressed in radians in the OKLCh hue space (`0`–`2π`).
Approximate reference values:

| Hue | Approximate radians |
| --- | --- |
| Red | 0.19 |
| Yellow | 1.06 |
| Green | 2.19 |
| Cyan | 3.14 |
| Blue | 4.01 |
| Magenta | 5.46 |

Calibrated profiles (e.g. `velvia_50`) may supply custom anchor arrays tuned
for that stock's palette character.

### Variable-length array loader rule

The native C++ profile loader was historically strict: all colour arrays had
to be exactly 3 values. This rule is now relaxed for a whitelisted set of
variable-length fields: `palette.anchors` and `palette.anchor_weights`. All
other array fields in all profile sections still require exactly 3 components.
Attempting to use a non-three-element array outside this whitelist is still a
loader error.

### Calibrated examples

| Stock | Notes |
| --- | --- |
| `velvia_50` | Reversal stock. Custom radian anchors matching the stock's strong palette character. |
| `portra_400` | Negative stock. Sensitivities calibrated for the stock's warm/neutral palette. |
| `colorplus_200` | Negative stock. Conservative sensitivities. |
| `tri_x_400` | Monochrome. All sensitivities resolve to `0`; `color_character` group present for documentation only. |

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
