# Film Lab desktop — design language (v2)

The single source of truth for the native app's look. **Every UI element follows
this**; no default Qt Quick Controls (Basic) styling may show through. Approved
mockup (main window, crop mode, component sheet):
https://claude.ai/artifact/595M73HfHvmwkhUFEiJQpB — layout and behaviour are in
`docs/superpowers/specs/2026-09-27-ui-redesign-design.md`.

Character: **dark, quiet, structured — macOS-grade.** The photo is the only thing
allowed to be loud. Supersedes v1 "Graphite" (bevels, gradient cards, 38px
buttons, pill tab rows), which is retired.

## Principles

1. **The photo leads.** Neutral graphite chrome; the only color is the photo, the
   film tiles and one accent.
2. **One place for each thing.** Films and looks in the tray, adjustments in the
   inspector, files in the sidebar. Never tab rows inside tab rows.
3. **Lines, not boxes.** Panels meet at 1px hairlines. Rounded cards only for
   things you pick up: tiles, the film card, popovers.
4. **Quiet until changed.** Defaults read dim, edits read brighter, edited
   sections carry a dot.
5. **Every action, two ways.** Each command is in the menu bar with its shortcut;
   tooltips name the key.

## Color tokens (`theme/Theme.qml`)

| token | value | use |
|---|---|---|
| `window` | `#1C1C1E` | window, tray |
| `panel` | `#202022` | sidebar, inspector |
| `toolbar` | `#242426` | unified title + toolbar |
| `canvas` | `#131314` | photo surround |
| `inset` | `#161618` | recessed: segmented track, search field, scopes |
| `control` | `#3A3A3C` | quiet button fill, slider track |
| `selected` | `#4A4A4E` | active segment |
| `rowSelected` | `rgba(255,255,255,0.08)` | selected sidebar / history row |
| `hairline` | `rgba(255,255,255,0.07)` | every panel/section separator |
| `accent` | `#0A84FF` | selection ring, focus ring, the one commit button per view, edited dot |
| `text` | `#D4D4D8` | titles, edited values, primary text (never pure white) |
| `textSecondary` | `#949499` | control labels |
| `textCaption` | `#8E8E93` | captions, group headers, default values |
| `textTertiary` | `#6E6E73` | counts, hints, ISO lines |
| `sliderFill` | `#636368` | slider amount (tempered, never bright) |
| `knob` | `#CFCFD4` | slider knob, with `0 1px 3px rgba(0,0,0,.5)` shadow |
| `danger` | `#E0655B` | errors only |

White text appears only on the accent fill (commit button, highlighted menu item).

## Type (Geist, bundled)

| role | size · weight · color |
|---|---|
| Titles, section headers, film name | 13 · 600 · `text` |
| Body, sidebar rows | 13 · 400 · `text` / `#B8B8BD` |
| Control labels | 12 · 400 · `textSecondary` |
| Values | 12 · tabular figures · `text` if edited, `textCaption` if default |
| Captions, camera line | 11 · 400 · `textCaption` |
| Group headers (Library, History) | 11 · 600 · `textCaption`, sentence case |
| Empty-state title | 28 · 600 |

Never below 11px. Sentence case for labels; US spelling ("Color").

## Space, shape, motion

- 4px grid. Control height 26; segment height 24; section header row 40;
  slider row = 12px label line + 7px gap + 4px track.
- Radii: controls 6, segmented track 7 (segments 5), cards/tiles 6–8, popovers
  and window 10.
- Panel widths: sidebar 232, inspector 300; tray 176; toolbar 52.
- Motion 120–160ms ease-out, opacity and position only. No bounce.
- Icons: Phosphor, 16px, ~1.6 stroke, tinted `textSecondary`; every icon-only
  button has a tooltip naming its shortcut.

## Components

- **Toolbar (unified):** sidebar toggle + "Film Lab"; file name (13/600) over a
  camera line (11, `textCaption`, tabular); centred compare segmented control;
  zoom; Crop, Export (accent), help; window controls.
- **Segmented control:** `inset` track with 1px inner hairline, active segment
  `selected` fill + `0 1px 2px rgba(0,0,0,.4)`. Only for switching views of the
  same thing; never stacked; one per region.
- **Buttons:** accent filled (one per view, the commit), quiet (`control` fill),
  text-only (`textSecondary`), icon (transparent, hover `rowSelected`).
- **Section header:** 40px row, title 13/600, chevron (down = open, right =
  closed), edited dot (accent, 5px), Reset on hover, optional one-word state
  ("Off", "Auto grain") when collapsed; hairline below. Never boxed.
- **Slider row:** label left, value right; 4px `control` track, `sliderFill`
  amount (bipolar sliders fill from a centre tick), 14px `knob`. Double-click
  resets; keyboard focus shows an accent ring around the knob.
- **Switch:** label left (12, `textSecondary`), 26×16 track right — `control`
  off, `accent` on, 12px `knob`. For on/off film behaviours (Adaptive scene tone,
  Match grain to film speed).
- **Inspector section:** section header (above) over a body padded 16 / 14,
  rows 14 apart, hairline under the whole section. Subgroups inside a section get
  an 11/600 caption ("Grain", "Halation"), never a nested header.
- **Picker popover:** popover surface, rows 28 (40 with box art), group captions
  11/600, current row bold with an accent check; hover `rowSelected`.
- **Scope:** `inset` well, radius 6, pinned above the inspector sections;
  histogram 64 tall (R/G/B fills at ~0.3 alpha), click switches to the
  vectorscope (152 tall).
- **Color wheel:** hue ring fading to `inset` at the centre, 13px handle with a
  `knob` ring; drag from the centre to tint, double-click clears.
- **Film card:** 8px-radius raised row (`#2A2A2D`, inner hairline): box art 40,
  film name, "type · ISO", chevron; opens the Films tray. Blurb below in caption.
- **Tiles (Films/Looks/Roll):** radius 6; hover = 1px light ring and a live
  preview on the canvas; selected = 2px gap + 2px accent ring. Box-art badge
  18px bottom-left on film tiles; edited badge on roll tiles.
- **Sidebar rows:** 28px, radius 6, selected `rowSelected`; folder icon tinted
  accent when selected; counts in `textTertiary`.
- **Floating mode toolbar (crop):** 44px, radius 12, `rgba(36,36,38,.92)` with
  shadow and inner hairline, centred at the canvas bottom; inspector dims to ~0.4.
- **Menu bar:** File · Edit · Photo · View · Help; every command with its
  shortcut; highlighted item = accent fill, white text.
- **Tooltips, popovers:** `#2A2A2D`, radius 8 (tooltip) / 10 (popover), inner
  hairline, wrap at 260px.
- **Export sheet (520 wide, labels in a 112px column):** *Save to* — inset folder
  box (folder icon, path elided in the middle, caret) opening a picker grouped
  Favourites / Recent / Folders (Film Lab Exports, Next to the original, Choose
  folder…), plus a star to pin the folder. *File name* — template field, token
  chips `{name} {film} {date} {seq} {camera}` that insert at the cursor, the
  resulting name in caption below, and a note when the name is taken (danger colour
  for Replace/Skip). *If the name exists* — Add number / Replace / Skip. A hairline,
  then Format, Quality or Resolution. Every choice is remembered (`exportPrefs`).
  After an export the canvas status line offers **Show in folder**.

## Behaviour rules (kept from v1)

- Controls that do not apply are dimmed (~0.4), not hidden.
- Lightroom Edit-In mode: no sidebar library, tray shows Films | Looks only,
  the commit button reads "Save & Return".
