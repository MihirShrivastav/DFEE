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
#include <filesystem>
#include <numbers>
#include <optional>
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
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            auto& pixel = mat.at<cv::Vec3f>(y, x);
            pixel[0] = gamma_encode_22(image.at(x, y, 0));
            pixel[1] = gamma_encode_22(image.at(x, y, 1));
            pixel[2] = gamma_encode_22(image.at(x, y, 2));
        }
    }
    return mat;
}

Image gamma_decode_mat_22(const cv::Mat& mat) {
    Image image(mat.cols, mat.rows, 3);
    for (int y = 0; y < mat.rows; ++y) {
        for (int x = 0; x < mat.cols; ++x) {
            const auto& pixel = mat.at<cv::Vec3f>(y, x);
            image.at(x, y, 0) = gamma_decode_22(pixel[0]);
            image.at(x, y, 1) = gamma_decode_22(pixel[1]);
            image.at(x, y, 2) = gamma_decode_22(pixel[2]);
        }
    }
    return image;
}

cv::Mat compute_gamma_luminance(const cv::Mat& gamma_rgb) {
    cv::Mat luminance(gamma_rgb.rows, gamma_rgb.cols, CV_32F);
    for (int y = 0; y < gamma_rgb.rows; ++y) {
        for (int x = 0; x < gamma_rgb.cols; ++x) {
            const auto& pixel = gamma_rgb.at<cv::Vec3f>(y, x);
            luminance.at<float>(y, x) = 0.2126F * pixel[0] + 0.7152F * pixel[1] + 0.0722F * pixel[2];
        }
    }
    return luminance;
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
    cv::Mat luminance = compute_gamma_luminance(gamma_rgb);

    const auto recompute_luminance = [&]() {
        luminance = compute_gamma_luminance(gamma_rgb);
    };

    if (contrast_value != 0.0F) {
        const float k = std::clamp(contrast_value / 100.0F, -1.0F, 1.0F) * 2.5F;
        if (std::fabs(k) > 0.02F) {
            const float denom = std::tanh(k * 0.5F);
            if (std::fabs(denom) > 1.0e-6F) {
                for (int y = 0; y < gamma_rgb.rows; ++y) {
                    for (int x = 0; x < gamma_rgb.cols; ++x) {
                        auto& pixel = gamma_rgb.at<cv::Vec3f>(y, x);
                        for (int c = 0; c < 3; ++c) {
                            pixel[c] = clamp01(0.5F + std::tanh(k * (pixel[c] - 0.5F)) / (2.0F * denom));
                        }
                    }
                }
                recompute_luminance();
            }
        }
    }

    const auto apply_additive_tonal = [&](const float slider_value, const float scale, const float pivot, const float range, const float power, const bool highlights_side) {
        if (slider_value == 0.0F) {
            return;
        }
        const float amount = std::clamp(slider_value / 100.0F, -1.0F, 1.0F) * scale;
        for (int y = 0; y < gamma_rgb.rows; ++y) {
            for (int x = 0; x < gamma_rgb.cols; ++x) {
                float weight = 0.0F;
                const float y_value = luminance.at<float>(y, x);
                if (highlights_side) {
                    weight = std::pow(std::clamp((y_value - pivot) / range, 0.0F, 1.0F), power);
                } else {
                    weight = std::pow(std::clamp((pivot - y_value) / range, 0.0F, 1.0F), power);
                }
                auto& pixel = gamma_rgb.at<cv::Vec3f>(y, x);
                pixel[0] = clamp01(pixel[0] + amount * weight);
                pixel[1] = clamp01(pixel[1] + amount * weight);
                pixel[2] = clamp01(pixel[2] + amount * weight);
            }
        }
        recompute_luminance();
    };

    apply_additive_tonal(highlights_value, 0.45F, 0.30F, 0.70F, 1.5F, true);
    apply_additive_tonal(shadows_value, 0.50F, 0.65F, 0.65F, 1.5F, false);
    apply_additive_tonal(whites_value, 0.30F, 0.60F, 0.40F, 2.0F, true);
    apply_additive_tonal(blacks_value, 0.25F, 0.35F, 0.35F, 2.0F, false);

    if (midtones_value != 0.0F) {
        const float gamma = std::clamp(1.0F / (1.0F + midtones_value * 0.01F), 0.2F, 5.0F);
        for (int y = 0; y < gamma_rgb.rows; ++y) {
            for (int x = 0; x < gamma_rgb.cols; ++x) {
                const float y_value = luminance.at<float>(y, x);
                const float weight = std::clamp(1.0F - std::pow(2.0F * y_value - 1.0F, 2.0F), 0.0F, 1.0F);
                auto& pixel = gamma_rgb.at<cv::Vec3f>(y, x);
                for (int c = 0; c < 3; ++c) {
                    const float bent = std::pow(std::clamp(pixel[c], 1.0e-8F, 1.0F), gamma);
                    pixel[c] = clamp01(pixel[c] * (1.0F - weight) + bent * weight);
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
        }

        Image rendered = preview.rgb_linear;
        {
            ScopedStageTimer stage(response.engine, "render_preview_apply_pre_film_sliders");
            rendered = apply_pre_film_preview_sliders(preview.rgb_linear, request, render_plan);
        }
        {
            ScopedStageTimer stage(response.engine, "render_preview_film_pipeline");
            FilmRenderer renderer;
            rendered = renderer.render(rendered, zone_masks, spatial_masks, render_plan);
        }
        {
            ScopedStageTimer stage(response.engine, "render_preview_post_color");
            rendered = apply_post_film_color(rendered, request);
        }
        {
            ScopedStageTimer stage(response.engine, "render_preview_post_effects");
            rendered = apply_curves(rendered, request.curves);
            rendered = apply_hsl(rendered, request);
            FilmRenderer renderer;
            rendered = renderer.apply_clarity(rendered, request.clarity);
            rendered = renderer.apply_texture(rendered, request.texture);
            rendered = renderer.apply_dehaze(rendered, request.dehaze);
            rendered = apply_post_bloom(rendered, request.bloom);
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
}

void EngineSession::refresh_preview_cache_from_draft() {
    if (!draft_decode_cache_.has_value()) {
        preview_cache_.reset();
        raw_preview_jpeg_cache_.reset();
        return;
    }

    CachedPreview preview;
    preview.filename = draft_decode_cache_->filename;
    preview.rgb_linear = resize_image_to_max_edge(draft_decode_cache_->decoded.rgb_linear, 1024);
    preview.luminance = compute_luminance(preview.rgb_linear);
    preview_cache_ = std::move(preview);
    raw_preview_jpeg_cache_.reset();
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
