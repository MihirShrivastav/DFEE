#include "dfee/session.hpp"

#include "dfee/analyzer.hpp"
#include "dfee/bias.hpp"
#include "dfee/bridge_utils.hpp"
#include "dfee/color_spaces.hpp"
#include "dfee/native_error.hpp"
#include "dfee/renderer.hpp"
#include "dfee/raw_metadata.hpp"
#include "dfee/solver.hpp"
#include "dfee/version.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <optional>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

#if DFEE_HAS_OPENCV
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace dfee {
namespace {

NativeEngineMetadata build_engine_metadata() {
    NativeEngineMetadata metadata;
    metadata.engine_version = kEngineVersion;
    metadata.cuda_status = query_cuda_status();
#if DFEE_HAS_LIBRAW
    metadata.libraw_enabled = true;
    metadata.libraw_version = "enabled";
#else
    metadata.libraw_enabled = false;
#endif
    return metadata;
}

void finalize_engine_metadata(NativeEngineMetadata& metadata) {
    metadata.metadata_json = serialize_native_engine_metadata_json(metadata);
}

std::uint32_t fnv1a_append_u32(std::uint32_t state, const std::uint32_t value) {
    constexpr std::uint32_t kPrime = 16777619U;
    state ^= (value & 0xFFU);
    state *= kPrime;
    state ^= ((value >> 8U) & 0xFFU);
    state *= kPrime;
    state ^= ((value >> 16U) & 0xFFU);
    state *= kPrime;
    state ^= ((value >> 24U) & 0xFFU);
    state *= kPrime;
    return state;
}

std::uint32_t fnv1a_append_string(std::uint32_t state, const std::string& value) {
    constexpr std::uint32_t kPrime = 16777619U;
    for (const unsigned char ch : value) {
        state ^= static_cast<std::uint32_t>(ch);
        state *= kPrime;
    }
    return state;
}

std::uint32_t quantize_hash_float(const float value) {
    return static_cast<std::uint32_t>(std::lround(value * 1000.0F));
}

std::uint32_t compute_stable_grain_seed(
    const std::string& filename,
    const std::string& stock,
    const std::string& print_stock,
    const MaterialEffectsPlan& effects) {
    std::uint32_t hash = 2166136261U;
    hash = fnv1a_append_string(hash, filename);
    hash = fnv1a_append_string(hash, stock);
    hash = fnv1a_append_string(hash, print_stock);
    hash = fnv1a_append_u32(hash, quantize_hash_float(effects.grain_size));
    hash = fnv1a_append_u32(hash, quantize_hash_float(effects.grain_roughness));
    hash = fnv1a_append_u32(hash, quantize_hash_float(effects.grain_chroma_strength));
    hash = fnv1a_append_u32(hash, static_cast<std::uint32_t>(effects.grain_strength > 0.0F ? 1U : 0U));
    return hash == 0U ? 1U : hash;
}

Image resize_image_to_max_edge(const Image& source, const int max_edge) {
    if (source.empty() || max_edge <= 0) {
        return source;
    }

    const int current_max = std::max(source.width, source.height);
    if (current_max <= max_edge) {
        return source;
    }

    const float scale = static_cast<float>(max_edge) / static_cast<float>(current_max);
    const int target_width = std::max(1, static_cast<int>(source.width * scale));
    const int target_height = std::max(1, static_cast<int>(source.height * scale));
#if DFEE_HAS_OPENCV
    cv::Mat source_mat(source.height, source.width, CV_32FC3);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            auto& pixel = source_mat.at<cv::Vec3f>(y, x);
            pixel[0] = source.at(x, y, 0);
            pixel[1] = source.at(x, y, 1);
            pixel[2] = source.at(x, y, 2);
        }
    }

    cv::Mat resized_mat;
    cv::resize(source_mat, resized_mat, cv::Size(target_width, target_height), 0.0, 0.0, cv::INTER_AREA);
    Image resized(target_width, target_height, source.channels);
    for (int y = 0; y < target_height; ++y) {
        for (int x = 0; x < target_width; ++x) {
            const auto& pixel = resized_mat.at<cv::Vec3f>(y, x);
            resized.at(x, y, 0) = pixel[0];
            resized.at(x, y, 1) = pixel[1];
            resized.at(x, y, 2) = pixel[2];
        }
    }
    return resized;
#else
    Image resized(target_width, target_height, source.channels);
    for (int y = 0; y < target_height; ++y) {
        const int source_y = std::min(source.height - 1, static_cast<int>(static_cast<float>(y) / scale));
        for (int x = 0; x < target_width; ++x) {
            const int source_x = std::min(source.width - 1, static_cast<int>(static_cast<float>(x) / scale));
            for (int channel = 0; channel < source.channels; ++channel) {
                resized.at(x, y, channel) = source.at(source_x, source_y, channel);
            }
        }
    }
    return resized;
#endif
}

float linear_to_srgb_channel(const float value) {
    const float clamped = clamp01(value);
    if (clamped <= 0.0031308F) {
        return 12.92F * clamped;
    }
    return 1.055F * std::pow(clamped, 1.0F / 2.4F) - 0.055F;
}

float gamma_encode_22(const float value) {
    return std::pow(std::max(value, 0.0F), 1.0F / 2.2F);
}

float gamma_decode_22(const float value) {
    return std::pow(clamp01(value), 2.2F);
}

#if DFEE_HAS_OPENCV
float sample_lut(const std::vector<float>& lut, const float normalized) {
    if (lut.empty()) {
        return normalized;
    }
    const float clamped = std::clamp(normalized, 0.0F, 1.0F);
    const float scaled = clamped * static_cast<float>(lut.size() - 1U);
    const std::size_t lower = static_cast<std::size_t>(scaled);
    const std::size_t upper = std::min(lower + 1U, lut.size() - 1U);
    const float mix = scaled - static_cast<float>(lower);
    return lut[lower] * (1.0F - mix) + lut[upper] * mix;
}

cv::Mat image_to_cv32fc3(const Image& image) {
    cv::Mat mat(image.height, image.width, CV_32FC3);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            auto& pixel = mat.at<cv::Vec3f>(y, x);
            pixel[0] = image.at(x, y, 0);
            pixel[1] = image.at(x, y, 1);
            pixel[2] = image.at(x, y, 2);
        }
    }
    return mat;
}

Image cv32fc3_to_image(const cv::Mat& mat) {
    Image image(mat.cols, mat.rows, 3);
    for (int y = 0; y < mat.rows; ++y) {
        for (int x = 0; x < mat.cols; ++x) {
            const auto& pixel = mat.at<cv::Vec3f>(y, x);
            image.at(x, y, 0) = pixel[0];
            image.at(x, y, 1) = pixel[1];
            image.at(x, y, 2) = pixel[2];
        }
    }
    return image;
}

cv::Mat gamma_encode_image_22(const Image& image) {
    cv::Mat mat(image.height, image.width, CV_32FC3);
    const float* src = image.pixels.data();
    for (int y = 0; y < image.height; ++y) {
        auto* row = mat.ptr<cv::Vec3f>(y);
        for (int x = 0; x < image.width; ++x) {
            auto& pixel = row[x];
            pixel[0] = gamma_encode_22(src[0]);
            pixel[1] = gamma_encode_22(src[1]);
            pixel[2] = gamma_encode_22(src[2]);
            src += 3;
        }
    }
    return mat;
}

Image gamma_decode_mat_22(const cv::Mat& mat) {
    Image image(mat.cols, mat.rows, 3);
    float* dst = image.pixels.data();
    for (int y = 0; y < mat.rows; ++y) {
        const auto* row = mat.ptr<cv::Vec3f>(y);
        for (int x = 0; x < mat.cols; ++x) {
            const auto& pixel = row[x];
            dst[0] = gamma_decode_22(pixel[0]);
            dst[1] = gamma_decode_22(pixel[1]);
            dst[2] = gamma_decode_22(pixel[2]);
            dst += 3;
        }
    }
    return image;
}

std::vector<std::pair<float, float>> parse_curve_points(const std::string& curves_text) {
    std::vector<float> numbers;
    numbers.reserve(16);
    std::string token;
    token.reserve(32);

    auto flush_token = [&]() {
        if (token.empty()) {
            return;
        }
        numbers.push_back(std::stof(token));
        token.clear();
    };

    for (const char ch : curves_text) {
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E') {
            token.push_back(ch);
        } else {
            flush_token();
        }
    }
    flush_token();

    std::vector<std::pair<float, float>> points;
    if ((numbers.size() % 2U) != 0U) {
        return points;
    }
    for (std::size_t i = 0; i + 1 < numbers.size(); i += 2U) {
        points.emplace_back(numbers[i], numbers[i + 1U]);
    }
    std::ranges::sort(points, [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    return points;
}

bool is_identity_curve(const std::vector<std::pair<float, float>>& points) {
    if (points.size() < 2U) {
        return true;
    }
    for (const auto& [x, y] : points) {
        if (std::fabs(x - y) > 1.0e-3F) {
            return false;
        }
    }
    return true;
}

std::vector<float> build_monotone_cubic_lut(const std::vector<std::pair<float, float>>& points, const int size = 4096) {
    if (points.empty()) {
        return {};
    }
    if (points.size() == 1U) {
        return std::vector<float>(static_cast<std::size_t>(size), clamp01(points.front().second));
    }

    std::vector<double> xs;
    std::vector<double> ys;
    xs.reserve(points.size());
    ys.reserve(points.size());
    for (const auto& [x, y] : points) {
        xs.push_back(static_cast<double>(x));
        ys.push_back(static_cast<double>(y));
    }

    if (points.size() == 2U) {
        std::vector<float> lut(static_cast<std::size_t>(size), 0.0F);
        for (int i = 0; i < size; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(size - 1);
            const double denom = std::max(1.0e-10, xs[1] - xs[0]);
            const double w = std::clamp((t - xs[0]) / denom, 0.0, 1.0);
            lut[static_cast<std::size_t>(i)] = clamp01(static_cast<float>(ys[0] * (1.0 - w) + ys[1] * w));
        }
        return lut;
    }

    const std::size_t n = points.size();
    std::vector<double> h(n - 1U, 0.0);
    std::vector<double> delta(n - 1U, 0.0);
    for (std::size_t i = 0; i + 1U < n; ++i) {
        h[i] = xs[i + 1U] - xs[i];
        delta[i] = (ys[i + 1U] - ys[i]) / std::max(h[i], 1.0e-10);
    }

    std::vector<double> m(n, 0.0);
    m.front() = delta.front();
    m.back() = delta.back();
    for (std::size_t i = 1; i + 1U < n; ++i) {
        m[i] = (delta[i - 1U] + delta[i]) / 2.0;
    }

    for (std::size_t i = 0; i + 1U < n; ++i) {
        if (std::fabs(delta[i]) < 1.0e-12) {
            m[i] = 0.0;
            m[i + 1U] = 0.0;
            continue;
        }
        const double alpha = m[i] / delta[i];
        const double beta = m[i + 1U] / delta[i];
        const double r2 = alpha * alpha + beta * beta;
        if (r2 > 9.0) {
            const double tau = 3.0 / std::sqrt(r2);
            m[i] = tau * alpha * delta[i];
            m[i + 1U] = tau * beta * delta[i];
        }
    }

    std::vector<float> lut(static_cast<std::size_t>(size), 0.0F);
    const double x0 = xs.front();
    const double x1 = xs.back();
    const double span = std::max(1.0e-10, x1 - x0);
    for (int i = 0; i < size; ++i) {
        const double t_norm = static_cast<double>(i) / static_cast<double>(size - 1);
        const double x_eval = x0 + t_norm * span;
        auto upper = std::upper_bound(xs.begin(), xs.end(), x_eval);
        std::size_t seg = upper == xs.begin() ? 0U : static_cast<std::size_t>(std::distance(xs.begin(), upper) - 1);
        if (seg >= n - 1U) {
            seg = n - 2U;
        }
        const double seg_h = std::max(h[seg], 1.0e-10);
        const double t = std::clamp((x_eval - xs[seg]) / seg_h, 0.0, 1.0);
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
        const double h10 = t3 - 2.0 * t2 + t;
        const double h01 = -2.0 * t3 + 3.0 * t2;
        const double h11 = t3 - t2;
        const double y_eval =
            h00 * ys[seg] +
            h10 * seg_h * m[seg] +
            h01 * ys[seg + 1U] +
            h11 * seg_h * m[seg + 1U];
        lut[static_cast<std::size_t>(i)] = clamp01(static_cast<float>(y_eval));
    }
    return lut;
}

cv::Mat gaussian_blur_downsampled_rgb(
    const cv::Mat& source,
    const int radius,
    const int max_working_edge) {
    const int kernel = std::max(3, radius * 2 + 1);
    const int current_max = std::max(source.cols, source.rows);
    if (current_max <= max_working_edge) {
        cv::Mat blurred;
        cv::GaussianBlur(source, blurred, cv::Size(kernel, kernel), static_cast<double>(radius) / 2.5);
        return blurred;
    }

    const float scale = static_cast<float>(max_working_edge) / static_cast<float>(current_max);
    const int target_width = std::max(1, static_cast<int>(std::lround(static_cast<float>(source.cols) * scale)));
    const int target_height = std::max(1, static_cast<int>(std::lround(static_cast<float>(source.rows) * scale)));

    cv::Mat reduced;
    cv::resize(source, reduced, cv::Size(target_width, target_height), 0.0, 0.0, cv::INTER_AREA);

    int scaled_radius = std::max(1, static_cast<int>(std::lround(static_cast<float>(radius) * scale)));
    int scaled_kernel = std::max(3, scaled_radius * 2 + 1);
    if ((scaled_kernel % 2) == 0) {
        ++scaled_kernel;
    }

    cv::Mat reduced_blur;
    cv::GaussianBlur(reduced, reduced_blur, cv::Size(scaled_kernel, scaled_kernel), std::max(0.5, static_cast<double>(scaled_radius) / 2.5));

    cv::Mat restored;
    cv::resize(reduced_blur, restored, cv::Size(source.cols, source.rows), 0.0, 0.0, cv::INTER_LINEAR);
    return restored;
}

DecodedRawChannelMasks resize_clipping_masks(
    const DecodedRawChannelMasks& source_masks,
    const int source_width,
    const int source_height,
    const int target_width,
    const int target_height) {
    if (source_width == target_width && source_height == target_height) {
        return source_masks;
    }

    const auto resize_one = [&](const std::vector<std::uint8_t>& source) {
        std::vector<std::uint8_t> resized(static_cast<std::size_t>(target_width) * static_cast<std::size_t>(target_height), 0U);
        for (int y = 0; y < target_height; ++y) {
            const int src_y0 = std::clamp(static_cast<int>(std::floor(static_cast<double>(y) * source_height / target_height)), 0, source_height - 1);
            const int src_y1 = std::clamp(static_cast<int>(std::ceil(static_cast<double>(y + 1) * source_height / target_height)) - 1, src_y0, source_height - 1);
            for (int x = 0; x < target_width; ++x) {
                const int src_x0 = std::clamp(static_cast<int>(std::floor(static_cast<double>(x) * source_width / target_width)), 0, source_width - 1);
                const int src_x1 = std::clamp(static_cast<int>(std::ceil(static_cast<double>(x + 1) * source_width / target_width)) - 1, src_x0, source_width - 1);
                std::uint8_t value = 0U;
                for (int src_y = src_y0; src_y <= src_y1 && value == 0U; ++src_y) {
                    for (int src_x = src_x0; src_x <= src_x1; ++src_x) {
                        if (source[static_cast<std::size_t>(src_y) * static_cast<std::size_t>(source_width) + static_cast<std::size_t>(src_x)] != 0U) {
                            value = 1U;
                            break;
                        }
                    }
                }
                resized[static_cast<std::size_t>(y) * static_cast<std::size_t>(target_width) + static_cast<std::size_t>(x)] = value;
            }
        }
        return resized;
    };

    return {
        .red = resize_one(source_masks.red),
        .green = resize_one(source_masks.green),
        .blue = resize_one(source_masks.blue),
    };
}

LuminanceImage resize_luminance_image(
    const LuminanceImage& source,
    const int target_width,
    const int target_height,
    const int interpolation = cv::INTER_LINEAR) {
    if (source.width == target_width && source.height == target_height) {
        return source;
    }
    cv::Mat source_mat(source.height, source.width, CV_32F);
    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            source_mat.at<float>(y, x) = source.at(x, y);
        }
    }
    cv::Mat resized_mat;
    cv::resize(source_mat, resized_mat, cv::Size(target_width, target_height), 0.0, 0.0, interpolation);
    LuminanceImage resized(target_width, target_height);
    for (int y = 0; y < target_height; ++y) {
        for (int x = 0; x < target_width; ++x) {
            resized.at(x, y) = resized_mat.at<float>(y, x);
        }
    }
    return resized;
}

ZoneMasks resize_zone_masks(
    const ZoneMasks& source,
    const int target_width,
    const int target_height) {
    ZoneMasks resized;
    for (std::size_t i = 0; i < source.zones.size(); ++i) {
        resized.zones[i] = resize_luminance_image(source.zones[i], target_width, target_height, cv::INTER_LINEAR);
    }
    return resized;
}

SpatialMasks resize_spatial_masks(
    const SpatialMasks& source,
    const int target_width,
    const int target_height) {
    return {
        .grain_receptivity_mask = resize_luminance_image(source.grain_receptivity_mask, target_width, target_height, cv::INTER_LINEAR),
        .halation_source_mask = resize_luminance_image(source.halation_source_mask, target_width, target_height, cv::INTER_LINEAR),
        .halation_receiver_mask = resize_luminance_image(source.halation_receiver_mask, target_width, target_height, cv::INTER_LINEAR),
    };
}

NativeRawPreviewResponse encode_preview_jpeg_bytes(const Image& preview_rgb, const std::string& filename) {
    NativeRawPreviewResponse response;
    response.filename = filename;

    cv::Mat rgb_u8(preview_rgb.height, preview_rgb.width, CV_8UC3);
    for (int y = 0; y < preview_rgb.height; ++y) {
        for (int x = 0; x < preview_rgb.width; ++x) {
            const float r = linear_to_srgb_channel(preview_rgb.at(x, y, 0));
            const float g = linear_to_srgb_channel(preview_rgb.at(x, y, 1));
            const float b = linear_to_srgb_channel(preview_rgb.at(x, y, 2));
            auto& pixel = rgb_u8.at<cv::Vec3b>(y, x);
            pixel[0] = static_cast<std::uint8_t>(std::clamp(b * 255.0F, 0.0F, 255.0F));
            pixel[1] = static_cast<std::uint8_t>(std::clamp(g * 255.0F, 0.0F, 255.0F));
            pixel[2] = static_cast<std::uint8_t>(std::clamp(r * 255.0F, 0.0F, 255.0F));
        }
    }

    std::vector<std::uint8_t> jpeg_bytes;
    if (!cv::imencode(".jpg", rgb_u8, jpeg_bytes)) {
        response.status = "error";
        response.error = {
            .code = "PREVIEW_RENDER_ENCODE_FAILED",
            .user_message = "The native preview render could not be encoded as JPEG.",
            .detail = "cv::imencode returned false for preview render: " + filename,
        };
        return response;
    }

    response.ok = true;
    response.status = "loaded";
    response.content_type = "image/jpeg";
    response.jpeg_bytes = std::move(jpeg_bytes);
    return response;
}

struct NativeRenderWorkResult {
    SolverInput solver_input;
    ZoneMasks zone_masks;
    SpatialMasks spatial_masks;
    RenderPlan render_plan;
};

SolverControls build_solver_controls(const NativePreviewRenderRequest& request) {
    SolverControls controls;
    controls.adaptation_strength = request.adaptation;
    controls.exposure_intent = "Preserve";
    controls.grain_amount = request.grain;
    controls.grain_strength = request.grain_strength;
    controls.grain_size = request.grain_size;
    controls.grain_roughness = request.grain_roughness;
    controls.halation_amount = request.halation;
    controls.sharpness = request.sharpness;
    controls.sharpness_mask = request.sharpness_mask;
    controls.film_color = request.film_color;
    controls.print_strength = request.print_strength;
    controls.print_c = request.print_c;
    controls.print_m = request.print_m;
    controls.print_y = request.print_y;
    controls.print_contrast = request.print_contrast;
    controls.print_black_point = request.print_black_point;
    return controls;
}

std::string escape_json_string(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8U);
    for (const char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

std::string json_number(const float value, const int precision = 6) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string json_float_array3(const std::array<float, 3>& values) {
    std::ostringstream out;
    out << "[" << json_number(values[0]) << "," << json_number(values[1]) << "," << json_number(values[2]) << "]";
    return out.str();
}

std::string json_string_array(const std::vector<std::string>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << escape_json_string(values[i]) << "\"";
    }
    out << "]";
    return out.str();
}

std::string json_float_map(const std::unordered_map<std::string, float>& values) {
    std::vector<std::pair<std::string, float>> ordered(values.begin(), values.end());
    std::ranges::sort(ordered, [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    std::ostringstream out;
    out << "{";
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        if (i > 0U) {
            out << ",";
        }
        out << "\"" << escape_json_string(ordered[i].first) << "\":" << json_number(ordered[i].second);
    }
    out << "}";
    return out.str();
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        throw std::runtime_error("Unable to open file for writing: " + path.string());
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out.good()) {
        throw std::runtime_error("Failed while writing file: " + path.string());
    }
}

bool export_trace_enabled() {
    const char* value = std::getenv("DFEE_TRACE_EXPORT");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void append_export_trace(const std::filesystem::path& project_root, const std::string& line) {
    if (!export_trace_enabled()) {
        return;
    }
    const auto path = project_root / "cpp_engine" / "out" / "export_trace.log";
    std::ofstream out(path, std::ios::app | std::ios::binary);
    if (!out.is_open()) {
        return;
    }
    out << line << "\n";
}

Image apply_pre_film_preview_sliders(
    const Image& rgb_input,
    const NativePreviewRenderRequest& request,
    RenderPlan& plan) {
    Image adjusted = rgb_input;

    plan.pre_film_normalization.exposure_compensation_stops += request.exposure;
    const float contrast_value = request.contrast + plan.pre_film_normalization.contrast_compensation;
    const float highlights_value = request.highlights + plan.pre_film_normalization.highlights_compensation;
    const float shadows_value = request.shadows + plan.pre_film_normalization.shadows_compensation;
    const float whites_value = request.whites + plan.pre_film_normalization.whites_compensation;
    const float blacks_value = request.blacks + plan.pre_film_normalization.blacks_compensation;
    const float midtones_value = request.midtones + plan.pre_film_normalization.midtones_compensation;

    cv::Mat gamma_rgb = gamma_encode_image_22(adjusted);

    if (contrast_value != 0.0F) {
        const float k = std::clamp(contrast_value / 100.0F, -1.0F, 1.0F) * 2.5F;
        if (std::fabs(k) > 0.02F) {
            const float denom = std::tanh(k * 0.5F);
            if (std::fabs(denom) > 1.0e-6F) {
                for (int y = 0; y < gamma_rgb.rows; ++y) {
                    auto* row = gamma_rgb.ptr<cv::Vec3f>(y);
                    for (int x = 0; x < gamma_rgb.cols; ++x) {
                        auto& pixel = row[x];
                        for (int c = 0; c < 3; ++c) {
                            pixel[c] = clamp01(0.5F + std::tanh(k * (pixel[c] - 0.5F)) / (2.0F * denom));
                        }
                    }
                }
            }
        }
    }

    const float highlights_amount = std::clamp(highlights_value / 100.0F, -1.0F, 1.0F) * 0.45F;
    const float shadows_amount = std::clamp(shadows_value / 100.0F, -1.0F, 1.0F) * 0.50F;
    const float whites_amount = std::clamp(whites_value / 100.0F, -1.0F, 1.0F) * 0.30F;
    const float blacks_amount = std::clamp(blacks_value / 100.0F, -1.0F, 1.0F) * 0.25F;
    const bool has_additive_tonal =
        highlights_amount != 0.0F || shadows_amount != 0.0F || whites_amount != 0.0F || blacks_amount != 0.0F;
    const bool has_midtones = midtones_value != 0.0F;
    if (has_additive_tonal || has_midtones) {
        const float midtone_gamma = has_midtones
            ? std::clamp(1.0F / (1.0F + midtones_value * 0.01F), 0.2F, 5.0F)
            : 1.0F;
        constexpr std::size_t kMidtoneLutSize = 4096U;
        std::vector<float> midtone_lut;
        if (has_midtones) {
            midtone_lut.resize(kMidtoneLutSize);
            for (std::size_t i = 0; i < kMidtoneLutSize; ++i) {
                const float sample = static_cast<float>(i) / static_cast<float>(kMidtoneLutSize - 1U);
                midtone_lut[i] = std::pow(std::max(sample, 1.0e-8F), midtone_gamma);
            }
        }

        for (int y = 0; y < gamma_rgb.rows; ++y) {
            auto* row = gamma_rgb.ptr<cv::Vec3f>(y);
            for (int x = 0; x < gamma_rgb.cols; ++x) {
                auto& pixel = row[x];
                float y_value = 0.2126F * pixel[0] + 0.7152F * pixel[1] + 0.0722F * pixel[2];

                const auto apply_tonal_shift = [&](const float amount, const float pivot, const float range, const float power, const bool highlights_side) {
                    if (amount == 0.0F) {
                        return;
                    }
                    const float normalized = highlights_side
                        ? std::clamp((y_value - pivot) / range, 0.0F, 1.0F)
                        : std::clamp((pivot - y_value) / range, 0.0F, 1.0F);
                    float weight = normalized;
                    if (power == 2.0F) {
                        weight *= normalized;
                    } else if (power == 1.5F) {
                        weight *= std::sqrt(normalized);
                    } else {
                        weight = std::pow(normalized, power);
                    }
                    pixel[0] = clamp01(pixel[0] + amount * weight);
                    pixel[1] = clamp01(pixel[1] + amount * weight);
                    pixel[2] = clamp01(pixel[2] + amount * weight);
                    y_value = 0.2126F * pixel[0] + 0.7152F * pixel[1] + 0.0722F * pixel[2];
                };

                apply_tonal_shift(highlights_amount, 0.30F, 0.70F, 1.5F, true);
                apply_tonal_shift(shadows_amount, 0.65F, 0.65F, 1.5F, false);
                apply_tonal_shift(whites_amount, 0.60F, 0.40F, 2.0F, true);
                apply_tonal_shift(blacks_amount, 0.35F, 0.35F, 2.0F, false);

                if (has_midtones) {
                    const float weight = std::clamp(4.0F * y_value * (1.0F - y_value), 0.0F, 1.0F);
                    for (int c = 0; c < 3; ++c) {
                        const float bent = sample_lut(midtone_lut, pixel[c]);
                        pixel[c] = clamp01(pixel[c] * (1.0F - weight) + bent * weight);
                    }
                }
            }
        }
    }

    adjusted = gamma_decode_mat_22(gamma_rgb);

    if (request.temp != 0.0F || request.tint != 0.0F) {
        Image oklab = rgb_to_oklab(adjusted);
        for (std::size_t i = 0; i < oklab.pixel_count(); ++i) {
            oklab.pixels[i * 3 + 2] += request.temp * 0.0008F;
            oklab.pixels[i * 3 + 1] += request.tint * 0.0008F;
        }
        adjusted = oklab_to_rgb(oklab);
    }

    return adjusted;
}

Image apply_post_film_color(
    const Image& rendered,
    const NativePreviewRenderRequest& request) {
    if (request.saturation == 0.0F && request.vibrance == 0.0F) {
        return rendered;
    }

    Image oklab = rgb_to_oklab(rendered);
    Image oklch = oklab_to_oklch(oklab);
    constexpr float kSkinHueCenter = 0.55F;
    constexpr float kChromaReference = 0.22F;

    for (std::size_t i = 0; i < oklch.pixel_count(); ++i) {
        float chroma = oklch.pixels[i * 3 + 1];
        const float hue = oklch.pixels[i * 3 + 2];

        if (request.saturation != 0.0F) {
            chroma *= (1.0F + request.saturation / 100.0F);
        }
        if (request.vibrance != 0.0F) {
            const float dullness = 1.0F - std::clamp(chroma / kChromaReference, 0.0F, 1.0F);
            const float skin_weight = std::pow(std::clamp(std::cos(hue - kSkinHueCenter), 0.0F, 1.0F), 4.0F);
            const float protection = 1.0F - skin_weight * 0.6F;
            const float vibrance_mult = 1.0F + (request.vibrance / 100.0F) * dullness * protection;
            chroma *= std::clamp(vibrance_mult, 0.01F, 4.0F);
        }
        oklch.pixels[i * 3 + 1] = std::max(chroma, 0.0F);
    }

    Image adjusted_oklab = oklch_to_oklab(oklch);
    for (std::size_t i = 0; i < adjusted_oklab.pixel_count(); ++i) {
        adjusted_oklab.pixels[i * 3 + 0] = oklab.pixels[i * 3 + 0];
    }
    return oklab_to_rgb(adjusted_oklab);
}

Image apply_curves(
    const Image& rendered,
    const std::string& curves_text) {
    const auto points = parse_curve_points(curves_text);
    if (points.size() < 2U || is_identity_curve(points)) {
        return rendered;
    }
    const auto lut = build_monotone_cubic_lut(points);
    if (lut.empty()) {
        return rendered;
    }

    cv::Mat gamma = gamma_encode_image_22(rendered);
    cv::Mat curved(gamma.rows, gamma.cols, CV_32FC3);
    for (int y = 0; y < gamma.rows; ++y) {
        for (int x = 0; x < gamma.cols; ++x) {
            const auto& src = gamma.at<cv::Vec3f>(y, x);
            auto& dst = curved.at<cv::Vec3f>(y, x);
            for (int c = 0; c < 3; ++c) {
                const int index = std::clamp(static_cast<int>(src[c] * 4095.0F), 0, 4095);
                dst[c] = lut[static_cast<std::size_t>(index)];
            }
        }
    }
    return gamma_decode_mat_22(curved);
}

Image apply_hsl(
    const Image& rendered,
    const NativePreviewRenderRequest& request) {
    const std::array<std::tuple<const char*, float, float, float, float>, 8> ranges{{
        {"red", 0.0F, 28.0F, request.hsl_red_h, request.hsl_red_s},
        {"orange", 30.0F, 22.0F, request.hsl_orange_h, request.hsl_orange_s},
        {"yellow", 60.0F, 25.0F, request.hsl_yellow_h, request.hsl_yellow_s},
        {"green", 120.0F, 38.0F, request.hsl_green_h, request.hsl_green_s},
        {"aqua", 180.0F, 32.0F, request.hsl_aqua_h, request.hsl_aqua_s},
        {"blue", 240.0F, 32.0F, request.hsl_blue_h, request.hsl_blue_s},
        {"purple", 285.0F, 28.0F, request.hsl_purple_h, request.hsl_purple_s},
        {"magenta", 330.0F, 28.0F, request.hsl_magenta_h, request.hsl_magenta_s},
    }};
    const std::array<float, 8> luminance_adjustments{
        request.hsl_red_l,
        request.hsl_orange_l,
        request.hsl_yellow_l,
        request.hsl_green_l,
        request.hsl_aqua_l,
        request.hsl_blue_l,
        request.hsl_purple_l,
        request.hsl_magenta_l,
    };

    bool any_non_zero = false;
    for (const auto& [_, __, ___, hue_shift, sat_shift] : ranges) {
        if (hue_shift != 0.0F || sat_shift != 0.0F) {
            any_non_zero = true;
            break;
        }
    }
    if (!any_non_zero) {
        for (const float value : luminance_adjustments) {
            if (value != 0.0F) {
                any_non_zero = true;
                break;
            }
        }
    }
    if (!any_non_zero) {
        return rendered;
    }

    Image oklab = rgb_to_oklab(rendered);
    Image oklch = oklab_to_oklch(oklab);
    for (std::size_t i = 0; i < oklch.pixel_count(); ++i) {
        const float hue_rad = oklch.pixels[i * 3 + 2];
        const float hue_deg = std::fmod(hue_rad * 180.0F / std::numbers::pi_v<float> + 360.0F, 360.0F);

        float hue_delta = 0.0F;
        float sat_delta = 0.0F;
        float light_delta = 0.0F;
        for (std::size_t range_index = 0; range_index < ranges.size(); ++range_index) {
            const auto& [name, center_deg, sigma_deg, hue_shift, sat_shift] = ranges[range_index];
            (void)name;
            if (hue_shift == 0.0F && sat_shift == 0.0F && luminance_adjustments[range_index] == 0.0F) {
                continue;
            }
            float diff = std::fmod(hue_deg - center_deg + 180.0F, 360.0F) - 180.0F;
            if (diff < -180.0F) {
                diff += 360.0F;
            }
            float weight = std::exp(-0.5F * std::pow(diff / sigma_deg, 2.0F));
            if (range_index == 0U) {
                float diff_wrap = std::fmod(hue_deg - (center_deg + 360.0F) + 180.0F, 360.0F) - 180.0F;
                if (diff_wrap < -180.0F) {
                    diff_wrap += 360.0F;
                }
                weight = std::max(weight, std::exp(-0.5F * std::pow(diff_wrap / sigma_deg, 2.0F)));
            }
            hue_delta += weight * hue_shift;
            sat_delta += weight * sat_shift;
            light_delta += weight * luminance_adjustments[range_index];
        }

        oklch.pixels[i * 3 + 2] = std::fmod(hue_rad + hue_delta * std::numbers::pi_v<float> / 180.0F + 2.0F * std::numbers::pi_v<float>, 2.0F * std::numbers::pi_v<float>);
        oklch.pixels[i * 3 + 1] = std::max(0.0F, oklch.pixels[i * 3 + 1] * std::clamp(1.0F + sat_delta / 100.0F, 0.0F, 4.0F));
        oklch.pixels[i * 3 + 0] = std::clamp(oklch.pixels[i * 3 + 0] + light_delta / 100.0F * 0.40F, 0.0F, 1.0F);
    }

    Image adjusted_oklab = oklch_to_oklab(oklch);
    for (std::size_t i = 0; i < adjusted_oklab.pixel_count(); ++i) {
        adjusted_oklab.pixels[i * 3 + 0] = oklch.pixels[i * 3 + 0];
    }
    return oklab_to_rgb(adjusted_oklab);
}

Image apply_post_bloom(
    const Image& rendered,
    const float amount) {
    if (amount <= 0.0F) {
        return rendered;
    }

    const float strength = std::clamp(amount / 100.0F, 0.0F, 1.0F);
    cv::Mat source = image_to_cv32fc3(rendered);
    cv::Mat luminance(rendered.height, rendered.width, CV_32F);
    for (int y = 0; y < rendered.height; ++y) {
        for (int x = 0; x < rendered.width; ++x) {
            const auto& pixel = source.at<cv::Vec3f>(y, x);
            luminance.at<float>(y, x) = 0.2126F * pixel[0] + 0.7152F * pixel[1] + 0.0722F * pixel[2];
        }
    }

    cv::Mat bloom_mask(rendered.height, rendered.width, CV_32F);
    for (int y = 0; y < rendered.height; ++y) {
        for (int x = 0; x < rendered.width; ++x) {
            const float t = std::clamp((luminance.at<float>(y, x) - 0.70F) / 0.18F, 0.0F, 1.0F);
            bloom_mask.at<float>(y, x) = t * t * (3.0F - 2.0F * t);
        }
    }

    cv::Mat masked_source(rendered.height, rendered.width, CV_32FC3);
    for (int y = 0; y < rendered.height; ++y) {
        for (int x = 0; x < rendered.width; ++x) {
            const float mask = bloom_mask.at<float>(y, x);
            const auto& src = source.at<cv::Vec3f>(y, x);
            auto& dst = masked_source.at<cv::Vec3f>(y, x);
            dst[0] = src[0] * mask;
            dst[1] = src[1] * mask;
            dst[2] = src[2] * mask;
        }
    }

    const int short_edge = std::min(rendered.width, rendered.height);
    const int r1 = std::max(3, short_edge / 22);
    const int r2 = std::max(7, short_edge / 11);
    const int r3 = std::max(13, short_edge / 5);

    cv::Mat b1 = gaussian_blur_downsampled_rgb(masked_source, r1, 640);
    cv::Mat b2 = gaussian_blur_downsampled_rgb(masked_source, r2, 640);
    cv::Mat b3 = gaussian_blur_downsampled_rgb(masked_source, r3, 640);

    cv::Mat screen(rendered.height, rendered.width, CV_32FC3);
    for (int y = 0; y < rendered.height; ++y) {
        for (int x = 0; x < rendered.width; ++x) {
            const auto bloom = 0.50F * b1.at<cv::Vec3f>(y, x) + 0.30F * b2.at<cv::Vec3f>(y, x) + 0.20F * b3.at<cv::Vec3f>(y, x);
            const cv::Vec3f warm_bloom{
                std::clamp(bloom[0] * 1.06F, 0.0F, 1.0F),
                std::clamp(bloom[1] * 1.02F, 0.0F, 1.0F),
                bloom[2] * 0.88F,
            };
            const auto& base = source.at<cv::Vec3f>(y, x);
            auto& dst = screen.at<cv::Vec3f>(y, x);
            for (int c = 0; c < 3; ++c) {
                dst[c] = std::clamp(1.0F - (1.0F - base[c]) * (1.0F - warm_bloom[c] * strength * 0.75F), 0.0F, 1.0F);
            }
        }
    }

    return cv32fc3_to_image(screen);
}

std::string serialize_feature_report_json(
    const SolverInput& input,
    const RenderPlan& render_plan,
    const FilmStockProfile& stock_profile,
    const PrintStockProfile* print_stock,
    const std::string& input_filename,
    const std::string& output_filename) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"engine_version\": \"" << escape_json_string(kEngineVersion) << "\",\n";
    out << "  \"input_file\": \"" << escape_json_string(input_filename) << "\",\n";
    out << "  \"output_file\": \"" << escape_json_string(output_filename) << "\",\n";
    out << "  \"stock_profile\": \"" << escape_json_string(stock_profile.stock_id) << "\",\n";
    out << "  \"print_stock\": \"" << escape_json_string(print_stock != nullptr ? print_stock->print_stock_id : "none") << "\",\n";
    out << "  \"image_diagnosis\": {"
        << "\"tonal_state\": \"" << escape_json_string(render_plan.input_diagnosis.tonal_state) << "\","
        << "\"dynamic_range_stops\": " << json_number(render_plan.input_diagnosis.dynamic_range_stops) << ","
        << "\"shadow_cast\": \"" << escape_json_string(render_plan.input_diagnosis.shadow_cast) << "\","
        << "\"midtone_anchor\": " << json_number(render_plan.input_diagnosis.midtone_anchor) << ","
        << "\"highlight_headroom\": " << json_number(render_plan.input_diagnosis.highlight_headroom) << ","
        << "\"neon_risk\": " << json_number(render_plan.input_diagnosis.neon_risk) << ","
        << "\"specular_candidate_strength\": " << json_number(render_plan.input_diagnosis.specular_candidate_strength)
        << "},\n";
    out << "  \"feature_summary\": {\n";
    out << "    \"tonal_distribution\": {";
    const auto& tonal = input.tonal_distribution;
    out << "\"luma_p01\": " << json_number(tonal.luma_p01) << ","
        << "\"luma_p05\": " << json_number(tonal.luma_p05) << ","
        << "\"luma_p25\": " << json_number(tonal.luma_p25) << ","
        << "\"luma_p50\": " << json_number(tonal.luma_p50) << ","
        << "\"luma_p75\": " << json_number(tonal.luma_p75) << ","
        << "\"luma_p95\": " << json_number(tonal.luma_p95) << ","
        << "\"luma_p99\": " << json_number(tonal.luma_p99) << ","
        << "\"luma_p995\": " << json_number(tonal.luma_p995) << ","
        << "\"dynamic_range_stops\": " << json_number(tonal.dynamic_range_stops) << ","
        << "\"midtone_anchor\": " << json_number(tonal.midtone_anchor) << ","
        << "\"highlight_headroom\": " << json_number(tonal.highlight_headroom) << ","
        << "\"shadow_depth\": " << json_number(tonal.shadow_depth) << ","
        << "\"tonal_skew\": \"" << escape_json_string(tonal.tonal_skew) << "\","
        << "\"contrast_index\": " << json_number(tonal.contrast_index) << ","
        << "\"black_point_actual\": " << json_number(tonal.black_point_actual) << ","
        << "\"white_point_actual\": " << json_number(tonal.white_point_actual)
        << "},\n";
    out << "    \"hue_saturation_state\": {";
    const auto& color = input.hue_saturation_state;
    out << "\"sat_shadow_mean\": " << json_number(color.sat_shadow_mean) << ","
        << "\"sat_mid_mean\": " << json_number(color.sat_mid_mean) << ","
        << "\"sat_highlight_mean\": " << json_number(color.sat_highlight_mean) << ","
        << "\"sat_p95\": " << json_number(color.sat_p95) << ","
        << "\"neon_risk\": " << json_number(color.neon_risk) << ","
        << "\"dominant_hue_bins\": " << json_string_array({color.dominant_hue_bins[0], color.dominant_hue_bins[1], color.dominant_hue_bins[2]}) << ","
        << "\"hue_entropy\": " << json_number(color.hue_entropy) << ","
        << "\"red_orange_density\": " << json_number(color.red_orange_density) << ","
        << "\"green_yellow_density\": " << json_number(color.green_yellow_density) << ","
        << "\"warm_cool_ratio\": " << json_number(color.warm_cool_ratio) << ","
        << "\"cyan_blue_ratio\": " << json_number(color.cyan_blue_ratio) << ","
        << "\"mean_chroma\": " << json_number(color.mean_chroma)
        << "},\n";
    out << "    \"spatial_frequency\": {";
    const auto& spatial = input.spatial_frequency;
    out << "\"texture_density\": " << json_number(spatial.texture_density) << ","
        << "\"smooth_area_ratio\": " << json_number(spatial.smooth_area_ratio) << ","
        << "\"edge_density\": " << json_number(spatial.edge_density) << ","
        << "\"digital_sharpness_score\": " << json_number(spatial.digital_sharpness_score) << ","
        << "\"specular_point_ratio\": " << json_number(spatial.specular_point_ratio) << ","
        << "\"large_highlight_area_ratio\": " << json_number(spatial.large_highlight_area_ratio)
        << "},\n";
    out << "    \"channel_behavior\": {\"clipping_ratios\": " << json_float_map(input.clipping_ratios) << "}";
    if (input.camera_input_bias.has_value()) {
        const auto& bias = *input.camera_input_bias;
        out << ",\n    \"camera_input_bias\": {"
            << "\"neutral_confidence\": " << json_number(bias.neutral_confidence) << ","
            << "\"global_cast_lab\": " << json_float_array3(bias.global_cast_lab) << ","
            << "\"shadow_cast_lab\": " << json_float_array3(bias.shadow_cast_lab) << ","
            << "\"midtone_cast_lab\": " << json_float_array3(bias.midtone_cast_lab) << ","
            << "\"highlight_cast_lab\": " << json_float_array3(bias.highlight_cast_lab) << ","
            << "\"blue_excess_index\": " << json_number(bias.blue_excess_index) << ","
            << "\"green_magenta_bias\": " << json_number(bias.green_magenta_bias) << ","
            << "\"warm_cool_bias\": " << json_number(bias.warm_cool_bias)
            << "}";
    }
    out << "\n  },\n";
    out << "  \"render_plan\": {\n";
    out << "    \"pre_film_normalization\": {"
        << "\"exposure_compensation_stops\": " << json_number(render_plan.pre_film_normalization.exposure_compensation_stops) << ","
        << "\"shadow_blue_normalization\": " << json_number(render_plan.pre_film_normalization.shadow_blue_normalization) << ","
        << "\"green_magenta_stabilization\": " << json_number(render_plan.pre_film_normalization.green_magenta_stabilization) << ","
        << "\"highlight_channel_recovery\": " << json_number(render_plan.pre_film_normalization.highlight_channel_recovery) << ","
        << "\"contrast_compensation\": " << json_number(render_plan.pre_film_normalization.contrast_compensation) << ","
        << "\"highlights_compensation\": " << json_number(render_plan.pre_film_normalization.highlights_compensation) << ","
        << "\"shadows_compensation\": " << json_number(render_plan.pre_film_normalization.shadows_compensation) << ","
        << "\"blacks_compensation\": " << json_number(render_plan.pre_film_normalization.blacks_compensation) << ","
        << "\"whites_compensation\": " << json_number(render_plan.pre_film_normalization.whites_compensation) << ","
        << "\"midtones_compensation\": " << json_number(render_plan.pre_film_normalization.midtones_compensation)
        << "},\n";
    out << "    \"film_response\": {"
        << "\"toe_strength\": " << json_number(render_plan.film_response.toe_strength) << ","
        << "\"toe_length\": " << json_number(render_plan.film_response.toe_length) << ","
        << "\"midtone_density\": " << json_number(render_plan.film_response.midtone_density) << ","
        << "\"shoulder_strength\": " << json_number(render_plan.film_response.shoulder_strength) << ","
        << "\"highlight_rolloff_start\": " << json_number(render_plan.film_response.highlight_rolloff_start) << ","
        << "\"black_density_floor\": " << json_number(render_plan.film_response.black_density_floor) << ","
        << "\"highlight_desaturation\": " << json_number(render_plan.film_response.highlight_desaturation) << ","
        << "\"blue_cyan_compression\": " << json_number(render_plan.film_response.blue_cyan_compression) << ","
        << "\"red_orange_compression\": " << json_number(render_plan.film_response.red_orange_compression) << ","
        << "\"neon_compression\": " << json_number(render_plan.film_response.neon_compression) << ","
        << "\"chroma_boost\": " << json_number(render_plan.film_response.chroma_boost) << ","
        << "\"channel_toe_mult\": " << json_float_array3(render_plan.film_response.channel_toe_mult) << ","
        << "\"channel_shoulder_mult\": " << json_float_array3(render_plan.film_response.channel_shoulder_mult) << ","
        << "\"channel_midtone_mult\": " << json_float_array3(render_plan.film_response.channel_midtone_mult) << ","
        << "\"shadow_bias_lab\": " << json_float_array3(render_plan.film_response.shadow_bias_lab) << ","
        << "\"midtone_bias_lab\": " << json_float_array3(render_plan.film_response.midtone_bias_lab) << ","
        << "\"highlight_bias_lab\": " << json_float_array3(render_plan.film_response.highlight_bias_lab) << ","
        << "\"pan_weight_r\": " << json_number(render_plan.film_response.pan_weight_r) << ","
        << "\"pan_weight_g\": " << json_number(render_plan.film_response.pan_weight_g) << ","
        << "\"pan_weight_b\": " << json_number(render_plan.film_response.pan_weight_b) << ","
        << "\"chroma_coupling\": " << json_float_map(render_plan.film_response.chroma_coupling) << ","
        << "\"dye_contamination\": " << json_float_map(render_plan.film_response.dye_contamination) << ","
        << "\"stock_type\": \"" << escape_json_string(render_plan.film_response.stock_type) << "\","
        << "\"film_color\": " << json_number(render_plan.film_response.film_color)
        << "},\n";
    out << "    \"material_effects\": {"
        << "\"grain_strength\": " << json_number(render_plan.material_effects.grain_strength) << ","
        << "\"grain_size\": " << json_number(render_plan.material_effects.grain_size) << ","
        << "\"grain_roughness\": " << json_number(render_plan.material_effects.grain_roughness) << ","
        << "\"grain_chroma_strength\": " << json_number(render_plan.material_effects.grain_chroma_strength) << ","
        << "\"halation_strength\": " << json_number(render_plan.material_effects.halation_strength) << ","
        << "\"bloom_strength\": " << json_number(render_plan.material_effects.bloom_strength) << ","
        << "\"edge_softening\": " << json_number(render_plan.material_effects.edge_softening) << ","
        << "\"sharpness\": " << json_number(render_plan.material_effects.sharpness) << ","
        << "\"sharpness_mask\": " << json_number(render_plan.material_effects.sharpness_mask)
        << "},\n";
    out << "    \"print_finish\": ";
    if (render_plan.print_finish.has_value()) {
        const auto& print_finish = *render_plan.print_finish;
        out << "{"
            << "\"strength\": " << json_number(print_finish.strength) << ","
            << "\"print_c\": " << json_number(print_finish.print_c) << ","
            << "\"print_m\": " << json_number(print_finish.print_m) << ","
            << "\"print_y\": " << json_number(print_finish.print_y) << ","
            << "\"print_contrast\": " << json_number(print_finish.print_contrast) << ","
            << "\"print_black_point\": " << json_number(print_finish.print_black_point) << ","
            << "\"shadow_lift\": " << json_number(print_finish.shadow_lift) << ","
            << "\"contrast_boost\": " << json_number(print_finish.contrast_boost) << ","
            << "\"highlight_rolloff\": " << json_number(print_finish.highlight_rolloff) << ","
            << "\"highlight_rolloff_rate\": " << json_number(print_finish.highlight_rolloff_rate) << ","
            << "\"toe_depth\": " << json_number(print_finish.toe_depth) << ","
            << "\"shadow_bias_lab\": " << json_float_array3(print_finish.shadow_bias_lab) << ","
            << "\"midtone_bias_lab\": " << json_float_array3(print_finish.midtone_bias_lab) << ","
            << "\"highlight_bias_lab\": " << json_float_array3(print_finish.highlight_bias_lab) << ","
            << "\"blue_suppression\": " << json_number(print_finish.blue_suppression) << ","
            << "\"red_boost\": " << json_number(print_finish.red_boost) << ","
            << "\"green_shift\": " << json_number(print_finish.green_shift) << ","
            << "\"saturation_scale\": " << json_number(print_finish.saturation_scale) << ","
            << "\"grain_strength\": " << json_number(print_finish.grain_strength) << ","
            << "\"grain_size\": " << json_number(print_finish.grain_size)
            << "}";
    } else {
        out << "null";
    }
    out << "\n  },\n";
    out << "  \"warnings\": " << json_string_array(render_plan.warnings) << "\n";
    out << "}\n";
    return out.str();
}

NativeRenderWorkResult render_native_image(
    const std::filesystem::path& project_root,
    const Image& source_rgb,
    const LuminanceImage& source_luminance,
    const DecodedRawChannelMasks& clipping_masks,
    const NativeRawMetadata& metadata,
    const std::unordered_map<std::string, float>& clipping_ratios,
    const NativePreviewRenderRequest& request,
    const FilmStockProfile& stock_profile,
    const PrintStockProfile* print_stock) {
    NativeRenderWorkResult result;
    append_export_trace(project_root, "render_native_image:analyze:start");
    ImageStateAnalyzer analyzer;
    result.solver_input.clipping_ratios = clipping_ratios;
    append_export_trace(project_root, "render_native_image:analyze_tonal:start");
    result.solver_input.tonal_distribution = analyzer.analyze_tonal(source_luminance, clipping_ratios);
    append_export_trace(project_root, "render_native_image:analyze_tonal:done");
    append_export_trace(project_root, "render_native_image:zone_masks:start");
    result.zone_masks = analyzer.generate_zone_masks(source_luminance, result.solver_input.tonal_distribution.midtone_anchor);
    append_export_trace(project_root, "render_native_image:zone_masks:done");
    append_export_trace(project_root, "render_native_image:analyze_color:start");
    result.solver_input.hue_saturation_state = analyzer.analyze_color(source_rgb, result.zone_masks);
    append_export_trace(project_root, "render_native_image:analyze_color:done");
    append_export_trace(project_root, "render_native_image:analyze_spatial:start");
    auto spatial = analyzer.analyze_spatial(source_luminance);
    result.solver_input.spatial_frequency = spatial.first;
    result.spatial_masks = std::move(spatial.second);
    append_export_trace(project_root, "render_native_image:analyze_spatial:done");
    append_export_trace(project_root, "render_native_image:bias:start");
    result.solver_input.camera_input_bias = CameraBiasEstimator().estimate_bias(source_rgb, clipping_masks, result.zone_masks);
    append_export_trace(project_root, "render_native_image:bias:done");
    if (metadata.iso > 0) {
        result.solver_input.raw_iso = metadata.iso;
    }
    append_export_trace(project_root, "render_native_image:analyze:done");

    const SolverControls controls = build_solver_controls(request);

    append_export_trace(project_root, "render_native_image:solve:start");
    result.render_plan = RenderPlanSolver().solve(result.solver_input, stock_profile, controls, print_stock);
    append_export_trace(project_root, "render_native_image:solve:done");
    return result;
}
#endif

}  // namespace

EngineSession::EngineSession(std::filesystem::path project_root)
    : project_root_(std::filesystem::absolute(std::move(project_root))),
      raw_dir_(project_root_ / "raw_files"),
      stocks_dir_(project_root_ / "profiles" / "stocks"),
      print_stocks_dir_(project_root_ / "profiles" / "print_stocks") {
    if (!std::filesystem::is_directory(project_root_)) {
        throw make_native_exception(
            "PROJECT_ROOT_NOT_FOUND",
            "The DFEE project root could not be found.",
            "Expected project root directory does not exist: " + project_root_.string());
    }
}

const std::filesystem::path& EngineSession::project_root() const noexcept {
    return project_root_;
}

NativeProfilesResponse EngineSession::list_profiles() const {
    NativeProfilesResponse response;
    response.engine = build_engine_metadata();

    {
        ScopedStageTimer total(response.engine, "list_profiles_total");
        {
            ScopedStageTimer stage(response.engine, "list_stock_profiles");
            for (const auto& stock : list_film_stock_profiles(stocks_dir_)) {
                response.stocks.push_back({
                    .stock_id = stock.stock_id,
                    .stock_name = stock.stock_name,
                    .stock_type = to_string(stock.stock_type),
                    .path = stock.path,
                });
            }
        }

        {
            ScopedStageTimer stage(response.engine, "list_print_stock_profiles");
            for (const auto& print_stock : list_print_stock_profiles(print_stocks_dir_)) {
                response.print_stocks.push_back({
                    .print_stock_id = print_stock.print_stock_id,
                    .print_stock_name = print_stock.print_stock_name,
                    .path = print_stock.path,
                });
            }
        }
    }
    finalize_engine_metadata(response.engine);
    return response;
}

NativeSelectResponse EngineSession::select_file(const NativeSelectRequest& request) {
    NativeSelectResponse result;
    result.filename = request.filename;
    result.engine = build_engine_metadata();

    {
        ScopedStageTimer total(result.engine, "select_file_total");
        const std::filesystem::path raw_path = raw_dir_ / request.filename;
        {
            ScopedStageTimer stage(result.engine, "select_validate_request");
            if (request.filename.empty()) {
                result.status = "error";
                result.message = "No RAW filename was provided.";
                result.error = {
                    .code = "RAW_FILENAME_MISSING",
                    .user_message = "Select a RAW file before continuing.",
                    .detail = "select_file received an empty filename.",
                };
                finalize_engine_metadata(result.engine);
                return result;
            }
        }
        {
            ScopedStageTimer stage(result.engine, "select_locate_raw_file");
            if (!std::filesystem::is_regular_file(raw_path)) {
                result.status = "not_found";
                result.message = "RAW file was not found under raw_files.";
                result.error = {
                    .code = "RAW_FILE_NOT_FOUND",
                    .user_message = "The requested RAW file was not found.",
                    .detail = "Expected RAW file at: " + raw_path.string(),
                };
                finalize_engine_metadata(result.engine);
                return result;
            }
        }
        {
            ScopedStageTimer stage(result.engine, "select_cache_session_state");
            if (selected_filename_ != request.filename) {
                clear_decode_caches();
            }
            selected_filename_ = request.filename;
        }
    }

    result.ok = true;
    result.status = "selected";
    result.message = "Native session selected the RAW file and reset any stale decode caches.";
    finalize_engine_metadata(result.engine);
    return result;
}

NativeRawMetadataResponse EngineSession::read_raw_metadata(const NativeRawMetadataRequest& request) const {
    NativeRawMetadataResponse response;
    response.filename = request.filename;
    response.engine = build_engine_metadata();

    {
        ScopedStageTimer total(response.engine, "read_raw_metadata_total");
        {
            ScopedStageTimer stage(response.engine, "read_raw_metadata_file");
            const auto file_response = read_raw_metadata_from_file({
                .filename = (raw_dir_ / request.filename).string(),
            });
            response.ok = file_response.ok;
            response.status = file_response.status;
            response.metadata = file_response.metadata;
            response.error = file_response.error;
        }
    }

    finalize_engine_metadata(response.engine);
    return response;
}

NativeRawDecodeResponse EngineSession::decode_raw(const NativeRawDecodeRequest& request) {
    NativeRawDecodeResponse response;
    response.filename = resolve_filename(request.filename);
    response.engine = build_engine_metadata();

    {
        ScopedStageTimer total(response.engine, request.draft_mode ? "decode_raw_draft_total" : "decode_raw_full_total");
        {
            ScopedStageTimer stage(response.engine, "decode_raw_resolve_session");
            if (response.filename.empty()) {
                response.status = "error";
                response.error = {
                    .code = "RAW_FILENAME_MISSING",
                    .user_message = "Select a RAW file before continuing.",
                    .detail = "decode_raw received an empty filename and no session file is currently selected.",
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
        }
        {
            ScopedStageTimer stage(response.engine, "decode_raw_cache_lookup");
            const auto& cache = request.draft_mode ? draft_decode_cache_ : full_decode_cache_;
            if (cache.has_value() && cache->filename == response.filename) {
                response.ok = true;
                response.status = "cached";
                response.summary = cache->decoded.summary;
                response.metadata = cache->decoded.metadata;
                finalize_engine_metadata(response.engine);
                return response;
            }
        }
        {
            ScopedStageTimer stage(response.engine, "decode_raw_file");
            const auto file_response = decode_raw_image_from_file({
                .filename = (raw_dir_ / response.filename).string(),
                .draft_mode = request.draft_mode,
            });
            response.ok = file_response.ok;
            response.status = file_response.status;
            response.summary = file_response.decoded.summary;
            response.metadata = file_response.decoded.metadata;
            response.error = file_response.error;
            if (file_response.ok) {
                CachedDecode cache_entry{
                    .filename = response.filename,
                    .draft_mode = request.draft_mode,
                    .decoded = file_response.decoded,
                };
                if (request.draft_mode) {
                    draft_decode_cache_ = std::move(cache_entry);
                    raw_preview_jpeg_cache_.reset();
                } else {
                    full_decode_cache_ = std::move(cache_entry);
                }
            }
        }
        {
            ScopedStageTimer stage(response.engine, "decode_raw_refresh_preview_cache");
            if (response.ok && request.draft_mode) {
                refresh_preview_cache_from_draft();
            }
        }
    }

    finalize_engine_metadata(response.engine);
    return response;
}

NativeRawPreviewResponse EngineSession::raw_preview(const NativeRawPreviewRequest& request) {
    NativeRawPreviewResponse response;
    response.filename = resolve_filename(request.filename);
    response.engine = build_engine_metadata();

    {
        ScopedStageTimer total(response.engine, "raw_preview_total");
        {
            ScopedStageTimer stage(response.engine, "raw_preview_resolve_session");
            if (response.filename.empty()) {
                response.status = "error";
                response.error = {
                    .code = "RAW_FILENAME_MISSING",
                    .user_message = "Select a RAW file before continuing.",
                    .detail = "raw_preview received an empty filename and no session file is currently selected.",
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
        }
        {
            ScopedStageTimer stage(response.engine, "raw_preview_cache_lookup");
            if (raw_preview_jpeg_cache_.has_value() &&
                raw_preview_jpeg_cache_->filename == response.filename &&
                raw_preview_jpeg_cache_->max_edge == request.max_edge) {
                response.ok = true;
                response.status = "cached";
                response.jpeg_bytes = raw_preview_jpeg_cache_->jpeg_bytes;
                finalize_engine_metadata(response.engine);
                return response;
            }
        }
        {
            ScopedStageTimer stage(response.engine, "raw_preview_build");
            const auto encoded = encode_raw_preview(response.filename, request.max_edge);
            response.ok = encoded.ok;
            response.status = encoded.status;
            response.content_type = encoded.content_type;
            response.jpeg_bytes = encoded.jpeg_bytes;
            response.error = encoded.error;
            if (encoded.ok) {
                raw_preview_jpeg_cache_ = CachedRawPreviewJpeg{
                    .filename = response.filename,
                    .max_edge = request.max_edge,
                    .jpeg_bytes = response.jpeg_bytes,
                };
            }
        }
    }

    finalize_engine_metadata(response.engine);
    return response;
}

NativePreviewRenderResponse EngineSession::render_preview(const NativePreviewRenderRequest& request) {
    NativePreviewRenderResponse response;
    response.filename = resolve_filename(request.filename);
    response.engine = build_engine_metadata();

#if !DFEE_HAS_OPENCV
    response.status = "unavailable";
    response.error = {
        .code = "OPENCV_UNAVAILABLE",
        .user_message = "Native preview rendering is not available in this build.",
        .detail = "DFEE was built without OpenCV discovery; configure with the windows-msvc-vcpkg preset.",
    };
    finalize_engine_metadata(response.engine);
    return response;
#else
    {
        ScopedStageTimer total(response.engine, "render_preview_total");
        {
            ScopedStageTimer stage(response.engine, "render_preview_resolve_session");
            if (response.filename.empty()) {
                response.status = "error";
                response.error = {
                    .code = "RAW_FILENAME_MISSING",
                    .user_message = "Select a RAW file before continuing.",
                    .detail = "render_preview received an empty filename and no session file is currently selected.",
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
        }

        if (request.stock == "none") {
            ScopedStageTimer stage(response.engine, "render_preview_passthrough");
            const auto raw = raw_preview({
                .filename = response.filename,
                .max_edge = 1024,
            });
            response.ok = raw.ok;
            response.status = raw.status;
            response.content_type = raw.content_type;
            response.jpeg_bytes = raw.jpeg_bytes;
            response.error = raw.error;
            finalize_engine_metadata(response.engine);
            return response;
        }

        {
            ScopedStageTimer stage(response.engine, "render_preview_ensure_preview_cache");
            if (!draft_decode_cache_.has_value() || draft_decode_cache_->filename != response.filename ||
                !preview_cache_.has_value() || preview_cache_->filename != response.filename) {
                const auto decode = decode_raw({
                    .filename = response.filename,
                    .draft_mode = true,
                });
                if (!decode.ok) {
                    response.status = decode.status;
                    response.error = decode.error;
                    finalize_engine_metadata(response.engine);
                    return response;
                }
            }
        }

        FilmStockProfile stock_profile;
        std::optional<PrintStockProfile> print_stock_profile;
        {
            ScopedStageTimer stage(response.engine, "render_preview_load_profiles");
            try {
                stock_profile = load_film_stock_profile(stocks_dir_ / (request.stock + ".yaml"));
                if (request.print_stock != "none") {
                    print_stock_profile = load_print_stock_profile(print_stocks_dir_ / (request.print_stock + ".yaml"));
                }
            } catch (const std::exception& ex) {
                response.status = "error";
                response.error = {
                    .code = "PROFILE_LOAD_FAILED",
                    .user_message = "The selected film or print profile could not be loaded.",
                    .detail = ex.what(),
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
        }

        const auto& preview = *preview_cache_;
        const auto& draft = *draft_decode_cache_;

        SolverInput solver_input;
        ZoneMasks zone_masks;
        SpatialMasks spatial_masks;
        {
            ScopedStageTimer stage(response.engine, "render_preview_analyze");
            bool used_cached_analysis = false;
            {
                ScopedStageTimer cache_lookup(response.engine, "render_preview_analyze_cache_lookup");
                if (preview_analysis_cache_.has_value() && preview_analysis_cache_->filename == response.filename) {
                    solver_input = preview_analysis_cache_->solver_input;
                    zone_masks = preview_analysis_cache_->zone_masks;
                    spatial_masks = preview_analysis_cache_->spatial_masks;
                    used_cached_analysis = true;
                }
            }

            if (!used_cached_analysis) {
                ScopedStageTimer compute_stage(response.engine, "render_preview_analyze_compute");
                ImageStateAnalyzer analyzer;
                solver_input.clipping_ratios = {
                    {"R", draft.decoded.summary.clipping_ratio_r},
                    {"G", draft.decoded.summary.clipping_ratio_g},
                    {"B", draft.decoded.summary.clipping_ratio_b},
                    {"RAW", draft.decoded.summary.raw_clipping_ratio},
                };
                solver_input.tonal_distribution = analyzer.analyze_tonal(preview.luminance, solver_input.clipping_ratios);
                zone_masks = analyzer.generate_zone_masks(preview.luminance, solver_input.tonal_distribution.midtone_anchor);
                solver_input.hue_saturation_state = analyzer.analyze_color(preview.rgb_linear, zone_masks);
                auto spatial_result = analyzer.analyze_spatial(preview.luminance);
                solver_input.spatial_frequency = spatial_result.first;
                spatial_masks = std::move(spatial_result.second);

                const auto preview_masks = resize_clipping_masks(
                    draft.decoded.clipping_masks,
                    draft.decoded.rgb_linear.width,
                    draft.decoded.rgb_linear.height,
                    preview.rgb_linear.width,
                    preview.rgb_linear.height);
                CameraBiasEstimator bias_estimator;
                solver_input.camera_input_bias = bias_estimator.estimate_bias(preview.rgb_linear, preview_masks, zone_masks);
                if (draft.decoded.metadata.iso > 0) {
                    solver_input.raw_iso = draft.decoded.metadata.iso;
                }

                preview_analysis_cache_ = CachedPreviewAnalysis{
                    .filename = response.filename,
                    .solver_input = solver_input,
                    .zone_masks = zone_masks,
                    .spatial_masks = spatial_masks,
                };
            }
        }

        RenderPlan render_plan;
        {
            ScopedStageTimer stage(response.engine, "render_preview_solve_plan");
            SolverControls controls;
            controls.adaptation_strength = request.adaptation;
            controls.exposure_intent = "Preserve";
            controls.grain_amount = request.grain;
            controls.grain_strength = request.grain_strength;
            controls.grain_size = request.grain_size;
            controls.grain_roughness = request.grain_roughness;
            controls.halation_amount = request.halation;
            controls.sharpness = request.sharpness;
            controls.sharpness_mask = request.sharpness_mask;
            controls.film_color = request.film_color;
            controls.print_strength = request.print_strength;
            controls.print_c = request.print_c;
            controls.print_m = request.print_m;
            controls.print_y = request.print_y;
            controls.print_contrast = request.print_contrast;
            controls.print_black_point = request.print_black_point;

            RenderPlanSolver solver;
            render_plan = solver.solve(
                solver_input,
                stock_profile,
                controls,
                print_stock_profile.has_value() ? &*print_stock_profile : nullptr);
            render_plan.material_effects.grain_seed = compute_stable_grain_seed(
                response.filename,
                request.stock,
                request.print_stock,
                render_plan.material_effects);
        }

        Image rendered = preview.rgb_linear;
        FilmRenderer renderer;
        {
            ScopedStageTimer stage(response.engine, "render_preview_apply_pre_film_sliders");
            rendered = apply_pre_film_preview_sliders(preview.rgb_linear, request, render_plan);
        }
        {
            ScopedStageTimer stage(response.engine, "render_preview_film_pipeline");
            {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_pre_film_normalization");
                rendered = renderer.apply_pre_film_normalization(
                    rendered,
                    zone_masks,
                    render_plan.pre_film_normalization);
            }
            if (render_plan.stock_type == "monochrome") {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_panchromatic");
                rendered = renderer.apply_panchromatic_conversion(rendered, render_plan.film_response);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_tone_response");
                rendered = renderer.apply_film_tone_response(rendered, render_plan.film_response);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_dye_contamination");
                rendered = renderer.apply_dye_contamination(rendered, render_plan.film_response);
            }
            if (render_plan.stock_type != "monochrome") {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_color_response");
                rendered = renderer.apply_color_response_and_coupling(
                    rendered,
                    zone_masks,
                    render_plan.film_response);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_acutance");
                rendered = renderer.apply_acutance_shaping(rendered, render_plan.material_effects);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_halation_bloom");
                rendered = renderer.apply_halation_bloom(
                    rendered,
                    zone_masks,
                    spatial_masks,
                    render_plan.material_effects);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_grain");
                rendered = renderer.apply_film_grain(rendered, spatial_masks, render_plan.material_effects);
            }
            if (render_plan.print_finish.has_value()) {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_print_finish");
                rendered = renderer.apply_print_finish(rendered, *render_plan.print_finish);
            }
        }
        {
            ScopedStageTimer stage(response.engine, "render_preview_post_color");
            rendered = apply_post_film_color(rendered, request);
        }
        {
            ScopedStageTimer stage(response.engine, "render_preview_post_effects");
            rendered = apply_curves(rendered, request.curves);
            rendered = apply_hsl(rendered, request);
            {
                ScopedStageTimer substage(response.engine, "render_preview_post_stage_clarity");
                rendered = renderer.apply_clarity(rendered, request.clarity);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_post_stage_texture");
                rendered = renderer.apply_texture(rendered, request.texture);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_post_stage_dehaze");
                rendered = renderer.apply_dehaze(rendered, request.dehaze);
            }
            {
                ScopedStageTimer substage(response.engine, "render_preview_post_stage_bloom");
                rendered = apply_post_bloom(rendered, request.bloom);
            }
        }
        {
            ScopedStageTimer stage(response.engine, "render_preview_encode_jpeg");
            const auto encoded = encode_preview_jpeg_bytes(rendered, response.filename);
            response.ok = encoded.ok;
            response.status = encoded.status;
            response.content_type = encoded.content_type;
            response.jpeg_bytes = encoded.jpeg_bytes;
            response.error = encoded.error;
        }
    }

    finalize_engine_metadata(response.engine);
    return response;
#endif
}

NativeExportResponse EngineSession::export_image(const NativeExportRequest& request) {
    NativeExportResponse response;
    response.filename = resolve_filename(request.filename);
    response.engine = build_engine_metadata();
    response.export_format = request.export_format;

#if !DFEE_HAS_OPENCV
    response.status = "unavailable";
    response.error = {
        .code = "OPENCV_UNAVAILABLE",
        .user_message = "Native export is not available in this build.",
        .detail = "DFEE was built without OpenCV discovery; configure with the windows-msvc-vcpkg preset.",
    };
    finalize_engine_metadata(response.engine);
    return response;
#else
    {
        ScopedStageTimer total(response.engine, "export_image_total");
        append_export_trace(project_root_, "export_image:start");
        {
            ScopedStageTimer stage(response.engine, "export_image_resolve_session");
            append_export_trace(project_root_, "export_image:resolve_session:start");
            if (response.filename.empty()) {
                response.status = "error";
                response.error = {
                    .code = "RAW_FILENAME_MISSING",
                    .user_message = "Select a RAW file before continuing.",
                    .detail = "export_image received an empty filename and no session file is currently selected.",
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
            append_export_trace(project_root_, "export_image:resolve_session:done");
        }

        const auto raw_path = raw_dir_ / response.filename;
        const std::string basename = raw_path.stem().string();
        std::string canonical_format = request.export_format;
        std::ranges::transform(canonical_format, canonical_format.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (canonical_format != "tiff" && canonical_format != "png16" && canonical_format != "png8") {
            canonical_format = "tiff";
        }
        response.export_format = canonical_format;

        Image rendered;
        std::optional<SolverInput> solver_input;
        std::optional<RenderPlan> render_plan;
        std::optional<FilmStockProfile> stock_profile;
        std::optional<PrintStockProfile> print_stock_profile;

        if (request.stock == "none") {
            ScopedStageTimer stage(response.engine, "export_image_passthrough");
            append_export_trace(project_root_, "export_image:passthrough:start");
            if (!full_decode_cache_.has_value() || full_decode_cache_->filename != response.filename) {
                const auto decode = decode_raw({
                    .filename = response.filename,
                    .draft_mode = false,
                });
                if (!decode.ok) {
                    response.status = decode.status;
                    response.error = decode.error;
                    finalize_engine_metadata(response.engine);
                    return response;
                }
            }
            rendered = full_decode_cache_->decoded.rgb_linear;
            append_export_trace(project_root_, "export_image:passthrough:done");
        } else {
            {
                ScopedStageTimer stage(response.engine, "export_image_ensure_full_cache");
                append_export_trace(project_root_, "export_image:ensure_full_cache:start");
                if (!full_decode_cache_.has_value() || full_decode_cache_->filename != response.filename) {
                    const auto decode = decode_raw({
                        .filename = response.filename,
                        .draft_mode = false,
                    });
                    if (!decode.ok) {
                        response.status = decode.status;
                        response.error = decode.error;
                        finalize_engine_metadata(response.engine);
                        return response;
                    }
                }
                append_export_trace(project_root_, "export_image:ensure_full_cache:done");
            }
            {
                ScopedStageTimer stage(response.engine, "export_image_load_profiles");
                append_export_trace(project_root_, "export_image:load_profiles:start");
                try {
                    stock_profile = load_film_stock_profile(stocks_dir_ / (request.stock + ".yaml"));
                    if (request.print_stock != "none") {
                        print_stock_profile = load_print_stock_profile(print_stocks_dir_ / (request.print_stock + ".yaml"));
                    }
                } catch (const std::exception& ex) {
                    response.status = "error";
                    response.error = {
                        .code = "PROFILE_LOAD_FAILED",
                        .user_message = "The selected film or print profile could not be loaded.",
                        .detail = ex.what(),
                    };
                    finalize_engine_metadata(response.engine);
                    return response;
                }
                append_export_trace(project_root_, "export_image:load_profiles:done");
            }
            {
                ScopedStageTimer stage(response.engine, "export_image_render");
                append_export_trace(project_root_, "export_image:render:start");
                const auto& decoded = full_decode_cache_->decoded;
                Image analysis_rgb;
                LuminanceImage analysis_luminance;
                DecodedRawChannelMasks analysis_clipping_masks;
                {
                    ScopedStageTimer substage(response.engine, "export_image_render_prepare_analysis");
                    constexpr int kAnalysisMaxEdge = 2048;
                    analysis_rgb = resize_image_to_max_edge(decoded.rgb_linear, kAnalysisMaxEdge);
                    analysis_luminance = compute_luminance(analysis_rgb);
                    analysis_clipping_masks = resize_clipping_masks(
                        decoded.clipping_masks,
                        decoded.rgb_linear.width,
                        decoded.rgb_linear.height,
                        analysis_rgb.width,
                        analysis_rgb.height);
                }
                const auto clipping_ratio_map = std::unordered_map<std::string, float>{
                    {"R", decoded.summary.clipping_ratio_r},
                    {"G", decoded.summary.clipping_ratio_g},
                    {"B", decoded.summary.clipping_ratio_b},
                    {"RAW", decoded.summary.raw_clipping_ratio},
                };
                ZoneMasks working_zone_masks;
                SpatialMasks working_spatial_masks;
                bool used_cached_analysis = false;
                {
                    ScopedStageTimer cache_lookup(response.engine, "export_image_render_working_analysis_cache_lookup");
                    if (export_analysis_cache_.has_value() && export_analysis_cache_->filename == response.filename) {
                        solver_input = export_analysis_cache_->solver_input;
                        working_zone_masks = export_analysis_cache_->zone_masks;
                        working_spatial_masks = export_analysis_cache_->spatial_masks;
                        used_cached_analysis = true;
                    }
                }
                if (!used_cached_analysis) {
                    NativeRenderWorkResult work;
                    {
                        ScopedStageTimer substage(response.engine, "export_image_render_working_analysis");
                        work = render_native_image(
                            project_root_,
                            analysis_rgb,
                            analysis_luminance,
                            analysis_clipping_masks,
                            decoded.metadata,
                            clipping_ratio_map,
                            request,
                            *stock_profile,
                            print_stock_profile.has_value() ? &*print_stock_profile : nullptr);
                    }
                    solver_input = work.solver_input;
                    working_zone_masks = std::move(work.zone_masks);
                    working_spatial_masks = std::move(work.spatial_masks);
                    export_analysis_cache_ = CachedExportAnalysis{
                        .filename = response.filename,
                        .solver_input = *solver_input,
                        .zone_masks = working_zone_masks,
                        .spatial_masks = working_spatial_masks,
                    };
                }
                {
                    ScopedStageTimer substage(response.engine, "export_image_render_solve_plan");
                    render_plan = RenderPlanSolver().solve(
                        *solver_input,
                        *stock_profile,
                        build_solver_controls(request),
                        print_stock_profile.has_value() ? &*print_stock_profile : nullptr);
                }
                render_plan->material_effects.grain_seed = compute_stable_grain_seed(
                    response.filename,
                    request.stock,
                    request.print_stock,
                    render_plan->material_effects);

                append_export_trace(project_root_, "export_image:resize_masks_for_fullres:start");
                ZoneMasks fullres_zone_masks;
                SpatialMasks fullres_spatial_masks;
                {
                    ScopedStageTimer substage(response.engine, "export_image_render_resize_masks");
                    fullres_zone_masks = resize_zone_masks(working_zone_masks, decoded.rgb_linear.width, decoded.rgb_linear.height);
                    fullres_spatial_masks = resize_spatial_masks(working_spatial_masks, decoded.rgb_linear.width, decoded.rgb_linear.height);
                }
                append_export_trace(project_root_, "export_image:resize_masks_for_fullres:done");

                append_export_trace(project_root_, "export_image:fullres_prefilm:start");
                Image fullres_prefilm;
                {
                    ScopedStageTimer substage(response.engine, "export_image_render_prefilm_fullres");
                    fullres_prefilm = apply_pre_film_preview_sliders(decoded.rgb_linear, request, *render_plan);
                }
                append_export_trace(project_root_, "export_image:fullres_prefilm:done");

                append_export_trace(project_root_, "export_image:fullres_renderer:start");
                FilmRenderer renderer;
                {
                    ScopedStageTimer substage(response.engine, "export_image_render_film_fullres");
                    {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_pre_film_normalization");
                        rendered = renderer.apply_pre_film_normalization(
                            fullres_prefilm,
                            fullres_zone_masks,
                            render_plan->pre_film_normalization);
                    }
                    if (render_plan->stock_type == "monochrome") {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_panchromatic");
                        rendered = renderer.apply_panchromatic_conversion(rendered, render_plan->film_response);
                    }
                    {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_tone_response");
                        rendered = renderer.apply_film_tone_response(rendered, render_plan->film_response);
                    }
                    {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_color_response");
                        if (render_plan->stock_type != "monochrome") {
                            rendered = renderer.apply_color_response_and_coupling(
                                rendered,
                                fullres_zone_masks,
                                render_plan->film_response);
                        }
                    }
                    {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_acutance");
                        rendered = renderer.apply_acutance_shaping(rendered, render_plan->material_effects);
                    }
                    {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_halation_bloom");
                        rendered = renderer.apply_halation_bloom(
                            rendered,
                            fullres_zone_masks,
                            fullres_spatial_masks,
                            render_plan->material_effects);
                    }
                    {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_grain");
                        rendered = renderer.apply_film_grain(rendered, fullres_spatial_masks, render_plan->material_effects);
                    }
                    if (render_plan->print_finish.has_value()) {
                        ScopedStageTimer film_stage(response.engine, "export_image_render_stage_print_finish");
                        rendered = renderer.apply_print_finish(rendered, *render_plan->print_finish);
                    }
                }
                append_export_trace(project_root_, "export_image:fullres_renderer:done");

                append_export_trace(project_root_, "export_image:fullres_post:start");
                {
                    ScopedStageTimer substage(response.engine, "export_image_render_post_fullres");
                    rendered = apply_post_film_color(rendered, request);
                    rendered = apply_curves(rendered, request.curves);
                    rendered = apply_hsl(rendered, request);
                    rendered = renderer.apply_clarity(rendered, request.clarity);
                    rendered = renderer.apply_texture(rendered, request.texture);
                    rendered = renderer.apply_dehaze(rendered, request.dehaze);
                    rendered = apply_post_bloom(rendered, request.bloom);
                }
                append_export_trace(project_root_, "export_image:fullres_post:done");
                append_export_trace(project_root_, "export_image:render:done");
            }
        }

        {
            ScopedStageTimer stage(response.engine, "export_image_write_output");
            append_export_trace(project_root_, "export_image:write_output:start");
            const std::string stock_label = request.stock.empty() ? "none" : request.stock;
            if (canonical_format == "tiff") {
                response.output_path = raw_dir_ / (basename + "_" + stock_label + "_dfee.tif");
                response.format_label = "16-bit TIFF";
            } else if (canonical_format == "png16") {
                response.output_path = raw_dir_ / (basename + "_" + stock_label + "_dfee_16.png");
                response.format_label = "16-bit PNG";
            } else {
                response.output_path = raw_dir_ / (basename + "_" + stock_label + "_dfee.png");
                response.format_label = "8-bit PNG";
            }

            cv::Mat output_mat(rendered.height, rendered.width, canonical_format == "png8" ? CV_8UC3 : CV_16UC3);
            for (int y = 0; y < rendered.height; ++y) {
                for (int x = 0; x < rendered.width; ++x) {
                    const float r = linear_to_srgb_channel(rendered.at(x, y, 0));
                    const float g = linear_to_srgb_channel(rendered.at(x, y, 1));
                    const float b = linear_to_srgb_channel(rendered.at(x, y, 2));
                    if (canonical_format == "png8") {
                        auto& pixel = output_mat.at<cv::Vec3b>(y, x);
                        pixel[0] = static_cast<std::uint8_t>(std::clamp(b * 255.0F, 0.0F, 255.0F));
                        pixel[1] = static_cast<std::uint8_t>(std::clamp(g * 255.0F, 0.0F, 255.0F));
                        pixel[2] = static_cast<std::uint8_t>(std::clamp(r * 255.0F, 0.0F, 255.0F));
                    } else {
                        auto& pixel = output_mat.at<cv::Vec<uint16_t, 3>>(y, x);
                        pixel[0] = static_cast<std::uint16_t>(std::clamp(b * 65535.0F, 0.0F, 65535.0F));
                        pixel[1] = static_cast<std::uint16_t>(std::clamp(g * 65535.0F, 0.0F, 65535.0F));
                        pixel[2] = static_cast<std::uint16_t>(std::clamp(r * 65535.0F, 0.0F, 65535.0F));
                    }
                }
            }
            if (!cv::imwrite(response.output_path.string(), output_mat)) {
                response.status = "error";
                response.error = {
                    .code = "EXPORT_WRITE_FAILED",
                    .user_message = "The native export could not be written to disk.",
                    .detail = "cv::imwrite returned false for " + response.output_path.string(),
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
            append_export_trace(project_root_, "export_image:write_output:done");
        }

        if (render_plan.has_value() && solver_input.has_value() && stock_profile.has_value()) {
            ScopedStageTimer stage(response.engine, "export_image_write_report");
            append_export_trace(project_root_, "export_image:write_report:start");
            response.report_path = raw_dir_ / (basename + "_" + request.stock + "_report.json");
            try {
                write_text_file(
                    response.report_path,
                    serialize_feature_report_json(
                        *solver_input,
                        *render_plan,
                        *stock_profile,
                        print_stock_profile.has_value() ? &*print_stock_profile : nullptr,
                        response.filename,
                        response.output_path.filename().string()));
            } catch (const std::exception& ex) {
                response.status = "error";
                response.error = {
                    .code = "EXPORT_REPORT_WRITE_FAILED",
                    .user_message = "The native export report could not be written.",
                    .detail = ex.what(),
                };
                finalize_engine_metadata(response.engine);
                return response;
            }
            append_export_trace(project_root_, "export_image:write_report:done");
        }

        response.ok = true;
        response.status = "success";
        append_export_trace(project_root_, "export_image:done");
    }

    finalize_engine_metadata(response.engine);
    return response;
#endif
}

NativeSessionCacheStateResponse EngineSession::cache_state() const {
    NativeSessionCacheStateResponse response;
    response.ok = true;
    response.engine = build_engine_metadata();
    response.cache.selected_filename = selected_filename_;
    if (draft_decode_cache_.has_value()) {
        response.cache.draft_decode_cached = true;
        response.cache.draft_width = draft_decode_cache_->decoded.summary.image_width;
        response.cache.draft_height = draft_decode_cache_->decoded.summary.image_height;
    }
    if (preview_cache_.has_value()) {
        response.cache.preview_cached = true;
        response.cache.preview_width = preview_cache_->rgb_linear.width;
        response.cache.preview_height = preview_cache_->rgb_linear.height;
    }
    if (raw_preview_jpeg_cache_.has_value()) {
        response.cache.raw_preview_jpeg_cached = true;
        response.cache.raw_preview_jpeg_bytes = raw_preview_jpeg_cache_->jpeg_bytes.size();
    }
    if (full_decode_cache_.has_value()) {
        response.cache.full_decode_cached = true;
        response.cache.full_width = full_decode_cache_->decoded.summary.image_width;
        response.cache.full_height = full_decode_cache_->decoded.summary.image_height;
    }
    finalize_engine_metadata(response.engine);
    return response;
}

CudaStatus EngineSession::cuda_status() const noexcept {
    return query_cuda_status();
}

std::string EngineSession::resolve_filename(const std::string& filename) const {
    if (!filename.empty()) {
        return filename;
    }
    return selected_filename_;
}

void EngineSession::clear_decode_caches() {
    draft_decode_cache_.reset();
    full_decode_cache_.reset();
    preview_cache_.reset();
    raw_preview_jpeg_cache_.reset();
    preview_analysis_cache_.reset();
    export_analysis_cache_.reset();
}

void EngineSession::refresh_preview_cache_from_draft() {
    if (!draft_decode_cache_.has_value()) {
        preview_cache_.reset();
        raw_preview_jpeg_cache_.reset();
        preview_analysis_cache_.reset();
        return;
    }

    CachedPreview preview;
    preview.filename = draft_decode_cache_->filename;
    preview.rgb_linear = resize_image_to_max_edge(draft_decode_cache_->decoded.rgb_linear, 1024);
    preview.luminance = compute_luminance(preview.rgb_linear);
    preview_cache_ = std::move(preview);
    raw_preview_jpeg_cache_.reset();
    preview_analysis_cache_.reset();
}

NativeRawPreviewResponse EngineSession::encode_raw_preview(const std::string& filename, const int max_edge) const {
    NativeRawPreviewResponse response;
    response.filename = filename;

#if !DFEE_HAS_OPENCV
    response.status = "unavailable";
    response.error = {
        .code = "OPENCV_UNAVAILABLE",
        .user_message = "Native RAW preview encoding is not available in this build.",
        .detail = "DFEE was built without OpenCV discovery; configure with the windows-msvc-vcpkg preset.",
    };
    return response;
#else
    if (!preview_cache_.has_value() || preview_cache_->filename != filename) {
        response.status = "error";
        response.error = {
            .code = "RAW_PREVIEW_NOT_CACHED",
            .user_message = "A draft RAW decode is required before serving the native RAW preview.",
            .detail = "No preview cache was present for filename: " + filename,
        };
        return response;
    }

    const Image preview_rgb = (max_edge > 0 && std::max(preview_cache_->rgb_linear.width, preview_cache_->rgb_linear.height) > max_edge)
        ? resize_image_to_max_edge(preview_cache_->rgb_linear, max_edge)
        : preview_cache_->rgb_linear;

    response = encode_preview_jpeg_bytes(preview_rgb, filename);
    if (!response.ok && response.error.code == "PREVIEW_RENDER_ENCODE_FAILED") {
        response.error.code = "RAW_PREVIEW_ENCODE_FAILED";
        response.error.user_message = "The native RAW preview could not be encoded as JPEG.";
    }
    return response;
#endif
}

}  // namespace dfee
