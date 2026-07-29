# Film Stock Tuning Pass — Changelog & Eye-Test Guide

A per-stock record of the family-by-family tuning pass: what changed, **why** (tied to the
[film-stock characteristics reference](film-stock-characteristics.md)), and **what to look for**
when eye-testing each stock. Values are hand-authored toward each film's documented/reputational
look and kept bounded to plausible ranges — not measured sensitometry.

Params are read at runtime: **no rebuild needed**, just restart the server to pick up YAML changes.

**Cast convention:** `*_bias_lab` and crossover casts are OKLab-ish `a = green(−)/red(+)`,
`b = blue(−)/yellow(+)`. So warm = +a/+b, cool = −b, green = −a.

---

## Family 1 — Kodak colour negative

Relative calibration held across the family: **saturation** Ektar (1.38) > UltraMax (1.35) >
Portra 800 (1.28) > Gold (1.22) > Portra 400 (1.20) > Portra 160 (1.18) > ColorPlus (1.15);
**contrast** Ektar (1.10) highest, Portra 800 (1.05) > 400 (0.96) > 160 (0.94) lowest.

### Portra 160 — *added per-hue chroma gain*
- **Change:** `hue_chroma_gain: {green: 0.04, blue: 0.03}`. Nothing else touched.
- **Why:** 160 is the quietest, most neutral Portra — low saturation, benchmark delicate skin.
  Reds/oranges are deliberately left at 0 (the existing `red_orange_midtone_compression: 0.42`
  already keeps skin from going ruddy; adding a negative here would double-desaturate skin).
  The gentle green/blue lift gives foliage and sky a touch of the pleasant Portra life without
  breaking its restraint.
- **Eye-test:** skin should stay warm, delicate, *never* orange; greens/blues slightly livelier
  than raw but still soft. If skin looks flat/grey, the green/blue lift is too strong.

### Portra 400 — *added per-hue chroma gain*
- **Change:** `hue_chroma_gain: {green: 0.05, blue: 0.04}`.
- **Why:** same family as 160 with a touch more life (doc: "a touch more life than 160"). Marginally
  stronger green/blue than 160; skin still protected by compression.
- **Eye-test:** the versatile default film look — natural warm skin, slightly more colour presence
  than 160, huge highlight latitude.

### Portra 800 — *added per-hue chroma gain*
- **Change:** `hue_chroma_gain: {green: 0.05, blue: 0.05, red: 0.03, magenta: 0.02}`.
- **Why:** the most saturated Portra with richer skin. Unlike 160/400 it gets a small **red/magenta**
  lift for the extra colour punch and warmer, richer (still flattering) skin the doc describes.
- **Eye-test:** richer than 400 — skin warmer/fuller, colours with more body, moderate warm halation
  glow on highlights. Should not tip into ruddy/over-saturated skin.

### Ektar 100 — *refined per-hue chroma gain*
- **Change:** `hue_chroma_gain` was `{blue:0.15, green:0.12, cyan:0.10, red:0.05}` → added
  `yellow: 0.06`, bumped `red: 0.05 → 0.07`.
- **Why:** doc calls for vivid blues/greens (kept — blues are the signature) *plus* punchy reds and
  a vivid landscape palette; yellows were previously untouched. Now the full landscape palette
  (blue → green → cyan → yellow → red) is vivid, matching "slide look on C-41."
- **Eye-test:** electric blue skies, punchy greens, vivid yellows/reds. Watch skin — Ektar's known
  weakness is running **red/ruddy** on lighter skin; that's faithful, but if it's extreme the
  red gain is too high.

### Gold 200 — *reviewed, kept*
- **Change:** none. Already fully authored (`hue_chroma_gain` warm yellows/oranges up + blues down,
  warm crossover). Confirmed consistent with the family (warmer than ColorPlus, less saturated
  than Ektar/UltraMax).
- **Eye-test:** strongly warm golden nostalgia, warm shadows + glowy warm highlights, muted blues.

### ColorPlus 200 — *added crossover*
- **Change:** added `crossover: {shadow_cast_a: -0.05, shadow_cast_b: -0.20, highlight_cast_a: 0.45,
  highlight_cast_b: 0.62, exposure_sensitivity: 0.38}`.
- **Why:** it had per-hue gain but no exposure-aware cast. Modelled as "Gold, but cheaper/flatter":
  warm highlights (slightly less clean than Gold's `0.55/0.70`) and mildly cool-green shadows for
  the budget-consumer character the doc notes.
- **Eye-test:** warm like Gold but a hair flatter and less refined; shadows shouldn't go as cleanly
  cool as Gold — a touch of muddy green is correct.

### UltraMax 400 — *reviewed, kept*
- **Change:** none. Fully authored and recently tuned (punchy reds/yellows, warm crossover,
  saturation 1.35 just under Ektar). Confirmed family ordering.
- **Eye-test:** vibrant point-and-shoot punch, strong reds/yellows, warm, coarser grain than Gold.

---

## Family 2 — Fuji colour negative

Both were already deeply authored; this was a faithfulness check. They sit opposite the warm Kodaks:
**cool, green/cyan-leaning**, which the crossover green shadow casts (Pro 400H `a −0.42`, Superia
`a −0.60`) and green/cyan-forward `hue_chroma_gain` already capture well.

### Pro 400H — *reviewed; added cyan chroma gain*
- **Change:** `hue_chroma_gain` `{green:0.06, magenta:-0.05}` → added `cyan: 0.04`.
- **Why:** the pastel/mint character was covered by the green lift + muted magenta + cool shadow
  crossover, but the doc's signature "airy cyans" wasn't explicitly reinforced. A small cyan lift
  gives that airy quality without breaking its low-saturation restraint (sat 1.05, the lowest neg).
- **Eye-test:** airy pastel — minty greens, gentle cyans, delicate neutral skin, magentas held cool
  and quiet. Should feel soft/washed in the highlights, never punchy.

### Superia 400 — *reviewed, kept*
- **Change:** none. Textbook match to the doc already: strong **green shadow cast** (crossover
  `a −0.60`, `shadow_bias_lab a −2.6`), punchy greens/cyans/blues with **subdued reds**
  (`hue_chroma_gain {green:0.15, cyan:0.12, blue:0.08, red:-0.06}`), medium-high saturation (1.28).
- **Eye-test:** cool and lively — greens/blues pop, shadows lean green, reds sit back vs any Kodak,
  skin cooler and can drift slightly green in mixed light (faithful).

---

## Family 3 — Cinema (ECN-2 origin)

Defining traits: **flat, low-contrast, gradeable** bases (contrast Eterna 0.82 < CineStill 50D 0.88
< 500T 0.90 < 250D 0.94), wide latitude, and **halation as the CineStill differentiator**
(strength: CineStill 800T 0.60 ≫ 50D 0.38 ≫ Vision3/Eterna 0.08–0.12). Per-hue gains kept small —
these are meant to be graded, not to arrive punchy.

### Vision3 250D — *added per-hue chroma gain*
- **Change:** `hue_chroma_gain: {red: 0.04, yellow: 0.04, magenta: 0.03}`.
- **Why:** doc notes ECN-2 "extra sensitivity to magenta/yellow/red." Tiny, controlled lifts honour
  that without breaking the flat gradeable base (sat stays 1.15, contrast 0.94). Crossover already
  present (near-neutral, low exposure sensitivity 0.35 = gradeable).
- **Eye-test:** clean near-neutral daylight, deep-but-controllable blacks, slightly warm-clean;
  magenta/yellow/red a touch more alive than raw. Should still look flat and easy to grade.

### Vision3 500T — *added per-hue chroma gain*
- **Change:** `hue_chroma_gain: {cyan: 0.05, blue: 0.03, red: 0.03}`.
- **Why:** tungsten stock — the cyan/blue lift supports the teal-shadow night mood the crossover
  already sets (`shadow_cast_b −0.60`), with a small red for ECN sensitivity. Sat stays low (1.10),
  contrast lowest-but-one (0.90).
- **Eye-test:** cool/blue in daylight, teal shadows, superb night colour; flat and gradeable.

### Fuji Eterna 250D — *added both signature tools*
- **Change:** added `hue_chroma_gain: {green: 0.03, red: -0.02}` and `crossover:
  {shadow_cast_a: -0.10, shadow_cast_b: -0.30, highlight_cast_a: 0.15, highlight_cast_b: 0.20,
  exposure_sensitivity: 0.35}`.
- **Why:** the most muted/desaturated stock (sat 1.02, contrast 0.82 — both the lowest). Hue gain is
  whisper-quiet (barely-there green, tiny red restraint) to preserve the desaturated pastel cine look;
  crossover is a gentle neutral-cool wash with low exposure sensitivity (flat gradeable).
- **Eye-test:** soft, muted, low-contrast pastel; accurate gentle skin; nothing should pop. If colours
  look lively, it's wrong — this is the flattest, quietest colour stock.

### CineStill 50D — *added both signature tools; warmed shadows*
- **Change:** added `hue_chroma_gain: {red: 0.05, yellow: 0.04, cyan: 0.04}` and `crossover:
  {shadow_cast_a: -0.08, shadow_cast_b: -0.25, highlight_cast_a: 0.35, highlight_cast_b: 0.40,
  exposure_sensitivity: 0.40}`; softened `shadow_bias_lab` b `−3.0 → −2.2`.
- **Why:** doc is "clean daylight, **slightly warm**," but the shadows were rendering quite cold
  (−3.0); pulled toward clean/slightly-warm. Warm-highlight crossover supports the halation glow
  (strength 0.38 — present but gentler than 800T). Clean medium saturation with warm reds/yellows
  and clean cyans (sky).
- **Eye-test:** clean daylight, a hint of warmth, **soft red halation glow** on bright speculars
  (gentler than 800T). Shadows should read clean, not icy.

### CineStill 800T — *reviewed, kept*
- **Change:** none. Signature verified: **strong red halation** (strength 0.60, outer radius 40,
  red-weighted warm core), **deep teal shadows** (crossover `shadow_cast_b −0.75`) + **warm/orange
  highlights** (`0.60/0.50`), neon cyan/orange hue gain. This is the family's showpiece.
- **Eye-test:** neon night — teal shadows, warm sodium highlights, and unmistakable **red halos**
  around lights. If the red halation isn't obvious on point lights, something regressed.

---

## Family 4 — Colour reversal (slide)

Saturation ordering held: **Velvia (1.55) ≫ Ektachrome (1.30) > Kodachrome (1.28) > Provia (1.15) >
Astia (1.03)**; contrast Velvia (1.25) highest, Astia/Provia gentle. Slides share deep blacks and
narrow latitude (high shoulder strength). Four of five gained their missing signature tool(s).

### Velvia 50 — *broadened per-hue gain; added crossover*
- **Change:** `hue_chroma_gain` gained `yellow: 0.06` and `orange: 0.05` (was green/blue/magenta/red);
  added `crossover: {shadow_cast_a: -0.10, shadow_cast_b: -0.70, highlight_cast_a: 0.30,
  highlight_cast_b: 0.30, exposure_sensitivity: 0.40}`.
- **Why:** doc: "best sat for yellows/oranges/purples" — those were missing from the vivid palette.
  The deep-cool-shadow crossover (`b −0.70`) is Velvia's signature velvet blue-black shadow with a
  slightly warm highlight. Everything stays maximal (sat 1.55, contrast 1.25, narrow latitude).
- **Eye-test:** electric everything — greens/reds/blues legendary, now vivid yellows/oranges too;
  deep cool-blue shadows, quick highlight clip. **Not** for skin (over-saturated/ruddy is faithful).

### Provia 100F — *added both signature tools*
- **Change:** `hue_chroma_gain: {blue: 0.04, green: 0.03}` + `crossover: {shadow_cast_a: -0.06,
  shadow_cast_b: -0.30, highlight_cast_a: 0.15, highlight_cast_b: 0.15, exposure_sensitivity: 0.35}`.
- **Why:** it's the **neutral reference chrome**, so both are deliberately minimal — a hair of clean
  blue/green, a gentle cool-neutral cast. Restrained saturation (1.15) preserved.
- **Eye-test:** accurate, true-to-life, slightly cool in studio; clean blues; nothing exaggerated.
  This is the "correct" slide — if it looks stylised, it's overdone.

### Astia 100F — *added both signature tools*
- **Change:** `hue_chroma_gain: {green: 0.03}` (skin hues deliberately untouched) + `crossover:
  {shadow_cast_a: -0.05, shadow_cast_b: -0.20, highlight_cast_a: 0.20, highlight_cast_b: 0.25,
  exposure_sensitivity: 0.30}`.
- **Why:** the **portrait slide** — softest, most natural, lowest saturation (1.03). Only a whisper
  of green; reds/oranges left alone so skin stays soft and natural; gentlest neutral-warm cast with
  the lowest exposure sensitivity of any stock.
- **Eye-test:** soft, natural, gentle — the "colour-neg of slides." Skin should be lovely and calm;
  the softest highlight rolloff of the slides.

### Ektachrome E100 — *added crossover*
- **Change:** `crossover: {shadow_cast_a: 0.05, shadow_cast_b: -0.45, highlight_cast_a: 0.35,
  highlight_cast_b: 0.25, exposure_sensitivity: 0.40}`.
- **Why:** modern E100 is slightly warm with **pronounced reds/pinks** (already in `hue_chroma_gain`
  red 0.12 / magenta 0.10) over clean-cool shadows. Crossover gives clean cool-blue shadows and warm
  reddish highlights — the sharp modern chrome look.
- **Eye-test:** sharp, clean, warm reds/pinks, cool clean shadows; more contrast/warmth than Provia.

### Kodachrome 64 — *added crossover*
- **Change:** `crossover: {shadow_cast_a: -0.05, shadow_cast_b: -0.55, highlight_cast_a: 0.40,
  highlight_cast_b: 0.45, exposure_sensitivity: 0.45}`.
- **Why:** the defining Kodachrome cross — **blue-shifted deep shadows + warm golden highlights** —
  finally per-stock. Kept **tasteful** (highlight warmth 0.40/0.45, not extreme) after the earlier
  over-cooked-reds episode; pairs with the existing legendary-red / luminous-blue / controlled-green
  (−0.05) hue gain.
- **Eye-test:** warm golden midtones, deep blue-shifted shadows, rich **reds**, luminous blues,
  3D micro-contrast pop; **creamy natural skin — never orange/waxy/fried**. If skin cooks, flag it.

---

## Family 5 — Black & white

No colour params (all colour sections are neutral no-ops). Tuning is **tone curve + grain**, plus
spectral (`pan_weight`) where authored. The focus was the two classic 400s, whose signature
distinction was blurred, and grain/contrast ordering across the set.

Final ordering — **contrast** (midtone): Tri-X 1.10 > Acros 1.08 > Delta 100 1.05 > Double-X 1.03 >
HP5 1.00 > Delta 3200 0.90. **Grain** (target PGI): Delta 3200 68 > Tri-X 56 > HP5 54 > Double-X 50
> Delta 100 34 > Acros 27.

### Tri-X 400 — *sharpened highlight contrast*
- **Change:** `midtone_contrast 1.08 → 1.10`, `shoulder_strength 0.78 → 0.80`,
  `highlight_rolloff_start 0.68 → 0.72`.
- **Why:** doc: "medium-high contrast (more than HP5), **extra contrast in the highlights**, punchy."
  Holding the shoulder later + firmer keeps highlight separation gutsy instead of rolling early.
  Strong toe (0.46) retained for classic blacks; coarse tactile grain (PGI 56) kept.
- **Eye-test:** gutsy, punchy, contrasty — highlights hold detail/contrast rather than glowing off;
  pronounced tactile grain. The classic reportage look.

### HP5 Plus 400 — *shifted contrast into the shadows*
- **Change:** `toe_strength 0.38 → 0.46`, `toe_length 0.34 → 0.32`, `shoulder_strength 0.70 → 0.66`,
  `highlight_rolloff_start 0.70 → 0.66`.
- **Why:** doc: "medium contrast, **extra contrast in the shadows**, deep darks, highlights push out."
  Previously HP5 was just a softer Tri-X at both ends, losing its character. Now it has the deep toe
  (shadow contrast) with an earlier/softer shoulder so highlights roll out gracefully — the forgiving,
  malleable documentary look. Midtone stays medium (1.00); grain subtler than Tri-X (PGI 54, size 0.5).
- **Eye-test:** deep rich shadows but **softer, forgiving highlights** (opposite emphasis to Tri-X);
  wide latitude, a touch more malleable/softer overall.

### Delta 100 — *reviewed, kept*
- **Change:** none. Very fine tabular grain (PGI 34), crisp medium contrast (1.05), late clean shoulder,
  green-weighted panchromatic response (`pan_weight 0.22/0.58/0.20`). Matches "sharp, refined, smooth."

### Delta 3200 — *reviewed, kept*
- **Change:** none. Textbook: low contrast (0.90, softest), **huge grain** (PGI 68, size 0.85,
  strength 0.78 — the largest in the library), grain peaking in the midtones. The point of the film.
- **Eye-test:** soft, moody, atmospheric, dominated by big prominent grain.

### Neopan Acros 100 — *finer grain (near-grainless)*
- **Change:** `grain.size 0.22 → 0.20`, `strength 0.20 → 0.16`, `micro_grit 0.12 → 0.10`,
  `target_pgi 30 → 27`.
- **Why:** doc: "finest ISO 100 B&W, near-grainless." It was already fine but sat close to Delta 100;
  now it's clearly the finest in the set. Strong green pan weight (0.18/0.62/0.20) and late shoulder
  (0.80) keep the long smooth tonal scale and clean whites.
- **Eye-test:** exceptionally smooth, delicate, near-grainless; clean bright whites, long tonal scale.

### Eastman Double-X (5222) — *softened to silvery medium contrast*
- **Change:** `midtone_contrast 1.12 → 1.03`.
- **Why:** it was the *most* contrasty B&W (1.12), contradicting the doc's "medium contrast, classic
  cine gradation, **silvery** timeless mid-tones." Lowered to a true medium so it reads as smooth
  vintage-cine gradation, sitting below Tri-X/Delta 100. Warm-ish pan weight (0.26/0.54/0.20) and
  moderate cine grain (PGI 50) retained.
- **Eye-test:** silvery, smooth mid-tones, classic Hollywood-movie gradation; moderate cinematic grain;
  gentler than Tri-X.

---

## Eye-test workflow

1. Restart the server (YAML is read at runtime — no rebuild).
2. Work family by family; within a family compare stocks against each other (the ordering notes above)
   as well as against the [characteristics reference](film-stock-characteristics.md).
3. Use images that exercise the range: skin, blue sky, foliage/green, saturated reds, bright speculars
   (halation), and deep shadow. Portraits for skin-critical stocks (Portra, Astia, Kodachrome), a
   sunlit/landscape frame for the vivid ones (Ektar, Velvia), and a night/point-light frame for CineStill.
4. Flag anything off and I'll iterate — the changes are all in bounded YAML params.
