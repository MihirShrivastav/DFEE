import numpy as np
import cv2
from .color_spaces import rgb_to_oklab, oklab_to_rgb, oklab_to_oklch, oklch_to_oklab

class FilmRenderer:
    def __init__(self):
        pass

    def render(self, rgb_linear, masks, render_plan):
        """
        Applies the complete film rendering pipeline using the solved render plan.
        
        Pipeline order mirrors the physical film process:
          Pre-film normalization  → Film emulsion response
          → Material effects      → Scanner finish
        """
        pre_film  = render_plan["pre_film_normalization"]
        response  = render_plan["film_response"]
        effects   = render_plan["material_effects"]
        finish    = render_plan["scanner_finish"]
        stock_type = render_plan.get("stock_type", "color_negative")

        # 1. Pre-Film Normalization (exposure, cast, highlight repair)
        rgb_proc = self._apply_pre_film_normalization(rgb_linear, masks, pre_film)

        # 2. Monochrome panchromatic mix (for B&W stocks — before tone curve)
        if stock_type == "monochrome":
            rgb_proc = self._apply_panchromatic_conversion(rgb_proc, response)

        # 3. Film Tone Response (per-channel S-curve with midtone density)
        rgb_proc = self._apply_film_tone_response(rgb_proc, response)

        # 4. Color Response — zone × hue × saturation using ACTUAL profile values
        if stock_type != "monochrome":
            rgb_proc = self._apply_color_response(rgb_proc, masks, response, finish)

        # 5. Local Contrast and Acutance Shaping
        rgb_proc = self._apply_acutance_shaping(rgb_proc, effects)

        # 6. Halation and Bloom
        rgb_proc = self._apply_halation_bloom(rgb_proc, masks, effects)

        # 7. Procedural Film Grain
        rgb_proc = self._apply_film_grain(rgb_proc, masks, effects)

        # 8. Scanner / Print Finish
        rgb_final = self._apply_scanner_finish(rgb_proc, finish)

        return rgb_final

    # ─── Pre-Film Normalization ───────────────────────────────────────────────

    def _apply_pre_film_normalization(self, rgb_linear, masks, pre_film):
        # 1. Exposure Compensation
        exp_factor = 2.0 ** pre_film["exposure_compensation_stops"]
        rgb_norm = rgb_linear * exp_factor

        # 2. Highlight Channel Repair — blend towards neutral white near clipping
        Y = (0.2126 * rgb_norm[:, :, 0]
           + 0.7152 * rgb_norm[:, :, 1]
           + 0.0722 * rgb_norm[:, :, 2])
        repair_mask = Y > 0.85
        if np.any(repair_mask):
            Y_rep = Y[repair_mask]
            blend = np.clip((Y_rep - 0.85) / 0.15, 0.0, 1.0)
            for c in range(3):
                rgb_norm[repair_mask, c] = (
                    (1.0 - blend) * rgb_norm[repair_mask, c] + blend * Y_rep
                )

        # 3. Camera Cast Correction in OKLab Space
        oklab = rgb_to_oklab(rgb_norm)
        Z1 = masks["luminance_zone_masks"]["Z1"]
        Z3 = masks["luminance_zone_masks"]["Z3"]

        oklab[:, :, 2] += Z1 * pre_film["shadow_blue_normalization"]
        oklab[:, :, 1] -= Z3 * pre_film["green_magenta_stabilization"] * np.sign(oklab[:, :, 1])

        return oklab_to_rgb(oklab)

    # ─── B&W Panchromatic Conversion ─────────────────────────────────────────

    def _apply_panchromatic_conversion(self, rgb_proc, response):
        """
        Converts to panchromatic luminance using spectral sensitivity weights.
        Tri-X is slightly blue-insensitive and green-heavy vs standard Rec.709.
        Weights: R=0.25, G=0.55, B=0.20  (classic panchromatic emulsion)
        """
        wr = response.get("pan_weight_r", 0.25)
        wg = response.get("pan_weight_g", 0.55)
        wb = response.get("pan_weight_b", 0.20)

        Y_pan = (wr * rgb_proc[:, :, 0]
               + wg * rgb_proc[:, :, 1]
               + wb * rgb_proc[:, :, 2])

        # Reconstruct as neutral RGB (all channels equal = monochrome)
        mono = np.stack([Y_pan, Y_pan, Y_pan], axis=-1)
        return mono.astype(np.float32)

    # ─── Film Tone Response ───────────────────────────────────────────────────

    def _apply_film_tone_response(self, rgb_proc, response):
        """
        Applies parametric S-curve to each channel independently.
        S(x) = x^alpha / (x^alpha + (1-x)^beta)
        Midtone density modifies the gamma at the inflection point.
        """
        t_str    = response["toe_strength"]
        s_str    = response["shoulder_strength"]
        d_floor  = response["black_density_floor"]
        mid_dens = response.get("midtone_density", 1.0)  # ← now used

        alpha = 1.0 + t_str
        beta  = 1.0 + s_str

        rgb_toned = np.zeros_like(rgb_proc)
        for c in range(3):
            ch = rgb_proc[:, :, c]

            # For values above 1.0 (overexposed after exposure boost), apply a
            # Reinhard-style soft compression that maps them back into just below 1.0.
            # f(x) = 1 - 1/(1 + k*(x-1)) for x > 1, identity for x <= 1.
            # k = 2 + shoulder_strength so stronger-shoulder profiles compress harder.
            k = 2.0 + s_str
            ch_safe = np.where(
                ch > 1.0,
                1.0 - 1.0 / (1.0 + k * (ch - 1.0)),
                ch
            )
            # Now clamp to the range expected by the algebraic S-curve
            ch_clamp = np.clip(ch_safe, 1e-12, 1.0 - 1e-12)

            # Algebraic S-curve — unchanged and correct for [0,1] input
            s_curve = (ch_clamp ** alpha) / (ch_clamp ** alpha + (1.0 - ch_clamp) ** beta)

            # Apply midtone density as a gamma lift/drop in the mid range
            mid_weight = np.exp(-((s_curve - 0.5) ** 2) / (2 * 0.12 ** 2))
            if mid_dens != 1.0:
                s_curve_gamma = s_curve ** (1.0 / mid_dens)
                s_curve = s_curve * (1.0 - mid_weight) + s_curve_gamma * mid_weight

            rgb_toned[:, :, c] = d_floor + (1.0 - d_floor) * s_curve

        return np.clip(rgb_toned, 0.0, 1.0)

    # ─── Color Response ───────────────────────────────────────────────────────

    def _apply_color_response(self, rgb_proc, masks, response, finish):
        """
        Performs zone × hue × saturation color response in OKLCH/OKLab space.
        Color biases now come from the ACTUAL stock profile YAML values.
        """
        oklab = rgb_to_oklab(rgb_proc)
        lch   = oklab_to_oklch(oklab)

        C = lch[:, :, 1]
        H = lch[:, :, 2]

        # Zone masks
        Z1 = masks["luminance_zone_masks"]["Z1"]   # lower shadows
        Z2 = masks["luminance_zone_masks"]["Z2"]   # upper shadows
        Z3 = masks["luminance_zone_masks"]["Z3"]   # midtones
        Z4 = masks["luminance_zone_masks"]["Z4"]   # upper midtones
        Z5 = masks["luminance_zone_masks"]["Z5"]   # highlights

        # ── 1. Zone-weighted OKLab color biases from profile ──────────────────
        # shadow_bias_lab  → [L_shift, a_shift, b_shift] applied in Z1
        # midtone_bias_lab → [L_shift, a_shift, b_shift] applied in Z3
        # highlight_bias_lab → [L_shift, a_shift, b_shift] applied in Z5
        #
        # We ignore L_shift here (that's what the tone curve handles).
        # Scale the a/b shifts by 0.004 to convert from "per-100-unit" to OKLab units.
        SCALE = 0.004

        shadow_bias    = response.get("shadow_bias_lab",    [0.0, 0.0,  0.0])
        midtone_bias   = response.get("midtone_bias_lab",   [0.0, 0.0,  0.0])
        highlight_bias = response.get("highlight_bias_lab", [0.0, 0.0,  0.0])

        a = oklab[:, :, 1].copy()
        b = oklab[:, :, 2].copy()

        # Shadow zone (Z1 + partial Z2)
        shadow_zone  = Z1 + Z2 * 0.5
        a += shadow_zone * shadow_bias[1] * SCALE
        b += shadow_zone * shadow_bias[2] * SCALE

        # Midtone zone (Z3 + partial Z2 and Z4)
        mid_zone = Z2 * 0.5 + Z3 + Z4 * 0.5
        a += mid_zone * midtone_bias[1] * SCALE
        b += mid_zone * midtone_bias[2] * SCALE

        # Highlight zone (Z5 + partial Z4)
        hi_zone = Z4 * 0.5 + Z5
        a += hi_zone * highlight_bias[1] * SCALE
        b += hi_zone * highlight_bias[2] * SCALE

        # ── 2. Hue-specific saturation compression ────────────────────────────
        # Red/orange band ≈ 0.3–0.9 rad, blue/cyan ≈ 3.5–4.5 rad
        weight_red_orange = np.clip(np.cos(H - 0.6), 0.0, 1.0) ** 2
        weight_blue_cyan  = np.clip(np.cos(H - 4.0), 0.0, 1.0) ** 2

        red_comp  = response.get("red_orange_compression",  0.0)
        blue_comp = response.get("blue_cyan_compression",   0.0)

        C_new = C * (1.0 - red_comp  * weight_red_orange * Z3)
        C_new = C_new * (1.0 - blue_comp * weight_blue_cyan  * Z5)

        # ── 3. Neon / high-chroma compression ────────────────────────────────
        # Real film dye layers saturate non-linearly. Extremely saturated inputs
        # (neon lights, LED signs, artificial colours) are compressed toward a
        # "film maximum chroma" rather than reproduced linearly.
        # neon_compression ∈ [0,1]: 0 = no effect, 1 = heavy compression.
        neon_comp = response.get("neon_compression", 0.0)
        if neon_comp > 0.0:
            # The "knee" in OKLCH chroma space.
            # Typical natural-scene chroma in OKLab: 0.0–0.20.
            # Neon/LED signs can hit 0.30+.
            knee_start = 0.15   # below this → no compression
            knee_end   = 0.35   # above this → full compression applied
            knee_range = knee_end - knee_start

            # Soft-knee weight: 0 below knee_start, rises to 1 at knee_end
            knee_w = np.clip((C_new - knee_start) / knee_range, 0.0, 1.0) ** 2

            # Roll-off target: approach knee_start asymptotically
            C_neon = knee_start + (C_new - knee_start) * (1.0 - neon_comp * knee_w)
            C_new  = C_new * (1.0 - knee_w) + C_neon * knee_w

        # ── 4. Highlight desaturation ─────────────────────────────────────────
        h_desat = response.get("highlight_desaturation", 0.0)
        C_final = C_new * (1.0 - h_desat * Z5)

        # Write back — apply BOTH the chroma compression (in LCH) and the
        # Lab bias shifts (in OKLab) correctly without one overwriting the other.
        # Correct order:
        #   1. Build new a/b from (C_final, H) — this captures chroma compression
        #   2. Add the Lab bias on top of those
        a_from_lch = C_final * np.cos(H)
        b_from_lch = C_final * np.sin(H)

        oklab_new = oklab.copy()
        oklab_new[:, :, 1] = a_from_lch + (a - oklab[:, :, 1])   # bias delta on top of chroma-adjusted a
        oklab_new[:, :, 2] = b_from_lch + (b - oklab[:, :, 2])   # bias delta on top of chroma-adjusted b

        return oklab_to_rgb(oklab_new)

    # ─── Acutance Shaping ─────────────────────────────────────────────────────

    def _apply_acutance_shaping(self, rgb_proc, effects):
        """Multi-scale frequency decomposition to shape local contrast and acutance."""
        oklab = rgb_to_oklab(rgb_proc)
        L = oklab[:, :, 0]
        h, w = L.shape

        k_low = 31
        if k_low >= min(h, w): k_low = min(h, w) // 2 * 2 - 1
        if k_low < 3: k_low = 3
        L_low = cv2.GaussianBlur(L, (k_low, k_low), 0)

        k_mid = 7
        L_mid  = cv2.GaussianBlur(L, (k_mid, k_mid), 0) - L_low
        L_high = L - (L_low + L_mid)

        # Boost mid-frequency presence; soften high-frequency digital edge
        edge_soft = effects["edge_softening"]
        L_new = L_low + L_mid * 1.12 + L_high * (1.0 - edge_soft)

        oklab[:, :, 0] = np.clip(L_new, 0.0, 1.0)
        return oklab_to_rgb(oklab)

    # ─── Halation & Bloom ─────────────────────────────────────────────────────

    def _apply_halation_bloom(self, rgb_proc, masks, effects):
        """Simulates optical halation and soft bloom."""
        h_str = effects["halation_strength"]
        b_str = effects["bloom_strength"]

        rgb_out = rgb_proc.copy()

        if h_str > 0.0:
            source   = masks["halation_source_mask"]
            receiver = masks["halation_receiver_mask"]

            k_hal = 21
            if k_hal >= min(rgb_proc.shape[0], rgb_proc.shape[1]):
                k_hal = 5
            halation_blur = cv2.GaussianBlur(source, (k_hal, k_hal), 0)
            bleed = halation_blur * receiver * h_str

            # Red-orange bleed characteristic
            rgb_out[:, :, 0] += bleed * 1.00
            rgb_out[:, :, 1] += bleed * 0.22
            rgb_out[:, :, 2] += bleed * 0.08

        if b_str > 0.0:
            k_bloom = 51
            if k_bloom >= min(rgb_proc.shape[0], rgb_proc.shape[1]):
                k_bloom = 15
            bloom_blur = cv2.GaussianBlur(rgb_out, (k_bloom, k_bloom), 0)
            Z5 = masks["luminance_zone_masks"]["Z5"][:, :, np.newaxis]
            rgb_out = ((1.0 - b_str * 0.12 * Z5) * rgb_out
                     + b_str * 0.12 * Z5 * bloom_blur)

        return np.clip(rgb_out, 0.0, 1.0)

    # ─── Film Grain ───────────────────────────────────────────────────────────

    def _apply_film_grain(self, rgb_proc, masks, effects):
        """Synthesizes organic film grain modulated by density and smooth areas."""
        g_str    = effects["grain_strength"]
        g_sz     = effects["grain_size"]
        g_chroma = effects["grain_chroma_strength"]

        if g_str == 0.0:
            return rgb_proc

        h, w, _ = rgb_proc.shape

        np.random.seed(42)
        noise_luma     = np.random.normal(0.0, 1.0, (h, w)).astype(np.float32)
        noise_chroma_a = np.random.normal(0.0, 1.0, (h, w)).astype(np.float32)
        noise_chroma_b = np.random.normal(0.0, 1.0, (h, w)).astype(np.float32)

        if g_sz > 0.1:
            k_sz = int(g_sz * 5)
            if k_sz % 2 == 0: k_sz += 1
            if k_sz >= 3:
                noise_luma     = cv2.GaussianBlur(noise_luma,     (k_sz, k_sz), 0)
                noise_chroma_a = cv2.GaussianBlur(noise_chroma_a, (k_sz, k_sz), 0)
                noise_chroma_b = cv2.GaussianBlur(noise_chroma_b, (k_sz, k_sz), 0)
                noise_luma     /= max(np.std(noise_luma),     1e-8)
                noise_chroma_a /= max(np.std(noise_chroma_a), 1e-8)
                noise_chroma_b /= max(np.std(noise_chroma_b), 1e-8)

        Z1 = masks["luminance_zone_masks"]["Z1"]
        Z2 = masks["luminance_zone_masks"]["Z2"]
        Z3 = masks["luminance_zone_masks"]["Z3"]
        density_mod  = Z2 + Z3 + Z1 * 0.4
        smooth_mod   = masks["grain_receptivity_mask"]
        total_mod    = density_mod * smooth_mod

        oklab = rgb_to_oklab(rgb_proc)
        oklab[:, :, 0] += noise_luma     * g_str * 0.05 * total_mod
        oklab[:, :, 1] += noise_chroma_a * g_str * g_chroma * 0.08 * total_mod
        oklab[:, :, 2] += noise_chroma_b * g_str * g_chroma * 0.08 * total_mod

        return oklab_to_rgb(oklab)

    # ─── Scanner Finish ───────────────────────────────────────────────────────

    def _apply_scanner_finish(self, rgb_proc, finish):
        """Applies final scan/print contrast, warmth, and black/white point scaling."""
        oklab = rgb_to_oklab(rgb_proc)
        L = oklab[:, :, 0]

        # Scanner contrast: simple slope around 0.5 midgray.
        # Values > 1.0 add contrast, < 1.0 reduce it.
        # We intentionally keep this mild — the film curve already shaped contrast;
        # the scanner finish is a subtle paper/print characteristic, not a second S-curve.
        contrast = finish["scan_contrast"]
        if contrast != 1.0:
            # Slope around midgray: brighter above 0.5, darker below 0.5
            slope  = np.clip(contrast, 0.5, 2.0)
            L_new  = np.clip(0.5 + (L - 0.5) * slope, 0.0, 1.0)
            oklab[:, :, 0] = L_new

        warmth = finish["scan_warmth"]
        oklab[:, :, 2] += warmth * 0.015

        bp = finish["black_point"]
        wp = finish["white_point"]

        rgb_scan = oklab_to_rgb(oklab)
        rgb_scan = bp + (wp - bp) * rgb_scan

        return np.clip(rgb_scan, 0.0, 1.0)
