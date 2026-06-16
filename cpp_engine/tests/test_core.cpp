#include "dfee/analyzer.hpp"
#include "dfee/color_spaces.hpp"
#include "dfee/image.hpp"
#include "dfee/profile.hpp"
#include "dfee/session.hpp"
#include "dfee/version.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require_close(const float actual, const float expected, const float tolerance) {
    assert(std::fabs(actual - expected) <= tolerance);
}

void test_oklab_roundtrip() {
    dfee::Image rgb(1, 1, 3);
    rgb.pixels = {0.42F, 0.18F, 0.72F};
    const dfee::Image roundtrip = dfee::oklab_to_rgb(dfee::rgb_to_oklab(rgb));
    require_close(roundtrip.pixels[0], rgb.pixels[0], 1.0e-4F);
    require_close(roundtrip.pixels[1], rgb.pixels[1], 1.0e-4F);
    require_close(roundtrip.pixels[2], rgb.pixels[2], 1.0e-4F);
}

void test_zone_partition() {
    dfee::LuminanceImage luminance(16, 16);
    for (size_t i = 0; i < luminance.values.size(); ++i) {
        luminance.values[i] = 0.01F + static_cast<float>(i) / static_cast<float>(luminance.values.size());
    }

    const dfee::ImageStateAnalyzer analyzer;
    const auto masks = analyzer.generate_zone_masks(luminance, 0.18F);
    for (size_t i = 0; i < luminance.values.size(); ++i) {
        float sum = 0.0F;
        for (const auto& zone : masks.zones) {
            sum += zone.values[i];
        }
        require_close(sum, 1.0F, 1.0e-5F);
    }
}

void test_tonal_analysis() {
    dfee::LuminanceImage luminance(10, 10);
    for (size_t i = 0; i < luminance.values.size(); ++i) {
        luminance.values[i] = static_cast<float>(i) / 99.0F;
    }

    const dfee::ImageStateAnalyzer analyzer;
    const auto tonal = analyzer.analyze_tonal(luminance, {{"red", 0.0F}, {"green", 0.0F}, {"blue", 0.0F}});
    require_close(tonal.luma_p50, 0.5F, 1.0e-5F);
    assert(tonal.white_point_actual > tonal.black_point_actual);
}

void test_profile_loading() {
    const std::filesystem::path repo_root = DFEE_REPO_ROOT;
    const auto stock = dfee::load_film_stock_profile(repo_root / "profiles" / "stocks" / "astia_100.yaml");
    const auto print = dfee::load_print_stock_profile(repo_root / "profiles" / "print_stocks" / "kodak_2383.yaml");
    assert(!stock.stock_id.empty());
    assert(!stock.stock_name.empty());
    assert(!print.print_stock_id.empty());
    assert(!print.print_stock_name.empty());

    dfee::EngineSession session(repo_root);
    const auto listing = session.list_profiles();
    assert(!listing.stocks.empty());
    assert(!listing.print_stocks.empty());
    assert(listing.engine.engine_version == dfee::kEngineVersion);
    assert(!listing.engine.timings.empty());
    assert(listing.engine.metadata_json.find("list_profiles_total") != std::string::npos);

    const auto selected = session.select_file({.filename = "DSC00246.ARW"});
    assert(selected.ok);
    assert(!selected.engine.timings.empty());
    assert(selected.engine.metadata_json.find("select_file_total") != std::string::npos);
    const auto initial_cache = session.cache_state();
    assert(initial_cache.ok);
    assert(initial_cache.cache.selected_filename == "DSC00246.ARW");
    assert(!initial_cache.cache.draft_decode_cached);
    assert(!initial_cache.cache.preview_cached);
    assert(!initial_cache.cache.full_decode_cached);

    const auto metadata = session.read_raw_metadata({.filename = "DSC00246.ARW"});
#if DFEE_HAS_LIBRAW
    assert(metadata.ok);
    assert(metadata.metadata.image_width > 0);
    assert(metadata.metadata.raw_width > 0);
    assert(!metadata.metadata.metadata_json.empty());

    const auto draft_decode = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = true});
    assert(draft_decode.ok);
    assert(draft_decode.metadata.image_width == metadata.metadata.image_width);
    assert(draft_decode.metadata.image_height == metadata.metadata.image_height);
    const auto draft_cache = session.cache_state();
    assert(draft_cache.cache.draft_decode_cached);
    assert(draft_cache.cache.preview_cached);
    assert(draft_cache.cache.draft_width == draft_decode.summary.image_width);
    assert(draft_cache.cache.draft_height == draft_decode.summary.image_height);
    assert(draft_cache.cache.preview_width <= draft_cache.cache.draft_width);
    assert(draft_cache.cache.preview_height <= draft_cache.cache.draft_height);

    const auto cached_draft = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = true});
    assert(cached_draft.ok);
    assert(cached_draft.status == "cached");

    const auto full_decode = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = false});
    assert(full_decode.ok);
    const auto full_cache = session.cache_state();
    assert(full_cache.cache.full_decode_cached);
    assert(full_cache.cache.full_width == full_decode.summary.image_width);
    assert(full_cache.cache.full_height == full_decode.summary.image_height);
#else
    assert(!metadata.ok);
    assert(metadata.error.code == "LIBRAW_UNAVAILABLE");
    const auto failed_decode = session.decode_raw({.filename = "DSC00246.ARW", .draft_mode = true});
    assert(!failed_decode.ok);
    assert(failed_decode.error.code == "LIBRAW_UNAVAILABLE");
#endif
}

}  // namespace

int main() {
    try {
        test_oklab_roundtrip();
        test_zone_partition();
        test_tonal_analysis();
        test_profile_loading();
        std::cout << "dfee_tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "dfee_tests exception: " << ex.what() << "\n";
        return 1;
    }
}
