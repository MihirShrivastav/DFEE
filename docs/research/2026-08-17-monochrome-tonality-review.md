# Monochrome Stock Tonality — Review & Recalibration Proposal

**Complaint:** B&W stocks feel almost identical in tonality/contrast when switching between them on most photos.

**Verdict:** Correct, and there are **two independent causes** — one in the *pipeline* (dominant on the TIFF/Lightroom path) and one in the *authored curves*. Fixing only the curves will not be enough because the pipeline is halving whatever difference exists.

---

## Finding 1 — The TIFF path applies film tone at 44% (dominant cause)

`apply_rendered_input_adjustments` (session.cpp:1213):
`tone_response_strength = 1 − (rendered_input/100) × 0.70`.

Default `rendered_input = 80` → **strength = 0.44**. The characteristic curve — the thing that gives a stock its contrast/toe/shoulder identity — is blended in at **44%** on the default Lightroom workflow; the other 56% is the TIFF's own (Lightroom-rendered) tone. So most of what you see is the *same* Lightroom tone regardless of stock.

At 0.44, the midtone-slope gap between, say, HP5 (0.124) and Delta 100 (0.134) collapses from 0.010 to ~0.004 output/stop — imperceptible. Only the extremes (Pan F vs Delta 3200) survive at all.

This is deliberate for colour negatives (avoids double-tone-mapping / blown highlights), but it is **too aggressive for B&W**, where the film's tonal rendering *is* the point and there is no colour to double-map.

## Finding 2 — The authored curves are clustered

Effective midtone slope (gamma ÷ latitude); 6 of 11 sit in a 9% band:

| Stock | gamma | lat | slope | toe_onset / hard | shoulder_onset / hard |
|---|---|---|---|---|---|
| Pan F+ 50 | 1.30 | 7.8 | **0.167** | 2.6 / 0.85 | 3.8 / 0.75 |
| Tri-X 400 | 1.24 | 8.2 | 0.151 | 2.8 / 0.72 | 3.9 / 0.66 |
| TMax 100 | 1.22 | 8.4 | 0.145 | 2.9 / 0.72 | 4.0 / 0.66 |
| Delta 100 | 1.18 | 8.8 | 0.134 | 3.2 / 0.68 | 4.4 / 0.62 |
| Delta 400 | 1.16 | 8.6 | 0.135 | 3.0 / 0.72 | 4.0 / — |
| TMax 400 | 1.16 | 8.7 | 0.133 | 3.1 / 0.68 | 4.1 / 0.64 |
| FP4+ 125 | 1.16 | 8.8 | 0.132 | 3.2 / 0.70 | 4.2 / 0.64 |
| Acros 100 | 1.14 | 9.0 | 0.127 | 3.3 / 0.66 | 4.3 / 0.62 |
| HP5+ 400 | 1.12 | 9.0 | 0.124 | 3.3 / 0.62 | 4.3 / 0.60 |
| Double-X | 1.10 | 9.0 | 0.122 | 3.5 / 0.64 | 4.2 / — |
| Delta 3200 | 1.06 | 9.2 | 0.115 | 3.4 / 0.58 | 4.3 / — |

The toe/shoulder params are also within a narrow range, so the *shape* differences (which is where B&W films really differ) are muted too.

---

## What actually differs between these films (research-backed)

At normal development, B&W films are **similar in gross contrast by design** (each is developed to a comparable contrast index). So exaggerating gamma spread would be *inaccurate*. The genuine, perceptible distinctions are in **curve shape** and **spectral response**:

- **Tri-X 400** — strong mid contrast, **non-linear "blow-out" highlights** (earlier/harder shoulder), rich blacks, gritty; classic panchromatic (reds render lighter). [Refs 1,2]
- **T-Max 100/400** — famously **long straight line** (holds highlights, smooth, linear), cleaner, finer grain, extended red. Late, gentle shoulder. [Refs 1,3]
- **Delta 100/400** — T-grain, clean, close to the T-Max family; slightly gentler than T-Max.
- **FP4+ 125** — medium contrast, **smooth tonal gradation**, fine grain, classic; forgiving mids. [Ref 4]
- **HP5+ 400** — medium contrast but **very wide latitude / long gentle toe**, forgiving, pushes well; open shadows. [Ref 4]
- **Pan F+ 50** — **high contrast, short tonal scale**, very fine grain, punchy; firmer toe & shoulder.
- **Acros 100** — extremely fine grain, **smooth even tonality**, very neutral/clean highlights; even spectral (slightly green-weighted).
- **Eastman Double-X** — cine neg, **moderate-low contrast**, classic silver, wide latitude; distinct spectral (less red / more blue → lighter skies, darker reds).
- **Delta 3200** — soft, low contrast, wide latitude, grainy.

The three axes to spread (in priority order):
1. **Shoulder / highlight hold** — Tri-X & Pan F shoulder early and hard; T-Max/Delta/Acros hold highlights with a long straight line. Currently too uniform.
2. **Toe / shadow openness** — HP5, Double-X, Delta 3200 long gentle toe; Pan F/T-Max/Delta crisper.
3. **Spectral (pan weights)** — Acros even, Tri-X/T-Max redder, Double-X bluer/cine. Currently reasonable; widen slightly for Acros/Double-X/Tri-X.

Gross gamma: keep the realistic ordering, only gently de-cluster the middle (±~5%), not exaggerate.

---

## Proposed changes (for approval — nothing changed yet)

**A. Pipeline (highest impact): let more film through on B&W TIFFs.** Options, least→most invasive:
   1. Lower the default `rendered_input` for monochrome only (e.g. 80→55) so tone_response_strength rises to ~0.62.
   2. Apply the **contrast/shape** part of the curve at higher strength for mono (mirror how Shadow Lift already applies at full strength on the final tone), leaving highlight-clip protection intact.
   3. Reduce `kMaxToneAtten` globally (0.70→~0.55) — affects colour too; most risk.
   *Recommendation:* option 1 (mono-only default) or 2. Needs an A/B render to confirm highlights don't harden.

**B. Curves: differentiate shape, not just gamma.** Concretely (deltas from current):
   - **Tri-X** — earlier, firmer shoulder (shoulder_onset 3.9→3.6, hardness 0.66→0.74) for the blow-out highlight; keep contrast.
   - **T-Max 100/400** — longer straight line (shoulder_onset →4.6, hardness →0.55; toe_onset −0.1, firmer toe) — the signature.
   - **HP5** — longer gentle toe (toe_onset 3.3→3.7, hardness 0.62→0.55), slightly softer overall.
   - **Pan F** — keep punchy; firmer toe & shoulder already; nudge slope up (1.30→1.34).
   - **Double-X** — lower shoulder for a softer highlight; widen its blue-lean pan weights.
   - **Acros** — keep even/smooth; hold highlights (soft high shoulder); confirm the even pan weights.
   - **Delta 3200** — softest toe & lowest slope (already close).
   Net gamma spread widens only modestly (~0.11→0.17), staying realistic.

**C. Verify on real images.** A/B two or three B&W-suitable TIFFs through 4–5 stocks before/after, using the bespoke harness, to confirm the switch is now *noticeable but not cartoonish*.

## Coordination note
A parallel process has been editing `solver.cpp/.hpp` on this branch. The pipeline change (A) touches `session.cpp`; the curve changes (B) touch the stock YAMLs. Worth confirming who owns the tone pipeline before editing to avoid collisions.

## Sources
1. [Tri-X vs T-Max (Full Frame/Medium)](https://medium.com/full-frame/tri-x-vs-tmax-890081a55264)
2. [Kodak Tri-X profile — Casual Photophile](https://casualphotophile.com/2016/05/15/kodak-tri-x-film-profile)
3. [Characteristic Curves for Film — 35mmc](https://www.35mmc.com/07/02/2022/contrast-and-tonality-part-3-characteristic-curves-for-film-and-paper-by-sroyon/)
4. [Ilford FP4 vs HP5 — Markus Hagner](https://markus-hagner-photography.com/ilford-fp4-vs-hp5/)
