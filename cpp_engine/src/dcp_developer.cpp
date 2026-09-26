// DNG-camera-profile RAW developer. See dfee/dcp_developer.hpp.
//
// Portions derived from the Adobe DNG SDK (dng_render.cpp, dng_color_spec.cpp,
// dng_reference.cpp, dng_temperature.cpp, dng_camera_profile.cpp).
// Copyright 2006-2012 Adobe Systems Incorporated. Used under the Adobe DNG SDK license.

#include "dfee/dcp_developer.hpp"

// Needs LibRaw (camera-RGB decode) and OpenCV (image buffers); compiled out otherwise.
#if DFEE_HAS_LIBRAW && DFEE_HAS_OPENCV

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>

#include <regex>
#include <sstream>

#include <yaml-cpp/yaml.h>

#include <zlib.h>

#if __has_include(<libraw/libraw.h>)
#include <libraw/libraw.h>
#else
#include <libraw.h>
#endif

namespace fs = std::filesystem;

namespace dfee::dcp {
namespace {

// ---------------------------------------------------------------------------
// Small linear algebra
// ---------------------------------------------------------------------------
using Vec3 = std::array<double, 3>;

Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
    return r;
}
Vec3 mul(const Mat3& a, const Vec3& v) {
    return {a[0] * v[0] + a[1] * v[1] + a[2] * v[2], a[3] * v[0] + a[4] * v[1] + a[5] * v[2],
            a[6] * v[0] + a[7] * v[1] + a[8] * v[2]};
}
Mat3 scaled(const Mat3& a, double s) {
    Mat3 r = a;
    for (double& v : r) v *= s;
    return r;
}
Mat3 lerp(const Mat3& a, const Mat3& b, double g) {  // g*a + (1-g)*b
    Mat3 r{};
    for (int i = 0; i < 9; ++i) r[i] = g * a[i] + (1.0 - g) * b[i];
    return r;
}
Mat3 diag(const Vec3& v) { return {v[0], 0, 0, 0, v[1], 0, 0, 0, v[2]}; }
Mat3 invert(const Mat3& m) {
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    const double k = std::abs(det) > 1e-12 ? 1.0 / det : 0.0;
    return {A * k, -(b * i - c * h) * k, (b * f - c * e) * k, B * k, (a * i - c * g) * k, -(a * f - c * d) * k,
            C * k, -(a * h - b * g) * k, (a * e - b * d) * k};
}
double max_entry(const Vec3& v) { return std::max({v[0], v[1], v[2]}); }

// ---------------------------------------------------------------------------
// Colorimetry (dng_xy_coord / dng_temperature / dng_color_spec)
// ---------------------------------------------------------------------------
struct XY {
    double x, y;
};
constexpr XY kD50{0.3457, 0.3585};

Vec3 xy_to_xyz(const XY& c) {
    const double x = std::clamp(c.x, 1e-6, 0.999999), y = std::clamp(c.y, 1e-6, 0.999999);
    return {x / y, 1.0, (1.0 - x - y) / y};
}
XY xyz_to_xy(const Vec3& v) {
    const double t = v[0] + v[1] + v[2];
    if (t <= 0.0) return kD50;
    return {v[0] / t, v[1] / t};
}
const Vec3 kPcsXYZ = xy_to_xyz(kD50);

struct Ruvt {
    double r, u, v, t;
};
const Ruvt kTempTable[] = {
#include "dcp/temp_table.inc"
};

double xy_to_temperature(const XY& xy) {  // Robertson's method, as dng_temperature
    const double u = 2.0 * xy.x / (1.5 - xy.x + 6.0 * xy.y);
    const double v = 3.0 * xy.y / (1.5 - xy.x + 6.0 * xy.y);
    double last_dt = 0.0;
    for (int index = 1; index <= 30; ++index) {
        double du = 1.0, dv = kTempTable[index].t;
        const double len = std::sqrt(1.0 + dv * dv);
        du /= len;
        dv /= len;
        const double uu = u - kTempTable[index].u, vv = v - kTempTable[index].v;
        double dt = -uu * dv + vv * du;
        if (dt <= 0.0 || index == 30) {
            if (dt > 0.0) dt = 0.0;
            dt = -dt;
            const double f = index == 1 ? 0.0 : dt / (last_dt + dt);
            return 1.0e6 / (kTempTable[index - 1].r * f + kTempTable[index].r * (1.0 - f));
        }
        last_dt = dt;
    }
    return 5000.0;
}

double illuminant_to_temperature(int light) {  // dng_camera_profile::IlluminantToTemperature
    switch (light) {
        case 17: case 3: return 2850.0;               // Standard A, Tungsten
        case 24: return 3200.0;                       // ISO studio tungsten
        case 23: return 5000.0;                       // D50
        case 20: case 1: case 9: case 4: case 18: return 5500.0;  // D55, daylight, fine, flash, B
        case 21: case 19: case 10: return 6500.0;     // D65, C, cloudy
        case 22: case 11: return 7500.0;              // D75, shade
        case 12: return (5700.0 + 7100.0) * 0.5;
        case 13: return (4600.0 + 5500.0) * 0.5;
        case 14: case 2: return (3800.0 + 4500.0) * 0.5;
        case 15: return (3250.0 + 3800.0) * 0.5;
        case 16: return (2600.0 + 3250.0) * 0.5;
        default: return 0.0;
    }
}

Mat3 map_white_matrix(const XY& w1, const XY& w2) {  // linearised Bradford
    const Mat3 mb{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
    Vec3 a = mul(mb, xy_to_xyz(w1)), b = mul(mb, xy_to_xyz(w2));
    Vec3 s{};
    for (int i = 0; i < 3; ++i) {
        a[i] = std::max(a[i], 0.0);
        b[i] = std::max(b[i], 0.0);
        s[i] = std::clamp(a[i] > 0.0 ? b[i] / a[i] : 10.0, 0.1, 10.0);
    }
    return mul(mul(invert(mb), diag(s)), mb);
}

Mat3 normalize_forward(const Mat3& m) {
    const Vec3 xyz = mul(m, Vec3{1.0, 1.0, 1.0});
    return mul(mul(diag(kPcsXYZ), invert(diag(xyz))), m);
}

// Profile prepared for a render (dng_color_spec ctor).
struct Spec {
    double t1 = 5000, t2 = 5000;
    Mat3 c1{}, c2{}, f1{}, f2{};
    bool has_forward = false;
};

Spec make_spec(const Profile& p) {
    Spec s;
    s.t1 = illuminant_to_temperature(p.illuminant1);
    s.t2 = illuminant_to_temperature(p.illuminant2);
    s.c1 = p.color1;
    s.c2 = p.color2;
    s.f1 = p.has_forward1 ? normalize_forward(p.forward1) : Mat3{};
    s.f2 = p.has_forward2 ? normalize_forward(p.forward2) : s.f1;
    s.has_forward = p.has_forward1;
    if (!p.has_color2 || s.t1 <= 0 || s.t2 <= 0 || s.t1 == s.t2) {
        s.t1 = s.t2 = 5000.0;
        s.c2 = s.c1;
        s.f2 = s.f1;
    } else if (s.t1 > s.t2) {
        std::swap(s.t1, s.t2);
        std::swap(s.c1, s.c2);
        std::swap(s.f1, s.f2);
    }
    return s;
}

// Weight of calibration 1 for a white point (dng_color_spec::FindXYZtoCamera).
double weight_for(const Spec& s, const XY& white) {
    const double t = xy_to_temperature(white);
    if (t <= s.t1) return 1.0;
    if (t >= s.t2) return 0.0;
    return (1.0 / t - 1.0 / s.t2) / (1.0 / s.t1 - 1.0 / s.t2);
}

XY neutral_to_xy(const Spec& s, const Vec3& neutral) {
    XY last = kD50;
    for (int pass = 0; pass < 30; ++pass) {
        const Mat3 xyz_to_camera = lerp(s.c1, s.c2, weight_for(s, last));
        XY next = xyz_to_xy(mul(invert(xyz_to_camera), neutral));
        if (std::abs(next.x - last.x) + std::abs(next.y - last.y) < 1e-7) return next;
        if (pass == 29) next = {(last.x + next.x) * 0.5, (last.y + next.y) * 0.5};
        last = next;
    }
    return last;
}

// ---------------------------------------------------------------------------
// Tone functions (dng_render.cpp)
// ---------------------------------------------------------------------------
const float kAcr3[] = {
#include "dcp/acr3_table.inc"
};
constexpr int kAcr3Size = static_cast<int>(sizeof(kAcr3) / sizeof(kAcr3[0]));

double acr3(double x) {
    const double y = std::clamp(x, 0.0, 1.0) * (kAcr3Size - 1);
    const int i = std::clamp(static_cast<int>(y), 0, kAcr3Size - 2);
    const double f = y - i;
    return kAcr3[i] * (1.0 - f) + kAcr3[i + 1] * f;
}

// Natural cubic spline through the profile tone curve points (dng_spline_solver).
struct Spline {
    std::vector<double> x, y, s;
    explicit Spline(const std::vector<std::pair<float, float>>& pts) {
        for (const auto& [px, py] : pts) {
            x.push_back(px);
            y.push_back(py);
        }
        const std::size_t n = x.size();
        s.assign(n, 0.0);
        if (n < 3) return;
        std::vector<double> a(n), b(n), c(n), r(n);
        for (std::size_t i = 1; i + 1 < n; ++i) {
            const double h0 = x[i] - x[i - 1], h1 = x[i + 1] - x[i];
            a[i] = h0;
            b[i] = 2.0 * (h0 + h1);
            c[i] = h1;
            r[i] = 6.0 * ((y[i + 1] - y[i]) / h1 - (y[i] - y[i - 1]) / h0);
        }
        for (std::size_t i = 2; i + 1 < n; ++i) {  // Thomas algorithm
            const double m = a[i] / b[i - 1];
            b[i] -= m * c[i - 1];
            r[i] -= m * r[i - 1];
        }
        for (std::size_t i = n - 2; i >= 1; --i) {
            s[i] = (r[i] - c[i] * s[i + 1]) / b[i];
            if (i == 1) break;
        }
    }
    [[nodiscard]] double eval(double v) const {
        if (x.empty()) return v;
        if (v <= x.front()) return y.front();
        if (v >= x.back()) return y.back();
        const std::size_t i =
            static_cast<std::size_t>(std::upper_bound(x.begin(), x.end(), v) - x.begin()) - 1;
        const double h = x[i + 1] - x[i], t = (v - x[i]) / h, u = 1.0 - t;
        return u * y[i] + t * y[i + 1] + ((u * u * u - u) * s[i] + (t * t * t - t) * s[i + 1]) * h * h / 6.0;
    }
};

struct ExposureRamp {  // dng_function_exposure_ramp
    double slope, black, radius = 0, qscale = 0;
    ExposureRamp(double white, double blk, double min_black) : slope(1.0 / (white - blk)), black(blk) {
        radius = std::min(0.5 * min_black, (1.0 / 16.0) / slope);
        qscale = radius > 0.0 ? slope / (4.0 * radius) : 0.0;
    }
    [[nodiscard]] double eval(double x) const {
        if (x <= black - radius) return 0.0;
        if (x >= black + radius) return std::min((x - black) * slope, 1.0);
        const double y = x - (black - radius);
        return qscale * y * y;
    }
};

double exposure_tone(double x, double exposure) {  // dng_function_exposure_tone (darkening only)
    if (exposure >= 0.0) return x;
    const double s = std::pow(2.0, exposure);
    const double a = 16.0 / 9.0 * (1.0 - s), b = s - 0.5 * a, c = 1.0 - a - b;
    return x <= 0.25 ? x * s : (a * x + b) * x + c;
}

double srgb_encode(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x <= 0.0031308 ? 12.92 * x : 1.055 * std::pow(x, 1.0 / 2.4) - 0.055;
}
double srgb_decode(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
}

// ---------------------------------------------------------------------------
// HSV + hue/sat map (dng_utils.h / dng_reference.cpp RefBaselineHueSatMap)
// ---------------------------------------------------------------------------
void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v) {
    v = std::max({r, g, b});
    const float gap = v - std::min({r, g, b});
    if (gap > 0.0F) {
        if (r == v) {
            h = (g - b) / gap;
            if (h < 0.0F) h += 6.0F;
        } else if (g == v) {
            h = 2.0F + (b - r) / gap;
        } else {
            h = 4.0F + (r - g) / gap;
        }
        s = gap / v;
    } else {
        h = 0.0F;
        s = 0.0F;
    }
}

void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b) {
    if (s > 0.0F) {
        if (h < 0.0F) h += 6.0F;
        if (h >= 6.0F) h -= 6.0F;
        const int i = static_cast<int>(h);
        const float f = h - static_cast<float>(i);
        const float p = v * (1.0F - s), q = v * (1.0F - s * f), t = v * (1.0F - s * (1.0F - f));
        switch (i) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    } else {
        r = g = b = v;
    }
}

void apply_hue_sat(const HueSatTable& t, bool srgb_encoded, float& r, float& g, float& b) {
    float h, s, v;
    rgb_to_hsv(r, g, b, h, s, v);
    const float h_scale = t.hue < 2 ? 0.0F : static_cast<float>(t.hue) / 6.0F;
    const float s_scale = static_cast<float>(t.sat - 1);
    const int max_h0 = t.hue - 1, max_s0 = t.sat - 2;
    const int hue_step = t.sat, val_step = t.hue * hue_step;
    const float* base = t.data.data();
    auto at = [&](int idx) { return base + static_cast<std::ptrdiff_t>(idx) * 3; };

    float v_enc = v;
    float hue_shift, sat_scale, val_scale;
    const float hs = h * h_scale, ss = s * s_scale;
    int h0 = static_cast<int>(hs), s0 = std::min(static_cast<int>(ss), max_s0), h1 = h0 + 1;
    if (h0 >= max_h0) {
        h0 = max_h0;
        h1 = 0;
    }
    const float hf1 = hs - static_cast<float>(h0), sf1 = ss - static_cast<float>(s0);
    const float hf0 = 1.0F - hf1, sf0 = 1.0F - sf1;
    if (t.val < 2) {
        const float* e00 = at(h0 * hue_step + s0);
        const float* e01 = at(h1 * hue_step + s0);
        float hs0[3], hs1[3];
        for (int k = 0; k < 3; ++k) {
            hs0[k] = hf0 * e00[k] + hf1 * e01[k];
            hs1[k] = hf0 * e00[3 + k] + hf1 * e01[3 + k];
        }
        hue_shift = sf0 * hs0[0] + sf1 * hs1[0];
        sat_scale = sf0 * hs0[1] + sf1 * hs1[1];
        val_scale = sf0 * hs0[2] + sf1 * hs1[2];
    } else {
        if (srgb_encoded) v_enc = static_cast<float>(srgb_encode(v));
        const float vs = v_enc * static_cast<float>(t.val - 1);
        const int v0 = std::min(static_cast<int>(vs), t.val - 2);
        const float vf1 = vs - static_cast<float>(v0), vf0 = 1.0F - vf1;
        const float* e00 = at(v0 * val_step + h0 * hue_step + s0);
        const float* e01 = at(v0 * val_step + h1 * hue_step + s0);
        const float* e10 = e00 + val_step * 3;
        const float* e11 = e01 + val_step * 3;
        float c0[3], c1[3];
        for (int k = 0; k < 3; ++k) {
            c0[k] = vf0 * (hf0 * e00[k] + hf1 * e01[k]) + vf1 * (hf0 * e10[k] + hf1 * e11[k]);
            c1[k] = vf0 * (hf0 * e00[3 + k] + hf1 * e01[3 + k]) + vf1 * (hf0 * e10[3 + k] + hf1 * e11[3 + k]);
        }
        hue_shift = sf0 * c0[0] + sf1 * c1[0];
        sat_scale = sf0 * c0[1] + sf1 * c1[1];
        val_scale = sf0 * c0[2] + sf1 * c1[2];
    }
    h += hue_shift * (6.0F / 360.0F);
    s = std::min(s * sat_scale, 1.0F);
    v_enc = std::clamp(v_enc * val_scale, 0.0F, 1.0F);
    v = (t.val >= 2 && srgb_encoded) ? static_cast<float>(srgb_decode(v_enc)) : v_enc;
    hsv_to_rgb(h, s, v, r, g, b);
}

// RGB-preserving tone curve (dng_reference.cpp RefBaselineRGBTone).
template <typename F>
void rgb_tone(float& r, float& g, float& b, const F& curve) {
    auto tone3 = [&](float& hi, float& mid, float& lo) {  // hi >= mid >= lo, hi > lo
        const float hi_in = hi, mid_in = mid, lo_in = lo;
        hi = static_cast<float>(curve(hi_in));
        lo = static_cast<float>(curve(lo_in));
        mid = lo + (hi - lo) * (mid_in - lo_in) / (hi_in - lo_in);
    };
    if (r >= g) {
        if (g > b) tone3(r, g, b);
        else if (b > r) tone3(b, r, g);
        else if (b > g) tone3(r, b, g);
        else {
            r = static_cast<float>(curve(r));
            g = static_cast<float>(curve(g));
            b = g;
        }
    } else {
        if (r >= b) tone3(g, r, b);
        else if (b > g) tone3(b, g, r);
        else tone3(g, b, r);
    }
}

// ---------------------------------------------------------------------------
// DCP (TIFF-structured) parsing
// ---------------------------------------------------------------------------
struct Reader {
    std::vector<unsigned char> d;
    bool le = true;
    [[nodiscard]] std::uint32_t u16(std::size_t o) const {
        return le ? (d[o] | (d[o + 1] << 8)) : ((d[o] << 8) | d[o + 1]);
    }
    [[nodiscard]] std::uint32_t u32(std::size_t o) const {
        return le ? (std::uint32_t(d[o]) | (std::uint32_t(d[o + 1]) << 8) | (std::uint32_t(d[o + 2]) << 16) |
                     (std::uint32_t(d[o + 3]) << 24))
                  : ((std::uint32_t(d[o]) << 24) | (std::uint32_t(d[o + 1]) << 16) |
                     (std::uint32_t(d[o + 2]) << 8) | std::uint32_t(d[o + 3]));
    }
    [[nodiscard]] float f32(std::size_t o) const {
        const std::uint32_t v = u32(o);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
};

struct Entry {
    std::uint32_t type = 0, count = 0;
    std::size_t offset = 0;  // where the value bytes live
};

std::map<std::uint32_t, Entry> read_ifd(const Reader& r) {
    std::map<std::uint32_t, Entry> tags;
    const std::size_t ifd = r.u32(4);
    if (ifd + 2 > r.d.size()) return tags;
    const std::uint32_t n = r.u16(ifd);
    static const int kSize[] = {0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8};
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::size_t e = ifd + 2 + i * 12;
        if (e + 12 > r.d.size()) break;
        Entry en;
        en.type = r.u16(e + 2);
        en.count = r.u32(e + 4);
        const std::size_t bytes = static_cast<std::size_t>(en.type < 13 ? kSize[en.type] : 1) * en.count;
        en.offset = bytes <= 4 ? e + 8 : r.u32(e + 8);
        if (en.offset + bytes <= r.d.size()) tags[r.u16(e)] = en;
    }
    return tags;
}

std::string read_ascii(const Reader& r, const Entry& e) {
    std::string s(reinterpret_cast<const char*>(r.d.data() + e.offset), e.count);
    return s.substr(0, s.find('\0'));
}
double read_number(const Reader& r, const Entry& e, std::size_t i) {
    switch (e.type) {
        case 3: return r.u16(e.offset + i * 2);
        case 4: return r.u32(e.offset + i * 4);
        case 10: {
            const auto num = static_cast<std::int32_t>(r.u32(e.offset + i * 8));
            const auto den = static_cast<std::int32_t>(r.u32(e.offset + i * 8 + 4));
            return den ? static_cast<double>(num) / den : 0.0;
        }
        case 5: {
            const double den = r.u32(e.offset + i * 8 + 4);
            return den ? r.u32(e.offset + i * 8) / den : 0.0;
        }
        case 11: return r.f32(e.offset + i * 4);
        default: return 0.0;
    }
}
bool read_matrix(const Reader& r, const std::map<std::uint32_t, Entry>& t, std::uint32_t tag, Mat3& m) {
    const auto it = t.find(tag);
    if (it == t.end() || it->second.count != 9) return false;
    for (std::size_t i = 0; i < 9; ++i) m[i] = read_number(r, it->second, i);
    return true;
}
std::vector<float> read_floats(const Reader& r, const Entry& e) {
    std::vector<float> v(e.count);
    for (std::size_t i = 0; i < e.count; ++i) v[i] = static_cast<float>(read_number(r, e, i));
    return v;
}

std::string normalise(std::string s) {
    std::string out;
    for (unsigned char c : s)
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

}  // namespace

std::optional<Profile> load_dcp(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    Reader r;
    r.d.assign(std::istreambuf_iterator<char>(f), {});
    if (r.d.size() < 8) return std::nullopt;
    r.le = r.d[0] == 'I';
    if (r.u16(2) != 0x4352 && r.u16(2) != 42) return std::nullopt;  // 'RC' DCP magic
    const auto t = read_ifd(r);
    Profile p;
    if (auto it = t.find(50708); it != t.end()) p.camera = read_ascii(r, it->second);
    if (auto it = t.find(50936); it != t.end()) p.name = read_ascii(r, it->second);
    if (auto it = t.find(50778); it != t.end()) p.illuminant1 = static_cast<int>(read_number(r, it->second, 0));
    if (auto it = t.find(50779); it != t.end()) p.illuminant2 = static_cast<int>(read_number(r, it->second, 0));
    if (!read_matrix(r, t, 50721, p.color1)) return std::nullopt;
    p.has_color2 = read_matrix(r, t, 50722, p.color2);
    p.has_forward1 = read_matrix(r, t, 50964, p.forward1);
    p.has_forward2 = read_matrix(r, t, 50965, p.forward2);
    auto read_table = [&](std::uint32_t dims_tag, std::uint32_t data_tag, HueSatTable& out) {
        const auto d = t.find(dims_tag), v = t.find(data_tag);
        if (d == t.end() || v == t.end() || d->second.count < 2) return;
        out.hue = static_cast<int>(read_number(r, d->second, 0));
        out.sat = static_cast<int>(read_number(r, d->second, 1));
        out.val = d->second.count > 2 ? static_cast<int>(read_number(r, d->second, 2)) : 1;
        out.data = read_floats(r, v->second);
        if (out.data.size() != static_cast<std::size_t>(out.hue) * out.sat * std::max(1, out.val) * 3) {
            out.data.clear();
        }
    };
    read_table(50937, 50938, p.hue_sat1);
    read_table(50937, 50939, p.hue_sat2);
    read_table(50981, 50982, p.look);
    if (auto it = t.find(51107); it != t.end()) p.hue_sat_encoding = static_cast<int>(read_number(r, it->second, 0));
    if (auto it = t.find(51108); it != t.end()) p.look_encoding = static_cast<int>(read_number(r, it->second, 0));
    if (auto it = t.find(51109); it != t.end()) {
        p.baseline_exposure_offset = static_cast<float>(read_number(r, it->second, 0));
    }
    if (auto it = t.find(50940); it != t.end()) {
        const auto v = read_floats(r, it->second);
        for (std::size_t i = 0; i + 1 < v.size(); i += 2) p.tone_curve.emplace_back(v[i], v[i + 1]);
    }
    return p;
}

std::optional<LookPreset> load_look_preset(const fs::path& xmp_path, std::string& error) {
    std::ifstream f(xmp_path, std::ios::binary);
    if (!f) {
        error = "cannot read " + xmp_path.string();
        return std::nullopt;
    }
    const std::string xmp((std::istreambuf_iterator<char>(f)), {});
    std::smatch m;
    if (!std::regex_search(xmp, m, std::regex(R"(crs:LookTable="([0-9A-F]+)\")"))) {
        error = "no crs:LookTable";
        return std::nullopt;
    }
    const std::string key = "crs:Table_" + m[1].str() + "=\"";
    const auto start = xmp.find(key);
    if (start == std::string::npos) {
        error = "look table payload missing";
        return std::nullopt;
    }
    const auto begin = start + key.size();
    const std::string text = xmp.substr(begin, xmp.find('"', begin) - begin);

    // Text -> binary: Z85-like alphabet adjusted for XMP (dng_big_table::DecodeFromString).
    static const unsigned char kDecode[96] = {
        0xFF, 0x44, 0xFF, 0x54, 0x53, 0x52, 0xFF, 0x49, 0x4B, 0x4C, 0x46, 0x41, 0xFF, 0x3F, 0x3E, 0x45,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x40, 0xFF, 0xFF, 0x42, 0xFF, 0x47,
        0x51, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
        0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x4D, 0xFF, 0x4E, 0x43, 0xFF,
        0x48, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
        0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x4F, 0x4A, 0x50, 0xFF, 0xFF};
    std::vector<unsigned char> bin;
    bin.reserve(text.size() * 4 / 5 + 4);
    std::uint32_t phase = 0, value = 0;
    for (unsigned char e : text) {
        if (e < 32 || e > 127) continue;
        const std::uint32_t d = kDecode[e - 32];
        if (d > 85) continue;
        ++phase;
        if (phase == 1) value = d;
        else if (phase == 2) value += d * 85U;
        else if (phase == 3) value += d * 85U * 85U;
        else if (phase == 4) value += d * 85U * 85U * 85U;
        else {
            value += d * 85U * 85U * 85U * 85U;
            for (int k = 0; k < 4; ++k) bin.push_back(static_cast<unsigned char>(value >> (8 * k)));
            phase = 0;
        }
    }
    for (std::uint32_t k = 0; k + 1 < phase; ++k) bin.push_back(static_cast<unsigned char>(value >> (8 * k)));
    if (bin.size() < 5) {
        error = "look table payload too short";
        return std::nullopt;
    }
    // Binary -> uncompressed stream: 4-byte little-endian size, then zlib data.
    uLongf size = bin[0] | (bin[1] << 8) | (bin[2] << 16) | (static_cast<uLongf>(bin[3]) << 24);
    std::vector<unsigned char> raw(size);
    if (::uncompress(raw.data(), &size, bin.data() + 4, static_cast<uLong>(bin.size() - 4)) != Z_OK) {
        error = "look table zlib decode failed";
        return std::nullopt;
    }
    raw.resize(size);
    Reader r;
    r.d = std::move(raw);
    r.le = true;
    if (r.d.size() < 24) {
        error = "look table stream too short";
        return std::nullopt;
    }
    // dng_look_table::GetStream: magic, version, hue/sat/val divisions, deltas, encoding.
    const std::uint32_t version = r.u32(4);
    LookPreset p;
    p.look.hue = static_cast<int>(r.u32(8));
    p.look.sat = static_cast<int>(r.u32(12));
    p.look.val = static_cast<int>(r.u32(16));
    const std::size_t n = static_cast<std::size_t>(p.look.hue) * p.look.sat * p.look.val;
    if (version < 1 || version > 2 || r.d.size() < 20 + n * 12 + 4) {
        error = "unexpected look table layout";
        return std::nullopt;
    }
    p.look.data.resize(n * 3);
    for (std::size_t i = 0; i < n * 3; ++i) p.look.data[i] = r.f32(20 + i * 4);
    p.look_encoding = static_cast<int>(r.u32(20 + n * 12));

    // Master PV2012 point curve: <crs:ToneCurvePV2012><rdf:Seq><rdf:li>x, y</rdf:li>...
    const auto c0 = xmp.find("<crs:ToneCurvePV2012>");
    if (c0 != std::string::npos) {
        const auto c1 = xmp.find("</crs:ToneCurvePV2012>", c0);
        const std::string block = xmp.substr(c0, c1 - c0);
        const std::regex li(R"(<rdf:li>\s*([0-9.]+)\s*,\s*([0-9.]+)\s*</rdf:li>)");
        for (std::sregex_iterator it(block.begin(), block.end(), li), end; it != end; ++it) {
            p.point_curve.emplace_back(std::stof((*it)[1].str()), std::stof((*it)[2].str()));
        }
    }
    p.name = xmp_path.stem().string();
    return p;
}

std::optional<fs::path> find_adobe_standard(const std::string& unique_camera_model, const std::string& make,
                                            const std::string& model, std::string* matched_name,
                                            const std::string& version) {
    static std::mutex mu;
    // normalised camera + "|" + version ("" or "v2") -> (path, camera)
    static std::map<std::string, std::pair<fs::path, std::string>> index;
    {
        std::lock_guard<std::mutex> lock(mu);
        if (index.empty()) {
            std::vector<fs::path> roots;
            if (const char* pd = std::getenv("ProgramData")) roots.emplace_back(fs::path(pd) / "Adobe/CameraRaw/CameraProfiles/Adobe Standard");
            if (const char* ad = std::getenv("APPDATA")) roots.emplace_back(fs::path(ad) / "Adobe/CameraRaw/CameraProfiles/Adobe Standard");
            for (const auto& root : roots) {
                std::error_code ec;
                if (!fs::is_directory(root, ec)) continue;
                for (const auto& e : fs::directory_iterator(root, ec)) {
                    if (e.path().extension() != ".dcp") continue;
                    // Cheap: take the camera and profile version from the file name
                    // ("<camera> Adobe Standard[ v2].dcp", occasionally "Adobe_Standard_v2").
                    std::string stem = e.path().stem().string();
                    std::replace(stem.begin(), stem.end(), '_', ' ');
                    const auto pos = stem.rfind(" Adobe Standard");
                    const std::string cam = pos == std::string::npos ? stem : stem.substr(0, pos);
                    std::string ver = pos == std::string::npos ? "" : stem.substr(pos + 15);
                    ver.erase(0, ver.find_first_not_of(' '));
                    index.emplace(normalise(cam) + "|" + normalise(ver), std::make_pair(e.path(), cam));
                }
            }
        }
    }
    // A requested version (e.g. "v2") falls back to the base profile when not installed.
    const std::string want = normalise(version);
    auto hit = [&](const std::string& key) -> std::optional<fs::path> {
        auto it = index.find(normalise(key) + "|" + want);
        if (it == index.end() && !want.empty()) it = index.find(normalise(key) + "|");
        if (it == index.end()) return std::nullopt;
        if (matched_name) *matched_name = it->second.second;
        return it->second.first;
    };
    if (!unique_camera_model.empty())
        if (auto p = hit(unique_camera_model)) return p;
    if (auto p = hit(make + " " + model)) return p;
    if (auto p = hit(model)) return p;
    // Variant bodies share their base model's sensor profile (e.g. "SL3-P" -> "SL3"):
    // strip a trailing "-X" / " X" suffix and retry.
    for (const std::string& base : {make + " " + model, model}) {
        const auto cut = base.find_last_of("- ");
        if (cut != std::string::npos && cut > 0 && base.size() - cut <= 4)
            if (auto p = hit(base.substr(0, cut))) return p;
    }
    // Fuzzy: a profile whose normalised name ends with the normalised model.
    const std::string nm = normalise(model);
    if (!nm.empty()) {
        for (const auto& [key, val] : index) {
            const auto bar = key.rfind('|');
            if (key.substr(bar + 1) != "") continue;  // fuzzy matches use the base profile
            const std::string cam_key = key.substr(0, bar);
            if (cam_key.size() >= nm.size() && cam_key.compare(cam_key.size() - nm.size(), nm.size(), nm) == 0) {
                if (matched_name) *matched_name = val.second;
                return val.first;
            }
        }
    }
    return std::nullopt;
}

std::optional<RawInput> decode_raw(const fs::path& path, bool half_size, std::string& error) {
    auto proc = std::make_unique<LibRaw>();
    if (proc->open_file(path.string().c_str()) != LIBRAW_SUCCESS) {
        error = "LibRaw cannot open file";
        return std::nullopt;
    }
    auto& prm = proc->imgdata.params;
    prm.half_size = half_size ? 1 : 0;
    prm.no_auto_bright = 1;
    prm.use_camera_wb = 0;
    prm.use_auto_wb = 0;
    for (float& m : prm.user_mul) m = 1.0F;  // keep camera RGB un-balanced (DNG stage 3)
    prm.output_color = 0;                    // raw camera colour: the DCP does the colour
    prm.output_bps = 16;
    prm.gamm[0] = prm.gamm[1] = 1.0;
    prm.highlight = 0;
    // Apply the DNG DefaultCrop, as Lightroom does (LibRaw does not). Same rule as the
    // legacy decoder in raw_decode.cpp: cropbox is relative to the visible area.
    {
        const auto& sizes = proc->imgdata.sizes;
        const auto& inset = sizes.raw_inset_crops[0];
        if (inset.cwidth > 0 && inset.cheight > 0) {
            const int vis_w = static_cast<int>(sizes.width);
            const int vis_h = static_cast<int>(sizes.height);
            const int cl = std::clamp(static_cast<int>(inset.cleft) - static_cast<int>(sizes.left_margin), 0,
                                      std::max(0, vis_w - 1));
            const int ct = std::clamp(static_cast<int>(inset.ctop) - static_cast<int>(sizes.top_margin), 0,
                                      std::max(0, vis_h - 1));
            const int cw = std::min(static_cast<int>(inset.cwidth), vis_w - cl);
            const int ch = std::min(static_cast<int>(inset.cheight), vis_h - ct);
            if (cw > 0 && ch > 0 && (cl > 0 || ct > 0 || cw < vis_w || ch < vis_h)) {
                prm.cropbox[0] = static_cast<unsigned>(cl);
                prm.cropbox[1] = static_cast<unsigned>(ct);
                prm.cropbox[2] = static_cast<unsigned>(cw);
                prm.cropbox[3] = static_cast<unsigned>(ch);
            }
        }
    }
    if (proc->unpack() != LIBRAW_SUCCESS) {
        error = "LibRaw unpack failed";
        return std::nullopt;
    }
    if (proc->dcraw_process() != LIBRAW_SUCCESS) {
        error = "LibRaw process failed";
        return std::nullopt;
    }
    int err = 0;
    libraw_processed_image_t* img = proc->dcraw_make_mem_image(&err);
    if (!img || img->colors != 3) {
        error = "LibRaw produced no RGB image";
        if (img) LibRaw::dcraw_clear_mem(img);
        return std::nullopt;
    }
    RawInput out;
    out.camera.create(img->height, img->width, CV_32FC3);
    const double scale = img->bits == 16 ? 1.0 / 65535.0 : 1.0 / 255.0;
    for (int y = 0; y < img->height; ++y) {
        auto* row = out.camera.ptr<cv::Vec3f>(y);
        for (int x = 0; x < img->width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * img->width + x) * 3;
            for (int c = 0; c < 3; ++c) {
                const double v = img->bits == 16 ? reinterpret_cast<const std::uint16_t*>(img->data)[i + c]
                                                 : img->data[i + c];
                row[x][c] = static_cast<float>(v * scale);
            }
        }
    }
    LibRaw::dcraw_clear_mem(img);
    const auto& col = proc->imgdata.color;
    const double g = col.cam_mul[1] > 0 ? col.cam_mul[1] : 1.0;
    for (int c = 0; c < 3; ++c) out.neutral[c] = col.cam_mul[c] > 0 ? g / col.cam_mul[c] : 1.0;
    // LibRaw reports -999 when the file carries no BaselineExposure tag.
    const float be = col.dng_levels.baseline_exposure;
    out.has_baseline_exposure = proc->imgdata.idata.dng_version != 0 && be > -100.0F;
    out.baseline_exposure = out.has_baseline_exposure ? be : 0.0F;
    // Fallback colour profile from the file itself.
    {
        Profile fb;
        fb.name = "Embedded";
        auto nonzero = [](const Mat3& m) { return std::any_of(m.begin(), m.end(), [](double v) { return v != 0.0; }); };
        const auto& d0 = col.dng_color[0];
        const auto& d1 = col.dng_color[1];
        if (proc->imgdata.idata.dng_version != 0 && (d0.parsedfields & LIBRAW_DNGFM_COLORMATRIX)) {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) {
                    fb.color1[i * 3 + j] = d0.colormatrix[i][j];   // XYZ -> camera
                    fb.forward1[i * 3 + j] = d0.forwardmatrix[i][j];  // camera -> XYZ(D50)
                    fb.color2[i * 3 + j] = d1.colormatrix[i][j];
                    fb.forward2[i * 3 + j] = d1.forwardmatrix[i][j];
                }
            fb.illuminant1 = d0.illuminant;
            fb.illuminant2 = d1.illuminant;
            fb.has_forward1 = (d0.parsedfields & LIBRAW_DNGFM_FORWARDMATRIX) && nonzero(fb.forward1);
            fb.has_color2 = (d1.parsedfields & LIBRAW_DNGFM_COLORMATRIX) && nonzero(fb.color2);
            fb.has_forward2 = fb.has_color2 && (d1.parsedfields & LIBRAW_DNGFM_FORWARDMATRIX) && nonzero(fb.forward2);
            out.fallback_profile = fb;
            out.fallback_source = "dng-embedded";
        } else {
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) fb.color1[i * 3 + j] = col.cam_xyz[i][j];
            if (nonzero(fb.color1)) {
                fb.illuminant1 = 21;  // Adobe's coefficient matrices are D65
                out.fallback_profile = fb;
                out.fallback_source = "libraw-matrix";
            }
        }
    }
    out.make = proc->imgdata.idata.normalized_make;
    out.model = proc->imgdata.idata.normalized_model;
    out.unique_camera_model = col.UniqueCameraModel;
    return out;
}

cv::Mat develop(const RawInput& raw, const Profile& profile, const DevelopOptions& options, std::string* log) {
    const Spec spec = make_spec(profile);
    const Vec3 neutral{raw.neutral[0], raw.neutral[1], raw.neutral[2]};
    const XY white = neutral_to_xy(spec, neutral);
    const double g = weight_for(spec, white);

    // dng_color_spec::SetWhiteXY
    const Mat3 color = lerp(spec.c1, spec.c2, g);
    Vec3 camera_white = mul(color, xy_to_xyz(white));
    const double ws = 1.0 / max_entry(camera_white);
    for (double& v : camera_white) v = std::clamp(ws * v, 0.001, 1.0);
    Mat3 camera_to_pcs;
    if (spec.has_forward) {
        camera_to_pcs = mul(lerp(spec.f1, spec.f2, g), invert(diag(camera_white)));
    } else {
        Mat3 pcs_to_camera = mul(color, map_white_matrix(kD50, white));
        const double sc = max_entry(mul(pcs_to_camera, kPcsXYZ));
        camera_to_pcs = invert(scaled(pcs_to_camera, 1.0 / sc));
    }
    const Mat3 prophoto_to_pcs{0.7977, 0.1352, 0.0313, 0.2880, 0.7119, 0.0001, 0.0000, 0.0000, 0.8249};
    const Mat3 srgb_to_pcs{0.4361, 0.3851, 0.1431, 0.2225, 0.7169, 0.0606, 0.0139, 0.0971, 0.7141};
    const Mat3 camera_to_rgb = mul(invert(prophoto_to_pcs), camera_to_pcs);
    const Mat3 rgb_to_final = mul(invert(srgb_to_pcs), prophoto_to_pcs);

    // Hue/sat map for this white (dng_camera_profile::HueSatMapForWhite).
    HueSatTable hue_sat;
    if (options.hue_sat) {
        if (profile.hue_sat1.valid() && profile.hue_sat2.valid() && g > 0.0 && g < 1.0) {
            hue_sat = profile.hue_sat1;
            for (std::size_t i = 0; i < hue_sat.data.size(); ++i) {
                hue_sat.data[i] = static_cast<float>(g * profile.hue_sat1.data[i] + (1.0 - g) * profile.hue_sat2.data[i]);
            }
        } else if (profile.hue_sat1.valid() && (g >= 1.0 || !profile.hue_sat2.valid())) {
            hue_sat = profile.hue_sat1;
        } else if (profile.hue_sat2.valid()) {
            hue_sat = profile.hue_sat2;
        }
    }

    const double exposure = options.exposure_bias + raw.baseline_exposure + profile.baseline_exposure_offset;
    const double white_level = 1.0 / std::pow(2.0, std::max(0.0, exposure));
    const double black = std::min(options.shadows ? 5.0 * 0.001 : 0.0, 0.99 * white_level);
    const ExposureRamp ramp(white_level, black, black);
    const Spline spline(profile.tone_curve);
    const bool use_spline = profile.tone_curve.size() >= 2;
    auto tone = [&](double x) {
        const double e = exposure_tone(x, exposure);
        return use_spline ? spline.eval(e) : acr3(e);
    };
    // Tabulate the combined tone curve (as dng_1d_table does) for speed.
    constexpr int kToneTable = 4096;
    std::vector<float> tone_lut(kToneTable + 1);
    for (int i = 0; i <= kToneTable; ++i) tone_lut[i] = static_cast<float>(tone(static_cast<double>(i) / kToneTable));
    auto tone_fast = [&](double x) {
        const double y = std::clamp(x, 0.0, 1.0) * kToneTable;
        const int i = std::min(static_cast<int>(y), kToneTable - 1);
        const double f = y - i;
        return tone_lut[i] * (1.0 - f) + tone_lut[i + 1] * f;
    };

    if (log) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "white xy=(%.4f,%.4f) T=%.0fK g=%.2f exposure=%+.2f hs=%s look=%s curve=%s",
                      white.x, white.y, xy_to_temperature(white), g, exposure, hue_sat.valid() ? "yes" : "no",
                      (options.look && profile.look.valid()) ? "yes" : "no", use_spline ? "profile" : "acr3");
        *log = buf;
    }

    // A look preset (Adobe Color) supplies its own look table: replacing the DCP's, or
    // (stack_looks, what Lightroom does) applied after it.
    const bool preset_look = options.preset && options.preset_look;
    const HueSatTable& look_table = preset_look ? options.preset->look : profile.look;
    const int look_encoding = preset_look ? options.preset->look_encoding : profile.look_encoding;
    // Its point curve (0..255) is applied after the base tone, in an encoded space.
    std::vector<std::pair<float, float>> pc;
    if (options.preset && options.preset_curve) {
        for (const auto& [x, yv] : options.preset->point_curve) pc.emplace_back(x / 255.0F, yv / 255.0F);
    }
    const Spline point_curve(pc);
    const bool use_point_curve = pc.size() >= 2;
    auto enc = [&](double v) {
        v = std::clamp(v, 0.0, 1.0);
        switch (options.curve_space) {
            case CurveSpace::Srgb: return srgb_encode(v);
            case CurveSpace::Gamma22: return std::pow(v, 1.0 / 2.2);
            default: return v;
        }
    };
    auto dec = [&](double v) {
        v = std::clamp(v, 0.0, 1.0);
        switch (options.curve_space) {
            case CurveSpace::Srgb: return srgb_decode(v);
            case CurveSpace::Gamma22: return std::pow(v, 2.2);
            default: return v;
        }
    };
    std::vector<float> pc_lut;
    if (use_point_curve) {
        pc_lut.resize(kToneTable + 1);
        for (int i = 0; i <= kToneTable; ++i) {
            const double x = static_cast<double>(i) / kToneTable;
            pc_lut[i] = static_cast<float>(dec(std::clamp(point_curve.eval(enc(x)), 0.0, 1.0)));
        }
    }
    auto point_fast = [&](float x) {
        const double y = std::clamp(static_cast<double>(x), 0.0, 1.0) * kToneTable;
        const int i = std::min(static_cast<int>(y), kToneTable - 1);
        const double f = y - i;
        return static_cast<float>(pc_lut[i] * (1.0 - f) + pc_lut[i + 1] * f);
    };

    cv::Mat out(raw.camera.size(), CV_32FC3);
    const bool look = options.look && look_table.valid();
    cv::parallel_for_(cv::Range(0, raw.camera.rows), [&](const cv::Range& range) {
        for (int y = range.start; y < range.end; ++y) {
            const auto* src = raw.camera.ptr<cv::Vec3f>(y);
            auto* dst = out.ptr<cv::Vec3f>(y);
            for (int x = 0; x < raw.camera.cols; ++x) {
                const Vec3 cam{std::min<double>(src[x][0], camera_white[0]),
                               std::min<double>(src[x][1], camera_white[1]),
                               std::min<double>(src[x][2], camera_white[2])};
                const Vec3 p = mul(camera_to_rgb, cam);
                float r = static_cast<float>(std::clamp(p[0], 0.0, 1.0));
                float gg = static_cast<float>(std::clamp(p[1], 0.0, 1.0));
                float b = static_cast<float>(std::clamp(p[2], 0.0, 1.0));
                if (hue_sat.valid()) apply_hue_sat(hue_sat, profile.hue_sat_encoding == 1, r, gg, b);
                r = static_cast<float>(ramp.eval(r));
                gg = static_cast<float>(ramp.eval(gg));
                b = static_cast<float>(ramp.eval(b));
                if (options.stack_looks && preset_look && options.look && profile.look.valid())
                    apply_hue_sat(profile.look, profile.look_encoding == 1, r, gg, b);
                if (look) apply_hue_sat(look_table, look_encoding == 1, r, gg, b);
                auto apply_point = [&] {
                    if (options.curve_rgb_preserving) {
                        rgb_tone(r, gg, b, point_fast);
                    } else {
                        r = point_fast(r);
                        gg = point_fast(gg);
                        b = point_fast(b);
                    }
                };
                if (use_point_curve && options.curve_before_tone) apply_point();
                rgb_tone(r, gg, b, tone_fast);
                if (use_point_curve && !options.curve_before_tone) apply_point();
                const Vec3 o = mul(rgb_to_final, Vec3{r, gg, b});
                dst[x] = {static_cast<float>(srgb_encode(o[2])), static_cast<float>(srgb_encode(o[1])),
                          static_cast<float>(srgb_encode(o[0]))};  // BGR
            }
        }
    });
    return out;
}

namespace {

struct ExposureTable {
    float default_proprietary = kDefaultProprietaryBaselineExposure;
    std::map<std::string, float> cameras;  // Adobe profile camera name -> EV
};

ExposureTable load_exposure_table(const fs::path& path) {
    ExposureTable t;
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) return t;
    try {
        const YAML::Node root = YAML::LoadFile(path.string());
        if (root["default_proprietary"]) t.default_proprietary = root["default_proprietary"].as<float>();
        if (const YAML::Node cams = root["cameras"]; cams && cams.IsMap()) {
            for (const auto& kv : cams) t.cameras[kv.first.as<std::string>()] = kv.second.as<float>();
        }
    } catch (const std::exception&) {
        return ExposureTable{};  // a malformed table must not stop a RAW from opening
    }
    return t;
}

// Lightroom's "Adobe Color" look (camera-agnostic XMP), when Lightroom / Camera Raw is installed.
std::optional<fs::path> find_adobe_color_xmp() {
    for (const char* env : {"ProgramData", "APPDATA"}) {
        const char* root = std::getenv(env);
        if (!root) continue;
        const fs::path p = fs::path(root) / "Adobe/CameraRaw/Settings/Adobe/Profiles/Adobe Raw/Adobe Color.xmp";
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return p;
    }
    return std::nullopt;
}

}  // namespace

std::optional<DevelopedRaw> develop_raw_file(const fs::path& path, const DeveloperSettings& settings, bool half_size,
                                             std::string& error) {
    auto raw = decode_raw(path, half_size, error);
    if (!raw) return std::nullopt;

    // Lightroom defaults cameras that have a revised profile to it ("Adobe Standard v2").
    std::string camera;
    const auto dcp_path = find_adobe_standard(raw->unique_camera_model, raw->make, raw->model, &camera, "v2");
    std::optional<Profile> profile = dcp_path ? load_dcp(*dcp_path) : std::nullopt;
    DevelopedRaw out;
    if (profile) {
        out.profile_source = dcp_path->stem().string();
    } else if (raw->fallback_profile) {
        profile = raw->fallback_profile;
        out.profile_source = raw->fallback_source;
    } else {
        error = "no colour profile for " + raw->make + " " + raw->model;
        return std::nullopt;
    }

    // Cached per process: the table and preset are small and do not change while running.
    static std::mutex mu;
    static std::map<std::string, ExposureTable> tables;
    static std::optional<std::optional<LookPreset>> adobe_color;
    const ExposureTable* table = nullptr;
    const LookPreset* preset = nullptr;
    {
        std::lock_guard<std::mutex> lock(mu);
        const std::string key = settings.exposure_table.string();
        auto it = tables.find(key);
        if (it == tables.end()) it = tables.emplace(key, load_exposure_table(settings.exposure_table)).first;
        table = &it->second;
        if (!adobe_color) {
            std::string preset_error;
            const auto xmp = find_adobe_color_xmp();
            adobe_color = xmp ? load_look_preset(*xmp, preset_error) : std::nullopt;
        }
        if (settings.adobe_color && *adobe_color) preset = &**adobe_color;
    }

    DevelopOptions options;
    // DNGs carry their BaselineExposure; for proprietary RAWs it is Adobe's hidden
    // per-camera value, measured into profiles/raw/baseline_exposure.yaml.
    if (!raw->has_baseline_exposure) {
        const auto it = table->cameras.find(camera);
        options.exposure_bias = it != table->cameras.end() ? it->second : table->default_proprietary;
    }
    // Adobe Color: its look table applies on top of the DCP look; its point curve is left
    // off (measured against Lightroom exports, 2026-09-27).
    if (preset && dcp_path) {
        options.preset = preset;
        options.stack_looks = true;
        options.preset_curve = false;
        out.profile_source += " + " + preset->name;
    }
    out.exposure_ev = options.exposure_bias + raw->baseline_exposure + profile->baseline_exposure_offset;
    out.srgb = develop(*raw, *profile, options);
    return out;
}

}  // namespace dfee::dcp

#endif  // DFEE_HAS_LIBRAW && DFEE_HAS_OPENCV
