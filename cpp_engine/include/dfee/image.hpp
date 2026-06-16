#pragma once

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace dfee {

struct Image {
    int width = 0;
    int height = 0;
    int channels = 3;
    std::vector<float> pixels;

    Image() = default;
    Image(int image_width, int image_height, int image_channels = 3);

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t pixel_count() const noexcept;
    [[nodiscard]] std::size_t value_count() const noexcept;

    float& at(int x, int y, int channel);
    const float& at(int x, int y, int channel) const;
};

struct LuminanceImage {
    int width = 0;
    int height = 0;
    std::vector<float> values;

    LuminanceImage() = default;
    LuminanceImage(int image_width, int image_height);

    [[nodiscard]] bool empty() const noexcept;
    float& at(int x, int y);
    const float& at(int x, int y) const;
};

[[nodiscard]] float clamp01(float value) noexcept;
[[nodiscard]] LuminanceImage compute_luminance(const Image& rgb);

}  // namespace dfee
