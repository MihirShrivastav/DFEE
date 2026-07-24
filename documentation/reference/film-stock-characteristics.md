# Film Stock Characteristics Reference

A practical, per-stock description of how each emulation *should* look, for checking renders
during tuning. Grouped by family. For each stock: overall temperature/cast, contrast,
saturation, skin, notable per-hue behaviour, shadow/highlight rendering, grain, and (where
relevant) halation.

**How to read casts:** in the profile, `*_bias_lab` is `[L, a, b]` where **a = green(−)/red(+)**
and **b = blue(−)/yellow(+)**. So "warm" = +a/+b, "cool" = −b, "green" = −a.

**Note:** these are the documented/reputational looks of the real stocks, distilled from
reviews and shooter consensus — a target to tune toward, not measured sensitometry.

---

## Kodak color negative

### Portra 160
- **Temp/cast:** warm, but the most neutral of the Portra line.
- **Contrast:** low–medium, very smooth. **Saturation:** low.
- **Skin:** the benchmark — warm, natural, delicate; never orange.
- **Hues:** restrained across the board; pastels stay gentle.
- **Shadows/highlights:** wide latitude, gentle highlight rolloff, clean shadows.
- **Grain:** very fine. **Halation:** minimal.
- **Vibe:** soft, natural portraiture; the "quiet" Portra.

### Portra 400
- **Temp/cast:** warm; same colour family as 160.
- **Contrast:** low–medium. **Saturation:** low–medium (a touch more life than 160).
- **Skin:** very natural warm skin, the most versatile/editable stock.
- **Shadows/highlights:** huge latitude, forgiving overexposure, soft highlights.
- **Grain:** fine but visibly more than 160/Ektar. **Halation:** slight on speculars.
- **Vibe:** the default modern film look; flexible.

### Portra 800
- **Temp/cast:** warm. **Contrast:** medium (higher than 400). **Saturation:** medium (most saturated Portra).
- **Skin:** natural but richer; still flattering.
- **Shadows/highlights:** good latitude for a high-speed film; holds highlights.
- **Grain:** noticeable but fine for ISO 800. **Halation:** moderate warm glow on highlights.
- **Vibe:** low-light Portra with more colour punch.

### Ektar 100
- **Temp/cast:** fairly neutral, slightly cool vs Portra. **Contrast:** medium–high.
- **Saturation:** **very high** — the most saturated Kodak neg, near-slide vividness.
- **Skin:** runs **red/ruddy** on lighter skin (its weakness — not a portrait film).
- **Hues:** vivid blues and greens, punchy reds; landscape-optimised.
- **Shadows/highlights:** deep saturated shadows, less latitude than Portra.
- **Grain:** finest of any colour neg. **Halation:** low.
- **Vibe:** vivid landscape/architecture; slide look on C-41.

### Gold 200
- **Temp/cast:** **strongly warm** — golden yellow/orange, the nostalgic Kodak look.
- **Contrast:** medium. **Saturation:** medium, warm-biased.
- **Skin:** warm, golden, pleasing (can go slightly yellow).
- **Hues:** yellows/oranges dominate; blues muted/warmed.
- **Shadows/highlights:** warm shadows, glowy warm highlights.
- **Grain:** moderate, classic consumer grain. **Halation:** slight warm.
- **Vibe:** sunny nostalgia, golden-hour everyday.

### ColorPlus 200
- **Temp/cast:** warm, similar to Gold but a bit flatter/cooler and cheaper-looking.
- **Contrast:** medium. **Saturation:** medium.
- **Skin:** warm, slightly less refined than Gold.
- **Grain:** moderate–coarse. **Halation:** slight.
- **Vibe:** budget warm consumer; vintage snapshot.

### UltraMax 400
- **Temp/cast:** warm. **Contrast:** medium. **Saturation:** **high**, punchy consumer colour.
- **Skin:** warm, can be a touch saturated/ruddy.
- **Hues:** strong reds/yellows, lively.
- **Grain:** coarse-ish for a consumer 400. **Halation:** slight.
- **Vibe:** vibrant point-and-shoot; saturated warm snapshots.

---

## Fuji color negative

### Pro 400H
- **Temp/cast:** **cool / minty** — magentas pushed cooler, cyans slightly yellow.
- **Contrast:** low, soft. **Saturation:** low (pastel).
- **Skin:** natural, neutral, delicate (its signature strength).
- **Hues:** famous pastel/mint greens and airy cyans; understated.
- **Shadows/highlights:** soft, pastel highlights (wash gently), gentle shadows.
- **Grain:** fine. **Halation:** minimal.
- **Vibe:** airy, pastel, wedding/editorial; the Fuji answer to Portra.

### Superia 400
- **Temp/cast:** **cool, green-leaning** — Fuji's extra cyan-green layer → green shadows/overtones.
- **Contrast:** medium. **Saturation:** medium–high (more saturated than Kodak Gold).
- **Skin:** cooler than Kodak; can go slightly green/magenta-off in mixed light.
- **Hues:** punchy greens and blues; reds a bit subdued vs Kodak.
- **Grain:** moderate–coarse. **Halation:** slight.
- **Vibe:** cool, lively consumer Fuji; greens pop.

---

## Cinema (color negative, ECN-2 origin)

### Vision3 250D (daylight)
- **Temp/cast:** near-neutral daylight; slightly warm-clean. **Contrast:** low (designed flat for grading).
- **Saturation:** medium, controlled. Extra-sensitive to magenta/yellow/red.
- **Shadows/highlights:** **very wide latitude** (~13 stops), rich blacks, huge highlight retention.
- **Grain:** fine. **Halation:** low (has anti-halation layer).
- **Vibe:** flat, gradeable, cinematic daylight; deep controllable blacks.

### Vision3 500T (tungsten)
- **Temp/cast:** tungsten-balanced → in daylight reads cool/blue unless corrected; teal shadows.
- **Contrast:** low, flat for grading. **Saturation:** medium.
- **Shadows/highlights:** wide latitude, superb low-light/night colour.
- **Grain:** moderate (ISO 500). **Halation:** low (AHU).
- **Vibe:** cinematic night/tungsten; teal-shadow gradeable base.

### Fuji Eterna 250D
- **Temp/cast:** neutral–slightly cool, gentle. **Contrast:** **low, muted** (soft cine look).
- **Saturation:** low, desaturated/pastel.
- **Skin:** accurate, gentle. **Shadows/highlights:** soft, wide, filmic rolloff.
- **Grain:** moderate, smooth. **Halation:** low.
- **Vibe:** muted, soft cinematic; low-contrast pastel.

### CineStill 50D
- **Temp/cast:** clean daylight, slightly warm. **Contrast:** medium. **Saturation:** medium, clean.
- **Shadows/highlights:** good latitude, clean highlights.
- **Grain:** fine (ISO 50). **Halation:** **present** (remjet removed) — soft red glow on bright highlights, gentler than 800T.
- **Vibe:** clean daylight cine with a hint of halation glow.

### CineStill 800T
- **Temp/cast:** tungsten → **teal shadows, warm/orange highlights** in mixed/night light.
- **Contrast:** medium. **Saturation:** medium.
- **Shadows/highlights:** holds neon/night colour; warm blooming highlights.
- **Grain:** coarse (ISO 800, pushed often). **Halation:** **signature strong red halos** around lights (no remjet) — the defining trait.
- **Vibe:** neon night, cyberpunk; red-halation streetlights.

---

## Color reversal (slide)

### Velvia 50
- **Temp/cast:** slightly warm. **Contrast:** **high**, punchy. **Saturation:** **extreme** (the most saturated).
- **Hues:** legendary greens, reds, blues; best sat for yellows/oranges/purples; deep cool shadows.
- **Skin:** poor for skin (over-saturated/ruddy) — a landscape film.
- **Shadows/highlights:** deep blacks, narrow latitude, quick highlight clip.
- **Grain:** ultra-fine. **Halation:** minimal.
- **Vibe:** maximal punchy landscape; velvet blacks, electric colour.

### Provia 100F
- **Temp/cast:** **neutral, accurate**, slightly cool in studio. **Contrast:** medium, true-to-life.
- **Saturation:** medium–high but restrained (vs Velvia).
- **Skin:** decent for a slide, cooler. **Shadows/highlights:** ultra-fine, clean, good latitude for slide.
- **Grain:** ultra-fine. **Halation:** minimal.
- **Vibe:** accurate pro slide; the neutral reference chrome.

### Astia 100F
- **Temp/cast:** neutral–slightly warm. **Contrast:** medium (more than Provia, less than Velvia).
- **Saturation:** medium, **natural** — "most like colour-neg of the slides."
- **Skin:** the slide for portraits — soft, natural skin.
- **Shadows/highlights:** smooth, softest slide rolloff.
- **Grain:** very fine. **Halation:** minimal.
- **Vibe:** the gentle, natural slide; soft-skin chrome.

### Ektachrome E100
- **Temp/cast:** slightly **warm**, pronounced reds/pinks. **Contrast:** medium–high, very sharp.
- **Saturation:** medium–high, clean modern slide.
- **Skin:** decent in good light; warmer than Provia.
- **Shadows/highlights:** clean, contrasty, slide latitude.
- **Grain:** very fine. **Halation:** minimal.
- **Vibe:** sharp clean modern chrome; warm reds.

### Kodachrome 64
- **Temp/cast:** warm golden midtones; **deep, slightly blue-shifted blues**.
- **Contrast:** **high micro-contrast**, 3D "pop"; deep punchy shadows (not muddy).
- **Saturation:** high but **not garish** — controlled richness.
- **Skin:** warm, creamy, natural (never orange/waxy) — a rare vivid-yet-flattering-skin film.
- **Hues:** **legendary reds** (uniquely rich red layer); luminous blues; controlled greens.
- **Shadows/highlights:** dark saturated shadows, warm highlights.
- **Grain:** fine. **Halation:** low.
- **Vibe:** iconic National-Geographic look; rich reds, golden glow, dimensional.

---

## Black & white

### Tri-X 400
- **Contrast:** medium–high (more than HP5), extra contrast in the **highlights**.
- **Tonality:** rich, gutsy, classic. **Grain:** pronounced, distinctive, tactile.
- **Vibe:** the classic gritty reportage B&W; punchy.

### HP5 Plus 400
- **Contrast:** medium, extra contrast in the **shadows**; deep darks, highlights push out.
- **Tonality:** strong, forgiving, wide latitude. **Grain:** present but subtler than Tri-X.
- **Vibe:** flexible documentary B&W; a bit softer/more malleable than Tri-X.

### Delta 100
- **Contrast:** medium, crisp. **Tonality:** clean, modern (tabular grain), smooth.
- **Grain:** very fine. **Vibe:** sharp, refined fine-grain B&W.

### Delta 3200
- **Contrast:** **low**, soft. **Tonality:** moody, atmospheric.
- **Grain:** **huge, prominent** (the point of the film). **Vibe:** grainy low-light/night B&W.

### Neopan Acros 100
- **Contrast:** medium, smooth. **Tonality:** exceptionally smooth, long tonal scale, clean whites.
- **Grain:** **ultra-fine** (finest ISO 100 B&W). **Vibe:** clean, delicate, near-grainless.

### Eastman Double-X (5222)
- **Contrast:** medium, classic cine gradation. **Tonality:** silvery, timeless mid-tones.
- **Grain:** moderate, cinematic. **Vibe:** vintage Hollywood B&W movie look.
