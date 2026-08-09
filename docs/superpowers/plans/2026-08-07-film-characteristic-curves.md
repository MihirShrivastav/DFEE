# Film Characteristic Curves Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the ad-hoc film tone logistic with a scientifically-grounded per-stock D–logE characteristic curve that varies contrast by stock and by scene exposure, driven by the image analysis we already compute.

**Architecture:** A new pure curve unit (`characteristic_curve.{hpp,cpp}`) evaluates a log-domain toe→straight-line→shoulder transfer. The solver reads a `characteristic_curve:` YAML block into `FilmResponsePlan` and maps the creative controls onto its parameters; the renderer applies it per channel (per-dye-layer, absorbing crossover) with scene placement from `midtone_anchor`. Gated behind a new `filmic_v4` pipeline so v3/v2/parity are untouched. Prototype Velvia 50 / Portra 400 / Tri-X 400, validate via the bespoke harness, then roll out.

**Tech Stack:** C++20, OpenCV (image buffers), yaml-cpp (profiles), assert-based test binary `dfee_tests` (`cpp_engine/tests/test_core.cpp`), MSVC Release build via CMake preset `out/build/windows-msvc-vcpkg`. Harness: `experiments/bespoke_portra800.cpp`.

## Follow-up: Toe Correctness (2026-08-07)

The first renderer implementation did not execute `curve_eval(...)`: it rebuilt a
scene-percentile luma curve with a generic smoothstep toe. This left
`latitude_stops`, `toe_onset`, and `toe_hardness` inert and could make high-contrast
v4 profiles crush shadows when the straight-line join fell below `d_min`.

The corrected renderer evaluates the authored characteristic curve in log exposure
through a `[-16,+12]` stop LUT. The pure curve derives its straight-line slope from
`gamma / latitude_stops`, and caps only an impossible toe join immediately above the
black floor to retain C1 continuity and shadow gradation. The v4 path remains
luminance-based, preserving chroma by RGB scaling; legacy pipelines are unchanged.

For developed TIFF/JPEG input, v4 must also honor the existing `rendered_input`
contract. The desktop's default `80` resolves to a restrained tone blend rather than
a second full display curve. RAW input retains a full-strength characteristic curve.
That blend occurs in perceptual curve space: `source_tone + strength *
(stock_tone - source_tone)`. This makes zero strength exact identity and avoids the
shadow-weighting error of interpolating the two results in linear light.

## Stock Rollout: Gold 200 And Ektar 100 (2026-08-07)

Gold 200 and Ektar 100 are migrated to v4 from their existing researched colour and
material profiles. Kodak's Gold technical data describes a daylight consumer negative
with colour saturation, fine grain, high sharpness, and approximately two stops
under/three stops over tolerance. Its v4 curve is consequently broad and gentle at
both ends, while retaining slightly firmer midtone contrast than Portra 400.

Kodak's Ektar technical data identifies an ISO-100, ultra-fine-grain negative with
ultra-vivid colour, exceptional sharpness, and edge definition. Its v4 curve has
stronger midtone separation and a shorter, cleaner toe than Gold, but remains a
negative-film shoulder rather than a reversal-style hard clip. The existing Ektar
red/orange limiter and landscape/product hue response remain responsible for colour.

Sources: [Gold 200 technical data](https://imaging.kodakalaris.com/sites/default/files/files/resources/E7022_Gold_200.pdf), [Ektar 100 technical data](https://www.kodakprofessional.com/sites/default/files/wysiwyg/pro/resources/e4046_ektar_100.pdf).

## Stock Rollout: Vision3 250D And 500T (2026-08-07)

Vision3 250D and 500T now use the v4 tonal model while retaining their existing
researched colour-response, grain, halation, and crossover calibration. Both are
motion-picture negatives with a longer usable exposure range than the still-photo
negative profiles. The 250D curve has slightly firmer daylight separation; 500T
has the longer toe and shoulder required for the stock's shadow recovery and
practical-light retention. Neither is calibrated by crushing shadows or hard
clipping highlights.

Kodak documents 250D as retaining at least 2.5 stops of shadow latitude below a
3-percent black card and at least 3.5 stops of specular highlight latitude above
a white card. Kodak documents 500T's advanced dye layering as reducing shadow
grain and its sub-micron technology as providing two stops of extended highlight
latitude. The authored values are a rendering calibration, not a claim that the
digital input contains the film negative's entire exposure range.

Sources: [Vision3 250D brochure](https://www.kodak.com/content/pdfs/KODAK-VISION3-250D-5207-7207-brochure.pdf), [Vision3 500T technical information](https://www.kodak.com/content/products-brochures/motion-picture/KODAK-VISION3-5219-7219-technical-information.pdf).

## Stock Rollout: ColorPlus 200 (2026-08-08)

ColorPlus 200 now has its v4 curve without changing its existing quieter colour,
consumer-grain, or restrained crossover calibration. Kodak does not publish a
current ColorPlus sensitometric data sheet, so this is intentionally a conservative
role calibration rather than a fabricated density claim: softer than Gold through
the mids, gently open at black, and with practical daylight-negative latitude.

The available product information consistently describes ISO-200, daylight-balanced
C-41 ColorPlus as fine-grained, sharp, naturally to richly saturated, and broadly
forgiving. Those facts justify a general-use negative response, but do not justify
copying Gold's stronger colour energy or inventing hard shadows/highlights.

Sources: [Kodak ColorPlus product information](https://kodak.photosys.com/products/colorplus-200-color-negative-film-35mm-36-exp), [B&H ColorPlus specification](https://www.bhphotovideo.com/c/product/1476366-REG/kodak_603147_color_print_film_200_36.html/specs).

## Stock Rollout: CineStill 50D, 400D, And 800T (2026-08-08)

The CineStill family now uses v4 characteristic curves and stock-authored halation
thresholds. The threshold is a profile neutral point: a 50-percent UI Halation
Threshold value preserves that stock's normal behavior, while the user can lower
or raise it without forcing every profile to bloom at the same luminance.

50D is calibrated as the low-speed daylight, fine-grain/highlight-latitude option;
400D as the soft-palette daylight still film; and 800T as a cool/tungsten-balanced
high-speed C-41 negative with a modestly firmer response. 800T's colour response
has been restrained from the prior generic teal-orange grade. Its red halation is
now tighter, redder, and triggered only by clear overexposed point sources instead
of broad bright surfaces.

This distinction follows CineStill's own guidance: 800T's red glow occurs around
points of light surrounded by darker tones, 400D can halate at focused overexposed
highlights, 400D has a soft palette/natural saturation, and 50D is a daylight
fine-grain motion-picture emulsion. The 800T tonal curve also accounts for its
standard C-41 development, which CineStill says moderately increases gamma.

Sources: [800T usage guide](https://cinestillfilm.com/blogs/news/cinestill-800t-in-your-toolbox), [400D product details](https://cinestillfilm.com/collections/the-400d-film-family/products/400dynamic-35mm), [50D technical guidance](https://help.cinestillfilm.com/hc/en-us/articles/360028918672-What-is-different-about-CineStill-50Daylight-film).

## Stock Rollout: Fujifilm Negative Family (2026-08-09)

Fujicolor C200, Superia X-TRA 400, Pro 400H, and Eterna 250D now use v4
characteristic curves and high stock-specific halation thresholds. Their existing
per-hue colour, grain, density, and compression differences remain in place. The
only colour adjustment in this slice reduces their crossover casts to a bounded
palette tendency: Fujifilm's fourth-layer technology is documented as neutral-gray
and skin stability over varied exposure/light, not a license for a universal
green-cyan grade.

C200 is the fine-grain ISO-200 consumer baseline; Superia is the firmer, more
vivid/textured ISO-400 consumer option; Pro 400H prioritizes continuous gradation,
neutral grays, and controlled shadow colour; and standard Eterna 250D remains the
low-contrast, long-latitude daylight cinema negative. Eterna Vivid is a separate,
high-contrast/high-saturation product and is not represented by this profile.

Sources: [C200 product information](https://asset.fujifilm.com/www/us/files/2019-09/cce1e1943550fc3e76c22411066f0100/films_c200_datasheet_01.pdf), [Superia X-TRA 400 product information](https://asset.fujifilm.com/www/in/files/2020-07/32cf7e5def364084eb8cf03ff011df0f/films_superia-xtra400_datasheet_01.pdf), [Pro 400H product information](https://www.fujifilm.com.hk/products/professional_films/pdf/pro_400h_datasheet.pdf), [Eterna 250D manual](https://manualzz.com/doc/27787292/fujifilm-motion-picture-film-manual).

## Stock Rollout: Fujichrome Astia 100F (2026-08-09)

Astia 100F (RAP100F) is now on the v4 characteristic-curve path. Fujifilm's
published E-6 data describes it as its softest-toned, subdued-color Fujichrome:
an ISO-100 professional transparency with continuous highlight-to-shadow skin
gradation, high color fidelity from its multi-color-correction layers, RMS 7
grain, and usable -0.5 to +2 stop push/pull processing.

The v4 calibration therefore uses a gentle 1.18 rendered contrast index, a
6.4-stop transparency range, and soft toe/shoulder joins. It deliberately stays
shorter than a color-negative response. The profile removes the prior invented
warm crossover in favor of near-neutral dye behavior, lowers global saturation,
and protects red-orange and neon colors so skin and subtly colored wardrobe do
not become overly dense. Its small halation is retained only for exceptional
specular sources through a high stock threshold. These are output-rendering
parameters derived from the stock's relative sensitometric role, not a claim
that the engine reproduces a physical E-6 density curve or scanner response.

Source: [Fujichrome Astia 100F professional data sheet](https://www.fujifilm.com.hk/products/professional_films/pdf/astia_100f_datasheet.pdf).

## Global Constraints

- Never regress legacy pipelines: `parity_v1` and `filmic_v2` MUST stay byte-identical (`dfee_tests` guards this). All new behavior is gated behind `effect_pipeline_version == "filmic_v4"`.
- `filmic_v3` must remain fully intact (it is the A/B reference during development).
- The post-film Light panel (`apply_scene_referred_tone`) is NOT touched by this work.
- Do not touch the three WIP files: `cpp_engine/bindings/python/dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py`.
- Commit messages: no `Co-Authored-By` lines.
- After any engine change, rebuild the desktop Release (`desktop/out/build`, target `DFEE`) — that is what the user runs — and the cpp_engine Release (`cpp_engine/out/build/windows-msvc-vcpkg`).
- `gamma` in a `characteristic_curve:` block is the RENDERED combined scene→positive contrast index (negatives: neg-γ × print-γ; reversal: film's own γ), derived from datasheets — not the negative-only γ.
- Mid-grey reference in linear = 0.18; its perceptual value `mid_out = pow(0.18, 1/2.2) ≈ 0.4586`.
- Curve output is perceptual tone in [0,1]; convert to linear via `pow(y, 2.2)` before writing pixels.

---

### Task 1: Characteristic curve math (pure, unit-tested)

**Files:**
- Create: `cpp_engine/include/dfee/characteristic_curve.hpp`
- Create: `cpp_engine/src/characteristic_curve.cpp`
- Modify: `cpp_engine/CMakeLists.txt` (add `src/characteristic_curve.cpp` to the `dfee_core` sources list)
- Test: `cpp_engine/tests/test_core.cpp` (append a `test_characteristic_curve()` and call it from `main`)

**Interfaces:**
- Produces:
  - `struct dfee::CharacteristicCurve { float gamma=1.0f; float latitude_stops=8.0f; float toe_onset=2.0f; float toe_hardness=1.0f; float shoulder_onset=2.0f; float shoulder_hardness=1.0f; float d_min=0.0f; float d_max=1.0f; };`
  - `float dfee::curve_eval(const CharacteristicCurve& c, float logE);` — returns perceptual tone in [0,1]; `logE` is stops from mid-grey.
  - `float dfee::scene_logE(float linear_value, float midtone_anchor, float exposure_shift_stops);`

- [ ] **Step 1: Write the failing test** — append to `cpp_engine/tests/test_core.cpp`:

```cpp
static void test_characteristic_curve() {
    using dfee::CharacteristicCurve;
    CharacteristicCurve c; // gamma 1.0, latitude 8, onsets 2, d_min 0, d_max 1
    constexpr float kMidOut = 0.4586f;         // pow(0.18, 1/2.2)
    // (a) mid-grey (logE 0) maps to the mid output reference.
    assert(std::abs(dfee::curve_eval(c, 0.0f) - kMidOut) < 0.02f);
    // (b) monotonic increasing across the whole log-E range.
    float prev = -1.0f;
    for (int i = -60; i <= 60; ++i) { float y = dfee::curve_eval(c, i * 0.1f); assert(y >= prev - 1e-6f); prev = y; }
    // (c) straight-line slope near mid ~= gamma/kStopsRef (kStopsRef = 8 => 0.125 per stop at gamma 1).
    float slope = (dfee::curve_eval(c, 0.5f) - dfee::curve_eval(c, -0.5f)) / 1.0f;
    assert(std::abs(slope - 0.125f) < 0.02f);
    // (d) higher gamma => steeper mid slope.
    CharacteristicCurve hi = c; hi.gamma = 2.0f;
    float slope_hi = (dfee::curve_eval(hi, 0.5f) - dfee::curve_eval(hi, -0.5f)) / 1.0f;
    assert(slope_hi > slope * 1.5f);
    // (e) toe/shoulder compress: slope beyond the onsets is < straight-line slope.
    float toe_slope = dfee::curve_eval(c, -3.5f) - dfee::curve_eval(c, -4.5f);
    assert(toe_slope < slope);
    // (f) endpoints approach d_min/d_max and stay in range.
    assert(dfee::curve_eval(c, -40.0f) >= 0.0f && dfee::curve_eval(c, -40.0f) < 0.05f);
    assert(dfee::curve_eval(c, 40.0f) <= 1.0f && dfee::curve_eval(c, 40.0f) > 0.95f);
    // scene_logE: value == anchor => 0; one stop brighter => +1; exposure shift adds.
    assert(std::abs(dfee::scene_logE(0.18f, 0.18f, 0.0f) - 0.0f) < 1e-4f);
    assert(std::abs(dfee::scene_logE(0.36f, 0.18f, 0.0f) - 1.0f) < 1e-4f);
    assert(std::abs(dfee::scene_logE(0.18f, 0.18f, 1.5f) - 1.5f) < 1e-4f);
    std::printf("test_characteristic_curve passed\n");
}
```
Add `test_characteristic_curve();` in `main` next to the other test calls, and `#include "dfee/characteristic_curve.hpp"` at the top.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_tests`
Expected: FAIL to compile — `characteristic_curve.hpp` not found / `curve_eval` undefined.

- [ ] **Step 3: Write the header** — `cpp_engine/include/dfee/characteristic_curve.hpp`:

```cpp
#pragma once
namespace dfee {
struct CharacteristicCurve {
    float gamma = 1.0f;             // rendered contrast index (mid-slope)
    float latitude_stops = 8.0f;    // reference span; sets stops-per-output via kStopsRef
    float toe_onset = 2.0f;         // stops below mid where the toe join begins
    float toe_hardness = 1.0f;      // knee tightness (1 = C1 continuous)
    float shoulder_onset = 2.0f;    // stops above mid where the shoulder join begins
    float shoulder_hardness = 1.0f;
    float d_min = 0.0f;             // rendered black floor (perceptual)
    float d_max = 1.0f;             // rendered white ceiling (perceptual)
};
// Perceptual tone in [0,1] for scene log-exposure `logE` (stops from mid-grey).
float curve_eval(const CharacteristicCurve& c, float logE);
// Scene log-exposure of a linear value relative to the metered mid-grey anchor, plus shift.
float scene_logE(float linear_value, float midtone_anchor, float exposure_shift_stops);
}
```

- [ ] **Step 4: Write the implementation** — `cpp_engine/src/characteristic_curve.cpp`:

```cpp
#include "dfee/characteristic_curve.hpp"
#include <algorithm>
#include <cmath>

namespace dfee {
namespace {
constexpr float kMidOut = 0.4586f;   // pow(0.18, 1/2.2): mid-grey perceptual output
constexpr float kStopsRef = 8.0f;    // gamma 1.0 => 1/8 output per stop of log-E
}

float curve_eval(const CharacteristicCurve& c, float logE) {
    const float s = c.gamma / kStopsRef;                 // straight-line slope (output per stop)
    const float d_min = std::clamp(c.d_min, 0.0f, 0.9f);
    const float d_max = std::clamp(c.d_max, d_min + 0.05f, 1.0f);
    const float lin = kMidOut + s * logE;                // straight-line output
    float y;
    if (logE < -c.toe_onset) {
        // C1 exponential toe: matches value & slope s at the join, asymptotes to d_min.
        const float y0 = kMidOut - s * c.toe_onset;      // straight-line value at the join
        const float foot = std::max(y0 - d_min, 1e-4f);
        const float k = (s / foot) * std::max(c.toe_hardness, 0.05f);
        y = d_min + foot * std::exp(k * (logE + c.toe_onset));
    } else if (logE > c.shoulder_onset) {
        // C1 exponential shoulder: asymptotes to d_max.
        const float y1 = kMidOut + s * c.shoulder_onset;
        const float head = std::max(d_max - y1, 1e-4f);
        const float k = (s / head) * std::max(c.shoulder_hardness, 0.05f);
        y = d_max - head * std::exp(-k * (logE - c.shoulder_onset));
    } else {
        y = lin;
    }
    return std::clamp(y, 0.0f, 1.0f);
}

float scene_logE(float linear_value, float midtone_anchor, float exposure_shift_stops) {
    const float v = std::max(linear_value, 1e-6f);
    const float a = std::max(midtone_anchor, 1e-6f);
    return std::log2(v / a) + exposure_shift_stops;
}
}
```
Add `src/characteristic_curve.cpp` to the `dfee_core` target's source list in `cpp_engine/CMakeLists.txt`.

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_tests && cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_tests.exe`
Expected: `test_characteristic_curve passed` and `dfee_tests passed`.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/include/dfee/characteristic_curve.hpp cpp_engine/src/characteristic_curve.cpp cpp_engine/CMakeLists.txt cpp_engine/tests/test_core.cpp
git commit -m "engine: sensitometric characteristic curve math (pure + unit-tested)"
```

---

### Task 2: `filmic_v4` pipeline gate

**Files:**
- Modify: `cpp_engine/src/session.cpp:52-92` (pipeline constants, `is_subtractive_effect_pipeline`, `validate_effect_pipeline_version`; add `is_characteristic_curve_pipeline`)
- Modify: `cpp_engine/include/dfee/session.hpp` (declare `is_characteristic_curve_pipeline` if the others are declared there; otherwise keep file-local)
- Test: `cpp_engine/tests/test_core.cpp` (`test_pipeline_gate()`)

**Interfaces:**
- Produces: `bool is_characteristic_curve_pipeline(const std::string&)` (true only for `filmic_v4`); `is_subtractive_effect_pipeline` returns true for BOTH `filmic_v3` and `filmic_v4`; `validate_effect_pipeline_version` accepts `filmic_v4`.

- [ ] **Step 1: Write the failing test** — append to `test_core.cpp`:

```cpp
static void test_pipeline_gate() {
    assert(dfee::is_characteristic_curve_pipeline("filmic_v4"));
    assert(!dfee::is_characteristic_curve_pipeline("filmic_v3"));
    assert(dfee::is_subtractive_effect_pipeline("filmic_v4"));   // v4 still runs subtractive stages
    assert(dfee::is_subtractive_effect_pipeline("filmic_v3"));
    assert(!dfee::validate_effect_pipeline_version("filmic_v4").has_value()); // accepted
    std::printf("test_pipeline_gate passed\n");
}
```
Call it from `main`. (If these helpers are in an anonymous namespace in session.cpp, expose them via `session.hpp` or a small internal header so the test links — do that as part of Step 3.)

- [ ] **Step 2: Run test to verify it fails**

Run: build `dfee_tests`. Expected: link/compile error — `is_characteristic_curve_pipeline` undefined / `filmic_v4` rejected.

- [ ] **Step 3: Implement the gate** — in `cpp_engine/src/session.cpp` near line 54 add:

```cpp
constexpr const char* kCharacteristicEffectPipelineVersion = "filmic_v4";
```
Change `is_subtractive_effect_pipeline` to:

```cpp
[[nodiscard]] bool is_subtractive_effect_pipeline(const std::string& value) {
    const std::string n = normalized_effect_pipeline_version(value);
    return n == kSubtractiveEffectPipelineVersion || n == kCharacteristicEffectPipelineVersion;
}
[[nodiscard]] bool is_characteristic_curve_pipeline(const std::string& value) {
    return normalized_effect_pipeline_version(value) == kCharacteristicEffectPipelineVersion;
}
```
In `validate_effect_pipeline_version`, add `|| normalized == kCharacteristicEffectPipelineVersion` to the accept condition and append `, filmic_v4` to the `.detail` message. Declare both helpers in `session.hpp` (or a shared internal header) so the test and other TUs can call them.

- [ ] **Step 4: Run test to verify it passes**

Run: build + run `dfee_tests`. Expected: `test_pipeline_gate passed`, `dfee_tests passed`.

- [ ] **Step 5: Commit**

```bash
git add cpp_engine/src/session.cpp cpp_engine/include/dfee/session.hpp cpp_engine/tests/test_core.cpp
git commit -m "engine: recognize filmic_v4 pipeline (characteristic-curve gate)"
```

---

### Task 3: Solver wiring — YAML → curve + control mapping + scene placement

**Files:**
- Modify: `cpp_engine/include/dfee/solver.hpp:84+` (add fields to `FilmResponsePlan`)
- Create: `cpp_engine/include/dfee/curve_mapping.hpp` + `cpp_engine/src/curve_mapping.cpp` (pure mapping function, unit-testable)
- Modify: `cpp_engine/src/solver.cpp` (near the `plan.film_response = {...}` build, ~636, and the tone-steering block ~547) to populate the curve when `is_characteristic_curve_pipeline`
- Modify: `cpp_engine/CMakeLists.txt` (add `src/curve_mapping.cpp`)
- Test: `cpp_engine/tests/test_core.cpp` (`test_curve_mapping()`)

**Interfaces:**
- Consumes: `CharacteristicCurve` (Task 1); `is_characteristic_curve_pipeline` (Task 2).
- Produces (added to `FilmResponsePlan`):
  - `bool use_characteristic_curve = false;`
  - `CharacteristicCurve characteristic_curve;`
  - `float scene_midtone_anchor = 0.18f;`
  - `float scene_exposure_shift = 0.0f;`
  - `std::array<float,3> curve_gamma_mult{1.0f,1.0f,1.0f};` (per-dye-layer gamma; absorbs crossover)
- Produces (in `curve_mapping.hpp`):
  - `CharacteristicCurve map_characteristic_curve(const CharacteristicCurve& authored, float film_contrast, float highlight_rolloff, float shadow_lift);`
  - `float map_exposure_shift(float film_exposure_ev, bool is_reversal);`

- [ ] **Step 1: Write the failing test** — append to `test_core.cpp`:

```cpp
static void test_curve_mapping() {
    dfee::CharacteristicCurve base; base.gamma = 1.0f; base.shoulder_onset = 2.0f; base.toe_onset = 2.0f; base.d_min = 0.0f;
    // Film Contrast +100 raises gamma; -100 lowers it.
    assert(dfee::map_characteristic_curve(base, 200.f, 100.f, 0.f).gamma > 1.0f);
    assert(dfee::map_characteristic_curve(base, 0.f,   100.f, 0.f).gamma < 1.0f);
    // Highlight Rolloff up pulls the shoulder onset EARLIER (smaller).
    assert(dfee::map_characteristic_curve(base, 100.f, 200.f, 0.f).shoulder_onset < base.shoulder_onset);
    // Shadow Lift up raises d_min (lifted toe).
    assert(dfee::map_characteristic_curve(base, 100.f, 100.f, 100.f).d_min > base.d_min);
    // Exposure shift: negatives get +2/3 expose-to-right bias at EV 0; reversal neutral.
    assert(std::abs(dfee::map_exposure_shift(0.f, false) - 0.66f) < 0.05f);
    assert(std::abs(dfee::map_exposure_shift(0.f, true)  - 0.0f)  < 1e-4f);
    assert(std::abs(dfee::map_exposure_shift(1.f, true)  - 1.0f)  < 1e-4f);
    std::printf("test_curve_mapping passed\n");
}
```
Call from `main`; `#include "dfee/curve_mapping.hpp"`.

- [ ] **Step 2: Run test to verify it fails** — build `dfee_tests`; expected: `curve_mapping.hpp` missing.

- [ ] **Step 3: Implement the mapping unit** — `cpp_engine/include/dfee/curve_mapping.hpp`:

```cpp
#pragma once
#include "dfee/characteristic_curve.hpp"
namespace dfee {
CharacteristicCurve map_characteristic_curve(const CharacteristicCurve& authored,
                                             float film_contrast, float highlight_rolloff, float shadow_lift);
float map_exposure_shift(float film_exposure_ev, bool is_reversal);
}
```
`cpp_engine/src/curve_mapping.cpp`:

```cpp
#include "dfee/curve_mapping.hpp"
#include <algorithm>
namespace dfee {
CharacteristicCurve map_characteristic_curve(const CharacteristicCurve& a,
                                             float film_contrast, float highlight_rolloff, float shadow_lift) {
    CharacteristicCurve c = a;
    const float con = std::clamp(film_contrast / 100.0f, 0.0f, 2.0f);       // 1.0 = neutral
    c.gamma = std::clamp(a.gamma * (0.6f + 0.4f * con), 0.3f, 3.5f);        // +-40% around authored at ends
    const float roll = std::clamp(highlight_rolloff / 100.0f, 0.0f, 2.0f);  // 1.0 = neutral
    c.shoulder_onset = std::clamp(a.shoulder_onset - (roll - 1.0f) * 0.8f, 0.4f, 6.0f); // more roll = earlier shoulder
    const float lift = std::clamp(shadow_lift / 100.0f, -1.0f, 1.0f);
    if (lift > 0.0f) { c.d_min = std::clamp(a.d_min + lift * 0.10f, 0.0f, 0.30f); }        // matte lift
    else             { c.toe_onset = std::clamp(a.toe_onset + (-lift) * 0.8f, 0.4f, 6.0f); } // crush deeper
    return c;
}
float map_exposure_shift(float film_exposure_ev, bool is_reversal) {
    const float bias = is_reversal ? 0.0f : 0.66f; // negatives designed to over-expose ~2/3 stop
    return film_exposure_ev + bias;
}
}
```
Add `src/curve_mapping.cpp` to `dfee_core` in CMake.

- [ ] **Step 4: Wire into the solver** — in `cpp_engine/src/solver.cpp`, add the fields to the `FilmResponsePlan` struct in `solver.hpp` (Interfaces above). After the existing tone block (and only when `controls.characteristic_pipeline` — add that bool to `SolverControls`, set from `is_characteristic_curve_pipeline` in session.cpp alongside `subtractive_pipeline` at lines 958 and 2654), populate:

```cpp
if (controls.characteristic_pipeline && has_key(stock_profile.numeric_values, "characteristic_curve.gamma")) {
    CharacteristicCurve authored;
    authored.gamma            = get_numeric(stock_profile.numeric_values, "characteristic_curve.gamma", 1.0f);
    authored.latitude_stops   = get_numeric(stock_profile.numeric_values, "characteristic_curve.latitude_stops", 8.0f);
    authored.toe_onset        = get_numeric(stock_profile.numeric_values, "characteristic_curve.toe_onset", 2.0f);
    authored.toe_hardness     = get_numeric(stock_profile.numeric_values, "characteristic_curve.toe_hardness", 1.0f);
    authored.shoulder_onset   = get_numeric(stock_profile.numeric_values, "characteristic_curve.shoulder_onset", 2.0f);
    authored.shoulder_hardness= get_numeric(stock_profile.numeric_values, "characteristic_curve.shoulder_hardness", 1.0f);
    authored.d_min            = get_numeric(stock_profile.numeric_values, "characteristic_curve.d_min", 0.0f);
    authored.d_max            = get_numeric(stock_profile.numeric_values, "characteristic_curve.d_max", 1.0f);
    plan.film_response.characteristic_curve = map_characteristic_curve(
        authored, controls.film_contrast, controls.highlight_rolloff, controls.shadow_lift);
    plan.film_response.use_characteristic_curve = true;
    plan.film_response.scene_midtone_anchor = tonal.midtone_anchor;
    plan.film_response.scene_exposure_shift = map_exposure_shift(
        controls.film_exposure_ev, stock_profile.stock_type == StockType::ColorReversal);
    plan.film_response.curve_gamma_mult = get_array3(
        stock_profile.numeric_arrays, "characteristic_curve.channel_gamma_mult", {1.0f,1.0f,1.0f});
}
```
(Use the existing `get_numeric`/`get_array3` helpers and whatever the codebase's "key present" check is; if none exists, treat gamma default sentinel `<= 0` as "absent".) Include `dfee/curve_mapping.hpp`.

- [ ] **Step 5: Run tests** — build + run `dfee_tests`. Expected: `test_curve_mapping passed`, all pass.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/include/dfee/solver.hpp cpp_engine/include/dfee/curve_mapping.hpp cpp_engine/src/curve_mapping.cpp cpp_engine/src/solver.cpp cpp_engine/src/session.cpp cpp_engine/CMakeLists.txt cpp_engine/tests/test_core.cpp
git commit -m "engine: solver wiring for characteristic curve (YAML + control mapping + placement)"
```

---

### Task 4: Renderer — apply the curve per channel with scene placement

**Files:**
- Modify: `cpp_engine/src/renderer.cpp` — `apply_film_tone_response` (~1297-1368)
- Test: `cpp_engine/tests/test_core.cpp` (`test_curve_render()`)

**Interfaces:**
- Consumes: `FilmResponsePlan.use_characteristic_curve`, `.characteristic_curve`, `.scene_midtone_anchor`, `.scene_exposure_shift`, `.curve_gamma_mult` (Task 3); `curve_eval`/`scene_logE` (Task 1).

- [ ] **Step 1: Write the failing test** — append to `test_core.cpp` (build a 1-row linear ramp Image, run through a FilmRenderer with a curve plan, assert monotonic output and that higher gamma widens output spread):

```cpp
static void test_curve_render() {
    const int N = 64;
    dfee::Image ramp(N, 1, 3);
    for (int i = 0; i < N; ++i) { float v = static_cast<float>(i) / (N - 1); for (int c = 0; c < 3; ++c) ramp.pixels[i*3+c] = v; }
    dfee::FilmRenderer renderer;
    dfee::FilmResponsePlan plan;                       // defaults
    plan.use_characteristic_curve = true;
    plan.scene_midtone_anchor = 0.18f; plan.scene_exposure_shift = 0.0f;
    plan.characteristic_curve.gamma = 1.0f;
    dfee::Image out1 = renderer.apply_film_tone_response(ramp, plan);
    // monotonic
    for (int i = 1; i < N; ++i) assert(out1.pixels[i*3] >= out1.pixels[(i-1)*3] - 1e-5f);
    // higher gamma => larger output spread between the 25th and 75th input samples
    dfee::FilmResponsePlan plan2 = plan; plan2.characteristic_curve.gamma = 2.2f;
    dfee::Image out2 = renderer.apply_film_tone_response(ramp, plan2);
    float spread1 = out1.pixels[48*3] - out1.pixels[16*3];
    float spread2 = out2.pixels[48*3] - out2.pixels[16*3];
    assert(spread2 > spread1);
    std::printf("test_curve_render passed\n");
}
```
Call from `main`.

- [ ] **Step 2: Run test to verify it fails** — build `dfee_tests`. Expected: fails the `spread2 > spread1` / monotonic assertions (renderer still uses the logistic; `use_characteristic_curve` ignored).

- [ ] **Step 3: Implement** — in `apply_film_tone_response`, at the top of the per-channel LUT loop, branch on the curve. Keep the existing logistic path unchanged for `!use_characteristic_curve`:

```cpp
if (response.use_characteristic_curve) {
    for (int channel = 0; channel < 3; ++channel) {
        auto& lut = tone_luts[static_cast<std::size_t>(channel)];
        lut.resize(kToneLutSize);
        CharacteristicCurve cc = response.characteristic_curve;
        cc.gamma *= response.curve_gamma_mult[static_cast<std::size_t>(channel)];  // per-dye-layer => crossover
        for (std::size_t s = 0; s < kToneLutSize; ++s) {
            const float ch = static_cast<float>(s) / static_cast<float>(kToneLutSize - 1U);
            const float logE = scene_logE(ch, response.scene_midtone_anchor, response.scene_exposure_shift);
            const float y = curve_eval(cc, logE);          // perceptual [0,1]
            lut[s] = std::pow(y, 2.2F);                     // back to linear for the pipeline
        }
    }
    // (reuse the existing per-pixel LUT application below)
} else {
    // ... existing logistic LUT build ...
}
```
Ensure the per-pixel apply loop that follows uses `tone_luts` for both branches. `#include "dfee/characteristic_curve.hpp"`.

- [ ] **Step 4: Run test to verify it passes** — build + run `dfee_tests`. Expected: `test_curve_render passed`, all pass.

- [ ] **Step 5: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "engine: renderer applies per-channel characteristic curve with scene placement"
```

---

### Task 5: Harness A/B knobs (pipeline switch)

**Files:**
- Modify: `cpp_engine/experiments/bespoke_portra800.cpp` (~248)

**Interfaces:** Consumes nothing new; enables `DFEE_PIPELINE` to select the pipeline for A/B.

- [ ] **Step 1: Implement** — replace the hard-coded pipeline line with:

```cpp
req.effect_pipeline_version = [](){ const char* p = std::getenv("DFEE_PIPELINE"); return p ? std::string(p) : std::string("filmic_v3"); }();
```
(The Light-panel env knobs and `film_exposure_ev` arg already exist for exposure-sweep testing.)

- [ ] **Step 2: Build + smoke test**

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_bespoke_portra800`
Then render one image both ways and confirm both succeed:
```bash
EXE=cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_bespoke_portra800.exe
IN=comparision/2316908974.tif
DFEE_PIPELINE=filmic_v3 "$EXE" "$IN" C:/Users/mihir/AppData/Local/Temp/dfee_sat/ab_v3.jpg velvia_50
DFEE_PIPELINE=filmic_v4 "$EXE" "$IN" C:/Users/mihir/AppData/Local/Temp/dfee_sat/ab_v4.jpg velvia_50
```
Expected: both write successfully (v4 falls back to logistic for velvia_50 until Task 6 adds its curve block, so they may look identical here — that's fine).

- [ ] **Step 3: Commit**

```bash
git add cpp_engine/experiments/bespoke_portra800.cpp
git commit -m "experiments: DFEE_PIPELINE knob for filmic_v3 vs filmic_v4 A/B"
```

---

### Task 6: Prototype stock curves + validation (Velvia 50 / Portra 400 / Tri-X 400)

**Files:**
- Modify: `profiles/stocks/velvia_50.yaml`, `profiles/stocks/portra_400.yaml`, `profiles/stocks/tri_x_400.yaml` (add `characteristic_curve:` block)
- No test file — validation is via the harness with numeric acceptance criteria below.

**Interfaces:** Consumes the full v4 path (Tasks 1–5).

- [ ] **Step 1: Research datasheet parameters.** For each stock, pull the published characteristic curve / contrast index from the manufacturer datasheet (Kodak E-6/C-41, Fuji, Kodak B&W) via WebSearch/WebFetch. Record, per stock: rendered `gamma` (see Global Constraints — combined, not neg-only), `latitude_stops`, `toe_onset`, `shoulder_onset`, `d_min`, `d_max`. Target values to author (starting points, refine in Step 3):
  - Velvia 50 (reversal, punchy): `gamma 1.9, latitude_stops 5.0, toe_onset 1.6, shoulder_onset 1.8, toe_hardness 1.4, shoulder_hardness 1.4, d_min 0.02, d_max 1.0`
  - Portra 400 (negative, soft/wide): `gamma 1.0, latitude_stops 10.0, toe_onset 3.0, shoulder_onset 3.2, toe_hardness 0.8, shoulder_hardness 0.7, d_min 0.03, d_max 0.98`
  - Tri-X 400 (B&W, moderate): `gamma 1.15, latitude_stops 8.0, toe_onset 2.4, shoulder_onset 2.6, toe_hardness 1.0, shoulder_hardness 1.0, d_min 0.02, d_max 1.0`

- [ ] **Step 2: Add the YAML block** to each stock, e.g. `profiles/stocks/velvia_50.yaml`:

```yaml
characteristic_curve:
  gamma: 1.9
  latitude_stops: 5.0
  toe_onset: 1.6
  toe_hardness: 1.4
  shoulder_onset: 1.8
  shoulder_hardness: 1.4
  d_min: 0.02
  d_max: 1.0
  channel_gamma_mult: [1.0, 1.0, 1.0]   # per-dye-layer gamma (crossover); tune per stock
```

- [ ] **Step 3: Validate the transfer curve (gamma).** Render the synthetic ramp (`C:/Users/mihir/AppData/Local/Temp/dfee_sat/ramp.tif`, created earlier — regenerate if absent) under v4 and measure the mid-slope:
```bash
EXE=cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_bespoke_portra800.exe
DFEE_PIPELINE=filmic_v4 "$EXE" ramp.tif out_v4.jpg velvia_50
```
Acceptance: the measured output mid-slope (in perceptual space) ranks Velvia > Tri-X > Portra, matching the authored gammas; distinct and monotonic. Adjust `gamma`/onsets in the YAML and re-render until the ordering and steepness look right.

- [ ] **Step 4: Validate exposure-dependent contrast (the core claim).** Exposure sweep on a real image (`comparision/2316908974.tif`), Film Exposure = −2 / 0 / +2 (harness arg 9 `film_exposure_ev`):
```bash
for ev in -2 0 2; do DFEE_PIPELINE=filmic_v4 "$EXE" comparision/2316908974.tif sweep_${ev}.jpg portra_400 auto_balanced 100 0 0 100 $ev; done
```
Acceptance: at −2 the shadows crush into the toe (deep, compressed, lower local contrast); at +2 the shadows open and highlights roll into the shoulder (creamy). Confirm by measuring p10 (rises with EV) and p90 (compresses/rolls, not hard-clips) across the three renders.

- [ ] **Step 5: Visual A/B v3 vs v4** for each of the three stocks on a real image; confirm v4 has visibly more distinct, stock-appropriate contrast than v3 without looking crushed-digital or oversaturated (the user's stated failure modes).

- [ ] **Step 6: Parity + full test.** Run `dfee_tests.exe`; expected `dfee_tests passed` (parity_v1/filmic_v2 byte-identical). Rebuild desktop `DFEE` Release.

- [ ] **Step 7: Commit**

```bash
git add profiles/stocks/velvia_50.yaml profiles/stocks/portra_400.yaml profiles/stocks/tri_x_400.yaml
git commit -m "profiles: datasheet-derived characteristic curves for Velvia 50 / Portra 400 / Tri-X 400 (v4 prototype)"
```

---

### Task 7: Rollout procedure (all remaining stocks) + docs

**Files:**
- Modify: remaining `profiles/stocks/*.yaml` (add `characteristic_curve:` block per stock)
- Modify: `docs/superpowers/specs/2026-08-07-film-characteristic-curves-design.md` (mark rollout status)

**Interfaces:** Consumes the validated v4 path.

- [ ] **Step 1: Per-stock procedure (repeat for each remaining stock, one commit per small batch by family):**
  1. Research the stock's datasheet gamma/latitude/toe/shoulder (or, where no datasheet exists, estimate from its family and existing `tone_response` character).
  2. Add the `characteristic_curve:` block.
  3. Render v3 vs v4 A/B on a representative image; confirm the v4 look matches the stock's known character and is distinct from its neighbors.
  4. Run `dfee_tests` (parity).
- [ ] **Step 2:** Once all stocks carry a `characteristic_curve:` block and look right, decide (separately, with the user) whether to make `filmic_v4` the default `effect_pipeline_version` in the desktop/app request defaults.
- [ ] **Step 3: Commit** each family batch, e.g. `git commit -m "profiles: characteristic curves for the Kodak negatives (v4)"`.

---

## Self-Review

**Spec coverage:** model (Task 1), scene placement (Tasks 1,3,4), per-dye-layer color/crossover (Task 3 `curve_gamma_mult`, Task 4), filmic_v4 gate (Task 2), control re-layering (Task 3 `map_characteristic_curve`/`map_exposure_shift`), Light panel untouched (not referenced), prototype 3 stocks + datasheet derivation + harness validation (Task 6), rollout (Task 7), parity guard (Tasks 2/6/7). All spec sections covered.

**Placeholder scan:** all code steps contain real code; validation steps have concrete commands + numeric acceptance criteria. No TBD/TODO.

**Type consistency:** `CharacteristicCurve`, `curve_eval`, `scene_logE`, `map_characteristic_curve`, `map_exposure_shift`, and the `FilmResponsePlan` fields (`use_characteristic_curve`, `characteristic_curve`, `scene_midtone_anchor`, `scene_exposure_shift`, `curve_gamma_mult`) are named identically across Tasks 1, 3, 4. `is_characteristic_curve_pipeline` consistent across Tasks 2–3.
