# Engine 0C — Golden Renders, Profile Cache and Look Proxies Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the engine what the Phase 2 Films tray needs — a fast, small render of the open photo through any recipe (`EngineSession::render_look_proxy`) — while proving the main preview's output does not change.

**Architecture:** First pin today's preview output with golden reference renders (synthetic TIFF scenes × film recipes). Then cache parsed stock/print profiles by path + mtime. Then extract `render_preview`'s film and neutral paths into two private pipeline functions that take a *source* (image + analysis + masks) and *options* (stage-name prefix, grain on/off, geometry on/off); `render_preview` calls them with the preview cache and must still match the goldens. Finally `render_look_proxy` calls the same functions with a cached downscale (≈256 px) of the preview source and its masks, grain off, and returns packed RGB8.

**Tech Stack:** C++20, OpenCV (imgcodecs, imgproc), yaml-cpp, OpenMP, CMake (`cpp_engine/out/build/windows-msvc-vcpkg`).

**Spec:** `docs/superpowers/specs/2026-09-27-ui-redesign-design.md` — "Engine additions (no change to rendering maths)" and Phasing step 0.

## Global Constraints

- Preview rendering maths must not change: every golden recipe matches its stored reference (decoded JPEG pixels, max abs diff ≤ 8 and mean abs diff ≤ 0.5 per channel; the run also reports how many are bit-exact).
- Proxy: `max_edge` ≈ 256, grain off by default, reuses the preview's analysis (solver input shared, masks resized), packed RGB8 out; target p50 ≤ 60 ms per tile (logged; the test fails only above 250 ms).
- Parsed stock/print profiles are cached per path + modification time; a profile edited on disk is re-read.
- New tests use throwing checks (`expect`), never bare `assert` (Release compiles asserts out).
- Do not touch `cpp_engine/bindings/python/dfee_native_module.cpp` or `dfee_native_bridge.py` / `server.py` (the user's work in progress); build only the targets named here so `dfee_native.pyd` is not rebuilt while a server may hold it.
- Tests render only synthetic images written to `raw_files/` (git-ignored) and remove them afterwards; no export runs.
- The desktop app still builds and its UI scripts still pass (it compiles `dfee_core` from source).

## Review Focus

1. **Switching photos**: a proxy for photo B after photo A must use B's pixels and masks, never A's cached downscale. Task 4 test renders A then B and checks B's proxy against B's preview.
2. **Monochrome stocks** take the panchromatic path in the proxy as in the preview. Task 4 test includes `tri_x_400` in the proxy-vs-preview comparison.
3. **Geometry in a proxy** (crop / rotate / flip / straighten) gives the same framing as the preview, so tile proportions match. Task 4 test checks proxy aspect ratio equals the preview's.
4. **Preview after proxies**: rendering proxies must not disturb the preview caches — the golden preview still matches afterwards. Task 4 test re-renders a golden recipe after a batch of proxies.
5. **A profile edited while the app runs** is picked up on the next render; an unchanged one is parsed once. Task 2 test touches a copied profile's mtime.

---

### Task 1: Golden preview references

**Files:**
- Create: `cpp_engine/tests/test_session.cpp`
- Create (recorded): `cpp_engine/tests/golden/preview/*.png` (24 files)
- Modify: `cpp_engine/CMakeLists.txt` (new test target)

**Interfaces:**
- Produces: test executable `dfee_session_tests` (ctest name `dfee_session_tests`); env `DFEE_GOLDEN_RECORD=1` writes references instead of comparing; helpers `make_scene`, `base_request`, `decode_jpeg`, `compare_images`, `golden_recipes()` reused by later tasks.

- [ ] **Step 1: Add the test target**

In `cpp_engine/CMakeLists.txt`, directly after the `dfee_tests` block (after `add_test(NAME dfee_tests COMMAND dfee_tests)` and its `set_tests_properties` lines), add:

```cmake
    # Session-level tests: golden preview references, profile cache, look proxies.
    add_executable(dfee_session_tests tests/test_session.cpp)
    target_link_libraries(dfee_session_tests PRIVATE dfee_core)
    target_compile_definitions(dfee_session_tests PRIVATE DFEE_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
    add_test(NAME dfee_session_tests COMMAND dfee_session_tests)
```

- [ ] **Step 2: Write `cpp_engine/tests/test_session.cpp`**

```cpp
// Session-level tests: golden preview references (rendering maths must not change),
// the profile cache, and look proxies. Throwing checks only (Release builds drop
// assert). Record references with DFEE_GOLDEN_RECORD=1.
#include "dfee/bridge_types.hpp"
#include "dfee/session.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

const std::filesystem::path kRepoRoot = DFEE_REPO_ROOT;
const std::filesystem::path kGoldenDir = kRepoRoot / "cpp_engine" / "tests" / "golden" / "preview";
const std::filesystem::path kFixtureDir = kRepoRoot / "raw_files";

void expect(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error("check failed: " + what);
}

std::uint16_t to16(double v) {
    return static_cast<std::uint16_t>(std::lround(std::clamp(v, 0.0, 1.0) * 65535.0));
}

// Deterministic display-referred test scenes (16-bit sRGB TIFF, written BGR).
// 0: grey ramp over colour patches; 1: bright sky with a clipped sun over warm
// ground; 2: low key with a point light (halation).
cv::Mat make_scene(int kind, int width, int height) {
    static const double patches[6][3] = {
        {0.80, 0.20, 0.20}, {0.20, 0.70, 0.25}, {0.20, 0.30, 0.80},
        {0.85, 0.62, 0.50}, {0.45, 0.65, 0.90}, {0.30, 0.45, 0.20}};
    cv::Mat img(height, width, CV_16UC3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double u = x / static_cast<double>(width - 1);
            const double v = y / static_cast<double>(height - 1);
            double r = 0.0, g = 0.0, b = 0.0;
            if (kind == 0) {
                if (v < 0.5) {
                    r = g = b = u;
                } else {
                    const auto& c = patches[std::min(5, static_cast<int>(u * 6.0))];
                    r = c[0]; g = c[1]; b = c[2];
                }
            } else if (kind == 1) {
                const double sky = 0.75 + 0.25 * (1.0 - v);
                if (v < 0.6) {
                    r = sky * 0.92; g = sky * 0.96; b = std::min(1.0, sky * 1.05);
                } else {
                    r = 0.70 - 0.30 * (v - 0.6); g = 0.55 - 0.25 * (v - 0.6); b = 0.35;
                }
                const double dx = u - 0.7, dy = v - 0.2;
                if (dx * dx + dy * dy < 0.006) { r = g = b = 1.0; }
            } else {
                r = g = b = 0.04 + 0.08 * u * v;
                b += 0.02;
                const double dx = u - 0.3, dy = v - 0.4;
                if (dx * dx + dy * dy < 0.004) { r = 1.0; g = 0.95; b = 0.85; }
            }
            img.at<cv::Vec3w>(y, x) = cv::Vec3w(to16(b), to16(g), to16(r));
        }
    }
    return img;
}

std::filesystem::path write_scene(const std::string& name, int kind, int width, int height) {
    std::filesystem::create_directories(kFixtureDir);
    const auto path = kFixtureDir / ("session_test_" + name + ".tif");
    expect(cv::imwrite(path.string(), make_scene(kind, width, height)), "write fixture " + name);
    return path;
}

// The desktop app's request shape (filmic_v4, explicit grain so no auto-resolution).
dfee::NativePreviewRenderRequest base_request(const std::filesystem::path& file, const std::string& stock) {
    dfee::NativePreviewRenderRequest r;
    r.filename = file.string();
    r.stock = stock;
    r.effect_pipeline_version = "filmic_v4";
    r.exposure_placement = "as_shot";
    r.grain = "Custom";
    r.grain_strength = 0.4F;
    r.grain_size = 0.6F;
    r.grain_roughness = 0.5F;
    return r;
}

struct Recipe {
    std::string name;
    std::string stock;
    std::string print_stock = "none";
    bool geometry = false;
};

std::vector<Recipe> golden_recipes() {
    return {
        {"none", "none"},
        {"portra_400", "portra_400"},
        {"ektar_100", "ektar_100"},
        {"velvia_50", "velvia_50"},
        {"tri_x_400", "tri_x_400"},
        {"cinestill_800t", "cinestill_800t"},
        {"portra_400_print_2383", "portra_400", "kodak_2383"},
        {"portra_400_geometry", "portra_400", "none", true},
    };
}

dfee::NativePreviewRenderRequest recipe_request(const std::filesystem::path& file, const Recipe& recipe) {
    auto r = base_request(file, recipe.stock);
    r.print_stock = recipe.print_stock;
    if (recipe.geometry) {
        r.crop_x = 0.10F; r.crop_y = 0.10F; r.crop_w = 0.80F; r.crop_h = 0.70F;
        r.rotate_quadrant = 1;
        r.flip_h = true;
        r.straighten_deg = 3.5F;
    }
    return r;
}

cv::Mat decode_jpeg(const std::vector<std::uint8_t>& bytes) {
    cv::Mat img = cv::imdecode(bytes, cv::IMREAD_COLOR);
    expect(!img.empty(), "decode preview JPEG");
    return img;
}

struct ImageDiff {
    int max_abs = 0;
    double mean_abs = 0.0;
    bool exact = false;
};

ImageDiff compare_images(const cv::Mat& a, const cv::Mat& b) {
    expect(a.size() == b.size() && a.type() == b.type(), "images have the same size and type");
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    double max_value = 0.0;
    cv::minMaxLoc(diff.reshape(1), nullptr, &max_value);
    const cv::Scalar mean = cv::mean(diff);
    ImageDiff d;
    d.max_abs = static_cast<int>(max_value);
    d.mean_abs = (mean[0] + mean[1] + mean[2]) / 3.0;
    d.exact = d.max_abs == 0;
    return d;
}

struct Scene {
    std::string name;
    int kind;
};

const std::vector<Scene>& golden_scenes() {
    static const std::vector<Scene> scenes = {{"ramp", 0}, {"bright", 1}, {"lowkey", 2}};
    return scenes;
}

// Renders every scene × recipe and either records the decoded preview as the
// reference PNG or compares against it. Returns the number of bit-exact matches.
int run_golden(dfee::EngineSession& session, bool record) {
    std::filesystem::create_directories(kGoldenDir);
    int exact = 0;
    int total = 0;
    for (const auto& scene : golden_scenes()) {
        const auto file = write_scene(scene.name, scene.kind, 240, 160);
        for (const auto& recipe : golden_recipes()) {
            const auto response = session.render_preview(recipe_request(file, recipe));
            expect(response.ok, "render " + scene.name + "/" + recipe.name + ": " + response.error.detail);
            const cv::Mat image = decode_jpeg(response.jpeg_bytes);
            const auto ref_path = kGoldenDir / (scene.name + "__" + recipe.name + ".png");
            ++total;
            if (record) {
                expect(cv::imwrite(ref_path.string(), image), "write reference " + ref_path.string());
                ++exact;
                continue;
            }
            expect(std::filesystem::exists(ref_path), "reference exists: " + ref_path.string()
                + " (record with DFEE_GOLDEN_RECORD=1)");
            const cv::Mat reference = cv::imread(ref_path.string(), cv::IMREAD_COLOR);
            const auto d = compare_images(image, reference);
            expect(d.max_abs <= 8 && d.mean_abs <= 0.5,
                   scene.name + "/" + recipe.name + " drifted from its reference (max " +
                   std::to_string(d.max_abs) + ", mean " + std::to_string(d.mean_abs) + ")");
            if (d.exact) ++exact;
        }
        std::filesystem::remove(file);
    }
    std::cout << "  golden previews: " << exact << "/" << total << (record ? " recorded" : " bit-exact") << "\n";
    return exact;
}

void test_preview_golden() {
    const char* record_env = std::getenv("DFEE_GOLDEN_RECORD");
    const bool record = record_env != nullptr && std::string(record_env) == "1";
    dfee::EngineSession session(kRepoRoot);
    run_golden(session, record);
}

}  // namespace

int main() {
    try {
        test_preview_golden();
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "dfee_session_tests: all passed\n";
    return 0;
}
```

- [ ] **Step 3: Build and run it before recording (it must fail)**

Run:
```bash
cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe
```
Expected: `FAILED: check failed: reference exists: …ramp__none.png (record with DFEE_GOLDEN_RECORD=1)`, exit code 1.

- [ ] **Step 4: Record the references on the current engine, then compare**

```bash
DFEE_GOLDEN_RECORD=1 cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe
ls cpp_engine/tests/golden/preview | wc -l
```
Expected: the record run prints `golden previews: 24/24 recorded`; both compare runs print `golden previews: N/24 bit-exact` and `all passed` (note N in the ledger — 24 means the pipeline is fully deterministic; anything lower is OpenMP float ordering, which the tolerance absorbs); 24 files.

- [ ] **Step 5: Commit**

```bash
git add cpp_engine/CMakeLists.txt cpp_engine/tests/test_session.cpp cpp_engine/tests/golden/preview
git commit -m "test(engine): golden preview references for 3 scenes x 8 film recipes"
```

---

### Task 2: Profile cache

**Files:**
- Modify: `cpp_engine/include/dfee/session.hpp`, `cpp_engine/include/dfee/bridge_types.hpp`, `cpp_engine/src/session.cpp`
- Modify: `cpp_engine/tests/test_session.cpp`

**Interfaces:**
- Consumes: Task 1 test helpers.
- Produces: `NativeSessionCacheState::profile_cache_entries` (`std::size_t`) and `::profile_loads` (`std::size_t`, YAML parses since the session started); private `FilmStockProfile EngineSession::film_profile(const std::string& stock_id)` and `PrintStockProfile EngineSession::print_profile(const std::string& print_stock_id)` (cached by id, re-read when the file's mtime changes; throw like the YAML loaders when the file is missing).

- [ ] **Step 1: Failing test**

Add to `test_session.cpp` (inside the anonymous namespace, before the closing `}  // namespace`):

```cpp
// A session over a private copy of the profiles, so a test can touch files.
std::filesystem::path make_profile_root() {
    const auto root = std::filesystem::temp_directory_path() / "dfee_session_profile_root";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "profiles");
    for (const char* dir : {"stocks", "print_stocks"}) {
        std::filesystem::copy(kRepoRoot / "profiles" / dir, root / "profiles" / dir,
                              std::filesystem::copy_options::recursive);
    }
    return root;
}

void test_profile_cache() {
    const auto root = make_profile_root();
    const auto file = write_scene("profile_cache", 0, 240, 160);
    {
        dfee::EngineSession session(root);
        auto request = base_request(file, "portra_400");
        expect(session.render_preview(request).ok, "first render");
        expect(session.cache_state().cache.profile_loads == 1, "portra parsed once");
        expect(session.render_preview(request).ok, "second render");
        expect(session.cache_state().cache.profile_loads == 1, "second render reuses the parsed profile");
        request.print_stock = "kodak_2383";
        expect(session.render_preview(request).ok, "render with print");
        expect(session.cache_state().cache.profile_loads == 2, "print stock parsed once");
        expect(session.cache_state().cache.profile_cache_entries == 2, "two cached profiles");
        const auto yaml = root / "profiles" / "stocks" / "portra_400.yaml";
        std::filesystem::last_write_time(yaml, std::filesystem::last_write_time(yaml) + std::chrono::seconds(5));
        expect(session.render_preview(request).ok, "render after edit");
        expect(session.cache_state().cache.profile_loads == 3, "an edited profile is re-read");
    }
    std::filesystem::remove(file);
    std::filesystem::remove_all(root);
}
```

and call it in `main` after `test_preview_golden();`:

```cpp
        test_profile_cache();
```

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests`
Expected: compile error — `profile_loads` is not a member of `NativeSessionCacheState`.

- [ ] **Step 2: Implement**

`cpp_engine/include/dfee/bridge_types.hpp`, in `struct NativeSessionCacheState` after `std::size_t cache_budget_bytes = 0;`:

```cpp
    std::size_t profile_cache_entries = 0;  // parsed stock + print profiles held
    std::size_t profile_loads = 0;          // YAML parses since the session started
```

`cpp_engine/include/dfee/session.hpp`: add `#include <unordered_map>` to the includes; in the private section after `struct CachedExportAnalysis { … };` add:

```cpp
    template <typename Profile>
    struct CachedProfile {
        std::filesystem::file_time_type mtime;
        Profile profile;
    };
```

after `void refresh_preview_cache_from_draft();` add:

```cpp
    // Parsed profiles, cached by id and re-read when the YAML's mtime changes.
    [[nodiscard]] FilmStockProfile film_profile(const std::string& stock_id);
    [[nodiscard]] PrintStockProfile print_profile(const std::string& print_stock_id);
```

and after `std::optional<CachedExportAnalysis> export_analysis_cache_;` add:

```cpp
    std::unordered_map<std::string, CachedProfile<FilmStockProfile>> film_profile_cache_;
    std::unordered_map<std::string, CachedProfile<PrintStockProfile>> print_profile_cache_;
    std::size_t profile_loads_ = 0;
```

`cpp_engine/src/session.cpp`: before `NativeSessionCacheStateResponse EngineSession::cache_state() const {` add:

```cpp
FilmStockProfile EngineSession::film_profile(const std::string& stock_id) {
    const auto path = stocks_dir_ / (stock_id + ".yaml");
    const auto mtime = std::filesystem::last_write_time(path);  // throws when missing, like the loader
    auto it = film_profile_cache_.find(stock_id);
    if (it == film_profile_cache_.end() || it->second.mtime != mtime) {
        auto profile = load_film_stock_profile(path);
        ++profile_loads_;
        it = film_profile_cache_.insert_or_assign(
            stock_id, CachedProfile<FilmStockProfile>{mtime, std::move(profile)}).first;
    }
    return it->second.profile;
}

PrintStockProfile EngineSession::print_profile(const std::string& print_stock_id) {
    const auto path = print_stocks_dir_ / (print_stock_id + ".yaml");
    const auto mtime = std::filesystem::last_write_time(path);
    auto it = print_profile_cache_.find(print_stock_id);
    if (it == print_profile_cache_.end() || it->second.mtime != mtime) {
        auto profile = load_print_stock_profile(path);
        ++profile_loads_;
        it = print_profile_cache_.insert_or_assign(
            print_stock_id, CachedProfile<PrintStockProfile>{mtime, std::move(profile)}).first;
    }
    return it->second.profile;
}

```

In `cache_state()`, before `finalize_engine_metadata(response.engine);` add:

```cpp
    response.cache.profile_cache_entries = film_profile_cache_.size() + print_profile_cache_.size();
    response.cache.profile_loads = profile_loads_;
```

Replace every profile load in `session.cpp` (four places: in `resolve_auto_grain`, `render_preview`, `export_image`):
- `load_film_stock_profile(stocks_dir_ / (request.stock + ".yaml"))` → `film_profile(request.stock)`
- `load_print_stock_profile(print_stocks_dir_ / (request.print_stock + ".yaml"))` → `print_profile(request.print_stock)`

Then `grep -n "load_film_stock_profile\|load_print_stock_profile" cpp_engine/src/session.cpp` must show only the two lines inside `film_profile` / `print_profile` (and `list_profiles`, if it parses for summaries — leave that one).

- [ ] **Step 3: Build and run**

Run: the Step 1 build command, then `cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe`
Expected: golden `24/24` (or the N noted in Task 1) and `all passed`.

- [ ] **Step 4: Commit**

```bash
git add cpp_engine/include/dfee/session.hpp cpp_engine/include/dfee/bridge_types.hpp cpp_engine/src/session.cpp cpp_engine/tests/test_session.cpp
git commit -m "feat(engine): cache parsed stock and print profiles by path and mtime"
```

---

### Task 3: Extract the preview pipelines (no output change)

**Files:**
- Modify: `cpp_engine/include/dfee/session.hpp`, `cpp_engine/src/session.cpp`

**Interfaces:**
- Consumes: `film_profile` / `print_profile` (Task 2).
- Produces (private): `struct PipelineSource { const Image* rgb_linear; bool rendered_input; const SolverInput* solver_input; const ZoneMasks* zone_masks; const SpatialMasks* spatial_masks; }`, `struct PipelineOptions { std::string stage_prefix = "render_preview"; bool include_grain = true; bool apply_geometry = true; bool dump_stages = true; }`, `std::optional<Image> run_film_pipeline(const NativePreviewRenderRequest&, const std::string& filename, const PipelineSource&, const PipelineOptions&, NativeEngineMetadata&, NativeError&)`, `Image run_neutral_pipeline(const NativePreviewRenderRequest&, const PipelineSource&, const PipelineOptions&, NativeEngineMetadata&)`. Stage timer names for the preview stay exactly as today (prefix `render_preview`).

This task is a pure refactor: the guard is the golden test from Task 1, already green, which must stay green.

- [ ] **Step 1: Confirm the guard is green before touching code**

Run: `cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe`
Expected: `all passed`.

- [ ] **Step 2: Declare the pipeline types and functions**

In `cpp_engine/include/dfee/session.hpp`, private section, after the `film_profile` / `print_profile` declarations:

```cpp
    // A cached render source: the scene-linear working image plus the analysis that
    // drives the solver and the masks at the same resolution.
    struct PipelineSource {
        const Image* rgb_linear = nullptr;
        bool rendered_input = false;
        const SolverInput* solver_input = nullptr;
        const ZoneMasks* zone_masks = nullptr;
        const SpatialMasks* spatial_masks = nullptr;
    };
    struct PipelineOptions {
        std::string stage_prefix = "render_preview";  // stage timer names: <prefix>_<stage>
        bool include_grain = true;
        bool apply_geometry = true;
        bool dump_stages = true;                       // DFEE_STAGE_DUMP debugging
    };
    // The film look: profiles -> plan -> film stages -> post -> geometry. Returns the
    // final scene-linear image, or nullopt with `error` set when a profile cannot load.
    [[nodiscard]] std::optional<Image> run_film_pipeline(
        const NativePreviewRenderRequest& request,
        const std::string& filename,
        const PipelineSource& source,
        const PipelineOptions& options,
        NativeEngineMetadata& engine,
        NativeError& error);
    // "No film": neutral scene placement, baseline develop, post and geometry.
    [[nodiscard]] Image run_neutral_pipeline(
        const NativePreviewRenderRequest& request,
        const PipelineSource& source,
        const PipelineOptions& options,
        NativeEngineMetadata& engine);
```

- [ ] **Step 3: Move the stages into the two functions**

In `cpp_engine/src/session.cpp`, add before `NativePreviewRenderResponse EngineSession::render_preview(`:

```cpp
Image EngineSession::run_neutral_pipeline(
    const NativePreviewRenderRequest& request,
    const PipelineSource& source,
    const PipelineOptions& options,
    NativeEngineMetadata& engine) {
    const std::string& p = options.stage_prefix;
    RenderPlan render_plan;
    {
        ScopedStageTimer stage(engine, p + "_neutral_scene_placement");
        render_plan = RenderPlanSolver().solve_neutral(*source.solver_input, build_solver_controls(request));
    }
    FilmRenderer renderer;
    Image rendered;
    {
        ScopedStageTimer stage(engine, p + "_neutral_pre_film");
        rendered = apply_pre_film_preview_sliders(*source.rgb_linear, request, render_plan);
        rendered = renderer.apply_pre_film_normalization(rendered, *source.zone_masks, render_plan.pre_film_normalization);
        // RAW baseline develop: give flat scene-linear RAW a camera-standard
        // tone so a no-stock preview looks like a developed photo, not linear.
        if (!source.rendered_input && is_subtractive_effect_pipeline_impl(request.effect_pipeline_version)) {
            rendered = apply_raw_baseline_develop(rendered);
        }
    }
    {
        ScopedStageTimer stage(engine, p + "_neutral_post");
        rendered = apply_post_film_light_panel(rendered, request);
        rendered = apply_post_film_color(rendered, request);
        rendered = apply_curves(rendered, request.curves);
        rendered = apply_hsl(rendered, request);
        apply_color_grading(rendered, make_color_grade_params(request, render_plan.film_response));
        rendered = renderer.apply_clarity(rendered, request.clarity);
        rendered = renderer.apply_texture(rendered, request.texture);
        rendered = renderer.apply_dehaze(rendered, request.dehaze);
        rendered = is_filmic_effect_pipeline(request.effect_pipeline_version)
            ? apply_post_bloom_filmic(rendered, request.bloom)
            : apply_post_bloom(rendered, request.bloom);
        if (options.apply_geometry) {
            rendered = apply_geometry(rendered, make_geometry_params(request));
        }
    }
    return rendered;
}

std::optional<Image> EngineSession::run_film_pipeline(
    const NativePreviewRenderRequest& request,
    const std::string& filename,
    const PipelineSource& source,
    const PipelineOptions& options,
    NativeEngineMetadata& engine,
    NativeError& error) {
    const std::string& p = options.stage_prefix;
    FilmStockProfile stock_profile;
    std::optional<PrintStockProfile> print_stock_profile;
    {
        ScopedStageTimer stage(engine, p + "_load_profiles");
        try {
            stock_profile = film_profile(request.stock);
            if (request.print_stock != "none") {
                print_stock_profile = print_profile(request.print_stock);
            }
        } catch (const std::exception& ex) {
            error = {
                .code = "PROFILE_LOAD_FAILED",
                .user_message = "The selected film or print profile could not be loaded.",
                .detail = ex.what(),
            };
            return std::nullopt;
        }
    }

    RenderPlan render_plan;
    {
        ScopedStageTimer stage(engine, p + "_solve_plan");
        SolverControls controls;
        controls.adaptation_strength = request.adaptation;
        if (request.exposure_placement == "auto_balanced") {
            controls.exposure_intent = "Auto";
        } else if (request.exposure_placement == "as_shot") {
            controls.exposure_intent = "Preserve";
        } else {
            throw std::invalid_argument("Unsupported exposure_placement: " + request.exposure_placement);
        }
        controls.grain_amount = request.grain;
        controls.grain_strength = request.grain_strength;
        controls.grain_size = request.grain_size;
        controls.grain_roughness = request.grain_roughness;
        controls.halation_amount = request.halation;
        controls.sharpness = request.sharpness;
        controls.sharpness_mask = request.sharpness_mask;
        controls.film_color = request.film_color;
        controls.highlight_color_hold = request.highlight_color_hold;
        controls.shadow_color_retention = request.shadow_color_retention;
        controls.palette_range = request.palette_range;
        controls.emulsion_color_density = request.emulsion_color_density;
        controls.film_color_density = request.film_color_density;
        controls.film_color_compression = request.film_color_compression;
        controls.highlight_rolloff = request.highlight_rolloff;
        controls.film_contrast = request.film_contrast;
        controls.crossover = request.crossover;
        controls.profile_strength = request.profile_strength;
        controls.adaptive = request.adaptive;
        controls.subtractive_pipeline = is_subtractive_effect_pipeline_impl(request.effect_pipeline_version);
        controls.characteristic_pipeline = is_characteristic_curve_pipeline_impl(request.effect_pipeline_version);
        controls.film_exposure_ev = request.film_exposure_ev;
        controls.halation_strength = request.halation_strength;
        controls.halation_threshold = request.halation_threshold;
        controls.shadow_lift = request.shadow_lift;
        controls.print_strength = request.print_strength;
        controls.print_c = request.print_c;
        controls.print_m = request.print_m;
        controls.print_y = request.print_y;
        controls.print_contrast = request.print_contrast;
        controls.print_black_point = request.print_black_point;

        RenderPlanSolver solver;
        render_plan = solver.solve(
            *source.solver_input,
            stock_profile,
            controls,
            print_stock_profile.has_value() ? &*print_stock_profile : nullptr);
        render_plan.material_effects.grain_seed = compute_stable_grain_seed(
            filename,
            request.stock,
            request.print_stock,
            render_plan.material_effects);
    }

    const auto& zone_masks = *source.zone_masks;
    const auto& spatial_masks = *source.spatial_masks;
    Image rendered;
    FilmRenderer renderer;
    FilmicHalationSource halation_source;
    {
        ScopedStageTimer stage(engine, p + "_apply_pre_film_sliders");
        if (source.rendered_input) {
            apply_rendered_input_adjustments(render_plan, request.rendered_input);
        }
        rendered = apply_pre_film_preview_sliders(*source.rgb_linear, request, render_plan);
    }
    {
        ScopedStageTimer stage(engine, p + "_film_pipeline");
        {
            ScopedStageTimer substage(engine, p + "_film_stage_pre_film_normalization");
            rendered = renderer.apply_pre_film_normalization(rendered, zone_masks, render_plan.pre_film_normalization);
            // RAW baseline develop establishes the working baseline, but RAW has
            // no baked display curve to protect. The stock must therefore retain
            // its full tone response. `rendered_input` applies to display-referred
            // inputs only (TIFF, developed RAW).
            if (!source.rendered_input && is_subtractive_effect_pipeline_impl(request.effect_pipeline_version)) {
                rendered = apply_raw_baseline_develop(rendered);
            }
            if (is_filmic_effect_pipeline(request.effect_pipeline_version)) {
                ScopedStageTimer source_stage(engine, p + "_film_stage_halation_source");
                halation_source = renderer.build_filmic_halation_source(
                    rendered,
                    render_plan.film_response.scene_exposure_shift,
                    render_plan.material_effects);
            }
            if (options.dump_stages) dump_stage(rendered, "10_baseline");
        }
        if (render_plan.stock_type == "monochrome") {
            ScopedStageTimer substage(engine, p + "_film_stage_panchromatic");
            rendered = renderer.apply_panchromatic_conversion(rendered, render_plan.film_response);
        }
        {
            ScopedStageTimer substage(engine, p + "_film_stage_tone_response");
            rendered = renderer.apply_film_tone_response(rendered, render_plan.film_response);
        }
        if (options.dump_stages) dump_stage(rendered, "20_tone");
        {
            ScopedStageTimer substage(engine, p + "_film_stage_dye_contamination");
            rendered = renderer.apply_dye_contamination(rendered, render_plan.film_response);
        }
        if (render_plan.stock_type != "monochrome") {
            const std::string color_stage = p + "_film_stage_color_response";
            ScopedStageTimer substage(engine, color_stage);
            rendered = renderer.apply_color_response_and_coupling(
                rendered,
                zone_masks,
                render_plan.film_response,
                &engine,
                color_stage);
        }
        if (options.dump_stages) dump_stage(rendered, "30_color");
        if (render_plan.stock_type != "monochrome" && is_subtractive_effect_pipeline_impl(request.effect_pipeline_version)) {
            ScopedStageTimer substage(engine, p + "_film_stage_density");
            rendered = renderer.apply_subtractive_density(rendered, render_plan.film_response);
        }
        if (render_plan.stock_type != "monochrome" && is_subtractive_effect_pipeline_impl(request.effect_pipeline_version)) {
            // Before compression, so its chroma shoulder self-limits any hue over-boost.
            ScopedStageTimer substage(engine, p + "_film_stage_hue_saturation");
            rendered = renderer.apply_hue_saturation(rendered, render_plan.film_response);
        }
        if (render_plan.stock_type != "monochrome" && is_subtractive_effect_pipeline_impl(request.effect_pipeline_version)) {
            ScopedStageTimer substage(engine, p + "_film_stage_compression");
            rendered = renderer.apply_color_compression(rendered, render_plan.film_response);
        }
        {
            ScopedStageTimer substage(engine, p + "_film_stage_acutance");
            rendered = renderer.apply_acutance_shaping(rendered, render_plan.material_effects);
        }
        {
            ScopedStageTimer substage(engine, p + "_film_stage_halation_bloom");
            rendered = is_filmic_effect_pipeline(request.effect_pipeline_version)
                ? renderer.apply_filmic_halation_bloom(rendered, halation_source, zone_masks, spatial_masks, render_plan.material_effects)
                : renderer.apply_halation_bloom(rendered, zone_masks, spatial_masks, render_plan.material_effects);
        }
        if (options.include_grain) {
            ScopedStageTimer substage(engine, p + "_film_stage_grain");
            rendered = is_filmic_effect_pipeline(request.effect_pipeline_version)
                ? renderer.apply_filmic_grain(rendered, spatial_masks, render_plan.material_effects)
                : renderer.apply_film_grain(rendered, spatial_masks, render_plan.material_effects);
        }
        if (render_plan.print_finish.has_value()) {
            ScopedStageTimer substage(engine, p + "_film_stage_print_finish");
            rendered = renderer.apply_print_finish(rendered, *render_plan.print_finish);
        }
    }
    {
        ScopedStageTimer stage(engine, p + "_post_color");
        rendered = apply_post_film_light_panel(rendered, request);
        rendered = apply_post_film_color(rendered, request);
    }
    {
        ScopedStageTimer stage(engine, p + "_post_effects");
        rendered = apply_curves(rendered, request.curves);
        rendered = apply_hsl(rendered, request);
        apply_color_grading(rendered, make_color_grade_params(request, render_plan.film_response));
        {
            ScopedStageTimer substage(engine, p + "_post_stage_clarity");
            rendered = renderer.apply_clarity(rendered, request.clarity);
        }
        {
            ScopedStageTimer substage(engine, p + "_post_stage_texture");
            rendered = renderer.apply_texture(rendered, request.texture);
        }
        {
            ScopedStageTimer substage(engine, p + "_post_stage_dehaze");
            rendered = renderer.apply_dehaze(rendered, request.dehaze);
        }
        {
            ScopedStageTimer substage(engine, p + "_post_stage_bloom");
            rendered = is_filmic_effect_pipeline(request.effect_pipeline_version)
                ? apply_post_bloom_filmic(rendered, request.bloom)
                : apply_post_bloom(rendered, request.bloom);
        }
    }
    if (options.apply_geometry) {
        ScopedStageTimer stage(engine, p + "_geometry");
        rendered = apply_geometry(rendered, make_geometry_params(request));
    }
    if (options.dump_stages) dump_stage(rendered, "40_final");
    return rendered;
}
```

(`apply_color_response_and_coupling`'s last parameter: if it is `const char*`, pass `color_stage.c_str()`; check its declaration in `renderer.hpp` and match it.)

- [ ] **Step 4: Make `render_preview` call them**

In `render_preview`, replace the neutral branch's body from `RenderPlan render_plan;` (the `render_preview_neutral_scene_placement` block) through the end of the `render_preview_neutral_post` block with:

```cpp
            const PipelineSource source{
                .rgb_linear = &preview_cache_->rgb_linear,
                .rendered_input = preview_cache_->rendered_input,
                .solver_input = &solver_input,
                .zone_masks = &zone_masks,
                .spatial_masks = &spatial_masks,
            };
            const Image rendered = run_neutral_pipeline(request, source, PipelineOptions{}, response.engine);
```

(keep the `render_preview_neutral_ensure_preview_cache`, `render_preview_neutral_analyze` and `render_preview_neutral_encode_jpeg` blocks as they are).

In the film branch, delete the `render_preview_load_profiles` block (its `FilmStockProfile`/`print_stock_profile` declarations and the try/catch), keep the `render_preview_ensure_preview_cache` block and the `render_preview_analyze` block, and replace everything from `RenderPlan render_plan;` (the `render_preview_solve_plan` block) through `dump_stage(rendered, "40_final");` with:

```cpp
        const PipelineSource source{
            .rgb_linear = &preview.rgb_linear,
            .rendered_input = preview.rendered_input,
            .solver_input = &solver_input,
            .zone_masks = &zone_masks,
            .spatial_masks = &spatial_masks,
        };
        NativeError pipeline_error;
        auto pipeline = run_film_pipeline(request, response.filename, source, PipelineOptions{}, response.engine, pipeline_error);
        if (!pipeline.has_value()) {
            response.status = "error";
            response.error = pipeline_error;
            finalize_engine_metadata(response.engine);
            return response;
        }
        const Image rendered = std::move(*pipeline);
```

(the `const auto& preview = *preview_cache_;` line stays above the analyze block; the `render_preview_encode_jpeg` block is unchanged.)

Note the one intended difference: profiles now load after the cached analysis instead of before it. Output is unaffected (analysis does not read profiles); only the order of entries in the timing list changes.

- [ ] **Step 5: Build, run the guard, run the engine suite**

Run:
```bash
cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests dfee_tests
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_tests.exe
```
Expected: `golden previews: N/24 bit-exact` with the same N as Task 1 and `all passed`; `dfee_tests` exits 0.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/include/dfee/session.hpp cpp_engine/src/session.cpp
git commit -m "refactor(engine): extract the film and neutral preview pipelines (output unchanged)"
```

---

### Task 4: `render_look_proxy`

**Files:**
- Modify: `cpp_engine/include/dfee/bridge_types.hpp`, `cpp_engine/include/dfee/session.hpp`, `cpp_engine/src/session.cpp`
- Modify: `cpp_engine/tests/test_session.cpp`
- Verify: desktop build and UI scripts

**Interfaces:**
- Consumes: `run_film_pipeline`, `run_neutral_pipeline`, `PipelineSource`, `PipelineOptions` (Task 3); `populate_preview_analysis_cache`.
- Produces:
  - `struct NativeLookProxyRequest { NativePreviewRenderRequest look; int max_edge = 256; bool include_grain = false; bool apply_geometry = true; }`
  - `struct NativeLookProxyResponse { bool ok; std::string status; int width; int height; std::vector<std::uint8_t> rgb8; NativeError error; NativeEngineMetadata engine; }` — `rgb8` is packed RGB, row-major, `width * height * 3`, sRGB-encoded exactly like the preview JPEG's input.
  - `[[nodiscard]] NativeLookProxyResponse EngineSession::render_look_proxy(const NativeLookProxyRequest& request);` — uses (and builds when needed) the photo's draft decode, preview cache and analysis; keeps one downscaled source per photo and `max_edge`; never changes the preview caches' contents; stage timers `render_look_proxy_*`.
  - `NativeSessionCacheState::proxy_source_cached` (`bool`), `proxy_width`, `proxy_height` (`int`).

- [ ] **Step 1: Failing tests**

Add to `test_session.cpp` (inside the anonymous namespace):

```cpp
cv::Mat proxy_to_bgr(const dfee::NativeLookProxyResponse& proxy) {
    expect(proxy.width > 0 && proxy.height > 0, "proxy has a size");
    expect(proxy.rgb8.size() == static_cast<std::size_t>(proxy.width) * proxy.height * 3, "proxy rgb8 is width*height*3");
    cv::Mat rgb(proxy.height, proxy.width, CV_8UC3, const_cast<std::uint8_t*>(proxy.rgb8.data()));
    cv::Mat bgr;
    cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
    return bgr;
}

// The preview, area-downscaled to the proxy's size: what a tile should look like.
cv::Mat preview_at(dfee::EngineSession& session, const dfee::NativePreviewRenderRequest& request, cv::Size size) {
    const auto preview = session.render_preview(request);
    expect(preview.ok, "preview renders: " + preview.error.detail);
    cv::Mat small;
    cv::resize(decode_jpeg(preview.jpeg_bytes), small, size, 0, 0, cv::INTER_AREA);
    return small;
}

dfee::NativeLookProxyRequest proxy_request(const dfee::NativePreviewRenderRequest& look) {
    dfee::NativeLookProxyRequest r;
    r.look = look;
    r.max_edge = 256;
    return r;
}

// Grain is off in proxies, so compare against a grain-free preview.
dfee::NativePreviewRenderRequest grain_free(dfee::NativePreviewRenderRequest r) {
    r.grain_strength = 0.0F;
    return r;
}

void test_proxy_matches_preview() {
    dfee::EngineSession session(kRepoRoot);
    const auto file = write_scene("proxy", 0, 1200, 800);
    for (const std::string stock : {"none", "portra_400", "tri_x_400", "velvia_50"}) {
        const auto look = grain_free(base_request(file, stock));
        const auto proxy = session.render_look_proxy(proxy_request(look));
        expect(proxy.ok, "proxy " + stock + ": " + proxy.error.detail);
        expect(std::max(proxy.width, proxy.height) == 256, "proxy " + stock + " fits 256");
        const auto d = compare_images(proxy_to_bgr(proxy), preview_at(session, look, {proxy.width, proxy.height}));
        std::cout << "  proxy vs preview " << stock << ": mean " << d.mean_abs << ", max " << d.max_abs << "\n";
        expect(d.mean_abs <= 6.0, "proxy " + stock + " looks like the preview (mean " + std::to_string(d.mean_abs) + ")");
    }
    std::filesystem::remove(file);
}

void test_proxy_geometry_framing() {
    dfee::EngineSession session(kRepoRoot);
    const auto file = write_scene("proxy_geometry", 1, 1200, 800);
    auto look = grain_free(base_request(file, "portra_400"));
    look.crop_x = 0.1F; look.crop_y = 0.2F; look.crop_w = 0.5F; look.crop_h = 0.7F;
    look.rotate_quadrant = 1;
    const auto proxy = session.render_look_proxy(proxy_request(look));
    expect(proxy.ok, "geometry proxy: " + proxy.error.detail);
    const auto preview = decode_jpeg(session.render_preview(look).jpeg_bytes);
    const double proxy_aspect = static_cast<double>(proxy.width) / proxy.height;
    const double preview_aspect = static_cast<double>(preview.cols) / preview.rows;
    expect(std::abs(proxy_aspect - preview_aspect) < 0.03, "proxy framing follows the crop and rotation");
    std::filesystem::remove(file);
}

void test_proxy_follows_the_photo() {
    dfee::EngineSession session(kRepoRoot);
    const auto file_a = write_scene("proxy_a", 0, 1200, 800);
    const auto file_b = write_scene("proxy_b", 2, 1200, 800);
    expect(session.render_look_proxy(proxy_request(grain_free(base_request(file_a, "portra_400")))).ok, "proxy A");
    const auto look_b = grain_free(base_request(file_b, "portra_400"));
    const auto proxy_b = session.render_look_proxy(proxy_request(look_b));
    expect(proxy_b.ok, "proxy B");
    const auto d = compare_images(proxy_to_bgr(proxy_b), preview_at(session, look_b, {proxy_b.width, proxy_b.height}));
    expect(d.mean_abs <= 6.0, "photo B's proxy is photo B (mean " + std::to_string(d.mean_abs) + ")");
    std::filesystem::remove(file_a);
    std::filesystem::remove(file_b);
}

void test_proxy_errors() {
    dfee::EngineSession session(kRepoRoot);
    const auto file = write_scene("proxy_errors", 0, 600, 400);
    const auto bad_stock = session.render_look_proxy(proxy_request(base_request(file, "no_such_stock")));
    expect(!bad_stock.ok && bad_stock.error.code == "PROFILE_LOAD_FAILED", "unknown stock is a profile error");
    auto tiny = proxy_request(base_request(file, "portra_400"));
    tiny.max_edge = 4;
    const auto too_small = session.render_look_proxy(tiny);
    expect(!too_small.ok && too_small.error.code == "PROXY_SIZE_INVALID", "max_edge below 16 is refused");
    std::filesystem::remove(file);
}

void test_proxy_speed_and_preview_untouched() {
    dfee::EngineSession session(kRepoRoot);
    const auto file = write_scene("proxy_speed", 0, 1800, 1200);
    expect(session.render_look_proxy(proxy_request(base_request(file, "portra_400"))).ok, "warm-up proxy");
    std::vector<double> ms;
    for (const auto& recipe : golden_recipes()) {
        const auto start = std::chrono::steady_clock::now();
        const auto proxy = session.render_look_proxy(proxy_request(recipe_request(file, recipe)));
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        expect(proxy.ok, "proxy " + recipe.name);
    }
    std::sort(ms.begin(), ms.end());
    const double p50 = ms[ms.size() / 2];
    std::cout << "  proxy p50 " << p50 << " ms (target <= 60), max " << ms.back() << " ms\n";
    expect(p50 <= 250.0, "proxy p50 within 250 ms");
    expect(session.cache_state().cache.proxy_source_cached, "proxy source is cached");
    std::filesystem::remove(file);
    // Proxies must not disturb previews: the golden references still match.
    run_golden(session, false);
}
```

and in `main`, after `test_profile_cache();`:

```cpp
        test_proxy_matches_preview();
        test_proxy_geometry_framing();
        test_proxy_follows_the_photo();
        test_proxy_errors();
        test_proxy_speed_and_preview_untouched();
```

Run: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests`
Expected: compile error — `render_look_proxy` is not a member of `dfee::EngineSession`.

- [ ] **Step 2: Types**

`cpp_engine/include/dfee/bridge_types.hpp`, after `struct NativePreviewRenderResponse { … };`:

```cpp
// A small render of the open photo through any recipe — a Films / Looks tile.
// Reuses the preview's analysis; grain is off unless asked for.
struct NativeLookProxyRequest {
    NativePreviewRenderRequest look;   // the recipe; look.filename picks the photo
    int max_edge = 256;
    bool include_grain = false;
    bool apply_geometry = true;
};

struct NativeLookProxyResponse {
    bool ok = false;
    std::string status;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb8;    // packed RGB, row-major, width * height * 3, sRGB
    NativeError error;
    NativeEngineMetadata engine;
};
```

and in `struct NativeSessionCacheState`, after `profile_loads`:

```cpp
    bool proxy_source_cached = false;
    int proxy_width = 0;
    int proxy_height = 0;
```

`cpp_engine/include/dfee/session.hpp`: public, after `render_preview`:

```cpp
    [[nodiscard]] NativeLookProxyResponse render_look_proxy(const NativeLookProxyRequest& request);
```

private, after `struct CachedExportAnalysis { … };`:

```cpp
    // The preview source downscaled for proxies (one photo, one size at a time).
    struct CachedProxySource {
        std::string filename;
        int max_edge = 0;
        Image rgb_linear;
        ZoneMasks zone_masks;
        SpatialMasks spatial_masks;
    };
```

and after `std::size_t profile_loads_ = 0;`:

```cpp
    std::optional<CachedProxySource> proxy_source_cache_;
```

- [ ] **Step 3: Implementation**

In `cpp_engine/src/session.cpp`, inside the file's anonymous namespace (next to `encode_preview_jpeg_bytes`), add:

```cpp
#if DFEE_HAS_OPENCV
// Area downscale for proxies (masks and the working image alike).
Image resize_image_area(const Image& source, int width, int height) {
    cv::Mat in(source.height, source.width, CV_32FC3, const_cast<float*>(source.pixels.data()));
    cv::Mat out;
    cv::resize(in, out, cv::Size(width, height), 0, 0, cv::INTER_AREA);
    Image result(width, height, 3);
    std::memcpy(result.pixels.data(), out.ptr<float>(), result.pixels.size() * sizeof(float));
    return result;
}

LuminanceImage resize_luminance_area(const LuminanceImage& source, int width, int height) {
    if (source.empty()) return {};
    cv::Mat in(source.height, source.width, CV_32FC1, const_cast<float*>(source.values.data()));
    cv::Mat out;
    cv::resize(in, out, cv::Size(width, height), 0, 0, cv::INTER_AREA);
    LuminanceImage result(width, height);
    std::memcpy(result.values.data(), out.ptr<float>(), result.values.size() * sizeof(float));
    return result;
}

// Scene-linear -> packed sRGB RGB8, the same transfer and rounding as the preview JPEG.
std::vector<std::uint8_t> to_srgb8_rgb(const Image& image) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(image.width) * image.height * 3);
    std::size_t i = 0;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                out[i++] = static_cast<std::uint8_t>(std::clamp(linear_to_srgb_channel(image.at(x, y, c)) * 255.0F, 0.0F, 255.0F));
            }
        }
    }
    return out;
}
#endif
```

(add `#include <cstring>` at the top if `std::memcpy` is not yet available.)

After `run_film_pipeline`, add:

```cpp
NativeLookProxyResponse EngineSession::render_look_proxy(const NativeLookProxyRequest& request) {
    NativeLookProxyResponse response;
    response.engine = build_engine_metadata();
#if !DFEE_HAS_OPENCV
    response.status = "unavailable";
    response.error = {
        .code = "OPENCV_UNAVAILABLE",
        .user_message = "Look previews are not available in this build.",
        .detail = "DFEE was built without OpenCV discovery.",
    };
    finalize_engine_metadata(response.engine);
    return response;
#else
    const auto fail = [&](NativeError error, std::string status = "error") {
        response.ok = false;
        response.status = std::move(status);
        response.error = std::move(error);
        finalize_engine_metadata(response.engine);
        return response;
    };
    try {
        ScopedStageTimer total(response.engine, "render_look_proxy_total");
        const std::string filename = resolve_filename(request.look.filename);
        if (filename.empty()) {
            return fail({.code = "RAW_FILENAME_MISSING", .user_message = "Select a photo before continuing.",
                         .detail = "render_look_proxy received an empty filename."});
        }
        if (request.max_edge < 16) {
            return fail({.code = "PROXY_SIZE_INVALID", .user_message = "The look preview size is too small.",
                         .detail = "max_edge must be at least 16, got " + std::to_string(request.max_edge)});
        }
        if (const auto version_error = validate_effect_pipeline_version_impl(request.look.effect_pipeline_version)) {
            return fail(*version_error);
        }
        {
            ScopedStageTimer stage(response.engine, "render_look_proxy_ensure_source");
            if (!draft_decode_cache_.has_value() || draft_decode_cache_->filename != filename ||
                !preview_cache_.has_value() || preview_cache_->filename != filename) {
                const auto decode = decode_raw({.filename = filename, .draft_mode = true});
                if (!decode.ok) return fail(decode.error, decode.status);
            }
            SolverInput solver_input;
            ZoneMasks zone_masks;
            SpatialMasks spatial_masks;
            populate_preview_analysis_cache(filename, solver_input, zone_masks, spatial_masks);
            if (!proxy_source_cache_.has_value() || proxy_source_cache_->filename != filename ||
                proxy_source_cache_->max_edge != request.max_edge) {
                const Image& full = preview_cache_->rgb_linear;
                const double scale = std::min(1.0, static_cast<double>(request.max_edge) / std::max(full.width, full.height));
                const int width = std::max(1, static_cast<int>(std::lround(full.width * scale)));
                const int height = std::max(1, static_cast<int>(std::lround(full.height * scale)));
                CachedProxySource cached;
                cached.filename = filename;
                cached.max_edge = request.max_edge;
                cached.rgb_linear = scale < 1.0 ? resize_image_area(full, width, height) : full;
                for (std::size_t z = 0; z < zone_masks.zones.size(); ++z) {
                    cached.zone_masks.zones[z] = resize_luminance_area(zone_masks.zones[z], width, height);
                }
                cached.spatial_masks.grain_receptivity_mask = resize_luminance_area(spatial_masks.grain_receptivity_mask, width, height);
                cached.spatial_masks.halation_source_mask = resize_luminance_area(spatial_masks.halation_source_mask, width, height);
                cached.spatial_masks.halation_receiver_mask = resize_luminance_area(spatial_masks.halation_receiver_mask, width, height);
                proxy_source_cache_ = std::move(cached);
            }
        }
        const auto& cached = *proxy_source_cache_;
        const PipelineSource source{
            .rgb_linear = &cached.rgb_linear,
            .rendered_input = preview_cache_->rendered_input,
            .solver_input = &preview_analysis_cache_->solver_input,
            .zone_masks = &cached.zone_masks,
            .spatial_masks = &cached.spatial_masks,
        };
        PipelineOptions options;
        options.stage_prefix = "render_look_proxy";
        options.include_grain = request.include_grain;
        options.apply_geometry = request.apply_geometry;
        options.dump_stages = false;

        Image rendered;
        if (request.look.stock == "none") {
            rendered = run_neutral_pipeline(request.look, source, options, response.engine);
        } else {
            NativeError error;
            auto out = run_film_pipeline(request.look, filename, source, options, response.engine, error);
            if (!out.has_value()) return fail(error);
            rendered = std::move(*out);
        }
        {
            ScopedStageTimer stage(response.engine, "render_look_proxy_encode_rgb8");
            response.width = rendered.width;
            response.height = rendered.height;
            response.rgb8 = to_srgb8_rgb(rendered);
        }
        response.ok = true;
        response.status = "rendered";
    } catch (const NativeException& ex) {
        return fail(ex.error());
    } catch (const std::exception& ex) {
        return fail({.code = "PROXY_RENDER_FAILED", .user_message = "The look preview could not be rendered.",
                     .detail = ex.what()});
    }
    finalize_engine_metadata(response.engine);
    return response;
#endif
}
```

(`NativeException`'s accessor: check `include/dfee/native_error.hpp`; if it is not `error()`, use the member it provides.)

In `cache_state()`, before `finalize_engine_metadata(response.engine);`:

```cpp
    if (proxy_source_cache_.has_value()) {
        response.cache.proxy_source_cached = true;
        response.cache.proxy_width = proxy_source_cache_->rgb_linear.width;
        response.cache.proxy_height = proxy_source_cache_->rgb_linear.height;
    }
```

In `clear_decode_caches()`, add `proxy_source_cache_.reset();` with the other resets.

- [ ] **Step 4: Build and run**

Run:
```bash
cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_session_tests dfee_tests
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_session_tests.exe
cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_tests.exe
```
Expected: per-stock `proxy vs preview` lines with mean ≤ 6; `proxy p50 … ms` (record the number in the ledger); the golden line again `N/24`; `all passed`; `dfee_tests` exits 0.

- [ ] **Step 5: The desktop still builds and runs**

Run: `cmake --build desktop/out/build --config Release`, then `powershell -ExecutionPolicy Bypass -File desktop/tests/run_ui.ps1 -Script desktop/tests/ui/smoke.script` and `… v2_inspector.script`.
Expected: build rc 0; both `failures=0`.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/include/dfee/bridge_types.hpp cpp_engine/include/dfee/session.hpp cpp_engine/src/session.cpp cpp_engine/tests/test_session.cpp
git commit -m "feat(engine): render_look_proxy — small look renders sharing the preview pipeline"
```
