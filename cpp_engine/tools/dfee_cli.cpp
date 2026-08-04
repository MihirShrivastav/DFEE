#include "dfee/analyzer.hpp"
#include "dfee/color_spaces.hpp"
#include "dfee/profile.hpp"
#include "dfee/renderer.hpp"
#include "dfee/session.hpp"
#include "dfee/solver.hpp"
#include "dfee/version.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout << "DFEE native CLI " << dfee::kEngineVersion << "\n"
              << "Usage:\n"
              << "  dfee_cli [--project-root PATH] --list-profiles\n"
              << "  dfee_cli [--project-root PATH] --cuda-status\n"
              << "  dfee_cli [--project-root PATH] --select RAW_FILENAME\n"
              << "  dfee_cli [--project-root PATH] --stock-assay STOCK_ID\n";
}

struct ColorPatch {
    const char* name;
    std::array<float, 3> linear_rgb;
};

void run_stock_assay(const std::filesystem::path& project_root, const std::string& stock_id) {
    const auto profile = dfee::load_film_stock_profile(
        project_root / "profiles" / "stocks" / (stock_id + ".yaml"));

    // Fixed linear-RGB probes, intentionally including near-neutrals, skin-like
    // warm colour, and the hue families handled by the profile's color response.
    const std::vector<ColorPatch> patches{
        {"neutral_shadow", {0.04F, 0.04F, 0.04F}},
        {"neutral_mid", {0.18F, 0.18F, 0.18F}},
        {"neutral_highlight", {0.75F, 0.75F, 0.75F}},
        {"skin_like", {0.58F, 0.27F, 0.16F}},
        {"red", {0.78F, 0.08F, 0.06F}},
        {"orange", {0.78F, 0.34F, 0.05F}},
        {"yellow", {0.78F, 0.70F, 0.06F}},
        {"green", {0.10F, 0.58F, 0.11F}},
        {"cyan", {0.06F, 0.56F, 0.68F}},
        {"blue", {0.06F, 0.18F, 0.78F}},
        {"magenta", {0.62F, 0.06F, 0.48F}},
        {"cyan_highlight", {0.52F, 0.84F, 0.98F}},
    };

    dfee::Image source(static_cast<int>(patches.size()), 1, 3);
    for (int x = 0; x < source.width; ++x) {
        for (int channel = 0; channel < 3; ++channel) {
            source.at(x, 0, channel) = patches[static_cast<std::size_t>(x)].linear_rgb[static_cast<std::size_t>(channel)];
        }
    }

    dfee::SolverInput input;
    input.tonal_distribution.tonal_skew = "normal";
    input.tonal_distribution.dynamic_range_stops = 8.0F;
    input.tonal_distribution.midtone_anchor = 0.18F;
    input.tonal_distribution.highlight_headroom = 0.25F;
    input.tonal_distribution.luma_p95 = 0.78F;
    dfee::SolverControls controls;
    controls.subtractive_pipeline = true;
    controls.adaptive = false;
    controls.grain_amount = "Off";
    controls.halation_amount = "Off";

    const dfee::RenderPlanSolver solver;
    const auto plan = solver.solve(input, profile, controls);
    const dfee::ImageStateAnalyzer analyzer;
    const auto zones = analyzer.generate_zone_masks(dfee::compute_luminance(source), 0.18F);
    const dfee::FilmRenderer renderer;
    auto rendered = renderer.apply_film_tone_response(source, plan.film_response);
    rendered = renderer.apply_dye_contamination(rendered, plan.film_response);
    rendered = renderer.apply_color_response_and_coupling(rendered, zones, plan.film_response);
    rendered = renderer.apply_subtractive_density(rendered, plan.film_response);
    rendered = renderer.apply_hue_saturation(rendered, plan.film_response);
    rendered = renderer.apply_color_compression(rendered, plan.film_response);

    const auto source_oklab = dfee::rgb_to_oklab(source);
    const auto rendered_oklab = dfee::rgb_to_oklab(rendered);
    const auto chroma = [](const dfee::Image& oklab, const int x) {
        return std::hypot(oklab.at(x, 0, 1), oklab.at(x, 0, 2));
    };
    const auto hue_degrees = [](const dfee::Image& oklab, const int x) {
        return std::atan2(oklab.at(x, 0, 2), oklab.at(x, 0, 1)) * 57.2957795F;
    };

    std::cout << "stock=" << profile.stock_id << "\n"
              << "pipeline=filmic_v3_static_color_assay\n"
              << "patch\tL_delta\tchroma_ratio\thue_delta_deg\n"
              << std::fixed << std::setprecision(4);
    for (int x = 0; x < source.width; ++x) {
        const float source_chroma = chroma(source_oklab, x);
        const float rendered_chroma = chroma(rendered_oklab, x);
        const bool hue_is_defined = source_chroma > 1.0e-5F;
        const float chroma_ratio = hue_is_defined ? rendered_chroma / source_chroma : 1.0F;
        std::cout << patches[static_cast<std::size_t>(x)].name << "\t"
                  << rendered_oklab.at(x, 0, 0) - source_oklab.at(x, 0, 0) << "\t"
                  << chroma_ratio << "\t";
        if (!hue_is_defined) {
            std::cout << "n/a\n";
            continue;
        }
        float hue_delta = hue_degrees(rendered_oklab, x) - hue_degrees(source_oklab, x);
        if (hue_delta > 180.0F) {
            hue_delta -= 360.0F;
        } else if (hue_delta < -180.0F) {
            hue_delta += 360.0F;
        }
        std::cout << hue_delta << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path project_root = DFEE_REPO_ROOT;
    std::string command;
    std::string select_filename;
    std::string assay_stock_id;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--project-root" && i + 1 < argc) {
            project_root = argv[++i];
        } else if (arg == "--list-profiles" || arg == "--cuda-status") {
            command = arg;
        } else if (arg == "--select" && i + 1 < argc) {
            command = arg;
            select_filename = argv[++i];
        } else if (arg == "--stock-assay" && i + 1 < argc) {
            command = arg;
            assay_stock_id = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage();
            return 2;
        }
    }

    if (command.empty()) {
        print_usage();
        return 2;
    }

    try {
        dfee::EngineSession session(project_root);
        if (command == "--list-profiles") {
            const auto profiles = session.list_profiles();
            std::cout << "Project root: " << session.project_root().string() << "\n";
            std::cout << "Film stocks: " << profiles.stocks.size() << "\n";
            for (const auto& stock : profiles.stocks) {
                std::cout << "  " << stock.stock_id << " | " << stock.stock_name << " | " << stock.stock_type << "\n";
            }
            std::cout << "Print stocks: " << profiles.print_stocks.size() << "\n";
            for (const auto& print : profiles.print_stocks) {
                std::cout << "  " << print.print_stock_id << " | " << print.print_stock_name << "\n";
            }
            return 0;
        }

        if (command == "--cuda-status") {
            const auto status = session.cuda_status();
            std::cout << "mode=" << status.mode << "\n";
            std::cout << "compiled=" << (status.compiled ? "true" : "false") << "\n";
            std::cout << "available=" << (status.available ? "true" : "false") << "\n";
            std::cout << "active=" << (status.active ? "true" : "false") << "\n";
            std::cout << "device_count=" << status.device_count << "\n";
            if (!status.device_name.empty()) {
                std::cout << "device_name=" << status.device_name << "\n";
            }
            if (!status.fallback_reason.empty()) {
                std::cout << "fallback_reason=" << status.fallback_reason << "\n";
            }
            return 0;
        }

        if (command == "--select") {
            const auto result = session.select_file({.filename = select_filename});
            std::cout << "ok=" << (result.ok ? "true" : "false") << "\n";
            std::cout << "status=" << result.status << "\n";
            std::cout << "filename=" << result.filename << "\n";
            std::cout << "message=" << result.message << "\n";
            return result.ok ? 0 : 1;
        }

        if (command == "--stock-assay") {
            run_stock_assay(project_root, assay_stock_id);
            return 0;
        }
    } catch (const std::exception& ex) {
        std::cerr << "dfee_cli failed: " << ex.what() << "\n";
        return 1;
    }

    return 2;
}
