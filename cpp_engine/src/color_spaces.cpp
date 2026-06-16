#include "dfee/color_spaces.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace dfee {
namespace {

void require_rgb_like(const Image& image, const char* operation) {
    if (image.channels != 3) {
        throw std::invalid_argument(std::string(operation) + " expects a 3-channel image");
    }
}

}  // namespace

Image rgb_to_oklab(const Image& rgb) {
    require_rgb_like(rgb, "rgb_to_oklab");
    Image out(rgb.width, rgb.height, 3);

    for (std::size_t i = 0; i < rgb.pixel_count(); ++i) {
        const float r = std::max(rgb.pixels[i * 3 + 0], 1.0e-12F);
        const float g = std::max(rgb.pixels[i * 3 + 1], 1.0e-12F);
        const float b = std::max(rgb.pixels[i * 3 + 2], 1.0e-12F);

        const float l = 0.4122214708F * r + 0.5363325363F * g + 0.0514459929F * b;
        const float m = 0.2119034982F * r + 0.6806995451F * g + 0.1073969566F * b;
        const float s = 0.0883024619F * r + 0.2817188376F * g + 0.6299787005F * b;

        const float lp = std::cbrt(l);
        const float mp = std::cbrt(m);
        const float sp = std::cbrt(s);

        out.pixels[i * 3 + 0] = 0.2104542553F * lp + 0.7936177850F * mp - 0.0040720468F * sp;
        out.pixels[i * 3 + 1] = 1.9779984951F * lp - 2.4285922050F * mp + 0.4505937099F * sp;
        out.pixels[i * 3 + 2] = 0.0259040371F * lp + 0.7827717612F * mp - 0.8086757983F * sp;
    }

    return out;
}

Image oklab_to_rgb(const Image& oklab) {
    require_rgb_like(oklab, "oklab_to_rgb");
    Image out(oklab.width, oklab.height, 3);

    for (std::size_t i = 0; i < oklab.pixel_count(); ++i) {
        const float L = oklab.pixels[i * 3 + 0];
        const float a = oklab.pixels[i * 3 + 1];
        const float b = oklab.pixels[i * 3 + 2];

        const float lp = L + 0.3963377774F * a + 0.2158017574F * b;
        const float mp = L - 0.1055613458F * a - 0.0638541728F * b;
        const float sp = L - 0.0894841775F * a - 1.2914855480F * b;

        const float l = lp * lp * lp;
        const float m = mp * mp * mp;
        const float s = sp * sp * sp;

        out.pixels[i * 3 + 0] = clamp01(4.0767416621F * l - 3.3077115913F * m + 0.2309699292F * s);
        out.pixels[i * 3 + 1] = clamp01(-1.2684380046F * l + 2.6097574011F * m - 0.3413193965F * s);
        out.pixels[i * 3 + 2] = clamp01(-0.0041960863F * l - 0.7034186147F * m + 1.7076147010F * s);
    }

    return out;
}

Image oklab_to_oklch(const Image& oklab) {
    require_rgb_like(oklab, "oklab_to_oklch");
    Image out(oklab.width, oklab.height, 3);

    for (std::size_t i = 0; i < oklab.pixel_count(); ++i) {
        const float L = oklab.pixels[i * 3 + 0];
        const float a = oklab.pixels[i * 3 + 1];
        const float b = oklab.pixels[i * 3 + 2];
        float hue = std::atan2(b, a);
        if (hue < 0.0F) {
            hue += 2.0F * std::numbers::pi_v<float>;
        }

        out.pixels[i * 3 + 0] = L;
        out.pixels[i * 3 + 1] = std::sqrt(a * a + b * b);
        out.pixels[i * 3 + 2] = hue;
    }

    return out;
}

Image oklch_to_oklab(const Image& oklch) {
    require_rgb_like(oklch, "oklch_to_oklab");
    Image out(oklch.width, oklch.height, 3);

    for (std::size_t i = 0; i < oklch.pixel_count(); ++i) {
        const float L = oklch.pixels[i * 3 + 0];
        const float C = oklch.pixels[i * 3 + 1];
        const float H = oklch.pixels[i * 3 + 2];
        out.pixels[i * 3 + 0] = L;
        out.pixels[i * 3 + 1] = C * std::cos(H);
        out.pixels[i * 3 + 2] = C * std::sin(H);
    }

    return out;
}

}  // namespace dfee
