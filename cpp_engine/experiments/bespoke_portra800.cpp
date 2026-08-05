// Bespoke RAW->film pipeline spike (Portra 800).
//
// This drives the REAL DFEE engine pipeline (solver + renderer + the full Portra 800
// profile: tone curve, per-zone Lab color biases, hue/saturation response, chroma
// coupling, dye contamination, subtractive density/compression, grain, halation) via
// EngineSession::render_preview. It is a standalone harness so we can iterate on the
// RAW front-end (decode/development) WITHOUT touching the shipping app, while keeping
// every stock calibration in play.
//
// Usage: dfee_bespoke_portra800 <input.raw> <output.jpg> [stock] [placement]
//                               [film_contrast] [shadow_lift] [temp] [film_color_density]
//   stock              default portra_800
//   placement          default auto_balanced  (or as_shot)
//   film_contrast      0..200, 100 = stock default (lower = softer)
//   shadow_lift        -100..100, 0 = stock base-fog (positive opens shadows)
//   temp               -100..100, 0 = neutral (positive warms)
//   film_color_density 0..200, 100 = stock default

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#ifndef DFEE_REPO_ROOT
#  define DFEE_REPO_ROOT "."
#endif

namespace {

// ── Newson/Delon/Galerne Boolean film-grain model (Monte Carlo prototype) ──
// Grain is a Boolean model: discs (grains) at Poisson-distributed centres, radius
// log-normal(mu=radius, sigma_r). The local grain density follows the input tone so
// coverage's MEAN == input and its VARIANCE (the visible grain) peaks in the mids —
// physically correct signal-dependence, not a hand-tuned envelope. The grain FIELD is
// consistent across output pixels (grains seeded per spatial cell, then thinned by the
// input at each grain's own location), which is what gives organic clumping instead of
// per-pixel noise. Rendered per output pixel by Monte-Carlo coverage over N jittered
// samples (jitter sigma = viewing/scale filter). Resolution-independent.
struct GrainParams {
    float radius = 1.1F;       // mean grain radius, px at working resolution
    float sigma_r = 0.30F;     // log-normal radius spread (0 = constant size)
    float sigma_filter = 0.55F;// Monte-Carlo jitter (grain softness / AA)
    int   n_samples = 100;     // MC samples per pixel
    float amount = 1.0F;       // 0..1 blend toward grainy
    std::uint32_t seed = 6543U;
};

inline std::uint32_t hash_u32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7FEB352DU; x ^= x >> 15; x *= 0x846CA68BU; x ^= x >> 16;
    return x;
}
inline std::uint32_t cell_seed(int cx, int cy, int ch, std::uint32_t s) {
    std::uint32_t h = hash_u32(static_cast<std::uint32_t>(cx) * 0x9E3779B1U + s);
    h = hash_u32(h ^ (static_cast<std::uint32_t>(cy) * 0x85EBCA77U));
    h = hash_u32(h ^ (static_cast<std::uint32_t>(ch) * 0xC2B2AE3DU));
    return h ? h : 1U;
}
struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t x) : s(x ? x : 1U) {}
    std::uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return static_cast<float>(next() >> 8) * (1.0F / 16777216.0F); }
    float gauss() {
        const float u1 = std::max(1.0e-7F, uni());
        const float u2 = uni();
        return std::sqrt(-2.0F * std::log(u1)) * std::cos(6.2831853F * u2);
    }
};
inline int poisson(Rng& r, float lambda) {
    if (lambda <= 0.0F) return 0;
    if (lambda > 20.0F) {  // Gaussian approx for large means
        const int k = static_cast<int>(std::lround(lambda + std::sqrt(lambda) * r.gauss()));
        return k < 0 ? 0 : k;
    }
    const float L = std::exp(-lambda);
    float p = 1.0F; int k = 0;
    do { ++k; p *= r.uni(); } while (p > L);
    return k - 1;
}

// Apply Boolean grain in place to a CV_32FC3 image in [0,1] (display domain for the
// prototype). The grain is computed ONCE on luminance (a single achromatic field) and
// applied as a luma delta to all channels — correct for B&W and a good default for
// colour negative (grain is largely luminance). Chroma grain can be layered later.
void apply_boolean_grain(cv::Mat& img, const GrainParams& gp) {
    const cv::Mat src = img.clone();
    const int H = img.rows, W = img.cols;
    const float r = std::max(0.25F, gp.radius);
    const float cell = r;                                   // one grain reaches <=1 cell
    const float r_query = r * (1.0F + 2.0F * gp.sigma_r);   // cell search halo for big grains
    const float u_max = 0.98F;
    const float log1m_umax = std::log(1.0F - u_max);
    const float inv_pir2 = 1.0F / (3.14159265F * r * r);
    const float lambda_max = -log1m_umax * inv_pir2;        // grains / px^2 at u_max

    // Precompute luminance (the grain's input tone).
    cv::Mat luma(H, W, CV_32F);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const cv::Vec3f& p = src.at<cv::Vec3f>(y, x);   // BGR
            luma.at<float>(y, x) = 0.0722F * p[0] + 0.7152F * p[1] + 0.2126F * p[2];
        }
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            Rng jit(cell_seed(x, y, 101, gp.seed));
            int covered = 0;
            for (int i = 0; i < gp.n_samples; ++i) {
                const float px = static_cast<float>(x) + 0.5F + gp.sigma_filter * jit.gauss();
                const float py = static_cast<float>(y) + 0.5F + gp.sigma_filter * jit.gauss();
                const int c0x = static_cast<int>(std::floor((px - r_query) / cell));
                const int c1x = static_cast<int>(std::floor((px + r_query) / cell));
                const int c0y = static_cast<int>(std::floor((py - r_query) / cell));
                const int c1y = static_cast<int>(std::floor((py + r_query) / cell));
                bool hit = false;
                for (int cy = c0y; cy <= c1y && !hit; ++cy) {
                    for (int cx = c0x; cx <= c1x && !hit; ++cx) {
                        Rng cr(cell_seed(cx, cy, 0, gp.seed));
                        const int q = poisson(cr, lambda_max * cell * cell);
                        for (int g = 0; g < q; ++g) {
                            const float gxp = (static_cast<float>(cx) + cr.uni()) * cell;
                            const float gyp = (static_cast<float>(cy) + cr.uni()) * cell;
                            float rr = r;
                            if (gp.sigma_r > 0.0F) {
                                rr = r * std::exp(gp.sigma_r * cr.gauss() - 0.5F * gp.sigma_r * gp.sigma_r);
                            }
                            const float keep = cr.uni();
                            const int gix = std::clamp(static_cast<int>(gxp), 0, W - 1);
                            const int giy = std::clamp(static_cast<int>(gyp), 0, H - 1);
                            float uc = std::clamp(luma.at<float>(giy, gix), 0.0F, u_max);
                            const float keep_prob = std::log(1.0F - uc) / log1m_umax;
                            if (keep >= keep_prob) continue;   // grain absent at its own tone
                            const float dx = px - gxp, dy = py - gyp;
                            if (dx * dx + dy * dy < rr * rr) { hit = true; break; }
                        }
                    }
                }
                if (hit) ++covered;
            }
            const float lu = luma.at<float>(y, x);
            const float grainy = static_cast<float>(covered) / static_cast<float>(gp.n_samples);
            const float delta = gp.amount * (grainy - lu);   // achromatic luma shift
            cv::Vec3f& o = img.at<cv::Vec3f>(y, x);
            o[0] = std::clamp(o[0] + delta, 0.0F, 1.0F);
            o[1] = std::clamp(o[1] + delta, 0.0F, 1.0F);
            o[2] = std::clamp(o[2] + delta, 0.0F, 1.0F);
        }
    }
}

// ── Newson locally-Gaussian analytic grain ────────────────────────────────
// Crisp, fast, signal-dependent grain: white noise convolved with a DISC kernel of
// radius r (so the noise has the Boolean grain's exact disc autocorrelation -> crisp,
// grain-sized structure, not a soft Gaussian blob), normalized to unit variance, then
// scaled per pixel by sqrt(u*(1-u)) (the exact Boolean point variance, peaking in the
// mids). Applied as an achromatic luma delta (+ optional subtle independent chroma).
[[nodiscard]] cv::Mat correlated_noise(int H, int W, float radius, std::uint32_t seed) {
    cv::Mat noise(H, W, CV_32F);
    Rng rng(seed);
    for (int y = 0; y < H; ++y) {
        float* row = noise.ptr<float>(y);
        for (int x = 0; x < W; ++x) row[x] = rng.gauss();
    }
    const int R = std::max(1, static_cast<int>(std::ceil(radius)));
    cv::Mat kern(2 * R + 1, 2 * R + 1, CV_32F, cv::Scalar(0));
    int cnt = 0;
    for (int ky = -R; ky <= R; ++ky)
        for (int kx = -R; kx <= R; ++kx)
            if (static_cast<float>(kx * kx + ky * ky) <= radius * radius) {
                kern.at<float>(ky + R, kx + R) = 1.0F; ++cnt;
            }
    kern /= std::sqrt(static_cast<float>(std::max(1, cnt)));  // unit-variance output
    cv::Mat out;
    cv::filter2D(noise, out, -1, kern, cv::Point(-1, -1), 0.0, cv::BORDER_REFLECT101);
    return out;
}

void apply_analytic_grain(cv::Mat& img, float radius, float amount, float chroma, std::uint32_t seed) {
    const int H = img.rows, W = img.cols;
    const cv::Mat luma_field = correlated_noise(H, W, radius, seed);       // achromatic grain
    cv::Mat cr, cb;
    if (chroma > 0.0F) {
        cr = correlated_noise(H, W, radius, seed ^ 0xA5A5A5A5U);
        cb = correlated_noise(H, W, radius, seed ^ 0x5A5A5A5AU);
    }
    for (int y = 0; y < H; ++y) {
        cv::Vec3f* row = img.ptr<cv::Vec3f>(y);
        const float* ln = luma_field.ptr<float>(y);
        const float* rn = chroma > 0.0F ? cr.ptr<float>(y) : nullptr;
        const float* bn = chroma > 0.0F ? cb.ptr<float>(y) : nullptr;
        for (int x = 0; x < W; ++x) {
            const float u = std::clamp(0.0722F * row[x][0] + 0.7152F * row[x][1] + 0.2126F * row[x][2], 0.0F, 1.0F);
            const float sigma = amount * 2.0F * std::sqrt(std::max(0.0F, u * (1.0F - u)));  // peak at u=0.5
            const float d = sigma * ln[x];
            float b = row[x][0] + d, g = row[x][1] + d, r = row[x][2] + d;
            if (chroma > 0.0F) {
                b += sigma * chroma * bn[x];
                r += sigma * chroma * rn[x];
            }
            row[x][0] = std::clamp(b, 0.0F, 1.0F);
            row[x][1] = std::clamp(g, 0.0F, 1.0F);
            row[x][2] = std::clamp(r, 0.0F, 1.0F);
        }
    }
}

inline float env_f(const char* k, float d) { const char* v = std::getenv(k); return v ? std::stof(v) : d; }
inline int   env_i(const char* k, int d)   { const char* v = std::getenv(k); return v ? std::stoi(v) : d; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <input.raw> <output.jpg> [stock] [placement]\n";
        return 2;
    }
    const std::string in_path = std::filesystem::absolute(argv[1]).string();
    const std::string out_path = argv[2];
    const std::string stock = (argc >= 4) ? argv[3] : "portra_800";
    const std::string placement = (argc >= 5) ? argv[4] : "auto_balanced";
    const float film_contrast = (argc >= 6) ? std::stof(argv[5]) : 100.0F;
    const float shadow_lift = (argc >= 7) ? std::stof(argv[6]) : 0.0F;
    const float temp = (argc >= 8) ? std::stof(argv[7]) : 0.0F;
    const float film_color_density = (argc >= 9) ? std::stof(argv[8]) : 100.0F;
    const float film_exposure_ev = (argc >= 10) ? std::stof(argv[9]) : 0.0F;

    dfee::EngineSession session(std::filesystem::path(DFEE_REPO_ROOT));

    dfee::NativePreviewRenderRequest req;      // defaults mirror the app's neutral controls
    req.filename = in_path;
    req.stock = stock;
    req.effect_pipeline_version = "filmic_v3"; // subtractive film pipeline
    req.exposure_placement = placement;
    req.adaptive = true;
    req.grain = "Auto";
    req.halation = "Auto";
    req.film_contrast = film_contrast;
    req.shadow_lift = shadow_lift;
    req.temp = temp;
    req.film_color_density = film_color_density;
    req.film_exposure_ev = film_exposure_ev;

    // Diagnostic: DFEE_EXPORT_OUT=<path.tif> reproduces the Lightroom round-trip
    // export (full-res render + write to output_path) and prints the failure reason.
    if (const char* eo = std::getenv("DFEE_EXPORT_OUT")) {
        dfee::NativeExportRequest ereq;
        static_cast<dfee::NativePreviewRenderRequest&>(ereq) = req;
        ereq.export_format = "tiff";
        ereq.output_path = std::filesystem::path(eo);
        const dfee::NativeExportResponse er = session.export_image(ereq);
        std::cout << "export ok=" << er.ok << " status=" << er.status
                  << " err_code=[" << er.error.code << "] msg=[" << er.error.user_message
                  << "] detail=[" << er.error.detail << "] out=[" << er.output_path.string() << "]\n";
        return er.ok ? 0 : 1;
    }

    // DFEE_GRAIN=boolean: render with the engine's grain OFF, then apply the prototype
    // Boolean-model grain (tuned via DFEE_GRAIN_R / _SIGMAR / _SIGMA / _N / _AMT).
    const char* gmode = std::getenv("DFEE_GRAIN");
    const std::string gm = gmode ? gmode : "";
    const bool boolean_grain = gm == "boolean";
    const bool analytic_grain = gm == "analytic";
    if (boolean_grain || analytic_grain) req.grain = "Off";

    const dfee::NativePreviewRenderResponse resp = session.render_preview(req);
    if (!resp.ok) {
        std::cerr << "render failed: " << resp.status << " — " << resp.error.code
                  << ": " << resp.error.user_message << "\n";
        return 1;
    }

    if (boolean_grain || analytic_grain) {
        const cv::Mat enc(1, static_cast<int>(resp.jpeg_bytes.size()), CV_8U,
                          const_cast<std::uint8_t*>(resp.jpeg_bytes.data()));
        cv::Mat bgr8 = cv::imdecode(enc, cv::IMREAD_COLOR);
        if (bgr8.empty()) { std::cerr << "decode failed\n"; return 1; }
        cv::Mat f;
        bgr8.convertTo(f, CV_32FC3, 1.0 / 255.0);
        if (analytic_grain) {
            const float radius = env_f("DFEE_GRAIN_R", 1.0F);
            const float amount = env_f("DFEE_GRAIN_AMT", 0.20F);
            const float chroma = env_f("DFEE_GRAIN_CHROMA", 0.0F);
            apply_analytic_grain(f, radius, amount, chroma, 12345U);
            cv::Mat out8; f.convertTo(out8, CV_8UC3, 255.0);
            cv::imwrite(out_path, out8, {cv::IMWRITE_JPEG_QUALITY, 92});
            std::cout << "wrote (analytic grain r=" << radius << " amt=" << amount
                      << " chroma=" << chroma << ") " << out_path << "\n";
            return 0;
        }
        GrainParams gp;
        gp.radius = env_f("DFEE_GRAIN_R", 1.1F);
        gp.sigma_r = env_f("DFEE_GRAIN_SIGMAR", 0.30F);
        gp.sigma_filter = env_f("DFEE_GRAIN_SIGMA", 0.55F);
        gp.n_samples = env_i("DFEE_GRAIN_N", 100);
        gp.amount = env_f("DFEE_GRAIN_AMT", 1.0F);
        apply_boolean_grain(f, gp);
        cv::Mat out8;
        f.convertTo(out8, CV_8UC3, 255.0);
        cv::imwrite(out_path, out8, {cv::IMWRITE_JPEG_QUALITY, 92});
        std::cout << "wrote (boolean grain r=" << gp.radius << " sigma_r=" << gp.sigma_r
                  << " N=" << gp.n_samples << ") " << out_path << "\n";
        return 0;
    }

    std::ofstream out(out_path, std::ios::binary);
    if (!out) { std::cerr << "cannot open " << out_path << "\n"; return 1; }
    out.write(reinterpret_cast<const char*>(resp.jpeg_bytes.data()),
              static_cast<std::streamsize>(resp.jpeg_bytes.size()));
    std::cout << "wrote " << out_path << " (" << resp.jpeg_bytes.size() << " bytes), stock="
              << stock << " placement=" << placement << "\n";
    return 0;
}
