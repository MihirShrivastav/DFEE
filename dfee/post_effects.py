"""
DFEE Post-Film Creative Effects
================================
All functions operate on a (H, W, 3) float32 linear-RGB array and return the same.
Applied AFTER film emulation, BEFORE gamma encoding for display.

Pipeline order:
  apply_curves → apply_hsl → apply_clarity → apply_texture → apply_dehaze → apply_bloom
"""

import numpy as np
import cv2


# ─────────────────────────────────────────────────────────────────────────────
# 1. CURVES  (PCHIP monotone cubic spline, no scipy required)
# ─────────────────────────────────────────────────────────────────────────────

def _monotone_cubic_lut(xs, ys, n=4096):
    """
    Fritsch-Carlson monotone cubic interpolation → n-point LUT.
    Guarantees the curve never oscillates or reverses between control points.
    xs, ys: sorted numpy arrays of control point coordinates in [0, 1].
    """
    n_pts = len(xs)
    if n_pts < 2:
        return np.linspace(float(ys[0]), float(ys[-1]), n, dtype=np.float32)
    if n_pts == 2:
        return np.interp(np.linspace(0.0, 1.0, n), xs, ys).astype(np.float32)

    h = np.diff(xs).astype(np.float64)
    delta = np.diff(ys).astype(np.float64) / np.where(h == 0, 1e-10, h)

    # Initial slopes: arithmetic mean of neighbouring secants
    m = np.zeros(n_pts, dtype=np.float64)
    m[0]  = delta[0]
    m[-1] = delta[-1]
    for i in range(1, n_pts - 1):
        m[i] = (delta[i - 1] + delta[i]) / 2.0

    # Fritsch-Carlson monotonicity conditions
    for i in range(n_pts - 1):
        if abs(delta[i]) < 1e-12:
            m[i] = 0.0
            m[i + 1] = 0.0
        else:
            alpha = m[i] / delta[i]
            beta  = m[i + 1] / delta[i]
            r2 = alpha ** 2 + beta ** 2
            if r2 > 9.0:
                tau = 3.0 / np.sqrt(r2)
                m[i]     = tau * alpha * delta[i]
                m[i + 1] = tau * beta  * delta[i]

    # Evaluate on uniform [0,1] grid remapped to [xs[0], xs[-1]]
    x_out  = np.linspace(0.0, 1.0, n, dtype=np.float64)
    x_eval = xs[0] + x_out * (xs[-1] - xs[0])

    seg = np.searchsorted(xs, x_eval, side='right') - 1
    seg = np.clip(seg, 0, n_pts - 2)

    t  = (x_eval - xs[seg]) / np.where(h[seg] == 0, 1e-10, h[seg])
    t2 = t * t;  t3 = t2 * t

    # Hermite basis polynomials
    h00 = 2*t3 - 3*t2 + 1
    h10 = t3   - 2*t2 + t
    h01 = -2*t3 + 3*t2
    h11 = t3   - t2

    y_out = (h00 * ys[seg] + h10 * h[seg] * m[seg]
             + h01 * ys[seg + 1] + h11 * h[seg] * m[seg + 1])

    return np.clip(y_out, 0.0, 1.0).astype(np.float32)


def apply_curves(rendered, control_points):
    """
    Apply a tone curve via PCHIP monotone spline (no scipy required).

    control_points: list of [x, y] pairs in [0,1] (e.g. [[0,0],[0.5,0.5],[1,1]]).
    Applied in gamma-encoded perceptual space so the curve maps to what users see.
    Identity = [[0,0],[1,1]] or any collinear set — returns rendered unchanged.
    """
    if not control_points or len(control_points) < 2:
        return rendered

    pts = sorted(control_points, key=lambda p: p[0])
    xs  = np.array([p[0] for p in pts], dtype=np.float64)
    ys  = np.array([p[1] for p in pts], dtype=np.float64)

    # Identity check
    if np.allclose(xs, ys, atol=1e-3):
        return rendered

    lut = _monotone_cubic_lut(xs, ys)   # 4096-point LUT

    # Apply in gamma space → curve → back to linear
    gamma = np.clip(rendered, 0.0, 1.0) ** (1.0 / 2.2)
    indices = np.clip((gamma * 4095).astype(np.int32), 0, 4095)
    result  = lut[indices]              # shape (H, W, 3), values in [0,1]

    return np.clip(result.astype(np.float32), 0.0, 1.0) ** 2.2


# ─────────────────────────────────────────────────────────────────────────────
# 2. HSL — 8-range hue/saturation/luminance in OKLCH space
# ─────────────────────────────────────────────────────────────────────────────

_HSL_RANGES = [
    # (name, center_deg, sigma_deg)
    ('red',     0.0,   28.0),
    ('orange',  30.0,  22.0),
    ('yellow',  60.0,  25.0),
    ('green',  120.0,  38.0),
    ('aqua',   180.0,  32.0),
    ('blue',   240.0,  32.0),
    ('purple', 285.0,  28.0),
    ('magenta',330.0,  28.0),
]


def apply_hsl(rendered, hsl_params):
    """
    Per-hue Hue/Saturation/Luminance adjustments in OKLCH space.

    hsl_params: dict with keys '<range>_h', '<range>_s', '<range>_l'
                for range in {red, orange, yellow, green, aqua, blue, purple, magenta}.
                All values in [-100, +100], default 0.
    """
    from .color_spaces import rgb_to_oklab, oklab_to_rgb, oklab_to_oklch, oklch_to_oklab

    if not hsl_params or all(float(v) == 0 for v in hsl_params.values()):
        return rendered

    oklab = rgb_to_oklab(rendered)
    lch   = oklab_to_oklch(oklab)

    C     = lch[:, :, 1].copy()
    H_rad = lch[:, :, 2]
    H_deg = (np.degrees(H_rad) % 360.0).astype(np.float32)

    dH = np.zeros_like(H_deg)
    dS = np.zeros_like(H_deg)
    dL = np.zeros_like(H_deg)

    for name, center, sigma in _HSL_RANGES:
        h_amt = float(hsl_params.get(f'{name}_h', 0))
        s_amt = float(hsl_params.get(f'{name}_s', 0))
        l_amt = float(hsl_params.get(f'{name}_l', 0))
        if h_amt == 0 and s_amt == 0 and l_amt == 0:
            continue

        # Circular angular distance (handles wrap at 0/360)
        diff = (H_deg - center + 180.0) % 360.0 - 180.0
        weight = np.exp(-0.5 * (diff / sigma) ** 2)

        # Red also wraps from the high end
        if name == 'red':
            diff2  = (H_deg - (center + 360.0) + 180.0) % 360.0 - 180.0
            weight = np.maximum(weight, np.exp(-0.5 * (diff2 / sigma) ** 2))

        dH += weight * h_amt
        dS += weight * s_amt
        dL += weight * l_amt

    lch_new = lch.copy()

    # Hue shift (degrees → radians)
    lch_new[:, :, 2] = (H_rad + np.radians(dH)) % (2.0 * np.pi)

    # Saturation: scale chroma. s_amt=+100 doubles, -100 zeroes out.
    s_scale = np.clip(1.0 + dS / 100.0, 0.0, 4.0)
    lch_new[:, :, 1] = np.clip(C * s_scale, 0.0, None)

    # Luminance: additive L shift (max ±0.4 at ±100)
    lch_new[:, :, 0] = np.clip(lch[:, :, 0] + dL / 100.0 * 0.40, 0.0, 1.0)

    oklab_new = oklch_to_oklab(lch_new)
    oklab_new[:, :, 0] = lch_new[:, :, 0]   # preserve exact L
    return np.clip(oklab_to_rgb(oklab_new), 0.0, 1.0)


# ─────────────────────────────────────────────────────────────────────────────
# 3. CLARITY — midtone local contrast (large-radius unsharp mask)
# ─────────────────────────────────────────────────────────────────────────────

def apply_clarity(rendered, amount):
    """
    Clarity: boost medium-frequency local contrast in midtones.

    Uses a large-radius blur to extract the medium-frequency band, then applies
    it with a parabolic midtone mask 4·L·(1-L) — peaks at L=0.5, zero at 0 and 1.
    This is identical in intent to Lightroom's Clarity.

    amount: -100 to +100  (negative = matte/flat, positive = punchy midtones)
    """
    if amount == 0:
        return rendered

    h, w    = rendered.shape[:2]
    strength = np.clip(amount / 100.0, -1.0, 1.0)

    gamma = np.clip(rendered, 0.0, 1.0) ** (1.0 / 2.2)

    # Blur radius ≈ 1/16 of short edge — captures "local" but not global structure
    r = max(5, min(h, w) // 16)
    if r % 2 == 0: r += 1
    blurred = cv2.GaussianBlur(gamma, (r, r), r / 3.0)
    detail  = gamma - blurred                       # medium-frequency band

    # Midtone mask
    L    = (0.2126 * gamma[:, :, 0] + 0.7152 * gamma[:, :, 1]
            + 0.0722 * gamma[:, :, 2])
    mask = (4.0 * L * (1.0 - L))[:, :, np.newaxis]  # parabola

    result = np.clip(gamma + detail * strength * 0.65 * mask, 0.0, 1.0)
    return result ** 2.2


# ─────────────────────────────────────────────────────────────────────────────
# 4. TEXTURE — fine microcontrast (small-radius unsharp mask)
# ─────────────────────────────────────────────────────────────────────────────

def apply_texture(rendered, amount):
    """
    Texture: boost fine microcontrast (hair, fabric, foliage, skin pores).

    Identical algorithm to Clarity but with a much smaller blur radius targeting
    high-frequency detail. Affects all tones nearly equally (no midtone bias).
    Pairs with Clarity: Texture=fine, Clarity=medium, Contrast=broad.

    amount: -100 to +100
    """
    if amount == 0:
        return rendered

    h, w    = rendered.shape[:2]
    strength = np.clip(amount / 100.0, -1.0, 1.0)

    gamma = np.clip(rendered, 0.0, 1.0) ** (1.0 / 2.2)

    # Small radius ≈ 1/64 of short edge — only fine structure
    r = max(3, min(h, w) // 64)
    if r % 2 == 0: r += 1
    blurred = cv2.GaussianBlur(gamma, (r, r), r / 2.0)
    detail  = gamma - blurred

    # Gentle mask: suppress only very deep shadows and blown highlights
    L    = (0.2126 * gamma[:, :, 0] + 0.7152 * gamma[:, :, 1]
            + 0.0722 * gamma[:, :, 2])
    mask = (np.clip(L * 5.0, 0.0, 1.0) * np.clip((1.0 - L) * 5.0, 0.0, 1.0)
            )[:, :, np.newaxis]

    result = np.clip(gamma + detail * strength * 0.55 * mask, 0.0, 1.0)
    return result ** 2.2


# ─────────────────────────────────────────────────────────────────────────────
# 5. DEHAZE — Dark Channel Prior (He et al., CVPR 2009)
# ─────────────────────────────────────────────────────────────────────────────

def apply_dehaze(rendered, amount):
    """
    Dehaze / Haze using the Dark Channel Prior algorithm.

    Positive amount removes atmospheric scattering (cuts through fog/haze).
    Negative amount adds haze/mist (atmospheric look).

    amount: -100 to +100

    Note: Uses Gaussian upsampling as fallback (no cv2.ximgproc needed).
    """
    if amount == 0:
        return rendered

    h, w     = rendered.shape[:2]
    strength  = abs(amount) / 100.0
    add_haze  = amount < 0

    # Work on gamma image for perceptually correct dark channel
    img = np.clip(rendered, 0.0, 1.0) ** (1.0 / 2.2)

    # ── Dark channel at ¼ resolution for speed ─────────────────────────────
    scale = 4
    small = cv2.resize(img, (max(1, w // scale), max(1, h // scale)),
                       interpolation=cv2.INTER_AREA)

    dark_small = np.min(small, axis=2)

    # Morphological erosion = local minimum (simulates patch-wise dark channel)
    patch = max(3, 15 // scale)
    if patch % 2 == 0: patch += 1
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (patch, patch))
    dark_small = cv2.erode(dark_small, kernel)

    # Upsample with smooth Gaussian to avoid block artifacts
    dark = cv2.resize(dark_small, (w, h), interpolation=cv2.INTER_LINEAR)
    dark = cv2.GaussianBlur(dark, (0, 0), sigmaX=max(h, w) * 0.006)

    # ── Atmospheric light estimate ─────────────────────────────────────────
    flat      = dark.flatten()
    threshold = np.percentile(flat, 99.9)
    atm_mask  = dark >= threshold
    if np.any(atm_mask):
        A = np.mean(img[atm_mask], axis=0)
    else:
        A = np.array([0.9, 0.9, 0.9], dtype=np.float32)
    A = np.clip(A, 0.5, 1.0)

    # ── Transmission map ──────────────────────────────────────────────────
    omega = 0.95 * strength
    A_max = max(np.max(A), 1e-8)
    t     = np.clip(1.0 - omega * dark / A_max, 0.1, 1.0)[:, :, np.newaxis]

    if add_haze:
        # Add atmospheric scattering: blend toward A
        result = img * t + A * (1.0 - t)
    else:
        # Recover scene radiance: J = (I - A) / t + A
        result = (img - A) / t + A

    return np.clip(result, 0.0, 1.0) ** 2.2


# ─────────────────────────────────────────────────────────────────────────────
# 6. FILM BLOOM — multi-scale screen-mode highlight glow
# ─────────────────────────────────────────────────────────────────────────────

def apply_bloom(rendered, amount):
    """
    Film Bloom: physically-inspired soft glow around bright highlights.

    Simulates how silver-halide emulsion spreads exposure laterally when hit
    by intense light — the base fogs gently, creating a warm, diffuse halo.

    Unlike a simple Gaussian glow, this uses 3 spatial scales composited
    with different weights, giving a gradient falloff that reads as organic:
      - Scale 1 (tight, 50% weight)  → glowing core right at the source
      - Scale 2 (medium, 30% weight) → mid-range spread
      - Scale 3 (wide, 20% weight)   → long, feathered diffusion tail

    Screen blend mode prevents highlights from overexposing while still
    brightening the surrounding area.

    The bloom tint is warm (film always blooms warm, never clinical white):
      R × 1.06, G × 1.02, B × 0.88

    amount: 0 to 100
    """
    if amount <= 0:
        return rendered

    h, w     = rendered.shape[:2]
    strength  = np.clip(amount / 100.0, 0.0, 1.0)

    # ── Highlight mask (smoothstep above threshold) ────────────────────────
    Y = (0.2126 * rendered[:, :, 0]
         + 0.7152 * rendered[:, :, 1]
         + 0.0722 * rendered[:, :, 2])

    threshold = 0.70
    knee      = 0.18
    t_mask    = np.clip((Y - threshold) / knee, 0.0, 1.0)
    # Smoothstep for a soft, natural threshold
    bloom_mask = (t_mask * t_mask * (3.0 - 2.0 * t_mask))[:, :, np.newaxis]

    source = rendered * bloom_mask     # Only highlights enter the bloom path

    # ── Multi-scale Gaussian blurs ─────────────────────────────────────────
    short = min(h, w)

    def gblur(img, radius):
        k = radius * 2 + 1
        if k % 2 == 0: k += 1
        k = max(3, k)
        return cv2.GaussianBlur(img, (k, k), radius / 2.5)

    r1 = max(3,  short // 22)   # tight core
    r2 = max(7,  short // 11)   # medium spread
    r3 = max(13, short //  5)   # wide diffusion tail

    b1 = gblur(source, r1)
    b2 = gblur(source, r2)
    b3 = gblur(source, r3)

    bloom_comp = 0.50 * b1 + 0.30 * b2 + 0.20 * b3

    # ── Warm tint (film blooms warm — silver halide + dye fog = cream glow) ─
    bloom_warm = bloom_comp.copy()
    bloom_warm[:, :, 0] = np.clip(bloom_comp[:, :, 0] * 1.06, 0.0, 1.0)  # R +6%
    bloom_warm[:, :, 1] = np.clip(bloom_comp[:, :, 1] * 1.02, 0.0, 1.0)  # G +2%
    bloom_warm[:, :, 2] =         bloom_comp[:, :, 2] * 0.88               # B -12%

    # ── Screen blend: output = 1 - (1-base)(1-bloom) ─────────────────────
    screen = 1.0 - (1.0 - rendered) * (1.0 - bloom_warm * strength * 0.75)

    return np.clip(screen, 0.0, 1.0)
