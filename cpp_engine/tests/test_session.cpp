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

}  // namespace

int main() {
    try {
        test_preview_golden();
        test_profile_cache();
        test_proxy_matches_preview();
        test_proxy_geometry_framing();
        test_proxy_follows_the_photo();
        test_proxy_errors();
        test_proxy_speed_and_preview_untouched();
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "dfee_session_tests: all passed\n";
    return 0;
}
