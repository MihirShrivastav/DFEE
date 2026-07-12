# DFEE Filmic Grain Model

This note defines the target behavior for `effect_pipeline_version=filmic_v2`
grain. It is the implementation guide for `M6-003`.

## Why Redesign Grain

The current native grain stage is the `parity_v1` baseline. It is deterministic,
fast enough after earlier optimization, and already better than simple noise:

- it generates sparse/grit fields
- it shapes particles through a boolean-style kernel
- it separates RGB dye layers
- it makes blue/yellow dye grain more visible
- it applies grain in gamma space
- it gates grain with the analyzer's smooth-region receptivity mask

The limitation is that the exposure response is too generic. Real grain is not
an even overlay and not one universal bell curve. It depends on stock speed,
emulsion family, density, exposure placement, development/scanning, enlargement,
and whether the image is color negative, reversal, or black-and-white.

`parity_v1` must remain untouched. The redesign belongs under `filmic_v2`.

## Research-Grounded Principles

### 1. Grain Is Perceptual Texture, Not Pixel Noise

Film grain comes from developed silver particles or dye clouds, but the visible
grain in a final image is an optical/perceptual effect. Kodak's Print Grain
Index exists because apparent grain depends on print size, viewing distance,
negative format, and human perception. A renderer should therefore control
visible grain texture, not merely add random variance.

Design implication:

- grain amplitude and size should be tied to output scale
- preview and export must stay visually consistent at different resolutions
- grain should have clumps, correlation, and dye-layer structure

### 2. Exposure Changes Grain Visibility

Underexposure does not literally create more crystals. It can make grain look
harsher because shadow detail is represented by fewer exposed grains/dye clouds,
then scan/print correction stretches that sparse signal.

Proper exposure generally looks smoother. Overexposed color negative often looks
smoother in shadows and lower mids because there is more usable negative
density, while bright highlights roll into the shoulder and should not receive a
uniform noisy overlay. Extremely dense negatives can still become scan-textured,
but that is a separate scanner/print behavior and should not be the default
film-emulsion grain model.

Design implication:

- lifted shadows and lower mids can show stronger/coarser grain
- well-exposed mids should show stock-character grain without harshness
- bright highlights should be smoother and lower contrast
- clipped/specular regions should not receive obvious salt-and-pepper texture

### 3. Stock Family Matters

The grain stage needs stock character. Four scalar fields are not enough for
long-term fidelity, though they are enough to bootstrap v2 behavior.

Initial families:

- `modern_color_negative_fine`: Portra, Vision3 daylight stocks
- `modern_color_negative_high_speed`: Portra 800, Vision3 500T, Cinestill 800T
- `consumer_color_negative`: Gold, Ultramax, ColorPlus, Superia-style stocks
- `color_reversal_fine`: Ektachrome, Provia, Velvia
- `bw_cubic`: Tri-X, HP5, Double-X
- `bw_tabular`: Delta, T-Max-like profiles

Expected behavior:

| Family | Grain Character |
| --- | --- |
| `modern_color_negative_fine` | fine, smooth, dye-cloud texture, correlated layers |
| `modern_color_negative_high_speed` | larger, more visible, more chroma texture |
| `consumer_color_negative` | moderate size, less refined clumping, visible lower-mid texture |
| `color_reversal_fine` | very fine, tighter, lower chroma grain, cleaner highlights |
| `bw_cubic` | monochrome, classic clumpy silver texture, stronger edge presence |
| `bw_tabular` | monochrome, finer/tighter, less chunky than cubic B&W |

### 4. Grain Should Be Deterministic

DFEE must not shimmer while sliders move. The same file, stock, print stock,
grain controls, pipeline version, and render size should produce identical grain.

Design implication:

- grain seed must include file/profile/settings/pipeline version
- v2 can cache precomputed fields keyed by seed, size, family, and controls
- warm preview should reuse fields rather than regenerating expensive patterns

## Proposed `filmic_v2` Profile Fields

Existing fields remain valid:

```yaml
grain:
  size: 0.45
  strength: 0.38
  chroma_strength: 0.08
  roughness: 0.5
  peak_zone: lower_mid_to_mid
  texture_masking: 0.8
```

Optional v2 fields:

```yaml
grain:
  family: modern_color_negative_fine
  target_pgi_35mm_4x6: 37
  clumpiness: 0.42
  micro_grit: 0.22
  layer_correlation: 0.78
  shadow_response: 0.85
  midtone_response: 1.0
  highlight_response: 0.35
  underexposure_coarsening: 0.35
  overexposure_smoothing: 0.28
```

These fields are optional so existing YAML remains compatible. When absent,
the native solver should infer reasonable values from `stock_type`, ISO/base ISO,
`grain.size`, `grain.strength`, `grain.chroma_strength`, and `grain.roughness`.

## Initial Native Algorithm

1. Resolve stock-family grain parameters.
2. Generate or fetch deterministic procedural fields:
   - low-frequency clump field
   - mid-frequency particle field
   - micro-grit field
   - optional independent dye-layer fields
3. Build an exposure-density response mask:
   - shadow/lower-mid boost
   - midtone stock baseline
   - highlight suppression
   - smooth-region visibility from analyzer mask
4. Apply grain in perceptual/gamma space for preview/export consistency.
5. For color stocks, blend common luminance grain with dye-layer grain using
   `layer_correlation` and `chroma_strength`.
6. For monochrome stocks, use a single silver-density field.
7. Clamp gently and avoid introducing false color in neutral highlights.

## Acceptance Tests

Native tests:

- `parity_v1` grain remains deterministic and unchanged.
- `filmic_v2` grain is deterministic for identical settings.
- shadow/lower-mid fixture has higher grain delta than highlight fixture.
- highlight fixture remains smoother than lower-mid fixture.
- fine-grain stock fixture has lower grain variance than high-speed fixture.
- monochrome stock fixture has near-identical per-channel grain deltas.
- color stock fixture has controlled but nonzero channel decorrelation.

Bridge/server tests:

- `effect_pipeline_version=filmic_v2` preview/export keeps route contracts.
- native reports continue to record `effect_pipeline_version`.

Performance checks:

- no-server preview probe compares `parity_v1` and `filmic_v2`.
- warm `filmic_v2` grain uses cached fields and stays bounded.
- export benchmark records `export_image_render_stage_grain`.

## Non-Goals For First Slice

- Do not tune every stock perfectly in the first patch.
- Do not require all profile YAML files to add new fields immediately.
- Do not make the CPU path depend on CUDA.
- Do not change Python `parity_v1` behavior.
