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
          Pre-film normalization  → Film emulsion response (with per-channel curves)
          → Dye contamination     → Color response
          → Luminance-chroma coupling → Acutance shaping
          → Material effects      → Scanner finish
        """
        pre_film  = render_plan["pre_film_normalization"]
        response  = render_plan["film_response"]
        effects   = render_plan["material_effects"]
        finish    = render_plan["scanner_finish"]
        stock_type = render_plan.get("stock_type", "color_negative")

        # Film Color density multiplier: 0=no film color, 100=stock default, 200=pushed
        fc = float(response.get("film_color", 100.0)) / 100.0

        # 1. Pre-Film Normalization (exposure, cast, highlight repair)
        rgb_proc = self._apply_pre_film_normalization(rgb_linear, masks, pre_film)

        # 2. Monochrome panchromatic mix (for B&W stocks — before tone curve)
        if stock_type == "monochrome":
            rgb_proc = self._apply_panchromatic_conversion(rgb_proc, response)

        # 3. Film Tone Response (per-channel S-curves with differential layer latitude)
        rgb_proc = self._apply_film_tone_response(rgb_proc, response)

        # 4. Cross-channel dye contamination (inter-image effect)
        rgb_proc = self._apply_dye_contamination(rgb_proc, response, fc)

        # 5. Color Response — zone × hue × saturation using ACTUAL profile values
        if stock_type != "monochrome":
            rgb_proc = self._apply_color_response(rgb_proc, masks, response, finish, fc)

        # 6. Luminance-chroma coupling (smooth highlight/shadow chroma rolloff)
        if stock_type != "monochrome":
            rgb_proc = self._apply_luminance_chroma_coupling(rgb_proc, response, fc)

        # 7. Local Contrast and Acutance Shaping
        rgb_proc = self._apply_acutance_shaping(rgb_proc, effects)

        # 8. Halation and Bloom
        rgb_proc = self._apply_halation_bloom(rgb_proc, masks, effects)

        # 9. Procedural Film Grain
        rgb_proc = self._apply_film_grain(rgb_proc, masks, effects)

        # 10. Scanner / Print Finish
        rgb_final = self._apply_scanner_finish(rgb_proc, finish)

        # 11. Theatrical Print Stock (optional second-stage emulsion)
        print_finish = render_plan.get("print_finish")
        if print_finish:
            rgb_final = self._apply_print_finish(rgb_final, print_finish)

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
        Applies parametric S-curve to each channel with per-channel multipliers.

        Physical basis: R/G/B dye layers of film have different spectral sensitivities
        and different amounts of latitude. Specifically:
          - Blue layer: shorter latitude (clips sooner at both ends)
          - Red layer:  widest latitude (gentlest toe and shoulder)
          - Green layer: intermediate

        channel_toe_mult / channel_shoulder_mult / channel_midtone_mult give each
        stock its characteristic cross-channel tonal balance without needing
        separate per-channel tone response YAML sections.

        S(x) = x^alpha / (x^alpha + (1-x)^beta)
        """
        t_str    = response["toe_strength"]
        s_str    = response["shoulder_strength"]
        d_floor  = response["black_density_floor"]
        mid_dens = response.get("midtone_density", 1.0)

        # Per-channel multipliers (default [1,1,1] = identical curves)
        toe_mult = response.get("channel_toe_mult",      [1.0, 1.0, 1.0])
        sh_mult  = response.get("channel_shoulder_mult", [1.0, 1.0, 1.0])
        md_mult  = response.get("channel_midtone_mult",  [1.0, 1.0, 1.0])

        rgb_toned = np.zeros_like(rgb_proc)
        for c in range(3):
            ch = rgb_proc[:, :, c]

            alpha_c = 1.0 + t_str * float(toe_mult[c])
            beta_c  = 1.0 + s_str * float(sh_mult[c])
            mid_c   = mid_dens * float(md_mult[c])

            # For values above 1.0 after exposure boost, soft Reinhard compression
            k = 2.0 + s_str * float(sh_mult[c])
            ch_safe = np.where(
                ch > 1.0,
                1.0 - 1.0 / np.maximum(1e-6, 1.0 + k * (ch - 1.0)),
                ch
            )
            ch_clamp = np.clip(ch_safe, 1e-12, 1.0 - 1e-12)

            # Algebraic S-curve
            s_curve = (ch_clamp ** alpha_c) / (
                ch_clamp ** alpha_c + (1.0 - ch_clamp) ** beta_c
            )

            # Midtone density modulation (Gaussian weight peaks at 0.5)
            mid_weight = np.exp(-((s_curve - 0.5) ** 2) / (2 * 0.12 ** 2))
            if mid_c != 1.0:
                s_curve_gamma = s_curve ** (1.0 / mid_c)
                s_curve = s_curve * (1.0 - mid_weight) + s_curve_gamma * mid_weight

            # Toe-only shadow lift: only lifts the deepest shadows, fades out by mid-shadow
            # (s_curve ≈ 0.20). Leaves midtones and highlights completely untouched.
            # Physical basis: film D-min / base fog lifts just the underexposed toe —
            # it does NOT compress or shift bright areas (unlike a global level shift).
            #
            # Weight: 1.0 at s_curve=0 (pure black), decays smoothly toward 0
            # The knee at 0.25 means the lift is gone by the lower-mid shadows.
            toe_fade = np.clip(s_curve / 0.25, 0.0, 1.0)  # 0→1 transition over [0, 0.25]
            shadow_weight = (1.0 - toe_fade) ** 2          # quadratic ease-out
            rgb_toned[:, :, c] = s_curve + d_floor * shadow_weight

        return np.clip(rgb_toned, 0.0, 1.0)

    # ─── Cross-Channel Dye Contamination ────────────────────────────────────────────

    def _apply_dye_contamination(self, rgb_proc, response, fc=1.0):
        """
        Simulates inter-image dye contamination between film layers.

        Physical basis: In real color negative film the cyan, magenta, and yellow
        dye couplers are not perfectly spectrally selective. The interlayer correction
        dyes partially compensate but a residual cross-contamination remains. This
        creates the characteristic warm richness of negative film midtones that cannot
        be achieved with per-channel curves or zone biases alone — it is an
        exposure-level interaction.

        Model: small off-diagonal terms in a 3×3 contamination matrix.
        Values are on the order of 0.01–0.05 (1–5% cross-bleeding).
        Defaults to identity (no effect) if dye_contamination not in profile.
        """
        dc = response.get("dye_contamination", {})
        if not dc or fc == 0.0:
            return rgb_proc

        r2g = float(dc.get("r_to_g", 0.0)) * fc
        g2r = float(dc.get("g_to_r", 0.0)) * fc
        b2g = float(dc.get("b_to_g", 0.0)) * fc
        b2r = float(dc.get("b_to_r", 0.0)) * fc
        r2b = float(dc.get("r_to_b", 0.0)) * fc
        g2b = float(dc.get("g_to_b", 0.0)) * fc

        # Skip entirely if all zeros (fast path)
        if r2g == 0 and g2r == 0 and b2g == 0 and b2r == 0 and r2b == 0 and g2b == 0:
            return rgb_proc

        R = rgb_proc[:, :, 0]
        G = rgb_proc[:, :, 1]
        B = rgb_proc[:, :, 2]

        # Each output channel = self + small contributions from the other two
        # Normalised so the diagonal stays at 1.0 (energy-preserving)
        R_out = np.clip(R + G * g2r + B * b2r, 0.0, 1.0)
        G_out = np.clip(G + R * r2g + B * b2g, 0.0, 1.0)
        B_out = np.clip(B + R * r2b + G * g2b, 0.0, 1.0)

        return np.stack([R_out, G_out, B_out], axis=-1)

    # ─── Color Response ───────────────────────────────────────────────────────

    def _apply_color_response(self, rgb_proc, masks, response, finish, fc=1.0):
        """
        Performs zone × hue × saturation color response in OKLCH/OKLab space.
        Color biases now come from the ACTUAL stock profile YAML values.
        fc (film_color): scales all color personality; 0=neutral, 1=stock default, 2=pushed.
        """
        oklab = rgb_to_oklab(rgb_proc)
        lch   = oklab_to_oklch(oklab)

        C = lch[:, :, 1]
        H = lch[:, :, 2]

        # ── 0. Global chroma / colour-density boost ───────────────────────────
        # RAW linear data is tonally flat and chromatically desaturated compared
        # to real film dye layers. This multiplicative lift compensates, giving
        # each stock its characteristic colour density before any per-hue shaping.
        # fc scales the *deviation* from neutral (1.0): at fc=0 → boost=1.0 (neutral)
        chroma_boost = 1.0 + (response.get("chroma_boost", 1.0) - 1.0) * fc
        if chroma_boost != 1.0:
            C = C * chroma_boost

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

        # Shadow zone (Z1 + partial Z2) — bias a/b scaled by fc
        shadow_zone  = Z1 + Z2 * 0.5
        a += shadow_zone * shadow_bias[1] * SCALE * fc
        b += shadow_zone * shadow_bias[2] * SCALE * fc

        # Midtone zone (Z3 + partial Z2 and Z4) — bias a/b scaled by fc
        mid_zone = Z2 * 0.5 + Z3 + Z4 * 0.5
        a += mid_zone * midtone_bias[1] * SCALE * fc
        b += mid_zone * midtone_bias[2] * SCALE * fc

        # Highlight zone (Z5 + partial Z4) — bias a/b scaled by fc
        hi_zone = Z4 * 0.5 + Z5
        a += hi_zone * highlight_bias[1] * SCALE * fc
        b += hi_zone * highlight_bias[2] * SCALE * fc

        # ── 2. Hue-specific saturation compression ────────────────────────────
        # Red/orange band ≈ 0.3–0.9 rad, blue/cyan ≈ 3.5–4.5 rad
        weight_red_orange = np.clip(np.cos(H - 0.6), 0.0, 1.0) ** 2
        weight_blue_cyan  = np.clip(np.cos(H - 4.0), 0.0, 1.0) ** 2

        red_comp  = response.get("red_orange_compression",  0.0) * fc
        blue_comp = response.get("blue_cyan_compression",   0.0) * fc

        C_new = C * (1.0 - red_comp  * weight_red_orange * Z3)
        C_new = C_new * (1.0 - blue_comp * weight_blue_cyan  * Z5)

        # ── 3. Neon / high-chroma compression ────────────────────────────────
        # Real film dye layers saturate non-linearly. Extremely saturated inputs
        # (neon lights, LED signs, artificial colours) are compressed toward a
        # "film maximum chroma" rather than reproduced linearly.
        # neon_compression ∈ [0,1]: 0 = no effect, 1 = heavy compression.
        neon_comp = response.get("neon_compression", 0.0) * fc
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

        # ── 4. Highlight desaturation — scaled by fc ──────────────────────────
        h_desat = response.get("highlight_desaturation", 0.0) * fc
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

    # ─── Luminance-Chroma Coupling ────────────────────────────────────────────────────

    def _apply_luminance_chroma_coupling(self, rgb_proc, response, fc=1.0):
        """
        Applies luminance-dependent chroma compression matching real film dye behaviour.
        fc (film_color): scales compression strengths and hue convergence.

        Physical basis:
          Film forms colour by dye-coupler chemistry. The amount of dye formed at any
          emulsion point is governed by LOCAL EXPOSURE (luminance), not by scene
          chromaticity independently. This creates two fundamental couplings:

          HIGHLIGHT COUPLING: As exposure increases toward the emulsion shoulder,
          all dye layers approach saturation simultaneously. The result is a smooth,
          continuous chroma rolloff toward white/cream — NOT a discrete zone-5 mask.
          Reversal films (Velvia, Kodachrome) roll off sharply; negative films (Portra)
          roll off very gradually, giving their famous highlight latitude.

          SHADOW COUPLING: Below the toe, silver density is too low to hold chroma
          reliably. Shadows absorb toward the film’s base+fog colour:
            - Negative film: slightly warm (orange base density)
            - Reversal film: near-neutral (no orange base)

          HIGHLIGHT HUE CONVERGENCE: In highlights all hues gently rotate toward the
          film’s characteristic ‘white point colour’ before going fully white. For
          most negative stocks this is a warm cream; for reversal stocks, cooler.

        Parameters (from chroma_coupling block in profile YAML):
          hi_rolloff_start  L (OKLCH) where highlight compression begins  [0.68-0.78]
          hi_rolloff_rate   exponent > 1 => slow start / sharp end;       [1.5-4.0]
                            < 1 => fast start / gradual end (Portra-like)
          hi_compression    max fraction of chroma removed at L=1          [0.40-0.85]
          sh_rolloff_start  L below which shadow chroma absorption begins  [0.12-0.22]
          sh_compression    max fraction removed in deepest shadows        [0.35-0.60]
          hi_hue_conv_rad   target hue (radians) highlights converge to    [0.1-0.5]
          hi_hue_conv_str   strength of hue convergence                    [0.10-0.30]
        """
        cc = response.get("chroma_coupling", {})
        stock_type = response.get("stock_type", "color_negative")

        # Defaults by stock type if not specified
        if stock_type == "color_negative":
            hi_start   = float(cc.get("hi_rolloff_start",  0.75))
            hi_rate    = float(cc.get("hi_rolloff_rate",   1.8))
            hi_comp    = float(cc.get("hi_compression",    0.50)) * fc
            sh_start   = float(cc.get("sh_rolloff_start",  0.18))
            sh_comp    = float(cc.get("sh_compression",    0.45)) * fc
            hi_hue_t   = float(cc.get("hi_hue_conv_rad",  0.28))  # warm cream
            hi_hue_str = float(cc.get("hi_hue_conv_str",  0.18)) * fc
        elif stock_type == "color_reversal":
            hi_start   = float(cc.get("hi_rolloff_start",  0.70))
            hi_rate    = float(cc.get("hi_rolloff_rate",   3.0))
            hi_comp    = float(cc.get("hi_compression",    0.70)) * fc
            sh_start   = float(cc.get("sh_rolloff_start",  0.16))
            sh_comp    = float(cc.get("sh_compression",    0.55)) * fc
            hi_hue_t   = float(cc.get("hi_hue_conv_rad",  0.15))  # near-neutral
            hi_hue_str = float(cc.get("hi_hue_conv_str",  0.12)) * fc
        else:  # monochrome (shouldn’t be called, but safe)
            return rgb_proc

        oklab = rgb_to_oklab(np.clip(rgb_proc, 0.0, 1.0))
        lch   = oklab_to_oklch(oklab)

        L = lch[:, :, 0]
        C = lch[:, :, 1]
        H = lch[:, :, 2]

        # ── Highlight chroma rolloff ───────────────────────────────────────────────
        # Normalise L to [0,1] within the rolloff region
        t_hi = np.clip((L - hi_start) / max(1.0 - hi_start, 1e-6), 0.0, 1.0)
        # Power function: hi_rate > 1 = slow start/sharp end (reversal-like)
        #                 hi_rate < 1 = fast start/gradual end (negative-like)
        hi_mask = t_hi ** hi_rate
        C_hi = C * (1.0 - hi_mask * hi_comp)

        # ── Shadow chroma absorption ────────────────────────────────────────────────
        t_sh = np.clip(1.0 - L / max(sh_start, 1e-6), 0.0, 1.0)
        sh_mask = t_sh ** 1.5   # gentle — shadows fade colour gradually
        C_new = C_hi * (1.0 - sh_mask * sh_comp)

        # ── Highlight hue convergence ─────────────────────────────────────────────
        # As luminance approaches 1.0, all hues slowly rotate toward the film’s
        # characteristic ‘white point hue’ (warm cream for neg, near-neutral for rev).
        # Short-arc angular interpolation to avoid jumping through 2π.
        dH = ((hi_hue_t - H) + np.pi) % (2 * np.pi) - np.pi   # shortest arc
        H_new = H + dH * hi_mask * hi_hue_str

        lch_new = lch.copy()
        lch_new[:, :, 1] = np.clip(C_new, 0.0, None)
        lch_new[:, :, 2] = H_new % (2.0 * np.pi)

        oklab_new = oklch_to_oklab(lch_new)
        oklab_new[:, :, 0] = lch_new[:, :, 0]   # preserve L exactly
        return np.clip(oklab_to_rgb(oklab_new), 0.0, 1.0)

    # ─── Acutance Shaping ─────────────────────────────────────────────────────

    def _apply_acutance_shaping(self, rgb_proc, effects):
        """Multi-scale frequency decomposition to shape local contrast and acutance, followed by Contrast-Adaptive Sharpening (CAS) with luminance masking."""
        oklab = rgb_to_oklab(rgb_proc)
        L = oklab[:, :, 0]
        h, w = L.shape

        k_low = 19
        if k_low >= min(h, w): k_low = min(h, w) // 2 * 2 - 1
        if k_low < 3: k_low = 3
        L_low = cv2.GaussianBlur(L, (k_low, k_low), 0)

        k_mid = 5
        L_mid  = cv2.GaussianBlur(L, (k_mid, k_mid), 0) - L_low
        L_high = L - (L_low + L_mid)

        # Subtle mid-frequency presence boost; soften high-frequency digital edge.
        # Keep the boost small to avoid halos bleeding from bright into dark areas.
        edge_soft = effects["edge_softening"]
        L_new = L_low + L_mid * 1.05 + L_high * (1.0 - edge_soft)
        L_processed = np.clip(L_new, 0.0, 1.0)

        # Contrast-Adaptive Sharpening (CAS) on Lightness channel
        sharpness = effects.get("sharpness", 0.0)
        sharpness_mask = effects.get("sharpness_mask", 0.5)

        if sharpness > 0.0:
            # Pad to handle edge boundaries
            L_padded = np.pad(L_processed, ((1, 1), (1, 1)), mode='reflect')

            # Extract 3x3 neighborhood slices
            a = L_padded[0:-2, 0:-2]
            b = L_padded[0:-2, 1:-1]
            c = L_padded[0:-2, 2:]
            d = L_padded[1:-1, 0:-2]
            e = L_padded[1:-1, 1:-1]  # center
            f = L_padded[1:-1, 2:]
            g = L_padded[2:, 0:-2]
            h = L_padded[2:, 1:-1]
            i = L_padded[2:, 2:]

            # Soft min & max (over cardinal neighbors and center)
            mnRGB = np.minimum(np.minimum(np.minimum(d, e), np.minimum(f, b)), h)
            mnRGB2 = np.minimum(mnRGB, np.minimum(np.minimum(a, c), np.minimum(g, i)))
            mnRGB = mnRGB + mnRGB2

            mxRGB = np.maximum(np.maximum(np.maximum(d, e), np.maximum(f, b)), h)
            mxRGB2 = np.maximum(mxRGB, np.maximum(np.maximum(a, c), np.maximum(g, i)))
            mxRGB = mxRGB + mxRGB2

            # Smooth minimum distance to signal limit divided by smooth max
            rcpMRGB = 1.0 / np.maximum(mxRGB, 1e-5)
            ampRGB = np.clip(np.minimum(mnRGB, 2.0 - mxRGB) * rcpMRGB, 0.0, 1.0)

            # Shaping amount of sharpening (equivalent to shader's rsqrt)
            ampRGB = np.sqrt(ampRGB)

            # Peak mapping: map sharpness slider [0.0, 2.0] internally for peak scaling
            sharp_val = np.clip(sharpness, 0.0, 1.0)
            peak = 8.0 - 3.0 * sharp_val

            # Negative lobe weights (correct AMD CAS weight formula: wRGB = -ampRGB / peak)
            wRGB = -ampRGB / peak

            # Filter shape: 5-tap filter normalization
            rcpWeightRGB = 1.0 / (1.0 + 4.0 * wRGB)
            window = (b + d) + (f + h)

            # Sharpened lightness channel
            L_sharp = np.clip((window * wRGB + e) * rcpWeightRGB, 0.0, 1.0)

            # Luminance masking: smooth bell curve centered at L_processed = 0.5
            # Zero at extremes (0 and 1) if sharpness_mask = 1.0, flat 1.0 if sharpness_mask = 0.0
            luma_mask = 1.0 - sharpness_mask * (1.0 - np.sin(np.pi * L_processed)**2)

            # Blend back based on the mask and overall sharpness
            delta_L = L_sharp - L_processed
            L_processed = np.clip(L_processed + delta_L * luma_mask * sharpness, 0.0, 1.0)

        oklab[:, :, 0] = L_processed
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
        """
        Film grain synthesis — channel-independent RGB Boolean model emulating physical emulsion layers.
        
        Real color film consists of three separate chemical emulsion layers (Red/Cyan, Green/Magenta, Blue/Yellow)
        each containing independent, uncorrelated silver halide crystal distributions.
        
        This model:
          • Generates independent noise fields for Red, Green, and Blue channels.
          • Adapts grain size and strength per-layer (Blue has coarser grain, Red has finer grain).
          • Uses the Boolean model (convolution of sparse random points with a circular structuring element)
            to create overlapping, flat-topped grain particles with sharp boundaries rather than cloudy Gaussian noise.
          • Adjusts particle sizes dynamically based on image resolution to maintain size parity across previews and exports.
          • Blends independent channel noise and common noise based on the stock's `grain_chroma_strength`
            to transition between monochromatic grain (B&W) and organic color dye clouds (Color negative).
          • Modulates grain amplitude as a function of the local channel exposure in gamma-2.2 space
            using a custom bell curve peaked at 0.40.
        """
        g_str    = effects["grain_strength"]
        g_sz     = effects["grain_size"]       # 0.0 – 2.0; 0.5 is default
        g_chroma = effects["grain_chroma_strength"]  # 0.0 (monochrome) to 0.20+ (high-speed negative)
        g_rough  = effects.get("grain_roughness", 0.5)

        if g_str == 0.0:
            return rgb_proc

        h, w, _ = rgb_proc.shape

        # ── Resolution scaling factor ────────────────────────────────────────
        # Maintains consistent grain scale relative to the image frame (baseline: 2048px width)
        scale_factor = w / 2048.0

        # ── Spatial seed from image content ──────────────────────────────────
        content_hash = int(abs(rgb_proc[0, 0, 0] * 1e6 + rgb_proc[h//2, w//2, 1] * 1e4
                               + rgb_proc[-1, -1, 2] * 1e2)) % (2**31)

        # ── Master fields pre-computation (Performance Optimization) ──────────
        # Avoids repeating expensive CPU random allocations and blurs per channel.
        rng = np.random.default_rng(content_hash)
        
        # Master sparse points (density of ~8% is optimal for clumping without massive blobs)
        sparse_master = (rng.random((h, w)) < 0.08).astype(np.float32)

        # Master high-frequency pixel grit
        grit_master = rng.standard_normal((h, w)).astype(np.float32)
        grit_master = grit_master - cv2.GaussianBlur(grit_master, (3, 3), 0.5)
        std_grit_m = np.std(grit_master)
        if std_grit_m > 1e-8:
            grit_master = grit_master / std_grit_m

        # Helper to generate a single channel's normalized noise field using the Boolean model
        def generate_noise_channel(sparse_in, grit_in, size_mult):
            # Compute channel-specific grain size scaled by resolution
            ch_g_sz = max(0.05, g_sz * size_mult * scale_factor)

            # Construct kernel representing a circular silver halide particle
            # Kernel width N (odd, tight sizing maps to 3px–7px at 2K for realistic scale)
            N = int(3.0 + ch_g_sz * 2.5) | 1
            if N < 3: N = 3
            N = min(N, min(h, w) - 1 if min(h, w) % 2 == 0 else min(h, w))
            if N % 2 == 0: N += 1

            # Binary flat-topped ellipse/circle structuring element (Boolean Shape Primitive)
            binary_disk = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (N, N)).astype(np.float32)
            binary_disk /= np.sum(binary_disk) + 1e-8

            # Softened Gaussian disk (tight clamp on sigma ensures we take the edge off without blurring the particle at high res)
            blur_sigma = 0.35 + min(ch_g_sz * 0.15, 0.25)
            gaussian_disk = cv2.GaussianBlur(binary_disk, (N, N), blur_sigma)
            gaussian_disk /= np.sum(gaussian_disk) + 1e-8

            # Interpolate structuring element between sharp binary and soft Gaussian based on roughness
            kernel = g_rough * binary_disk + (1.0 - g_rough) * gaussian_disk
            kernel /= np.sum(kernel) + 1e-8

            # Convolve sparse points to yield overlapping, flat-topped clumps (Boolean Model)
            noise = cv2.filter2D(sparse_in, -1, kernel)

            # Normalize to zero-mean and unit variance
            noise_mean = np.mean(noise)
            noise_std = np.std(noise)
            if noise_std > 1e-8:
                noise = (noise - noise_mean) / noise_std

            # Apply a steep tanh contrast function after convolution to restore razor-sharp particle boundaries
            contrast_factor = 6.0 + g_rough * 8.0
            noise = np.tanh(noise * contrast_factor)

            # Re-normalize clumps to unit variance
            std_clumps = np.std(noise)
            if std_clumps > 1e-8:
                noise = noise / std_clumps

            # Blend macro clumps and micro high-frequency grit based on roughness
            grit_blend = 0.10 + g_rough * 0.25
            noise = (1.0 - grit_blend) * noise + grit_blend * grit_in

            # Tactile edge sharpening using a Laplacian kernel
            if g_rough > 0.0:
                sharp_factor = g_rough * 1.0
                kernel_sharp = np.array([
                    [0, -sharp_factor, 0],
                    [-sharp_factor, 1.0 + 4.0 * sharp_factor, -sharp_factor],
                    [0, -sharp_factor, 0]
                ], dtype=np.float32)
                noise = cv2.filter2D(noise, -1, kernel_sharp)

            # Final variance normalisation
            std = np.std(noise)
            if std > 1e-8:
                noise = noise / std

            return noise

        # To avoid colored grain on monochrome stocks, check channel equality
        is_mono = (np.abs(rgb_proc[:, :, 0] - rgb_proc[:, :, 1]).max() < 1e-4 and 
                   np.abs(rgb_proc[:, :, 1] - rgb_proc[:, :, 2]).max() < 1e-4) or g_chroma <= 0.0

        if is_mono:
            # Monochrome fast path: only 1 convolution
            noise_g = generate_noise_channel(sparse_master, grit_master, 1.00)
            noise_r = noise_g
            noise_b = noise_g
        else:
            # Generate Red, Green, Blue noise fields independently using shifted master seeds
            # Shift seeds to ensure uncorrelated noise fields (emulates separate dye layers)
            sparse_r = sparse_master
            sparse_g = np.roll(sparse_master, 13, axis=0)
            sparse_b = np.roll(sparse_master, 23, axis=1)

            grit_r = grit_master
            grit_g = np.roll(grit_master, 13, axis=0)
            grit_b = np.roll(grit_master, 23, axis=1)

            noise_r_ind = generate_noise_channel(sparse_r, grit_r, 0.80)
            noise_g = generate_noise_channel(sparse_g, grit_g, 1.00)
            noise_b_ind = generate_noise_channel(sparse_b, grit_b, 1.25)

            # Blend channels based on chroma strength (g_chroma)
            # Red and Blue blend with Green (human visual base) to establish correlation
            chroma_mix = np.clip(g_chroma * 4.0, 0.0, 1.0)
            noise_r = (1.0 - chroma_mix) * noise_g + chroma_mix * noise_r_ind
            noise_b = (1.0 - chroma_mix) * noise_g + chroma_mix * noise_b_ind

            # Re-normalize to ensure standard deviation is exactly 1.0 for each channel
            for noise_ch in (noise_r, noise_g, noise_b):
                std_ch = np.std(noise_ch)
                if std_ch > 1e-8:
                    noise_ch /= std_ch

        # ── Apply to RGB in Gamma Space ───────────────────────────────────────
        # Apply grain in perceptual gamma space to emulate visual log density matching
        rgb_g = np.power(np.clip(rgb_proc, 1e-12, 1.0), 1.0 / 2.2).astype(np.float32)

        # Flat region receptivity mask to suppress grain in perfectly smooth gradients
        smooth_mod = masks["grain_receptivity_mask"]

        # Channel parameters: Red (Cyan dye), Green (Magenta dye), Blue (Yellow dye)
        # Blue channel (Yellow dye) has the highest grain visibility on film
        strength_mults = [0.75, 0.95, 1.35]
        noise_channels = [noise_r, noise_g, noise_b]

        for c in range(3):
            ch = rgb_g[:, :, c]

            # Channel-independent modulation curve (bell-curve peaked at 0.4)
            # mod(x) = (x^0.6 * (1-x)^0.9) / 0.364
            mod = (ch ** 0.6 * (1.0 - ch) ** 0.9) / 0.364
            mod = mod * smooth_mod

            # Opacity scale
            ch_str = g_str * 0.038 * strength_mults[c]

            # Inject grain noise
            rgb_g[:, :, c] = np.clip(ch + noise_channels[c] * ch_str * mod, 0.0, 1.0)

        # Convert back to linear space
        return np.power(rgb_g, 2.2).astype(np.float32)


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

    # ─── Theatrical Print Stock Finish ───────────────────────────────────────────

    def _apply_print_finish(self, rgb_in, pf):
        """
        Simulates the photochemical print stock stage — the second emulsion in the
        real pipeline (camera negative → optical printer → print stock → projector).

        This models:
          1. Shadow lift    — D-min base density of the print film
          2. Print S-curve  — the print stock's own contrast characteristic (stacked on negative)
          3. Colour bias    — zone-specific LAB shifts (warm for Kodak, teal for Fuji)
          4. Dye gamut      — saturation scaling to match each stock's dye primaries
          5. Print grain    — a very subtle secondary grain overlay from the print emulsion
        """
        strength = float(pf.get("strength", 1.0))  # 0–2 user slider, 1.0 = stock default
        if strength <= 0.0:
            return rgb_in

        # ── 1. CMY Color Head (Subtractive Printer Lights) ───────────────────────
        # Analog colorists use Cyan, Magenta, Yellow to balance prints.
        # Adding Cyan subtracts Red; Magenta subtracts Green; Yellow subtracts Blue.
        # Range of sliders is -100 to +100.
        print_c = float(pf.get("print_c", 0.0)) / 100.0
        print_m = float(pf.get("print_m", 0.0)) / 100.0
        print_y = float(pf.get("print_y", 0.0)) / 100.0
        
        rgb = rgb_in.copy()
        if print_c != 0.0:
            rgb[:, :, 0] = np.clip(rgb[:, :, 0] * (1.0 - print_c * 0.5), 0.0, 1.0)
        if print_m != 0.0:
            rgb[:, :, 1] = np.clip(rgb[:, :, 1] * (1.0 - print_m * 0.5), 0.0, 1.0)
        if print_y != 0.0:
            rgb[:, :, 2] = np.clip(rgb[:, :, 2] * (1.0 - print_y * 0.5), 0.0, 1.0)

        # ── 2. Shadow lift (D-min base density of print film + user Black Point) ─
        print_bp = float(pf.get("print_black_point", 0.0)) / 100.0
        base_lift = float(pf["shadow_lift"]) * strength
        # Positive print_bp adds more lift (faded), negative crushes
        shadow_lift = np.clip(base_lift + (print_bp * 0.05), 0.0, 0.2)
        rgb = np.clip(rgb + shadow_lift * (1.0 - rgb), 0.0, 1.0)

        # ── 3. Print contrast S-curve (stacked on top of negative curve) ────────
        print_contrast_slider = float(pf.get("print_contrast", 0.0)) / 100.0
        contrast_boost = float(pf["contrast_boost"]) + (print_contrast_slider * 0.5)
        cb = ((contrast_boost - 1.0) * strength) + 1.0  # scale boost by strength
        if abs(cb - 1.0) > 0.005:
            k = np.clip((cb - 1.0) * 3.0, -2.5, 2.5)
            denom = float(np.tanh(k * 0.5))
            if abs(denom) > 1e-6:
                rgb = np.clip(0.5 + np.tanh(k * (rgb - 0.5)) / (2.0 * denom), 0.0, 1.0)

        # ── 3. Highlight rolloff (print shoulder) ────────────────────────────────
        hi_start = float(pf["highlight_rolloff"])
        hi_rate  = float(pf["highlight_rolloff_rate"])
        # Per-pixel luminance
        Y = (0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2])
        # Soft rolloff above hi_start: compress towards 1.0
        above = np.clip((Y - hi_start) / (1.0 - hi_start + 1e-6), 0.0, 1.0)
        rolloff = np.clip(1.0 - np.power(above, hi_rate) * (1.0 - hi_start) * strength, hi_start, 1.0)
        scale = np.where(Y > hi_start, rolloff / np.maximum(Y, 1e-6), 1.0)
        rgb = np.clip(rgb * scale[:, :, np.newaxis], 0.0, 1.0)

        # ── 4. Zone-specific colour bias (shadow / midtone / highlight LAB shifts) ──
        oklab = rgb_to_oklab(rgb)
        L = oklab[:, :, 0]

        sh_bias = pf.get("shadow_bias_lab", [0.0, 0.0, 0.0])
        mt_bias = pf.get("midtone_bias_lab", [0.0, 0.0, 0.0])
        hi_bias = pf.get("highlight_bias_lab", [0.0, 0.0, 0.0])

        # Zone weights
        w_shadow    = np.clip(1.0 - L / 0.35, 0.0, 1.0) ** 1.5
        w_highlight = np.clip((L - 0.60) / 0.40, 0.0, 1.0) ** 1.5
        w_midtone   = np.clip(1.0 - w_shadow - w_highlight, 0.0, 1.0)

        bias_scale = strength * 0.01   # keep LAB shifts subtle
        for i, (sh, mt, hi) in enumerate(zip(sh_bias, mt_bias, hi_bias)):
            if i == 0:  # L channel
                oklab[:, :, 0] += bias_scale * (sh * w_shadow + mt * w_midtone + hi * w_highlight)
            elif i == 1:  # a channel
                oklab[:, :, 1] += bias_scale * (sh * w_shadow + mt * w_midtone + hi * w_highlight)
            elif i == 2:  # b channel
                oklab[:, :, 2] += bias_scale * (sh * w_shadow + mt * w_midtone + hi * w_highlight)

        # ── 5. Per-channel dye bias (red boost / blue suppression / green shift) ─
        red_boost    = float(pf.get("red_boost",    0.0)) * strength
        blue_supp    = float(pf.get("blue_suppression", 0.0)) * strength
        green_shift  = float(pf.get("green_shift",  0.0)) * strength

        rgb2 = oklab_to_rgb(oklab)
        rgb2[:, :, 0] = np.clip(rgb2[:, :, 0] * (1.0 + red_boost * 0.3), 0.0, 1.0)
        rgb2[:, :, 2] = np.clip(rgb2[:, :, 2] * (1.0 - blue_supp * 0.3), 0.0, 1.0)
        rgb2[:, :, 1] = np.clip(rgb2[:, :, 1] * (1.0 + green_shift * 0.15), 0.0, 1.0)

        # ── 6. Print dye saturation scale ───────────────────────────────────────
        sat_scale = float(pf.get("saturation_scale", 1.0))
        if abs(sat_scale - 1.0) > 0.005:
            # Scale chroma in OKLab
            oklab2 = rgb_to_oklab(rgb2)
            effective_sat = 1.0 + (sat_scale - 1.0) * strength
            oklab2[:, :, 1] *= effective_sat
            oklab2[:, :, 2] *= effective_sat
            rgb2 = oklab_to_rgb(oklab2)

        # ── 7. Subtle print grain overlay ────────────────────────────────────────
        g_str  = float(pf.get("grain_strength", 0.0)) * strength
        g_size = float(pf.get("grain_size",     0.3))
        if g_str > 0.005:
            h, w = rgb2.shape[:2]
            rng = np.random.default_rng(seed=42)
            # Raw noise at half resolution then upscale for a softer grain
            small_h = max(h // max(1, int(1.0 / (g_size + 0.01))), 1)
            small_w = max(w // max(1, int(1.0 / (g_size + 0.01))), 1)
            noise_small = rng.standard_normal((small_h, small_w)).astype(np.float32)
            noise = cv2.resize(noise_small, (w, h), interpolation=cv2.INTER_LINEAR)
            noise = noise[:, :, np.newaxis]
            # Luminance-weighted: grain most visible in midtones
            Y2 = (0.2126 * rgb2[:, :, 0] + 0.7152 * rgb2[:, :, 1] + 0.0722 * rgb2[:, :, 2])
            grain_mask = (4.0 * Y2 * (1.0 - Y2))[:, :, np.newaxis]
            rgb2 = np.clip(rgb2 + noise * grain_mask * g_str * 0.04, 0.0, 1.0)

        return np.clip(rgb2.astype(np.float32), 0.0, 1.0)
