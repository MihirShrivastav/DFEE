# Stock Reauthoring Program

## Purpose

DFEE must not present a stock variant merely because a real product had a
different ISO. A variant remains available only when its native response has a
visible, reproducible reason to exist beyond a slightly different grain amount.

This program reauthors the active profiles from sensitometric and material
behaviour, then decides whether a variant remains a primary recipe, becomes a
family variant, or is retired from the product catalog.

## Audit Baseline

The 2026-08-04 native assay used `9339116563.rw2`, `filmic_v3`, As Shot,
`adaptive=false`, grain Off, halation Off, bloom 0, and no print stock. It
isolates the static image-forming response from material finish and automatic
scene placement.

| Pair | Mean absolute RGB difference | Assessment |
| --- | ---: | --- |
| Cinestill 50D / 400D | 0.0059 | Not distinct enough |
| T-Max 100 / 400 | 0.0044 | Not distinct enough |
| Delta 100 / 400 | 0.0081 | Borderline, not distinct enough |
| Delta 400 / 3200 | 0.0206 | Materially distinct |

The active C++ renderer consumes the documented profile fields. The weakness is
not ignored YAML; it is shallow calibration and an incomplete model of
speed-dependent sensitometry. `adaptation.base_iso` currently drives mostly
grain adaptation, while family-level Auto placement uses the same stock bias
for several materially different stocks.

## Calibration Contract

Each stock must be calibrated against documented technical data and reference
scans for these independent behaviours:

1. **Sensitivity and latitude**: nominal EI, practical over/under exposure
   tolerance, and speed-dependent shadow failure.
2. **Characteristic response**: toe position and length, midtone gamma/density,
   shoulder onset, shoulder slope, and minimum density.
3. **Colour response**: balance, dye bias by tonal region, hue-family movement,
   saturation, and highlight desaturation.
4. **Spectral response**: monochrome panchromatic balance and filter-like colour
   separation; this is a primary look dimension, not a minor profile detail.
5. **Material response**: grain amplitude, particle scale, clumping,
   shadow/highlight dependency, sharpness/edge softness, halation, and bloom.
6. **Process response**: push/pull behaviour belongs to the later M7-004
   Process model and must not be faked with Film Exposure.

Auto Balanced provides a restrained scene-development starting point. It must
not erase a stock's calibrated sensitometry; Film Exposure remains an explicit
pre-emulation placement choice, not a generic brightness overlay.

## Required Engine Work

Current `tone_response` fields are sufficient for broad family looks but not
for calibrated speed variants. Add a versioned, profile-driven sensitometry
stage only after the benchmark fixtures are in place. The proposed parameters
are intentionally exposure-domain terms rather than more arbitrary display
curves:

- `sensitometry.nominal_ei`
- `sensitometry.toe_start_ev`, `toe_latitude_ev`, `toe_gamma`
- `sensitometry.midtone_gamma`
- `sensitometry.shoulder_start_ev`, `shoulder_latitude_ev`, `shoulder_gamma`
- `sensitometry.shadow_separation`
- `sensitometry.overexposure_smoothing` and `underexposure_breakup`

These values must operate before display encoding, have a no-op baseline for
existing `parity_v1`, appear in report JSON, and be tested with exposure ramps.
They are not approved YAML fields until that implementation exists.

## Family Priorities

### Cinestill

- **50D**: daylight-balanced ISO 50, exceptionally fine grain, maximum edge
  definition, broad highlight/shadow latitude, and remjet-free halation.
- **400D**: daylight-balanced ISO 400, softer palette, natural saturation,
  warm skin response, broad practical EI range, and a clearly different
  grain/latitude tradeoff.
- **800T**: tungsten-balanced high-speed stock, cool daylight crossover,
  stronger point-light halation, and substantially different shadow/grain
  behaviour.

50D and 400D must receive clearly different characteristic curves and material
response before both remain first-class selections. Current values make them
near-duplicates.

**Initial native calibration (2026-08-04).** The renderer now consumes
`tone_response.toe_length`, which had previously been loaded but inert. 50D was
authored with a shorter, cleaner toe, firmer midtone separation, and an earlier,
more protective highlight shoulder. 400D was authored with a longer, denser toe,
softer midtone response, and restrained colour response. The material-off assay
on the baseline fixture now measures 50D/400D at `0.0173` normalized RGB MAE,
above the `0.0100` near-duplicate threshold. This is an implementation milestone,
not final approval: the pair still needs exposure-ramp and human visual review.

The native test suite now resolves both real YAML profiles and compares a neutral
exposure ramp. It protects the authored distinction: 400D must retain denser
lower-mid shadows through its longer toe, while 50D must enter a more protective
highlight shoulder earlier.

**CineStill material review (2026-08-04).** The calibrated 50D/400D tone roles
remain valid. 400D is a purpose-built C-41 still film with a soft palette,
natural saturated color, warm skin, and broad EI 200-800 latitude; its default
halation was therefore reduced rather than treating it as a weaker 800T. 800T
remains the high-speed tungsten and mixed-light profile with the strongest
specular point-light red halation, but its prior large scene-wide cool-shadow
bias was removed. It now expresses its speed through the longest, densest toe,
larger controlled grain, and shadow latitude; source white balance remains a
development decision. On `9339116563.rw2` with material finish disabled, the
three profiles have no pair at or below `0.0100` normalized RGB MAE. Native
tests now also protect the 800T toe, grain, and halation hierarchy. This does
not weaken 800T's signature red glow: the effect remains strong and
specular-only, as documented by CineStill.

### Kodak Professional Still Negatives

The Portra line is a matched professional family, not three progressively more
stylised looks. Kodak positions Portra 160 around natural skin tones and very
fine grain; Portra 400 around warm, vibrant colour with exceptionally fine
400-speed grain; and Portra 800 around low-light detail, well-balanced colour,
and natural skin reproduction. The speed hierarchy belongs primarily in grain,
shadow latitude, and toe behavior, not a large automatic saturation increase.

- **Portra 160**: softest and cleanest portrait/fashion/commercial baseline;
  restrained colour, fine grain, smooth shadow-to-midtone transition, and a
  protective highlight shoulder.
- **Portra 400**: flexible all-light professional baseline; slightly more
  colour presence than 160, still skin-protected and fine-grained rather than
  halation-heavy or aggressively warm.
- **Portra 800**: low-light/high-action option; longer toe and visibly larger
  but controlled grain, with colour held near the family baseline instead of
  turning skin into a saturated orange response.
- **Ektar 100**: separate low-speed, ultra-fine-grain high-definition option.
  Kodak explicitly positions it for ultra-vivid colour, sharpness, and outdoor,
  travel, fashion, product, and commercial imagery. Its stronger colour body
  is concentrated in landscape/product hues while red-orange compression
  prevents a false skin-tone saturation or large orange hue rotation.

**Initial native calibration (2026-08-04).** The controlled native patch assay
found that the old Ektar profile drove the skin-like patch to `1.95x` chroma
and rotated orange by approximately `-22 degrees`; Portra 800 also produced a
`1.51x` skin-like chroma ratio. The revised Ektar response measures `1.49x`
on the skin-like patch and `-6 degrees` on orange, while retaining a stronger
blue response (`1.27x`) than the Portra profiles. Portra 160 is now the
softest/restrained curve, Portra 400 the firmer general-purpose curve, and
Portra 800 the longest-toed, low-light curve. The material-off RAW assay on
`9339116563.rw2` reports no pair at or below the `0.0100` normalized RGB MAE
near-duplicate guardrail. These changes reduce zonal colour bias, limit
red-orange response, and rationalise the fine-to-high-speed grain hierarchy.
Profile-role tests protect the family relationships. This remains an emulation
of stock character, not an assertion of a particular scanning or lab process.

### Kodak Consumer Negatives

**UltraMax 400** is a daylight-balanced, high-speed consumer C-41 negative for
varied everyday and lower-light situations. Kodak describes it as fine-grained,
vivid, sharp, and colour-consistent. Its DFEE role is therefore more practical,
visibly textured, and firmer through the midtones than Portra 400, with
controlled consumer grain. It is not a generic orange/yellow grade or a
halation-heavy effect.

**Initial native calibration (2026-08-04).** The previous profile drove the
skin-like patch to `1.91x` chroma and rotated orange by `-20 degrees`. The
revised response measures `1.44x` skin-like chroma and `-1.6 degrees` on
orange, while preserving vivid blue/cyan and a moderate ISO-400 grain role.
On `9339116563.rw2` with material finish disabled, UltraMax is distinct from
both Gold 200 and Portra 400 at the `0.0100` RGB MAE near-duplicate guardrail.

**Gold 200** is the lower-speed daylight consumer negative: fine-grained,
vibrant, consistently coloured, and deliberately forgiving. Kodak specifies
that it tolerates approximately two stops of underexposure and three stops of
overexposure. Its DFEE role therefore has the longest, softest consumer toe and
a protective shoulder, with PGI 44 fine consumer grain. It is warm-neutral and
vivid, but its color is bounded rather than a large yellow-orange bias.

**Initial native calibration (2026-08-04).** The prior profile produced `1.89x`
skin-like chroma and `-16 degrees` orange rotation. The reauthored profile
measures `1.43x` and `-0.9 degrees` respectively. Gold is finer and slightly
less vivid than UltraMax, while the real-RAW material-off assay on
`9339116563.rw2` confirms Gold, UltraMax, and Portra 400 all remain above the
`0.0100` RGB MAE near-duplicate guardrail.

**ColorPlus 200** is retained as a softer, lower-fidelity daylight consumer
negative, with visibly more texture and less color energy than Gold. There is
no current Kodak technical sheet suitable for a sensitometric claim, so this is
a conservative role based on ISO 200 daylight positioning, retailer material,
and reference-scan consensus. It must not be presented as a generic yellow
cast, added halation, or intentionally broken scan.

**Initial native calibration (2026-08-04).** The previous profile reached
`1.76x` skin-like chroma and `-10.8 degrees` orange rotation. The revised,
softer response measures `1.32x` and `-0.7 degrees`. The material-off assay on
`9339116563.rw2` places it below Gold and UltraMax in tonal spread, while still
above the `0.0100` RGB MAE near-duplicate guardrail against each. This profile
needs later portrait/reference-scan review before any stronger material claim.

### Fujifilm Negatives

Fujifilm's fourth-layer color technology is an input and mixed-light stability
claim, not permission to make every Fujifilm profile a cyan-green grade. The
profiles below retain modest Fuji palette emphasis only where it survives the
native patch assay without compromising neutral gray or skin.

- **Fujicolor C200**: daylight ISO 200, naturally rendered skin, wide exposure
  latitude, sharpness, and fine grain. This is the cleaner, fine-grain
  latitude-oriented consumer Fuji baseline.
- **Superia X-TRA 400**: daylight ISO 400, high-speed/wide-latitude consumer
  stock with fine grain, vivid natural color across reds, blues, yellows,
  violets, and greens, plus neutral gray/skin stability. It has more color
  energy and texture than C200, not a forced green cast.
- **Pro 400H**: discontinued professional ISO 400 with a fourth color layer.
  Its distinctive role is faithful gray and skin reproduction, smooth
  highlight-to-shadow gradation, fine grain, and resilience under mixed or
  fluorescent light. It is naturally colored, not globally desaturated.
- **Eterna 250D (8563/8663)**: daylight ISO 250 motion-picture camera negative
  with enhanced latitude, gradation balance, exceptional grain, and sharpness.
  It remains a restrained, latitude-first camera negative. Do not substitute
  the separate **Eterna Vivid 250D (8546/8646)**, which Fujifilm describes as
  the highest-contrast, high-saturation Eterna variant.

**Initial native calibration (2026-08-04).** The original C200/Superia pair
measured `0.00855` normalized RGB MAE on the material-off RAW assay and was
therefore a near duplicate. The reauthored C200 (`1.23x` skin-like chroma) and
Superia (`1.42x`) separate natural/fine ISO 200 from vivid/broader-spectrum ISO
400 behavior without large orange rotation. Pro 400H was corrected from an
over-muted `0.86x` skin-like response to `1.11x`, preserving its neutral-gray
and smooth-gradation role. Standard Eterna 250D was adjusted only to remove
unnecessary color suppression; it measures `1.02x` and remains substantially
more restrained than the non-active Vivid stock. The four-stock
`9339116563.rw2` material-off assay now has no pair at or below the `0.0100`
RGB MAE guardrail. These are stock-role calibrations, not claims to reproduce a
particular lab, telecine, scanner, or Fuji camera film-simulation mode.

### VISION3 Motion-Picture Negatives

VISION3 profiles must emulate their camera-negative role, not the exaggerated
"cinematic" scan preset often attached to them online. Both stocks are ECN-2
camera negatives intended for a color-timed/DI workflow, so their default
responses prioritise recoverable scene information, neutral reproduction, and
restrained halation rather than a baked teal-orange grade.

- **VISION3 250D (5207/7207)**: daylight-balanced, medium-speed, fine-structure
  exterior/general-purpose negative. Kodak specifies reduced shadow grain and
  two stops of extended highlight latitude. Its DFEE profile therefore has the
  smaller, more correlated grain field; restrained colour density; and an
  earlier, protective shoulder with a clean shadow floor.
- **VISION3 500T (5219/7219)**: tungsten-balanced, high-speed negative for
  low-light and tungsten work. Kodak likewise specifies reduced shadow grain
  and extended highlight latitude. Its profile keeps a longer, softer shadow
  transition than 250D, modestly warmer midtone colour response, and a larger
  but still controlled grain field. It is not given a blanket daylight-blue
  cast: correct source white balance belongs to the input/development stage,
  not a stock preset.
- **Halation**: the default effect remains low for both. 250D uses rem-jet
  backing and current 500T material uses an anti-halation undercoat; the
  well-known pronounced red glow is a rem-jet-removal derivative behaviour,
  not the native ECN-2 camera-negative baseline.

**Initial native calibration (2026-08-04).** The baseline material-off assay
previously measured 250D/500T at `0.00795` normalized RGB MAE, below the
`0.0100` near-duplicate guard. The Vision3 profiles are now authored as a
latitude-first pair with differentiated toe, color, density/compression, and
grain behaviour. Native role tests protect their wide-shoulder, low-halation
contract and 250D/500T material distinction. This is deliberately not an
attempt to recreate a particular show LUT, DI, scanner, or rem-jet-removal
process. The next negative-family passes cover portrait/professional still
negatives, consumer stocks, and Fujifilm families with their own references.

### Monochrome

Retain the meaningful families provisionally, but calibrate them by response
rather than ISO labels:

- Slow/fine: Pan F Plus 50, FP4 Plus 125, Delta 100, T-Max 100, Acros 100.
- General-purpose/classic: HP5 Plus, Tri-X 400, Double-X, Delta 400, T-Max 400.
- High-speed: Delta 3200.

Each requires a distinct panchromatic mix plus characteristic curve. Fine-grain
and tabular emulsions should not differ only by subtle toe/shoulder changes;
their highlight transition, local acutance, shadow separation, and grain
placement need distinct calibration.

**Initial native calibration (2026-08-04).** The first monochrome pass moves
the material-off curves out of their prior near-duplicate cluster without
inventing scanner-specific colour-filter behaviour. Pan F is now the crispest
low-speed curve; FP4 is deliberately more moderate. Delta 100 and T-Max 100
are clean, fine-grain curves with firmer midtone separation. Acros is tuned for
smooth gradation and restrained grain. HP5, T-Max 400, Delta 400, Tri-X, and
Double-X now use materially longer or denser toes according to their intended
latitude and image structure. Delta 3200 is explicitly the most compressed,
coarsest high-speed starting point, not simply a brighter ISO 400 profile.

This reflects manufacturer-level evidence, not a claim to reproduce a specific
developer, scanner, filtration, or print process. Ilford characterises Pan F
as high contrast and very fine grain; FP4 as fine grain and medium contrast;
and HP5 as a medium-contrast, broad-latitude ISO 400 stock. Fujifilm describes
ACROS II as ISO 100 with extremely fine grain and rich gradation. Kodak
describes Tri-X as a broad-latitude, push-capable classic-grain stock and
Double-X as an ISO 250 daylight / ISO 200 tungsten motion-picture negative.
Delta 3200 remains process-sensitive: ILFORD rates it for EI 400–6400 use and
notes its standard daylight ISO rating is 1000, so its push/pull behaviour is
reserved for the planned sensitometry stage.

On the baseline RAW fixture, with Auto placement and material effects disabled,
the first pass reduced pairings at or below the `0.0100` normalized RGB MAE
threshold from 29 to 11. This is a guardrail rather than a quality score: it
proves the authored curves are no longer largely interchangeable, but it cannot
certify film identity without controlled developer, scanner, and spectral-chart
fixtures.

Native tests now resolve the real YAML profiles and protect these intentional
separations on a neutral exposure ramp. A future monochrome chart-scan fixture
will validate the panchromatic weights; they remain conservative in this pass.

### Colour Reversal

Reversal profiles are calibrated as transparency material, not as a uniform
"more contrast and saturation" switch. The first native pass establishes the
following distinct roles:

- **Astia 100F**: the softest portrait-oriented option, with restrained
  saturation, longer tonal transitions, and greater highlight desaturation.
- **Provia 100F**: a faithful, vivid general-purpose ISO 100 reversal baseline
  with rich gradation and extremely fine grain.
- **Velvia 50**: the most contrast-forward, saturated nature/product option;
  its vivid rendering is not used as the default for every reversal stock.
- **Velvia 100**: still vivid, but a more moderate starting point than Velvia
  50, with a less aggressive curve and saturation response.
- **Ektachrome E100**: neutral balance, moderately enhanced saturation, low
  contrast, low D-min, and extended highlight/shadow detail. The old profile
  incorrectly assigned it a strong high-contrast, high-saturation curve and
  was corrected in this pass.
- **Kodachrome 64**: retained as a legacy, separately authored stock. Its
  historical process is not approximated by claiming generic E-6 behaviour.

On the baseline material-off fixture, this pass reduced reversal near-duplicate
pairs from two to one; Astia/E100 remain close under that deliberately narrow
assay. That is expected enough to retain until colour-chart and portrait-patch
fixtures can measure their palette and skin-rendering distinction. Native tests
now guard the calibrated family roles so E100 cannot regress to a generic
high-contrast slide profile and the Velvia hierarchy remains explicit.

### Candidate Coverage Gaps

Do not add a stock merely for catalog size. Candidates are accepted only after
reference material and a distinct response hypothesis exist:

- Kodak VISION3 50D and 200T: fill meaningful motion-picture daylight and
  tungsten speed gaps between the existing 250D and 500T.
- CineStill BwXX: only if calibrated separately from the existing Double-X
  profile for its still-photography process and scan behaviour.
- Harman Phoenix 200: a genuinely different modern colour-negative candidate
  if its colour instability, contrast, and halation can be represented without
  turning defects into generic effects.
- Current Fujifilm 400: evaluate separately from legacy Superia X-TRA 400;
  do not claim they are the same emulsion without evidence.

## Source Set

Use manufacturer technical documents and product guidance as the first layer of
evidence. Scanner, developer, and reference-scan variation must be recorded as
calibration context rather than attributed blindly to the emulsion.

- CineStill 50D: daylight balance, ISO 50, fine grain, latitude, and remjet-free
  halation: <https://help.cinestillfilm.com/hc/en-us/articles/360028918672-What-is-different-about-CineStill-50Daylight-film>
- CineStill 400D: daylight balance, wide practical EI range, palette, and skin
  response: <https://cinestillfilm.com/products/a-new-color-film-400dynamic>
- CineStill 800T: tungsten balance, high-speed use, and point-light halation:
  <https://cinestillfilm.com/blogs/news/cinestill-800t-in-your-toolbox>
- Kodak technical education on sensitometry and stock choice:
  <https://www.kodak.com/en/motion/page/filmmaker-resources/>
- Kodak professional color-negative brochure: Portra 160/400/800 role,
  grain, skin-tone, and saturation guidance:
  <https://www.kodak.com/global/plugins/acrobat/en/professional/products/films/2012Brochure.pdf>
- Kodak Ektar 100 product information: low-speed daylight balance, ultra-vivid
  colour, exceptional sharpness, enhanced saturation, and fine grain:
  <https://www.kodak.com/en/still-film/product/professional/ektar-100-film/>
- Kodak UltraMax 400 product information: daylight balance, high-speed use,
  fine grain, vivid but consistent colour, and sharp detail:
  <https://www.kodak.com/en/still-film/product/consumer/ultramax-400-film/>
- Kodak Gold 200 product information and technical data: daylight balance,
  broad exposure latitude, fine grain, vivid consistent colour, and PGI 44:
  <https://www.kodak.com/en/still-film/product/consumer/gold-200-film/>
  <https://www.kodak.com/global/plugins/acrobat/en/consumer/products/techInfo/e7022/E7022.pdf>
- ColorPlus 200 third-party product specification: ISO 200 daylight balance,
  fine grain, sharpness, rich colour saturation, and wide latitude. Kodak does
  not currently publish a comparable technical sheet, so treat this only as
  provisional support for the calibration hypothesis:
  <https://www.bhphotovideo.com/c/product/1476366-REG/kodak_603147_color_print_film_200_36.html/specs>
- Fujifilm C200 data sheet: ISO 200 daylight balance, natural skin, wide
  latitude, sharpness, and Super Uniform Fine Grain technology:
  <https://asset.fujifilm.com/www/us/files/2019-09/cce1e1943550fc3e76c22411066f0100/films_c200_datasheet_01.pdf>
- Fujifilm Superia X-TRA 400 data sheet: ISO 400, fine grain, wide latitude,
  natural skin/gray, and vivid reproduction across the spectrum:
  <https://asset.fujifilm.com/www/in/files/2020-07/32cf7e5def364084eb8cf03ff011df0f/films_superia-xtra400_datasheet_01.pdf>
- Fujifilm Pro 400H product and technical data: fourth-layer neutral/mixed-light
  stability, natural skin, smooth gradation, fine grain, and wide latitude:
  <https://www.fujifilm.com/us/en/business/professional-photography/film/pro-400h>
  <https://www.fujifilm.com.hk/products/professional_films/pdf/pro_400h_datasheet.pdf>
- Fujifilm Eterna 250D motion-picture manual: daylight ISO 250, enhanced
  latitude/gradation, grain, and sharpness; contrast with the separate Vivid
  250D product documentation:
  <https://manualzz.com/doc/27787292/fujifilm-motion-picture-film-manual>
  <https://www.fujifilm.it/aree/motionPicture/PDF/brochure_vivid250d.pdf>
- VISION3 250D technical data: daylight balance, DLT shadow detail, and two
  stops of extended highlight latitude:
  <https://www.kodak.com/content/products-brochures/Film/VISION3-250D-Technical-Data-EN.pdf>
- VISION3 500T technical data: tungsten balance, DLT shadow detail, extended
  highlight latitude, and anti-halation undercoat:
  <https://www.kodak.com/content/pdfs/motion/KODAK-VISION3-500T-5219-7219-technical-information.pdf>
- Kodak reference on low-speed texture and low-contrast latitude:
  <https://www.kodak.com/content/products-brochures/Film/kodak-essential-reference-guide-for-filmmakers.pdf>
- Fujifilm data-sheet index for C200, Superia, Pro 400H, Velvia, and Provia:
  <https://www.fujifilm.com/uk/en/consumer/support/films/negative-and-reversal>
- ILFORD product information and technical documents:
  <https://www.ilfordphoto.com/>
- ILFORD product guide: Pan F, FP4, HP5, Delta and process positioning:
  <https://www.ilfordphoto.com/wp/wp-content/uploads/2017/05/Ilford-Product-Brochure-LOW-RES-WEB-1.pdf>
- ILFORD Delta 3200 technical data: practical EI range and standard ISO rating:
  <https://www.ilfordphoto.com/amfile/file/download/file/1913/product/683/>
- Fujifilm NEOPAN 100 ACROS II data sheet:
  <https://asset.fujifilm.com/master/emea/files/2021-11/1948347ece68885a07d688d9e21a217f/films_neopan100acros2_135_01_0.pdf>
- Kodak TRI-X 400 product information:
  <https://www.kodak.com/en/still-film/product/professional/tri-x-400-film/>
- Kodak EASTMAN DOUBLE-X data sheet:
  <https://www.kodak.com/content/products-brochures/EASTMAN-DOUBLE-X-Negative-Film-datasheet-US-180924-EN.pdf>
- Kodak EKTACHROME E100 technical data: neutral balance, moderately enhanced
  saturation, low contrast, low D-min, and extended tonal detail:
  <https://www.kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/e4000_ektachrome_100.pdf>
- Fujifilm PROVIA 100F data sheet: fine grain, sharpness, faithful vivid colour,
  rich gradation, and push/pull characteristics:
  <https://asset.fujifilm.com/www/us/files/2020-03/6325e0d91ad8f74448c5968b5a954199/Provia100f.pdf>
- Fujifilm Velvia 50 product information: high saturation, fine grain, deep
  shadows, and -1/2 to +1 stop push/pull range:
  <https://www.fujifilm.com.hk/m/products/professional_films/color_reversalfilms/velvia_50/index.html>
- Fujifilm film-simulation reference: Provia as standard, Velvia as saturated
  high contrast, and Astia as portrait/skin-tone oriented:
  <https://fujifilm-dsc.com/en/manual/x100f/menu_shooting/film_simulation/index.html>

## Delivery Order

1. Run `stock_response_benchmark.py` on the reference fixture set and record
   near-duplicate pairs.
2. Add synthetic exposure ramps, neutral colour chart, skin-like warm colours,
   foliage-like yellow/green colours, blue/cyan highlights, and monochrome
   spectral patches to the native fixture suite.
3. Implement the sensitometry stage with a no-op compatibility path.
4. Reauthor Cinestill first, then monochrome, then the colour-negative and
   reversal families.
5. Require material-off separation plus human visual approval before retaining
   sibling speeds as primary stocks.
6. Group the desktop catalog by family and show a concise stock character, not
   a flat list of loosely differentiated ISO labels.

## Benchmark Usage

```powershell
python cpp_engine/tools/stock_response_benchmark.py 9339116563.rw2
python cpp_engine/tools/stock_response_benchmark.py 9339116563.rw2 --stocks cinestill_50d,cinestill_400d,cinestill_800t
```

The JSON artifact is written under `cpp_engine/out/benchmarks/` by default and
is intentionally not source-controlled.
