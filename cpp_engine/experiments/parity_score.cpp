// RAW-vs-Lightroom parity scorer (native RAW programme, phase P0).
//
// Measures how close Film Lab's native RAW path gets to Lightroom's default render,
// over a corpus of RAW files paired with edit-free Lightroom TIFF exports. It is the
// yardstick for every later RAW-developer change: numbers must improve monotonically.
//
// Two measurements per pair:
//   1. Developer parity  RAW -> Film Lab (stock "none")  vs  the Lightroom TIFF itself.
//   2. Film parity       RAW -> Film Lab (stock S)       vs  Lightroom TIFF -> Film Lab (stock S),
//      i.e. the native RAW path against the proven Lightroom round-trip, per stock.
//
// A TIFF is only used as a reference when it is provably a clean Lightroom render:
// it carries Lightroom's crs: develop block, its camera profile matches --profile,
// every tone/colour slider is zero with As Shot white balance, lens corrections are
// off (they move geometry), and its embedded ICC profile is sRGB. Anything else is
// skipped with the reason printed.
//
// Usage:
//   dfee_parity_score <corpus_dir> [--profile "Adobe Standard"] [--stocks none,portra_400]
//                     [--placement auto_balanced] [--limit N] [--ext arw] [--size 384]
//                     [--out report.json] [--dump dir]
//   DCP developer: --developer dcp [--exposure-table yaml] [--look-preset "Adobe Color.xmp"]
//                  [--preset-parts stack-look|stack|look|curve|both] [--curve-space srgb|gamma22|linear]
//                  [--curve-mode channel|rgb] [--curve-order post|pre]
//   Lightroom Adobe Color = --profile "Adobe Color" --look-preset <xmp> --preset-parts stack-look.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

#include "dfee/dcp_developer.hpp"
#include "dfee/bridge_types.hpp"
#include "dfee/session.hpp"

#ifndef DFEE_REPO_ROOT
#  define DFEE_REPO_ROOT "."
#endif

namespace fs = std::filesystem;
namespace dcpdev = dfee::dcp;

namespace {

// ---------------------------------------------------------------------------
// Minimal TIFF tag reader: pulls the XMP packet (tag 700) and ICC profile
// (tag 34675) out of IFD0 without loading the (often 200+ MB) pixel data.
// ---------------------------------------------------------------------------
struct TiffBlobs {
    std::string xmp;
    std::string icc;
};

std::optional<TiffBlobs> read_tiff_blobs(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    unsigned char hdr[8];
    if (!f.read(reinterpret_cast<char*>(hdr), 8)) return std::nullopt;
    const bool le = hdr[0] == 'I' && hdr[1] == 'I';
    const bool be = hdr[0] == 'M' && hdr[1] == 'M';
    if (!le && !be) return std::nullopt;
    auto u16 = [&](const unsigned char* p) -> std::uint32_t {
        return le ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]);
    };
    auto u32 = [&](const unsigned char* p) -> std::uint32_t {
        return le ? (std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
                     (std::uint32_t(p[3]) << 24))
                  : ((std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
                     std::uint32_t(p[3]));
    };
    if (u16(hdr + 2) != 42) return std::nullopt;  // classic TIFF only (not BigTIFF)
    const std::uint32_t ifd = u32(hdr + 4);
    f.seekg(ifd);
    unsigned char cnt[2];
    if (!f.read(reinterpret_cast<char*>(cnt), 2)) return std::nullopt;
    const std::uint32_t n = u16(cnt);
    std::vector<unsigned char> entries(n * 12U);
    if (!f.read(reinterpret_cast<char*>(entries.data()), static_cast<std::streamsize>(entries.size()))) {
        return std::nullopt;
    }
    TiffBlobs out;
    for (std::uint32_t i = 0; i < n; ++i) {
        const unsigned char* e = entries.data() + i * 12U;
        const std::uint32_t tag = u16(e);
        if (tag != 700 && tag != 34675) continue;
        const std::uint32_t count = u32(e + 4);  // BYTE/UNDEFINED -> count == byte length
        std::string blob(count, '\0');
        if (count <= 4) {
            std::copy(e + 8, e + 8 + count, blob.begin());
        } else {
            f.seekg(u32(e + 8));
            f.read(blob.data(), static_cast<std::streamsize>(count));
        }
        (tag == 700 ? out.xmp : out.icc) = std::move(blob);
    }
    return out;
}

// Value of a crs:/xmp: property written either as an attribute or an element.
std::optional<std::string> xmp_value(const std::string& xmp, const std::string& key) {
    const std::string attr = key + "=\"";
    if (auto p = xmp.find(attr); p != std::string::npos) {
        const auto s = p + attr.size();
        return xmp.substr(s, xmp.find('"', s) - s);
    }
    const std::string open = "<" + key + ">";
    if (auto p = xmp.find(open); p != std::string::npos) {
        const auto s = p + open.size();
        return xmp.substr(s, xmp.find('<', s) - s);
    }
    return std::nullopt;
}

bool is_zero_number(const std::string& v) {
    try {
        return std::abs(std::stod(v)) < 1e-9;
    } catch (...) {
        return false;
    }
}

// The profile Lightroom rendered with. A look profile such as Adobe Color is recorded as
// crs:CameraProfile="Adobe Standard" plus a <crs:Look crs:Name="Adobe Color"> block, so
// the look name (when present) is the profile the user picked.
std::optional<std::string> effective_profile(const std::string& xmp) {
    if (const auto look = xmp.find("<crs:Look>"); look != std::string::npos) {
        const auto end = xmp.find("</crs:Look>", look);
        if (auto name = xmp_value(xmp.substr(look, end == std::string::npos ? std::string::npos : end - look),
                                  "crs:Name"))
            return name;
    }
    return xmp_value(xmp, "crs:CameraProfile");
}

// Revision suffix of the base camera profile ("Adobe Standard v2" -> "v2"; else "").
std::string camera_profile_version(const std::string& xmp) {
    static const std::string base = "Adobe Standard";
    const std::string cp = xmp_value(xmp, "crs:CameraProfile").value_or("");
    if (cp.rfind(base, 0) != 0) return "";
    std::string v = cp.substr(base.size());
    v.erase(0, v.find_first_not_of(' '));
    return v;
}

// Returns an empty string when the TIFF is a clean reference, else the skip reason.
std::string reference_problem(const TiffBlobs& b, const std::string& want_profile) {
    if (b.xmp.empty()) return "no XMP";
    const auto profile = effective_profile(b.xmp);
    if (!profile) return "no Lightroom develop settings";
    if (profile->rfind(want_profile, 0) != 0) return "profile '" + *profile + "'";
    for (const char* k : {"crs:Exposure2012", "crs:Contrast2012", "crs:Highlights2012", "crs:Shadows2012",
                          "crs:Whites2012", "crs:Blacks2012", "crs:Clarity2012", "crs:Texture", "crs:Dehaze",
                          "crs:Vibrance", "crs:Saturation"}) {
        const auto v = xmp_value(b.xmp, k);
        if (v && !is_zero_number(*v)) return std::string("edited ") + k + "=" + *v;
    }
    if (const auto wb = xmp_value(b.xmp, "crs:WhiteBalance"); wb && *wb != "As Shot") return "WB '" + *wb + "'";
    if (const auto lens = xmp_value(b.xmp, "crs:LensProfileEnable"); lens && *lens == "1") return "lens profile on";
    if (b.icc.find("sRGB") == std::string::npos) return "ICC is not sRGB";
    return "";
}

// ---------------------------------------------------------------------------
// Colour maths: sRGB-encoded BGR [0,1] -> CIE L*a*b* (D65), and CIEDE2000.
// ---------------------------------------------------------------------------
inline float srgb_decode(float c) { return c <= 0.04045F ? c / 12.92F : std::pow((c + 0.055F) / 1.055F, 2.4F); }

inline float lab_f(float t) {
    constexpr float d = 6.0F / 29.0F;
    return t > d * d * d ? std::cbrt(t) : t / (3.0F * d * d) + 4.0F / 29.0F;
}

cv::Mat to_lab(const cv::Mat& bgr) {  // CV_32FC3 in, CV_32FC3 (L,a,b) out
    cv::Mat lab(bgr.size(), CV_32FC3);
    for (int y = 0; y < bgr.rows; ++y) {
        const auto* s = bgr.ptr<cv::Vec3f>(y);
        auto* d = lab.ptr<cv::Vec3f>(y);
        for (int x = 0; x < bgr.cols; ++x) {
            const float r = srgb_decode(s[x][2]), g = srgb_decode(s[x][1]), b = srgb_decode(s[x][0]);
            const float X = (0.4124564F * r + 0.3575761F * g + 0.1804375F * b) / 0.95047F;
            const float Y = 0.2126729F * r + 0.7151522F * g + 0.0721750F * b;
            const float Z = (0.0193339F * r + 0.1191920F * g + 0.9503041F * b) / 1.08883F;
            const float fx = lab_f(X), fy = lab_f(Y), fz = lab_f(Z);
            d[x] = {116.0F * fy - 16.0F, 500.0F * (fx - fy), 200.0F * (fy - fz)};
        }
    }
    return lab;
}

double ciede2000(const cv::Vec3f& p, const cv::Vec3f& q) {
    constexpr double kPi = 3.14159265358979323846;
    auto deg = [](double r) { return r * 180.0 / kPi; };
    auto rad = [](double d) { return d * kPi / 180.0; };
    const double L1 = p[0], a1 = p[1], b1 = p[2], L2 = q[0], a2 = q[1], b2 = q[2];
    const double C1 = std::hypot(a1, b1), C2 = std::hypot(a2, b2);
    const double Cb = 0.5 * (C1 + C2);
    const double G = 0.5 * (1.0 - std::sqrt(std::pow(Cb, 7) / (std::pow(Cb, 7) + std::pow(25.0, 7))));
    const double a1p = (1.0 + G) * a1, a2p = (1.0 + G) * a2;
    const double C1p = std::hypot(a1p, b1), C2p = std::hypot(a2p, b2);
    auto hue = [&](double bb, double ap) {
        if (bb == 0.0 && ap == 0.0) return 0.0;
        double h = deg(std::atan2(bb, ap));
        return h < 0.0 ? h + 360.0 : h;
    };
    const double h1p = hue(b1, a1p), h2p = hue(b2, a2p);
    const double dLp = L2 - L1, dCp = C2p - C1p;
    double dhp = 0.0;
    if (C1p * C2p != 0.0) {
        dhp = h2p - h1p;
        if (dhp > 180.0) dhp -= 360.0;
        else if (dhp < -180.0) dhp += 360.0;
    }
    const double dHp = 2.0 * std::sqrt(C1p * C2p) * std::sin(rad(dhp) / 2.0);
    const double Lbp = 0.5 * (L1 + L2), Cbp = 0.5 * (C1p + C2p);
    double hbp = h1p + h2p;
    if (C1p * C2p != 0.0) {
        if (std::abs(h1p - h2p) > 180.0) hbp += (hbp < 360.0) ? 360.0 : -360.0;
        hbp *= 0.5;
    }
    const double T = 1.0 - 0.17 * std::cos(rad(hbp - 30.0)) + 0.24 * std::cos(rad(2.0 * hbp)) +
                     0.32 * std::cos(rad(3.0 * hbp + 6.0)) - 0.20 * std::cos(rad(4.0 * hbp - 63.0));
    const double dTheta = 30.0 * std::exp(-std::pow((hbp - 275.0) / 25.0, 2));
    const double Rc = 2.0 * std::sqrt(std::pow(Cbp, 7) / (std::pow(Cbp, 7) + std::pow(25.0, 7)));
    const double Sl = 1.0 + 0.015 * std::pow(Lbp - 50.0, 2) / std::sqrt(20.0 + std::pow(Lbp - 50.0, 2));
    const double Sc = 1.0 + 0.045 * Cbp, Sh = 1.0 + 0.015 * Cbp * T;
    const double Rt = -std::sin(rad(2.0 * dTheta)) * Rc;
    return std::sqrt(std::pow(dLp / Sl, 2) + std::pow(dCp / Sc, 2) + std::pow(dHp / Sh, 2) +
                     Rt * (dCp / Sc) * (dHp / Sh));
}

// ---------------------------------------------------------------------------
// Image loading / alignment
// ---------------------------------------------------------------------------
cv::Mat to_float01(const cv::Mat& m) {
    cv::Mat f;
    const double scale = m.depth() == CV_16U ? 1.0 / 65535.0 : 1.0 / 255.0;
    m.convertTo(f, CV_32FC3, scale);
    return f;
}

cv::Mat shrink(const cv::Mat& img, int long_edge) {
    const double s = static_cast<double>(long_edge) / std::max(img.cols, img.rows);
    if (s >= 1.0) return img.clone();
    cv::Mat out;
    cv::resize(img, out, cv::Size(), s, s, cv::INTER_AREA);
    return out;
}

cv::Mat decode_jpeg(const std::vector<std::uint8_t>& bytes) {
    const cv::Mat enc(1, static_cast<int>(bytes.size()), CV_8U, const_cast<std::uint8_t*>(bytes.data()));
    return cv::imdecode(enc, cv::IMREAD_COLOR);
}

double mean_abs_diff(const cv::Mat& a, const cv::Mat& b) {
    cv::Mat d;
    cv::absdiff(a, b, d);
    const cv::Scalar m = cv::mean(d);
    return (m[0] + m[1] + m[2]) / 3.0;
}

bool g_register = true;  // --register 0 disables geometric registration

// Affine-register `img` onto `ref` (ECC on luminance). Lightroom applies built-in lens
// distortion correction for many mirrorless / Leica bodies and crops accordingly, so
// the two renders differ slightly in geometry; registering keeps the score about
// colour and tone rather than pixel alignment.
cv::Mat register_to(const cv::Mat& img, const cv::Mat& ref) {
    cv::Mat a, b;
    cv::cvtColor(ref, a, cv::COLOR_BGR2GRAY);
    cv::cvtColor(img, b, cv::COLOR_BGR2GRAY);
    // Local shifts on a 4x4 tile grid (phase correlation), then a least-squares affine
    // mapping ref coordinates -> img coordinates.
    constexpr int kGrid = 4;
    const int tw = a.cols / kGrid, th = a.rows / kGrid;
    std::vector<std::array<double, 4>> pts;  // (xr, yr, xi, yi)
    cv::Mat win;
    cv::createHanningWindow(win, cv::Size(tw, th), CV_32F);
    for (int gy = 0; gy < kGrid; ++gy) {
        for (int gx = 0; gx < kGrid; ++gx) {
            const cv::Rect r(gx * tw, gy * th, tw, th);
            double response = 0.0;
            const cv::Point2d s = cv::phaseCorrelate(a(r), b(r), win, &response);
            if (response < 0.05 || std::abs(s.x) > tw / 4.0 || std::abs(s.y) > th / 4.0) continue;
            const double cx = r.x + tw / 2.0, cy = r.y + th / 2.0;
            pts.push_back({cx, cy, cx + s.x, cy + s.y});
        }
    }
    if (pts.size() < 6) return img;
    // Solve [xi yi] = A * [xr yr 1] by normal equations (two 3x3 systems).
    cv::Mat M = cv::Mat::zeros(3, 3, CV_64F), bx = cv::Mat::zeros(3, 1, CV_64F), by = cv::Mat::zeros(3, 1, CV_64F);
    for (const auto& p : pts) {
        const double v[3] = {p[0], p[1], 1.0};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) M.at<double>(i, j) += v[i] * v[j];
            bx.at<double>(i) += v[i] * p[2];
            by.at<double>(i) += v[i] * p[3];
        }
    }
    cv::Mat cx, cy;
    if (!cv::solve(M, bx, cx, cv::DECOMP_SVD) || !cv::solve(M, by, cy, cv::DECOMP_SVD)) return img;
    cv::Mat warp = (cv::Mat_<double>(2, 3) << cx.at<double>(0), cx.at<double>(1), cx.at<double>(2),
                    cy.at<double>(0), cy.at<double>(1), cy.at<double>(2));
    cv::Mat out;
    // warp maps output(ref) coords -> source(img) coords, hence WARP_INVERSE_MAP.
    cv::warpAffine(img, out, warp, ref.size(), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP, cv::BORDER_REPLICATE);
    return out;
}

// Resize `ours` onto `ref`'s grid, rotating by 90 degrees if orientations disagree.
cv::Mat align_to(const cv::Mat& ours, const cv::Mat& ref) {
    std::vector<cv::Mat> candidates{ours};
    if ((ours.cols > ours.rows) != (ref.cols > ref.rows)) {
        cv::Mat cw, ccw;
        cv::rotate(ours, cw, cv::ROTATE_90_CLOCKWISE);
        cv::rotate(ours, ccw, cv::ROTATE_90_COUNTERCLOCKWISE);
        candidates = {cw, ccw};
    }
    cv::Mat best;
    double best_err = 1e9;
    for (const auto& c : candidates) {
        cv::Mat r;
        cv::resize(c, r, ref.size(), 0, 0, cv::INTER_AREA);
        const double e = mean_abs_diff(r, ref);
        if (e < best_err) {
            best_err = e;
            best = r;
        }
    }
    return g_register ? register_to(best, ref) : best;
}

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------
struct Metrics {
    double de_mean = 0, de_p95 = 0;
    double chroma_ratio = 0;           // mean C* ours / mean C* reference (1.0 = parity)
    double da = 0, db = 0;             // mean a*/b* offset (cast)
    std::array<double, 7> dL{};        // L* difference at luminance quantiles (ours - ref)
    double clip_ours = 0, clip_ref = 0;  // fraction of pixels with any channel >= 0.99
};

constexpr std::array<double, 7> kQuantiles{0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95};

double quantile(std::vector<float> v, double q) {
    if (v.empty()) return 0.0;
    const auto k = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

double clip_fraction(const cv::Mat& bgr) {
    std::size_t n = 0, c = 0;
    for (int y = 0; y < bgr.rows; ++y) {
        const auto* p = bgr.ptr<cv::Vec3f>(y);
        for (int x = 0; x < bgr.cols; ++x, ++n) {
            if (p[x][0] >= 0.99F || p[x][1] >= 0.99F || p[x][2] >= 0.99F) ++c;
        }
    }
    return n ? static_cast<double>(c) / static_cast<double>(n) : 0.0;
}

// Compare on the central 90% (edges carry vignetting / crop-rounding differences).
Metrics compare(const cv::Mat& ours_full, const cv::Mat& ref_full) {
    const int mx = ref_full.cols / 20, my = ref_full.rows / 20;
    const cv::Rect roi(mx, my, ref_full.cols - 2 * mx, ref_full.rows - 2 * my);
    const cv::Mat ours = ours_full(roi), ref = ref_full(roi);
    // Light low-pass so any residual sub-pixel/radial misregistration does not read as
    // colour error; flat-area colour and tone are unaffected.
    cv::Mat ours_s = ours, ref_s = ref;
    if (g_register) {
        cv::GaussianBlur(ours, ours_s, cv::Size(), 1.2);
        cv::GaussianBlur(ref, ref_s, cv::Size(), 1.2);
    }
    const cv::Mat lo = to_lab(ours_s), lr = to_lab(ref_s);

    Metrics m;
    std::vector<float> de, Lo, Lr;
    double co = 0, cr = 0, sa = 0, sb = 0;
    const auto n = static_cast<std::size_t>(lo.rows) * static_cast<std::size_t>(lo.cols);
    de.reserve(n);
    Lo.reserve(n);
    Lr.reserve(n);
    for (int y = 0; y < lo.rows; ++y) {
        const auto* po = lo.ptr<cv::Vec3f>(y);
        const auto* pr = lr.ptr<cv::Vec3f>(y);
        for (int x = 0; x < lo.cols; ++x) {
            de.push_back(static_cast<float>(ciede2000(pr[x], po[x])));
            Lo.push_back(po[x][0]);
            Lr.push_back(pr[x][0]);
            co += std::hypot(po[x][1], po[x][2]);
            cr += std::hypot(pr[x][1], pr[x][2]);
            sa += po[x][1] - pr[x][1];
            sb += po[x][2] - pr[x][2];
        }
    }
    double sum = 0;
    for (float v : de) sum += v;
    m.de_mean = sum / static_cast<double>(de.size());
    m.de_p95 = quantile(de, 0.95);
    m.chroma_ratio = cr > 0 ? co / cr : 0;
    m.da = sa / static_cast<double>(n);
    m.db = sb / static_cast<double>(n);
    for (std::size_t i = 0; i < kQuantiles.size(); ++i) {
        m.dL[i] = quantile(Lo, kQuantiles[i]) - quantile(Lr, kQuantiles[i]);
    }
    m.clip_ours = clip_fraction(ours);
    m.clip_ref = clip_fraction(ref);
    return m;
}

void accumulate(Metrics& acc, const Metrics& m) {
    acc.de_mean += m.de_mean;
    acc.de_p95 += m.de_p95;
    acc.chroma_ratio += m.chroma_ratio;
    acc.da += m.da;
    acc.db += m.db;
    for (std::size_t i = 0; i < acc.dL.size(); ++i) acc.dL[i] += m.dL[i];
    acc.clip_ours += m.clip_ours;
    acc.clip_ref += m.clip_ref;
}

Metrics divided(Metrics m, double n) {
    if (n <= 0) return m;
    m.de_mean /= n;
    m.de_p95 /= n;
    m.chroma_ratio /= n;
    m.da /= n;
    m.db /= n;
    for (auto& v : m.dL) v /= n;
    m.clip_ours /= n;
    m.clip_ref /= n;
    return m;
}

std::string metrics_json(const Metrics& m) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(4) << "{\"de_mean\":" << m.de_mean << ",\"de_p95\":" << m.de_p95
      << ",\"chroma_ratio\":" << m.chroma_ratio << ",\"da\":" << m.da << ",\"db\":" << m.db << ",\"dL\":[";
    for (std::size_t i = 0; i < m.dL.size(); ++i) o << (i ? "," : "") << m.dL[i];
    o << "],\"clip_ours\":" << m.clip_ours << ",\"clip_ref\":" << m.clip_ref << "}";
    return o.str();
}

void print_row(const std::string& label, const Metrics& m, int n) {
    std::cout << std::left << std::setw(22) << label << std::right << std::fixed << std::setprecision(2)
              << std::setw(4) << n << std::setw(8) << m.de_mean << std::setw(8) << m.de_p95 << std::setw(8)
              << m.chroma_ratio << std::setw(7) << m.da << std::setw(7) << m.db << "  ";
    for (double v : m.dL) std::cout << std::showpos << std::setw(6) << std::setprecision(1) << v << std::noshowpos;
    std::cout << std::setprecision(3) << "   " << m.clip_ours << "/" << m.clip_ref << "\n";
}

void print_header() {
    std::cout << std::left << std::setw(22) << "" << std::right << std::setw(4) << "n" << std::setw(8) << "dE"
              << std::setw(8) << "dE95" << std::setw(8) << "C*rat" << std::setw(7) << "da" << std::setw(7) << "db"
              << "  dL@q05  q10   q25   q50   q75   q90   q95   clip ours/ref\n";
}

// ---------------------------------------------------------------------------
// Engine rendering (mirrors the desktop app's defaults per input type)
// ---------------------------------------------------------------------------
dfee::NativePreviewRenderRequest base_request(const fs::path& file, const std::string& stock) {
    dfee::NativePreviewRenderRequest req;
    req.filename = file.string();
    req.stock = stock;
    req.effect_pipeline_version = "filmic_v4";  // what the desktop app sends
    req.adaptive = true;
    req.grain = "Off";  // grain is random texture; it would only add noise to the score
    req.halation = "Auto";
    return req;
}

std::optional<cv::Mat> render(dfee::EngineSession& session, const dfee::NativePreviewRenderRequest& req,
                              std::string& err) {
    const auto resp = session.render_preview(req);
    if (!resp.ok) {
        err = resp.error.code + ": " + resp.error.user_message;
        return std::nullopt;
    }
    const cv::Mat bgr8 = decode_jpeg(resp.jpeg_bytes);
    if (bgr8.empty()) {
        err = "preview JPEG decode failed";
        return std::nullopt;
    }
    return to_float01(bgr8);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

struct Pair {
    fs::path tif, raw;
    std::string brand;            // RAW extension, used to group results by camera family
    std::string profile_version;  // "" or e.g. "v2" ("Adobe Standard v2" in the reference)
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0]
                  << " <corpus_dir> [--profile \"Adobe Standard\"] [--stocks none,portra_400]"
                     " [--placement auto_balanced] [--limit N] [--ext arw] [--size 384]"
                     " [--out report.json] [--dump dir]\n";
        return 2;
    }
    const fs::path corpus = fs::absolute(argv[1]);
    std::string profile = "Adobe Standard", placement = "auto_balanced", only_ext, out_path, dump_dir;
    std::string developer = "engine";  // engine | dcp (prototype DNG-profile developer)
    std::optional<dcpdev::LookPreset> look_preset;  // e.g. Adobe Color
    dcpdev::DevelopOptions dcp_options;
    bool fit_exposure = false;
    bool use_fallback = true;  // --fallback 0 to score only cameras with an installed DCP
    // Per-camera baseline exposure for proprietary RAWs (profiles/raw/baseline_exposure.yaml).
    std::map<std::string, double> exposure_table;
    double exposure_default = 0.0;
    bool have_exposure_table = false, table_default_only = false;
    std::map<std::string, std::vector<double>> fitted;  // camera -> best exposure bias per file
    std::vector<std::string> stocks{"none"};
    int limit = 0, size = 384;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--profile") profile = v;
        else if (k == "--developer") developer = v;
        else if (k == "--exposure-bias") dcp_options.exposure_bias = std::stof(v);
        else if (k == "--shadows") dcp_options.shadows = v != "0";
        else if (k == "--hue-sat") dcp_options.hue_sat = v != "0";
        else if (k == "--look") dcp_options.look = v != "0";
        else if (k == "--fit-exposure") fit_exposure = v != "0";
        else if (k == "--fallback") use_fallback = v != "0";
        else if (k == "--default-only") table_default_only = v != "0";
        else if (k == "--register") g_register = v != "0";
        else if (k == "--look-preset") {
            std::string err;
            look_preset = dcpdev::load_look_preset(v, err);
            if (!look_preset) {
                std::cerr << "look preset: " << err << "\n";
                return 2;
            }
            dcp_options.preset = &*look_preset;
            std::cout << "look preset '" << look_preset->name << "': table " << look_preset->look.hue << "x"
                      << look_preset->look.sat << "x" << look_preset->look.val << ", point curve "
                      << look_preset->point_curve.size() << " pts\n";
        } else if (k == "--preset-parts") {  // look | curve | both | stack (DCP look + preset look + curve)
            dcp_options.preset_look = v != "curve";
            dcp_options.preset_curve = v != "look" && v != "stack-look";
            dcp_options.stack_looks = v == "stack" || v == "stack-look";
        } else if (k == "--curve-order") {  // post (after base tone) | pre
            dcp_options.curve_before_tone = v == "pre";
        } else if (k == "--curve-mode") {  // channel | rgb
            dcp_options.curve_rgb_preserving = v == "rgb";
        } else if (k == "--curve-space") {
            dcp_options.curve_space = v == "linear" ? dcpdev::CurveSpace::Linear
                                    : v == "gamma22" ? dcpdev::CurveSpace::Gamma22
                                                     : dcpdev::CurveSpace::Srgb;
        }
        else if (k == "--exposure-table") {
            const YAML::Node t = YAML::LoadFile(v);
            exposure_default = t["default_proprietary"].as<double>(0.0);
            for (const auto& kv : t["cameras"]) exposure_table[kv.first.as<std::string>()] = kv.second.as<double>();
            have_exposure_table = true;
        }
        else if (k == "--stocks") stocks = split_csv(v);
        else if (k == "--placement") placement = v;
        else if (k == "--limit") limit = std::stoi(v);
        else if (k == "--ext") only_ext = lower(v);
        else if (k == "--size") size = std::stoi(v);
        else if (k == "--out") out_path = v;
        else if (k == "--dump") dump_dir = v;
        else {
            std::cerr << "unknown option " << k << "\n";
            return 2;
        }
    }

    // ---- pair discovery -------------------------------------------------
    static const std::vector<std::string> kRawExt{".arw", ".nef", ".rw2", ".dng", ".raf",
                                                  ".cr2", ".cr3", ".orf", ".3fr", ".pef"};
    std::map<std::string, fs::path> raw_by_stem;  // lowercase stem -> RAW path
    for (const auto& e : fs::directory_iterator(corpus)) {
        const std::string ext = lower(e.path().extension().string());
        if (std::find(kRawExt.begin(), kRawExt.end(), ext) != kRawExt.end()) {
            raw_by_stem[lower(e.path().stem().string())] = e.path();
        }
    }
    // "<raw stem>-Edit[-N].tif" (Lightroom edit-in) or "<raw stem>-AC.tif" (Adobe Color export).
    const std::regex edit_suffix(R"((-Edit(-\d+)?|-AC)$)", std::regex::icase);
    std::vector<Pair> pairs;
    std::map<std::string, int> skipped;
    std::vector<fs::path> tifs;
    for (const auto& e : fs::directory_iterator(corpus)) {
        const std::string ext = lower(e.path().extension().string());
        if (ext == ".tif" || ext == ".tiff") tifs.push_back(e.path());
    }
    std::sort(tifs.begin(), tifs.end());
    for (const auto& tif : tifs) {
        const std::string stem = lower(std::regex_replace(tif.stem().string(), edit_suffix, ""));
        const auto raw = raw_by_stem.find(stem);
        if (raw == raw_by_stem.end()) {
            ++skipped["no matching RAW"];
            continue;
        }
        const std::string brand = lower(raw->second.extension().string()).substr(1);
        if (!only_ext.empty() && brand != only_ext) continue;
        const auto blobs = read_tiff_blobs(tif);
        if (!blobs) {
            ++skipped["unreadable TIFF header"];
            continue;
        }
        if (const std::string why = reference_problem(*blobs, profile); !why.empty()) {
            ++skipped[why.substr(0, why.find('='))];
            continue;
        }
        // Lightroom defaults some cameras to a revised profile ("Adobe Standard v2"); develop
        // with the same DCP version the reference names.
        pairs.push_back({tif, raw->second, brand, camera_profile_version(blobs->xmp)});
        if (limit > 0 && static_cast<int>(pairs.size()) >= limit) break;
    }

    std::cout << "corpus " << corpus.string() << "\nprofile '" << profile << "', placement " << placement
              << ", compare size " << size << "px\n";
    std::cout << "usable pairs: " << pairs.size() << "\n";
    for (const auto& [why, n] : skipped) std::cout << "  skipped " << n << "  (" << why << ")\n";
    if (pairs.empty()) return 1;
    if (!dump_dir.empty()) fs::create_directories(dump_dir);

    // ---- scoring --------------------------------------------------------
    dfee::EngineSession session{fs::path(DFEE_REPO_ROOT)};
    // results[stock] -> brand -> (sum, count); "_all" holds the overall aggregate.
    std::map<std::string, std::map<std::string, std::pair<Metrics, int>>> agg;
    std::ostringstream per_pair;
    bool first = true;

    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const Pair& p = pairs[i];
        std::cout << "[" << (i + 1) << "/" << pairs.size() << "] " << p.raw.filename().string() << std::flush;
        cv::Mat ref_full = cv::imread(p.tif.string(), cv::IMREAD_COLOR | cv::IMREAD_ANYDEPTH);
        if (ref_full.empty()) {
            std::cout << "  reference unreadable\n";
            continue;
        }
        const cv::Mat ref = shrink(to_float01(ref_full), size);
        ref_full.release();

        for (const std::string& stock : stocks) {
            std::string err;
            std::optional<cv::Mat> ours;
            if (developer == "dcp") {
                if (stock != "none") {
                    std::cout << "  [" << stock << ": film parity needs the engine developer; skipped]";
                    continue;
                }
                const auto raw = dcpdev::decode_raw(p.raw, true, err);
                if (!raw) {
                    std::cout << "  [decode failed: " << err << "]";
                    continue;
                }
                std::string cam;
                const auto dcp_path = dcpdev::find_adobe_standard(raw->unique_camera_model, raw->make, raw->model, &cam,
                                                             p.profile_version);
                auto prof = dcp_path ? dcpdev::load_dcp(*dcp_path) : std::nullopt;
                if (!prof && use_fallback && raw->fallback_profile) {
                    prof = raw->fallback_profile;
                    cam = raw->make + " " + raw->model + " [" + raw->fallback_source + "]";
                }
                if (!prof) {
                    std::cout << "  [no Adobe Standard DCP for '" << raw->make << " " << raw->model << "']";
                    continue;
                }
                // Development is per-pixel, so develop a downscaled copy for speed.
                dcpdev::RawInput small = *raw;
                small.camera = shrink(raw->camera, size * 2);
                std::string info;
                if (fit_exposure) {
                    // Golden-section search for the exposure bias that best matches Lightroom:
                    // this measures the per-camera baseline exposure Adobe applies internally.
                    auto score = [&](double bias) {
                        dcpdev::DevelopOptions o = dcp_options;
                        o.exposure_bias = static_cast<float>(bias);
                        return compare(align_to(dcpdev::develop(small, *prof, o), ref), ref).de_mean;
                    };
                    double lo = -1.0, hi = 2.0;
                    const double gr = 0.6180339887;
                    double x1 = hi - gr * (hi - lo), x2 = lo + gr * (hi - lo), f1 = score(x1), f2 = score(x2);
                    for (int it = 0; it < 16; ++it) {
                        if (f1 < f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - gr * (hi - lo); f1 = score(x1); }
                        else { lo = x1; x1 = x2; f1 = f2; x2 = lo + gr * (hi - lo); f2 = score(x2); }
                    }
                    const double best = 0.5 * (lo + hi);
                    fitted[cam].push_back(best);
                    dcpdev::DevelopOptions o = dcp_options;
                    o.exposure_bias = static_cast<float>(best);
                    ours = dcpdev::develop(small, *prof, o, &info);
                    std::cout << "  {" << cam << " fit " << std::showpos << std::setprecision(2) << best
                              << std::noshowpos << "EV}";
                } else {
                    dcpdev::DevelopOptions o = dcp_options;
                    if (have_exposure_table && !raw->has_baseline_exposure &&
                        lower(p.raw.extension().string()) != ".dng") {
                        const auto it = exposure_table.find(cam);
                        o.exposure_bias += static_cast<float>(
                            (!table_default_only && it != exposure_table.end()) ? it->second : exposure_default);
                    }
                    ours = dcpdev::develop(small, *prof, o, &info);
                    std::cout << "  {" << cam << (p.profile_version.empty() ? "" : " (" + p.profile_version + ")") << ": " << info << "}";
                }
            } else {
                auto req = base_request(p.raw, stock);
                req.exposure_placement = placement;  // the app's default for RAW
                ours = render(session, req, err);
            }
            if (!ours) {
                std::cout << "  [" << stock << " RAW render failed: " << err << "]";
                continue;
            }
            cv::Mat target = ref;  // developer parity: against the Lightroom TIFF itself
            if (stock != "none") {
                // Film parity: against the Lightroom TIFF run through the same stock, with
                // exactly the settings the Lightroom round-trip uses.
                auto treq = base_request(p.tif, stock);
                treq.exposure_placement = "as_shot";
                treq.rendered_input = 80.0F;  // decode colour space defaults to sRGB, as in the app
                auto t = render(session, treq, err);
                if (!t) {
                    std::cout << "  [" << stock << " TIFF render failed: " << err << "]";
                    continue;
                }
                target = shrink(*t, size);
            }
            const cv::Mat aligned = align_to(shrink(*ours, size * 2), target);
            const Metrics m = compare(aligned, target);
            std::cout << "  " << stock << " dE=" << std::fixed << std::setprecision(2) << m.de_mean;
            for (const std::string& key : {std::string("_all"), p.brand}) {
                auto& slot = agg[stock][key];
                accumulate(slot.first, m);
                ++slot.second;
            }
            per_pair << (first ? "" : ",\n") << "  {\"raw\":\"" << p.raw.filename().string() << "\",\"brand\":\""
                     << p.brand << "\",\"stock\":\"" << stock << "\",\"metrics\":" << metrics_json(m) << "}";
            first = false;
            if (!dump_dir.empty()) {
                cv::Mat side;
                cv::hconcat(target, aligned, side);
                cv::Mat out8;
                side.convertTo(out8, CV_8UC3, 255.0);
                cv::imwrite((fs::path(dump_dir) / (p.raw.stem().string() + "_" + stock + ".jpg")).string(), out8,
                            {cv::IMWRITE_JPEG_QUALITY, 90});
            }
        }
        std::cout << "\n";
    }

    // ---- report ---------------------------------------------------------
    std::cout << "\n(reference = Lightroom for stock 'none'; Lightroom-TIFF->same stock otherwise."
                 " dL in L* units, + = ours brighter; C*rat < 1 = ours less saturated)\n";
    for (const auto& [stock, by_brand] : agg) {
        std::cout << "\n== stock: " << stock << " ==\n";
        print_header();
        for (const auto& [brand, slot] : by_brand) {
            print_row(brand == "_all" ? "ALL" : brand, divided(slot.first, slot.second), slot.second);
        }
    }

    if (!fitted.empty()) {
        std::cout << "\n== fitted exposure bias per camera (Adobe's hidden baseline exposure) ==\n";
        for (const auto& [cam, v] : fitted) {
            double mean = 0, mn = 1e9, mx = -1e9;
            for (double b : v) {
                mean += b;
                mn = std::min(mn, b);
                mx = std::max(mx, b);
            }
            mean /= static_cast<double>(v.size());
            std::cout << "  " << std::left << std::setw(28) << cam << std::right << " n=" << v.size() << "  mean "
                      << std::showpos << std::fixed << std::setprecision(2) << mean << " EV  (range " << mn << " .. "
                      << mx << ")" << std::noshowpos << "\n";
        }
    }

    if (!out_path.empty()) {
        std::ofstream j(out_path);
        j << "{\n\"corpus\":\"" << std::regex_replace(corpus.string(), std::regex(R"(\\)"), "/")
          << "\",\"profile\":\"" << profile << "\",\"placement\":\"" << placement << "\",\"size\":" << size
          << ",\n\"summary\":{";
        bool fs1 = true;
        for (const auto& [stock, by_brand] : agg) {
            j << (fs1 ? "" : ",") << "\n \"" << stock << "\":{";
            bool fs2 = true;
            for (const auto& [brand, slot] : by_brand) {
                j << (fs2 ? "" : ",") << "\"" << brand << "\":{\"n\":" << slot.second
                  << ",\"metrics\":" << metrics_json(divided(slot.first, slot.second)) << "}";
                fs2 = false;
            }
            j << "}";
            fs1 = false;
        }
        j << "\n},\n\"pairs\":[\n" << per_pair.str() << "\n]\n}\n";
        std::cout << "\nwrote " << out_path << "\n";
    }
    return 0;
}
