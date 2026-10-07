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
