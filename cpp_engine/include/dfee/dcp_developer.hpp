// DNG-camera-profile RAW developer (native RAW programme, phase P2).
//
// Reproduces Lightroom/Camera Raw's baseline rendering of a RAW with an Adobe DCP
// profile, following the reference pipeline in Adobe's DNG SDK (dng_render.cpp,
// dng_color_spec.cpp, dng_reference.cpp): camera -> ProPhoto via the white-balanced
// forward/colour matrices -> HueSatMap -> exposure ramp -> LookTable -> RGB-preserving
// tone curve (profile curve, else the ACR3 default) -> sRGB.
//
// Portions derived from the Adobe DNG SDK. Copyright 2006-2012 Adobe Systems
// Incorporated. Used under the Adobe DNG SDK license. DCP files themselves are Adobe
// data: they are read from the user's own Lightroom / DNG Converter installation and
// are never bundled.
//
// Measured with experiments/parity_score.cpp against edit-free Lightroom Adobe Color
// exports: mean CIEDE2000 1.40 over 35 pairs, 1.39 on 15 held-out pairs (2026-09-27).

#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

namespace dfee::dcp {

using Mat3 = std::array<double, 9>;  // row-major 3x3

struct HueSatTable {
    int hue = 0, sat = 0, val = 0;  // divisions
    std::vector<float> data;        // (hueShiftDeg, satScale, valScale) per entry
    [[nodiscard]] bool valid() const { return !data.empty() && hue > 0 && sat > 1; }
};

struct Profile {
    std::string name;
    std::string camera;  // UniqueCameraModel
    int illuminant1 = 0, illuminant2 = 0;
    Mat3 color1{}, color2{}, forward1{}, forward2{};
    bool has_color2 = false, has_forward1 = false, has_forward2 = false;
    HueSatTable hue_sat1, hue_sat2, look;
    int hue_sat_encoding = 0, look_encoding = 0;  // 0 linear, 1 sRGB
    std::vector<std::pair<float, float>> tone_curve;  // empty => ACR3 default
    float baseline_exposure_offset = 0.0F;
};

[[nodiscard]] std::optional<Profile> load_dcp(const std::filesystem::path& path);

// Finds the installed Adobe Standard DCP for a camera. Tries the DNG
// UniqueCameraModel first, then make + model, then a normalised fuzzy match.
// `version` selects a revised profile ("v2" -> "<camera> Adobe Standard v2.dcp"),
// falling back to the base profile when that version is not installed.
[[nodiscard]] std::optional<std::filesystem::path> find_adobe_standard(const std::string& unique_camera_model,
                                                                       const std::string& make,
                                                                       const std::string& model,
                                                                       std::string* matched_name = nullptr,
                                                                       const std::string& version = {});

struct RawInput {
    cv::Mat camera;                    // CV_32FC3 camera RGB in R,G,B order, NOT white balanced, [0,1]
    std::array<double, 3> neutral{};   // as-shot camera neutral (1 / white-balance multipliers)
    float baseline_exposure = 0.0F;    // DNG BaselineExposure tag (0 for proprietary RAWs)
    bool has_baseline_exposure = false;
    std::string make, model, unique_camera_model;
    // Fallback when no Adobe DCP is installed for this camera: the DNG's own embedded
    // colour/forward matrices, else LibRaw's Adobe-derived XYZ->camera matrix (D65).
    std::optional<Profile> fallback_profile;
    std::string fallback_source;       // "dng-embedded" | "libraw-matrix"
};

[[nodiscard]] std::optional<RawInput> decode_raw(const std::filesystem::path& path, bool half_size,
                                                 std::string& error);

// A Camera Raw "look" profile (e.g. Adobe Raw/Adobe Color.xmp): it names the camera's
// Adobe Standard DCP as its base and carries its own look table plus a PV2012 point
// curve (0..255 input/output pairs). Measured against Lightroom Adobe Color exports
// (2026-09-27, 50 pairs): the preset table is applied ON TOP of the DCP's own look
// table, and the point curve is not applied as a plain curve (adding it darkens
// shadows ~1.5 L*, in every encoding and order tried). Best: --preset-parts stack-look.
struct LookPreset {
    std::string name;
    HueSatTable look;
    int look_encoding = 0;
    std::vector<std::pair<float, float>> point_curve;  // master RGB curve, 0..255
};

// Decodes the XMP look table (DNG SDK big-table encoding: Z85-like text -> zlib ->
// look-table stream) and the master ToneCurvePV2012.
[[nodiscard]] std::optional<LookPreset> load_look_preset(const std::filesystem::path& xmp_path, std::string& error);

enum class CurveSpace { Srgb, Gamma22, Linear };

struct DevelopOptions {
    float exposure_bias = 0.0F;  // extra stops on top of the baseline exposure
    bool shadows = true;         // Camera Raw's default 0.5% black ("Shadows 5")
    bool hue_sat = true;
    bool look = true;
    const LookPreset* preset = nullptr;          // look preset (Adobe Color); see stack_looks
    CurveSpace curve_space = CurveSpace::Srgb;   // encoding the point curve is applied in
    bool preset_look = true;           // use the preset's look table (else keep the DCP's)
    bool stack_looks = false;          // apply the DCP look, then the preset look (matches Lightroom)
    bool preset_curve = true;          // apply the preset's point curve
    bool curve_rgb_preserving = false; // point curve via RefBaselineRGBTone instead of per channel
    bool curve_before_tone = false;    // apply the point curve before the base (ACR3/profile) tone
};

// Returns a BGR CV_32FC3 image, sRGB-encoded, in [0,1].
[[nodiscard]] cv::Mat develop(const RawInput& raw, const Profile& profile, const DevelopOptions& options,
                              std::string* log = nullptr);

// ---- engine entry point ----------------------------------------------------------

// Baseline exposure for a proprietary RAW whose camera is not in the exposure table
// (median of the corpus fits in profiles/raw/baseline_exposure.yaml).
inline constexpr float kDefaultProprietaryBaselineExposure = 0.28F;

struct DeveloperSettings {
    std::filesystem::path exposure_table;  // profiles/raw/baseline_exposure.yaml; empty => built-in default
    bool adobe_color = true;               // apply Lightroom's Adobe Color look when installed
};

struct DevelopedRaw {
    cv::Mat srgb;                // CV_32FC3 BGR, sRGB-encoded, [0,1] -- like a Lightroom sRGB export
    std::string profile_source;  // e.g. "Nikon Z 7 Adobe Standard v2 + Adobe Color", "libraw-matrix"
    float exposure_ev = 0.0F;    // total baseline exposure applied
};

// Develops a RAW file to Lightroom's default Adobe Color render: the installed Adobe
// Standard DCP (revision v2 when present) when available, else the file's own matrices.
[[nodiscard]] std::optional<DevelopedRaw> develop_raw_file(const std::filesystem::path& path,
                                                           const DeveloperSettings& settings, bool half_size,
                                                           std::string& error);

}  // namespace dfee::dcp
