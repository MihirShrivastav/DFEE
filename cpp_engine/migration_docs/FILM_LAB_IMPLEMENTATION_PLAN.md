# Film Lab Implementation Plan

This plan completes the product flow introduced by M7-001 and M7-002. It is
an implementation backlog, not a promise that a control can be made authentic
by changing a generic digital-editor slider.

## Product Boundary

The Film Lab is the primary path for photographers who want an intentional
film rendering from a digital RAW. Its controls describe a photographic or
material outcome, then map to bounded, profile-aware native behavior.

Advanced Correction remains available for conventional operations such as
white balance, curves, HSL, detail, and local contrast. It must not silently
drive a Film Lab control, and a Film Lab control must not be implemented as a
renamed generic HSL, saturation, exposure, or blur adjustment.

Primary control names must be scene-agnostic. Tooltips may describe likely
effects on familiar subjects, but never assume a photo contains a particular
subject.

## Target Workflow

1. **Film Recipe**: camera stock, optional print stock, and recipe defaults.
2. **Film Exposure**: scene placement and stock-relative film exposure.
3. **Color Character**: stock-relative color behavior.
4. **Process**: calibrated push/pull behavior after exposure is established.
5. **Material Finish**: grain, halation, bloom, and optical texture.
6. **Print**: print medium plus CMY, density, contrast, and black point.
7. **Advanced Correction**: expert digital adjustment and diagnostics.

## Contract Rules

- Every new Film Lab parameter needs an explicit neutral default, valid range,
  native request field, report field, and versioned behavior statement.
- Preview and export must use the same parameter model and stage ordering.
- A change that deliberately changes rendering belongs to `filmic_v2` or a
  later named pipeline version. `parity_v1` remains reproducible.
- The native profile loader continues to reject unconsumed YAML fields. A new
  profile field requires loader, solver, renderer, report, fixture, and test
  support in the same slice.
- UI controls must describe what they change and retain a one-click neutral
  reset. Tooltips explain behavior without replacing the label with jargon.

## Delivery Order

### M7-003: Color Character — DONE

All code subtasks are merged. Outstanding human steps are listed after the
table.

| ID | Task | Native behavior | Status |
| --- | --- | --- | --- |
| M7-003A | Define profile and request schema | Add neutral, bounded fields (`−100..+100`) for `highlight_color_hold`, `shadow_color_retention`, `palette_range`, and `emulsion_color_density`. Parity guard rejects non-zero values under `parity_v1`. | Done |
| M7-003B | Implement highlight color hold | Scales the stock's resolved highlight chroma rolloff and highlight desaturation. High-zone only; never touches lightness. | Done |
| M7-003C | Implement shadow color retention | Scales the stock's resolved shadow chroma rolloff. Shadow-gated; no black lift. | Done |
| M7-003D | Implement palette separation | Per-pixel hue-anchor attraction in OKLCh; chroma-gated (neutrals preserved); hue-wrap stable via `sin(delta)`. Default 6 perceptual anchors unless the stock overrides. Later evolved into the bipolar **Palette Range** (see Primary Film Look Slice 1 below). | Done |
| M7-003E | Build the Color Character UI | Generic labels, concise tooltips, neutral/reset states, mono-disabled states, `film_color` relocated to Advanced. | Done |
| M7-003F (human) | Calibrate and validate | Visual acceptance pass on representative RAWs (reversal, negative, B&W) + preview/export timing probe. | **Outstanding — not yet run** |
| M7-003G (added) | Implement emulsion color density | Scales the stock's dye chroma body (`chroma_boost`). Distinct from legacy `film_color` multiplier. | Done |

### Primary Film Look Redesign — Slice 1: Palette Range — DONE (code)

Spec: `docs/superpowers/specs/2026-07-13-primary-film-look-controls-design.md`.
`palette_separation` was renamed to `palette_range` and evolved into a bipolar
control: negative merges hues toward dominant anchors with coupled
desaturation (harmonised/ethereal limited palette), positive separates hues and
lifts chroma. Calibrated to a plausibly-real ceiling (6-anchor default keeps
merge desaturation ~27.5%; `velvia_50` uses 4 tuned anchors for a stronger
signature). Outstanding human steps: visual-acceptance pass on representative
RAWs and a preview/export timing probe (shared with the M7-003F item above,
since both exercise the same colour pipeline). Remaining redesign slices:
Film Color Density, Highlight Rolloff + Film Contrast, Halation (numeric) +
Bloom, Grain finer controls, panel consolidation.

### M7-004: Process

Process must be distinct from Film Exposure. Film Exposure changes virtual
exposure reaching the stock; Process models how development changes the stock
response after capture.

| ID | Task | Native behavior | Acceptance criteria |
| --- | --- | --- | --- |
| M7-004A | Define process schema | Add `process_ev` with an initial supported range of `-2` to `+2`, optional stock calibration fields, and explicit unsupported-stock behavior. | No generic exposure, contrast, or saturation aliasing. |
| M7-004B | Implement calibrated push | Change tone slope, shoulder/toe, grain, and color response through profile-driven process coefficients. | +1 and +2 show distinct stock response without clipping or uncontrolled color shifts. |
| M7-004C | Implement calibrated pull | Change contrast, highlight latitude, grain, and color response through separate coefficients. | Negative values do not merely reverse push math. |
| M7-004D | Build Process UI and report | Use `Process` with an intuitive push/pull scale and a clear neutral state. | UI explains that this is development behavior, while reports record the calibrated process inputs. |
| M7-004E | Calibrate stock families | Start with a deliberately small supported set, then add fixtures and YAML values per stock family. | Unsupported stocks remain neutral or are clearly marked; no fabricated calibration claims. |

### M7-005: Print Stage

Separate print operations from Film Recipe once the visual language is stable.

| ID | Task | Native behavior | Acceptance criteria |
| --- | --- | --- | --- |
| M7-005A | Promote print to a dedicated group | Move print stock and existing CMY/density/contrast/black-point controls into `Print`. | Camera stock selection remains in Film Recipe; no request-contract break. |
| M7-005B | Define print-neutral behavior | Ensure no-print and neutral-print settings are deterministic and correctly reported. | Preview/export/no-print behavior remains compatible with current pipeline versions. |
| M7-005C | Add print calibration fixtures | Exercise stock-to-print combinations, monochrome behavior, and export consistency. | Supported combinations are tested; invalid combinations return actionable validation errors. |

### M7-006: Material Finish Completion

Close visual acceptance on the already redesigned bloom, halation, and grain
before adding more material controls.

| ID | Task | Acceptance criteria |
| --- | --- | --- |
| M7-006A | Complete M6-003 grain acceptance | Review representative bright, low-light, smooth, detailed, color, and monochrome RAWs at preview and export resolution. | No blotchy field artifacts, pixel-noise roughness collapse, or preview/export mismatch. |
| M7-006B | Complete bloom/halation acceptance | Review point lights, bright edges, broad highlights, and scenes without highlight sources. | Diffusion is source-bound and filmic; it does not become a global white blur or halo every edge. |
| M7-006C | Material UI polish | Present grain, halation, and bloom as bounded material choices with stock defaults and clear override states. | Control names remain general; stock defaults are discoverable without overcrowding the panel. |

### M7-007: Recipes, Compare, and Recoverability

Recipes are the durable user artifact, not an opaque browser-only state.

| ID | Task | Acceptance criteria |
| --- | --- | --- |
| M7-007A | Versioned recipe format | Persist selected stocks, Film Lab controls, pipeline version, and compatible advanced settings. | Recipe migration is explicit; unsupported values fail safely rather than silently changing a rendering. |
| M7-007B | Save, load, duplicate, and reset recipes | Add local recipe management first; defer cloud concerns. | A loaded recipe produces identical native report inputs and deterministic preview output. |
| M7-007C | Add A/B compare and before/after | Compare rendered states without duplicating full-resolution source buffers. | Interaction remains responsive and cache-aware on large RAW files. |

### M7-008: Guided Analysis and Practical Workflow

Guidance should explain the image and stock relationship, not invent automated
edits that hide user intent.

| ID | Task | Acceptance criteria |
| --- | --- | --- |
| M7-008A | Surface concise stock/scene diagnostics | Use native analysis to show exposure placement, highlight headroom, and applied material defaults. | Diagnostics are factual, logged/reported, and do not claim unavailable scene understanding. |
| M7-008B | Add safe starting-point actions | Offer explicit actions such as reset to stock default or return to as-shot placement. | Actions are reversible, recorded in history, and never overwrite a recipe without user intent. |
| M7-008C | Workflow accessibility and responsiveness | Keyboard navigation, labels, tooltips, focus states, narrow-panel layout, and sensible debouncing. | Controls remain operable without a mouse and do not send redundant preview renders. |

### M7-009: Release Hardening

| ID | Task | Acceptance criteria |
| --- | --- | --- |
| M7-009A | Contract matrix | Test every Film Lab parameter through C++, pybind, Python bridge, FastAPI preview/export, report JSON, and React request construction. | Missing, invalid, neutral, and non-neutral values are covered. |
| M7-009B | Image-quality regression set | Maintain small fixtures and approved visual references for all supported stock families. | Controlled numeric and perceptual tolerances detect unintended renderer drift. |
| M7-009C | Performance and memory probes | Measure warm/cold preview latency, repeated slider updates, export peak memory, and recipe/A-B cache behavior. | Results are recorded with hardware, pipeline version, source dimensions, and acceptance decision. |
| M7-009D | Documentation and API release review | Update README, architecture, API behavior, task list, and user-facing tooltip copy. | Docs, UI labels, and runtime contract agree. |

## Explicit Deferrals

- Masks, lens correction, denoise, and geometry tools are valuable editing
  capabilities but are not Film Lab controls. They require their own native
  processing and UX plan.
- CUDA is a cross-cutting acceleration project. It must accelerate stable CPU
  stages and preserve the Film Lab contract rather than define visual behavior.
- AI scene classification is not required for the first Film Lab. The UI must
  work correctly for any image without assuming subjects are present.

## Definition of Done for a Film Lab Slice

1. Contract documented and version behavior stated.
2. Native CPU implementation and complete fallback behavior exist.
3. Preview, export, report, and React all carry the same values.
4. Unit, bridge, route, and visual acceptance evidence are recorded.
5. Memory and preview performance are checked on representative RAWs.
6. API docs, migration task list, and APAM are updated in the same commit.
