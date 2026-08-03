# DFEE Desktop — "Graphite" Design Reference

The single source of truth for the native app's look. **Every UI element must
follow this** — no default Qt Quick Controls (Basic) styling is allowed to show
through. When adding or changing a control, restyle it here-first and reuse the
shared components in `qml/Main.qml`.

Core principle: **monochrome charcoal + tactile/skeuomorphic depth.** Colour only
ever comes from the photo, the boxart, the histogram, and the colour-grading
wheels. Nothing in the chrome is pure white; "active" is a *raised dark bevel
chip*, never a white fill (the sole exception is the checkbox tick glyph).

## Palette tokens (defined on `root` in Main.qml)

| token | value | use |
|-------|-------|-----|
| `bg` / `canvas` | `#0f0f10` | window + rail background, preview canvas |
| `panel` | `#1a1a1c` | (legacy) flat panel |
| `panelRaised` | `#202023` | popups / raised surfaces |
| `inset` | `#141416` | recessed fields: slider grooves, spinbox/combobox field, tab track |
| `border` | `#26262b` | stronger 1px divider |
| `hair` | `#14ffffff` | ~0.08 white hairline (AARRGGBB) — default 1px border |
| `textPrimary` | `#c7c7cc` | primary text (softened, never pure white) |
| `textSecondary` | `#8b8b90` | labels |
| `textMuted` | `#5a5a60` | disabled / captions |
| `textValue` | `#74747a` | dim right-hand slider read-outs |
| `accent` | `#e9e9ec` | reserved: checkbox tick chip only |
| `danger` | `#e0655b` | error text |

## Surfaces

- **Card (module / group):** radius `14`; gradient `#1d1d20 → #191a1c` (top-lit);
  `border 1px hair`; a 1px top-highlight rectangle `#12ffffff` inset by 1.
  Content inset `x:16 y:16`, `width parent-32`, inner spacing `12–14`.
- **Recessed field / track:** `color inset`, `border 1px hair`, radius `8–9`.
- **Popup:** `color panelRaised`, `border 1px border`, radius `10`.
- **Tooltip:** `color #232327`, `border 1px hair`, radius `7`, wraps at 260px,
  parented+anchored to its control (see `GraphiteTip`).

## The raised bevel chip (the one tactile primitive)

Used by buttons, tab/segmented active state, checkbox-on, and spinbox ± buttons:
- gradient `#34343a → #242429` (pressed: `#26262b → #1d1d20`; disabled: flat `panel`)
- `border 1px hair`
- 1px top-highlight `#16ffffff` inset by 1
- radius 7–8, text `textPrimary`

## Component rules

- **PrimaryButton:** raised bevel chip, full width, h38.
- **SecondaryButton:** flat `inset` fill + hair border, h38.
- **Segmented / Tabs:** recessed `inset` track (h34, radius9, 3px margin), active
  option = raised bevel chip; inactive = transparent, text `textSecondary`.
- **Checkbox (`GraphiteCheck`):** 18px, radius5. Off = recessed `inset`. On =
  raised bevel chip + light tick drawn in `#d7d7db`. Never a flat white box.
- **Slider (`FilmSlider`/`InspectorSlider`):** recessed groove `inset` (h4) with
  fill `#3c3c41`; raised metallic knob (`#cdcdd2 → #9a9aa1`, dark border). Label
  row is **fixed height 18** with vertically-centred children so the Reset button
  appearing never shifts the slider.
- **ComboBox:** `inset` field + hair border, `ChevronToggle` indicator, popup per
  Popup spec, highlighted delegate `#16ffffff`.
- **SpinBox (`GraphiteSpin`):** recessed `inset` field, value centred in
  `textPrimary`; − / + are raised bevel chips at the field edges. No white.
- **ScrollBar / indicators:** slim, `textMuted`-ish translucent handle on
  transparent track — never the Basic light bar.
- **BusyIndicator:** monochrome (`textSecondary`), no default blue.

## Typography

- Family: **Geist** (bundled). Antialiased.
- Window/app title `20 / Medium`. Card title `13 / Medium`. Section sub-label
  (`InspectorLabel`) `12 / Medium` `textSecondary`. Value read-out `12`.
  Tooltip / caption `11`.
- **Module names + "Film Lab" are Title Case.** Control labels are sentence case.
- **US spelling everywhere: "Color", not "Colour".**

## Motion / behaviour

- Collapse chevron points **down when collapsed** (expand), up when expanded.
- Conditional controls are **disabled (dimmed to ~0.35–0.5), not hidden**, when
  not applicable (Lightroom-style) — e.g. "Preserve rendered tone" on RAW.
- Mode-aware: in Lightroom edit-in mode, the Library pane, tab bar and Export tab
  are hidden; only Develop + "Save & Return to Lightroom" show.
