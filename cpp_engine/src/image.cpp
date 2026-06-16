#include "dfee/image.hpp"

#include <algorithm>

namespace dfee {

Image::Image(int image_width, int image_height, int image_channels)
    : width(image_width),
      height(image_height),
      channels(image_channels),
      pixels(static_cast<std::size_t>(image_width) * static_cast<std::size_t>(image_height) *
             static_cast<std::size_t>(image_channels), 0.0F) {
    if (width < 0 || height < 0 || channels <= 0) {
        throw std::invalid_argument("Invalid image dimensions");
    }
}

bool Image::empty() const noexcept {
    return width <= 0 || height <= 0 || channels <= 0 || pixels.empty();
}

std::size_t Image::pixel_count() const noexcept {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

std::size_t Image::value_count() const noexcept {
    return pixel_count() * static_cast<std::size_t>(channels);
}

float& Image::at(int x, int y, int channel) {
    return pixels[(static_cast<std::size_t>(y) * width + x) * channels + channel];
}

const float& Image::at(int x, int y, int channel) const {
    return pixels[(static_cast<std::size_t>(y) * width + x) * channels + channel];
}

LuminanceImage::LuminanceImage(int image_width, int image_height)
    : width(image_width),
      height(image_height),
      values(static_cast<std::size_t>(image_width) * static_cast<std::size_t>(image_height), 0.0F) {
    if (width < 0 || height < 0) {
        throw std::invalid_argument("Invalid luminance dimensions");
    }
}

bool LuminanceImage::empty() const noexcept {
    return width <= 0 || height <= 0 || values.empty();
}

float& LuminanceImage::at(int x, int y) {
    return values[static_cast<std::size_t>(y) * width + x];
}

const float& LuminanceImage::at(int x, int y) const {
    return values[static_cast<std::size_t>(y) * width + x];
}

float clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

LuminanceImage compute_luminance(const Image& rgb) {
    if (rgb.channels != 3) {
        throw std::invalid_argument("compute_luminance expects a 3-channel RGB image");
    }

    LuminanceImage luminance(rgb.width, rgb.height);
    for (int y = 0; y < rgb.height; ++y) {
        for (int x = 0; x < rgb.width; ++x) {
            luminance.at(x, y) = 0.2126F * rgb.at(x, y, 0) +
                                 0.7152F * rgb.at(x, y, 1) +
                                 0.0722F * rgb.at(x, y, 2);
        }
    }
    return luminance;
}

}  // namespace dfee
