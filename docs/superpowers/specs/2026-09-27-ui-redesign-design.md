# Film Lab desktop UI redesign — design

Date: 2026-09-27. Status: approved (design + phasing). Supersedes the inspector layout of
`desktop/DESIGN.md` v1 (a v2 revision ships with Phase 1).

## Why

Standalone RAW opening (c302db0) turned Film Lab from a Lightroom plug-in surface into an app people
live in, and the UI did not scale to that:

- two stacked tab rows (Develop / Geometry / Export, then Film / Light / Color), oversized buttons,
  no separation between groups;
- film choice — the product's central decision — buried in a combobox;
- no image details; no per-photo memory (the previous photo's look silently carries over to the
  next one);
- Lightroom's hierarchy is better, but its small type hurts readability, and it is a generic editor.

**Goal: a fresh UI built around Film Lab's purpose — making film looks easy.** Not a Lightroom clone.

## Decisions (confirmed with the user)

| Topic | Decision |
|---|---|
| Workflow | Both: one look synced across a portrait shoot; per-photo tweaks on travel rolls. Per-photo memory, copy/paste look and sync are core. |
| Film choice | A bottom tray — Roll / Films / Looks — the current photo rendered in each stock, hover to preview. |
| Edit memory | App catalog (SQLite in app data); photo folders are never written to. |
| New photos | Open clean (no film). Looks arrive by paste, sync or "Last look". |

Standing constraints: 13px body / 12px labels (calm comes from showing less, not shrinking);
controls named for the visual effect they produce
(`2026-07-13-primary-film-look-controls-design.md`); US spelling ("Color"); Lightroom Edit-In
round-trip keeps working; engine renders must not change (golden hashes).

## Layout

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│ ☰  Japan / DSC0421.ARW · A7R V · 35mm f/2 · 1/250 · ISO 100   Edited▾  Fit  │ Crop  Export  ? │
├────────┬───────────────────────────────────────────────────────┬─────────────────┤
│Library │                                                       │ scopes          │
│        │                                                       │ Film   Portra400│
│History │                     P H O T O                         │ ─ Exposure      │
│        │                                                       │ ─ Tone          │
│        │                                                       │ ─ Color         │
├────────┴───────────────────────────────────────────────────────┤ ─ Grain & light │
│ [Roll] [Films] [Looks]     Color negative · Slide · B&W         │ ─ Print         │
│ ▣ ▣ ▣ ▣ ▣ ▣ ▣ ▣  (the current photo in each stock)              │ ─ Fine-tune ▸   │
└─────────────────────────────────────────────────────────────────┴─────────────────┘
```

### Top bar
Panel toggle · breadcrumb (folder / file) · one quiet line of camera info (camera, lens, ISO,
shutter, aperture — from engine metadata, empty fields dropped) · compare mode (Edited / Split /
Side by side) and zoom · Crop · Export (Lightroom mode: **Save & Return**) · help.

### Left panel (collapsible)
Library (pinned folders) and History. In Lightroom mode the panel is hidden and History opens as a
top-bar popover.

### Canvas
The photo is the hero. **Crop is a canvas mode** (C / R) with a floating toolbar — aspect, straighten,
rotate ±90°, flip, reset, Done — replacing the Geometry tab. The existing crop maths is reused.

### Bottom tray
Segmented **Roll | Films | Looks** (Lightroom mode: Films | Looks).
- **Roll** — the current folder's photos; multi-select; edited badge; edited thumbnails.
- **Films** — the current photo rendered through every stock (with the current adjustments, so a
  tile is exactly what a click gives), grouped Color negative / Slide / B&W, box-art badge, ISO and
  a one-line visual description. Hover (≈120 ms) previews the stock on the canvas; click applies
  (one history step); Esc restores. `[` `]` still cycle stocks.
- **Looks** — saved presets rendered on the photo; "Save current as look" first, then a pinned
  **Last look** (the recipe last committed on any photo — also the continuity path in Lightroom
  mode, where each Edit-In is one photo).

### Inspector
One scrolling column, **no tab rows**, hairline separators, sections in darkroom order. Each section
header: title, reset (only when changed), collapse (state persisted). Small scopes pinned on top.

| Section | Controls |
|---|---|
| Film | stock card (box art, name, type, ISO), film strength |
| Exposure | scene placement (As shot / Auto balanced), film exposure |
| Tone | highlight rolloff, film contrast, shadow lift, preserve rendered tone, adaptive scene tone |
| Color | color density, color boost, crossover, split toning (two swatches = a second view of the grading wheels' shadow/highlight hue+sat), temperature, tint, vibrance, saturation |
| Grain & light | grain (match film speed, strength, size, roughness), halation (strength, threshold), bloom |
| Print | print stock, strength, color head C/M/Y, contrast, paper black |
| Fine-tune (collapsed) | basic tone (exposure, contrast, highlights, shadows, whites, blacks, midtones), detail (texture, clarity, dehaze, sharpening, mask), HSL, color grading wheels |

Monochrome stocks dim (not hide) the color controls.

### Export
A sheet: format (8-bit PNG, 16-bit PNG, 16-bit TIFF, JPEG), quality, DPI, destination. Ctrl+S opens
it in standalone mode; in Lightroom mode Ctrl+S / Save & Return exports directly.

### Keyboard (kept)
Left/Right photo (when no slider/text field has focus), Ctrl+Left/Right, `[` `]` stock, `\`
before/after, B compare cycle, Ctrl+Z/Y, Ctrl+S, ?/F1 help; focus returns to a plain `keySink`
item after every popup. New: C/R crop, Ctrl+Shift+C/V copy/paste look, Ctrl+Alt+V paste previous,
Ctrl+Shift+S sync.

## Per-photo edit memory

- **EditStore**: SQLite (Qt6::Sql, WAL) at `AppDataLocation/catalog.sqlite`.
- `photos(path_key UNIQUE, path, file_size, mtime, content_sig, stock, controls_json (incl.
  geometry), edited, rating, flag, thumb, thumb_stale, history_json (≤ 50 steps), schema_version,
  updated_at)` and `meta(key, value)`. `path_key` = normalised, lower-cased absolute path;
  `content_sig` (size + first 64 KB hash) re-attaches edits after a folder move.
- Loaded **before** the first preview request on every open; no record ⇒ defaults. Saved 400 ms
  after an edit, immediately on photo switch, quit and export; never while peeking. Missing keys
  are filled from defaults and unknown keys dropped, as presets already do.
- History persists (≤ 50 steps); the first step of a reopened photo is "Original".
- **Bypassed in Lightroom mode**: the TIFF already carries Lightroom's edits, and reopening a
  saved-back TIFF must not apply the film look twice.

## Engine additions (no change to rendering maths)

- Parsed stock/print profiles cached per path + mtime (YAML is re-parsed on every render today).
- `EngineSession::render_look_proxy` — a small (≈256 px) render of the current photo with a given
  recipe, reusing the preview's analysis (masks resized, solver input shared), grain off, RGB8 out.
  `render_preview` and the proxy share one extracted pipeline function; preview output stays
  byte-identical, gated by golden render hashes.
- Proxies run on the one worker at the lowest priority (Open > Preview/peek > Auto-grain > Export >
  Proxy), one in flight, only when idle, cancelled by image epoch; target p50 ≤ 60 ms per tile.

## Phasing

0. Foundations: QML split (pixel-identical), golden hashes + request dump, profile cache, proxy API,
   metadata to QML, EditStore (fixes carry-over), stock catalog, UI-script extensions.
1. New shell on the existing API (top bar, left panel, canvas + minimal crop mode, minimal export
   sheet, inspector chain, tray v1 with static Films tiles, DESIGN.md v2).
2. Live Films browser (scheduler, proxy tiles, hover peek).
3. Looks in the tray (presets rendered, Save current, Last look).
4. Finish crop and export.
5. Roll workflow (selection, copy/paste, sync with Undo).
6. Later: ratings/flags, filtering, batch export.

## Verification
Offscreen `DFEE_SCREENSHOT` matrix (standalone + Lightroom mode, 1280×800 and 1920×1080) against
baselines; `DFEE_UI_SCRIPT` scripts for shortcuts, focus, peek, memory and sync; golden render hashes
unchanged; request-dump diff for fixed recipes. Never inject system-wide input.
