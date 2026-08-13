# Preset System — Design Spec

**Status:** Approved for planning (2026-08-11)
**Product:** Film Lab (DFEE desktop app + Lightroom plugin, one binary two modes)

## Problem / Goal

There is no way to save and reuse a look. Users build a film recipe (stock + develop
controls) per image and lose it. We want a **preset system** with a **browser** that is more
intuitive than Lightroom's: create a preset from the current settings, organize presets into
folders/groups, and apply them with a live view of the actual photo. The Lightroom plugin's
left pane is empty (`width: 0` in `lightroomRoundTrip` mode) — that becomes the browser.

## What a preset is

A preset is a **complete look**: the `stock` plus every "look" control in
`EngineController::defaultFilmControls()` — film response, tone/exposure grade, color,
grain, halation/bloom, print finish, color grading (3-way + HSL). It **excludes per-image
geometry** (`crop_*`, `straighten_deg`, `rotate_quadrant`, `flip_h/v`) and transient input
handling that is image-specific — applying a preset never changes the crop or orientation.

Not partial/selective (no per-setting checkboxes, no stacking): clicking a preset yields
exactly its render every time. (A single global "apply strength / fade" blend is a possible
*future* addition, explicitly out of scope for v1.)

## Storage

Human-readable JSON, **one file per preset**, and **folders are real directories** so groups
map 1:1 to the filesystem and presets are portable/shareable/backup-able.

```
Documents/Film Lab/Presets/           (QStandardPaths::DocumentsLocation + /Film Lab/Presets)
  Golden Hour.json
  My Portraits/                       ← a user group (subdirectory)
    Soft Skin.json
    Punchy.json
```
(Storage moved to Documents so presets are easy to find/share, like other apps.)

Preset file schema:
```json
{
  "schemaVersion": 1,
  "name": "Golden Hour",
  "stock": "portra_400",
  "controls": { "film_contrast": 108.0, "temp": 12.0, "...": "... look controls only ..." },
  "createdAt": "2026-08-11T10:30:00Z"
}
```
- `group` is implied by the containing directory (root = ungrouped).
- Unknown/missing control keys on apply fall back to defaults (forward/backward compatible).
- `controls` stores only the look subset (geometry keys are never written or read).
- **Preset id** = its path relative to the presets root without extension (e.g.
  `My Portraits/Soft Skin`), stable across sessions; built-ins are namespaced (e.g.
  `Film Lab/Kodachrome Gold`).
- **Favorites** persist in a separate `presets/favorites.json` (a list of preset ids), not
  inside the preset files — so a built-in (read-only) can be favorited without mutating it.

**Built-in presets** ship read-only inside the app (Qt resource / install dir), shown in a
non-deletable **"Film Lab"** group, clearly marked. A curated starter set (~6–10 tasteful
looks across stocks) so the browser is never empty on first run. Built-ins can be applied and
**duplicated** (the copy becomes an editable user preset) but not renamed/deleted in place.

## The browser (UI)

Lives in the **left pane**. In `lightroomRoundTrip` (plugin) mode it fills the pane. In
standalone the left rail gets a **Library ⇄ Presets toggle** (same component either way).

- **Header:** `＋ New preset from current` · **list / grid** view toggle · search field.
- **Grid view:** each tile is a **live thumbnail of the current photo** rendered through that
  preset, with the preset name. **List view:** name + a small static swatch, denser, no
  per-preset render.
- **Interaction:** **hover → the main preview live-updates** to that preset (non-committal
  peek); **click → apply** (commits). Leaving the row / Esc restores the pre-hover state.
- **Groups:** collapsible sections; a pinned **★ Favorites** row at the top. `New group`
  button. Per-preset ⋯ / right-click menu: rename, duplicate, move to group, set favorite,
  delete. Built-ins expose only duplicate / favorite.
- **Create from current:** captures the current recipe, prompts for a name (+ optional group),
  writes the file, selects it.

## Live thumbnails (performance)

- Rendered on the existing worker thread at a small fixed size, **cached by (image identity +
  preset content hash)**.
- Generated **only for tiles currently visible** (viewport-driven), queued and throttled so
  scrolling never floods the engine.
- **Invalidated when the open image changes**; browsing the same image afterward is instant
  from cache. List view renders no thumbnails.
- Thumbnails reflect the current image *with its current geometry* + the preset's look.

## Apply & undo

Applying replaces `stock` + look controls in one shot (geometry untouched) and registers a
**single undoable step**, consistent with the existing Reset / before-after revisioning.
Hover-preview does not create undo steps.

## Architecture

- **`PresetStore`** — new C++ `QObject` (sibling to `EngineController`), owns the on-disk +
  built-in model and exposes to QML:
  - a list model of groups → presets (name, id, group, isBuiltIn, isFavorite);
  - invokables: `createFromCurrent(name, group)`, `apply(id)`, `previewHover(id)` /
    `clearHover()`, `rename(id, name)`, `remove(id)`, `move(id, group)`, `duplicate(id)`,
    `createGroup(name)`, `setFavorite(id, bool)`;
  - `thumbnail(id)` returning a cached image provider URL, rendered lazily.
- **`EngineController`** gains `Q_INVOKABLE QVariantMap captureRecipe() const` (stock + look
  controls, no geometry) and `void applyRecipe(const QVariantMap&)`, plus a hover/peek path
  (apply a recipe transiently and restore) so `PresetStore` stays decoupled from render
  internals. Thumbnail rendering reuses the engine's small-preview render with a given recipe.
- **`PresetBrowser.qml`** — the left-pane component (header, view toggle, grouped list/grid,
  context menu, new-preset dialog). Reused in both modes.

## Testing

- **Store unit tests** (C++): round-trip a recipe → JSON → recipe (look keys only, geometry
  excluded); groups map to directories; unknown-key tolerance on load; built-ins load
  read-only; duplicate/move/rename/delete mutate the filesystem correctly.
- **Capture/apply**: `captureRecipe()` then `applyRecipe()` restores every look control and
  the stock, and leaves geometry unchanged.
- **Manual/UI**: grid live-thumbnails render for the open image and invalidate on image
  change; hover previews and restores; list/grid toggle; create-from-current writes a file
  that reloads identically.

## Non-goals (v1)

- Partial/selective presets, setting-level checkboxes, preset stacking.
- Global apply-strength / fade blend (possible future).
- In-app import/export UI, cloud sync (files are already portable on disk).
- Editing a built-in preset in place (duplicate to edit).
- Auto-generating thumbnails for non-visible presets or a full offline thumbnail cache.
