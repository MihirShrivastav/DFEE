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

        # 1. Pre-Film Normalization (exposure, cast, highlight repair)
        rgb_proc = self._apply_pre_film_normalization(rgb_linear, masks, pre_film)

        # 2. Monochrome panchromatic mix (for B&W stocks — before tone curve)
        if stock_type == "monochrome":
            rgb_proc = self._apply_panchromatic_conversion(rgb_proc, response)

        # 3. Film Tone Response (per-channel S-curves with differential layer latitude)
        rgb_proc = self._apply_film_tone_response(rgb_proc, response)

        # 4. Cross-channel dye contamination (inter-image effect)
        rgb_proc = self._apply_dye_contamination(rgb_proc, response)

        # 5. Color Response — zone × hue × saturation using ACTUAL profile values
        if stock_type != "monochrome":
            rgb_proc = self._apply_color_response(rgb_proc, masks, response, finish)

        # 6. Luminance-chroma coupling (smooth highlight/shadow chroma rolloff)
        if stock_type != "monochrome":
            rgb_proc = self._apply_luminance_chroma_coupling(rgb_proc, response)

        # 7. Local Contrast and Acutance Shaping
        rgb_proc = self._apply_acutance_shaping(rgb_proc, effects)

        # 8. Halation and Bloom
        rgb_proc = self._apply_halation_bloom(rgb_proc, masks, effects)

        # 9. Procedural Film Grain
        rgb_proc = self._apply_film_grain(rgb_proc, masks, effects)

        # 10. Scanner / Print Finish
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
                1.0 - 1.0 / (1.0 + k * (ch - 1.0)),
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

            rgb_toned[:, :, c] = d_floor + (1.0 - d_floor) * s_curve

        return np.clip(rgb_toned, 0.0, 1.0)

    # ─── Cross-Channel Dye Contamination ────────────────────────────────────────────

    def _apply_dye_contamination(self, rgb_proc, response):
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
        if not dc:
            return rgb_proc

        r2g = float(dc.get("r_to_g", 0.0))
        g2r = float(dc.get("g_to_r", 0.0))
        b2g = float(dc.get("b_to_g", 0.0))
        b2r = float(dc.get("b_to_r", 0.0))
        r2b = float(dc.get("r_to_b", 0.0))
        g2b = float(dc.get("g_to_b", 0.0))

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

    def _apply_color_response(self, rgb_proc, masks, response, finish):
        """
        Performs zone × hue × saturation color response in OKLCH/OKLab space.
        Color biases now come from the ACTUAL stock profile YAML values.
        """
        oklab = rgb_to_oklab(rgb_proc)
        lch   = oklab_to_oklch(oklab)

        C = lch[:, :, 1]
        H = lch[:, :, 2]

        # ── 0. Global chroma / colour-density boost ───────────────────────────
        # RAW linear data is tonally flat and chromatically desaturated compared
        # to real film dye layers. This multiplicative lift compensates, giving
        # each stock its characteristic colour density before any per-hue shaping.
        chroma_boost = response.get("chroma_boost", 1.0)
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

    # ─── Luminance-Chroma Coupling ────────────────────────────────────────────────────

    def _apply_luminance_chroma_coupling(self, rgb_proc, response):
        """
        Applies luminance-dependent chroma compression matching real film dye behaviour.

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
            hi_comp    = float(cc.get("hi_compression",    0.50))
            sh_start   = float(cc.get("sh_rolloff_start",  0.18))
            sh_comp    = float(cc.get("sh_compression",    0.45))
            hi_hue_t   = float(cc.get("hi_hue_conv_rad",  0.28))  # warm cream
            hi_hue_str = float(cc.get("hi_hue_conv_str",  0.18))
        elif stock_type == "color_reversal":
            hi_start   = float(cc.get("hi_rolloff_start",  0.70))
            hi_rate    = float(cc.get("hi_rolloff_rate",   3.0))
            hi_comp    = float(cc.get("hi_compression",    0.70))
            sh_start   = float(cc.get("sh_rolloff_start",  0.16))
            sh_comp    = float(cc.get("sh_compression",    0.55))
            hi_hue_t   = float(cc.get("hi_hue_conv_rad",  0.15))  # near-neutral
            hi_hue_str = float(cc.get("hi_hue_conv_str",  0.12))
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
        """Multi-scale frequency decomposition to shape local contrast and acutance."""
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
        """
        Film grain synthesis — physically-informed model.

        Real film grain is NOT random pixel noise. It is caused by silver halide
        crystal clusters which:
          • Have a characteristic spatial size (ISO-dependent, coarser for faster film)
          • Are primarily a LUMINANCE phenomenon — colour film has some chroma grain
            from separate dye layer registration, but it is small and correlated
            (NOT independent rainbow speckle on a/b axes)
          • Cluster in organic, overlapping blobs — modelled by oversampling white
            noise then Gaussian-blurring and resampling back to image size
          • Are most visible in upper shadows and midtones (Z2–Z3), falling off in
            very deep shadows (low signal = low contrast for grain) and highlights
            (silver is uniformly dense → grain averages out)

        Chroma grain: a SINGLE correlated noise field, tinted along a stock-specific
        hue direction (warm for negative stocks, cool/neutral for reversal), not two
        independent a/b channels.
        """
        g_str    = effects["grain_strength"]
        g_sz     = effects["grain_size"]       # 0.0 – 1.0; 0 = very fine, 1 = very coarse
        g_chroma = effects["grain_chroma_strength"]  # fraction of luma grain in chroma

        if g_str == 0.0:
            return rgb_proc

        h, w, _ = rgb_proc.shape

        # ── Spatial seed from image content ──────────────────────────────────
        # Use a hash of the first pixel and image shape so each unique image gets
        # unique grain, but the same image+params always produces the same grain
        # (reproducible but not a fixed stamp shared across every image).
        content_hash = int(abs(rgb_proc[0, 0, 0] * 1e6 + rgb_proc[h//2, w//2, 1] * 1e4
                               + rgb_proc[-1, -1, 2] * 1e2)) % (2**31)
        rng = np.random.default_rng(content_hash)

        # ── Grain texture via oversample → blur → downsample ─────────────────
        # Oversampling at 2× then blurring at the native scale creates spatially-
        # correlated clusters that look organic, unlike blurring white noise at 1×.
        # The cluster radius in pixels = grain_size * scale_factor.
        scale  = 2                                  # oversample factor
        oh, ow = h * scale, w * scale

        # Luma noise: single channel
        base_noise = rng.standard_normal((oh, ow)).astype(np.float32)

        # Grain cluster size — grain_size 0.1 → ~3px radius, 0.6 → ~11px radius
        # (These are sizes in the OVERSAMPLED domain, so halve for native pixels)
        sigma = max(0.5, g_sz * 9.0)               # Gaussian sigma in oversampled space
        k = int(sigma * 4) | 1                     # odd kernel, 4-sigma coverage
        k = min(k, min(oh, ow) - 1 if min(oh, ow) % 2 == 0 else min(oh, ow))
        if k < 3: k = 3
        if k % 2 == 0: k += 1

        blurred = cv2.GaussianBlur(base_noise, (k, k), sigma)
        std = np.std(blurred)
        if std > 1e-8:
            blurred /= std                         # re-normalise so std=1 after blur

        # Downsample back to native resolution
        noise_luma = cv2.resize(blurred, (w, h), interpolation=cv2.INTER_AREA)

        # ── Zone-weighted modulation ──────────────────────────────────────────
        # Grain peaks in Z2 (upper shadows) + Z3 (midtones).
        # Z1 (deep shadows): low silver density → grain contrast drops → less visible
        # Z4–Z5 (highlights): dense silver → grain averages out → less visible
        Z1 = masks["luminance_zone_masks"]["Z1"]
        Z2 = masks["luminance_zone_masks"]["Z2"]
        Z3 = masks["luminance_zone_masks"]["Z3"]
        Z4 = masks["luminance_zone_masks"]["Z4"]
        density_mod = Z1 * 0.35 + Z2 * 1.0 + Z3 * 0.75 + Z4 * 0.20

        smooth_mod  = masks["grain_receptivity_mask"]   # suppresses grain in very flat areas
        total_mod   = density_mod * smooth_mod

        # ── Apply to OKLab ────────────────────────────────────────────────────
        oklab = rgb_to_oklab(rgb_proc)

        # Luminance: the primary grain channel
        # Scale of 0.04 maps g_str≈0.4 (Tri-X) to a visible but not crushing amount
        luma_amount = g_str * 0.04
        oklab[:, :, 0] = np.clip(
            oklab[:, :, 0] + noise_luma * luma_amount * total_mod,
            0.0, 1.0
        )

        # Chroma: correlated with luma noise (same spatial texture, small amplitude).
        # A single tint direction per stock prevents rainbow speckle.
        # g_chroma is typically 0.0 (monochrome), 0.01–0.03 (reversal), 0.05–0.20 (neg).
        if g_chroma > 0.0:
            # The same noise field tinted along a warm direction (a: +red, b: +yellow)
            # models the dye-layer misregistration in colour film.
            # We use a scaled version of the luma field — they are correlated spatially.
            chroma_amount = g_str * g_chroma * 0.018
            # Tint ratio: slightly more on b (yellow-blue) than a (green-red), matching
            # typical dye-cloud colour variability in C-41/E-6 processes.
            oklab[:, :, 1] += noise_luma * chroma_amount * 0.55 * total_mod   # a (red-green)
            oklab[:, :, 2] += noise_luma * chroma_amount * 0.80 * total_mod   # b (yellow-blue)
            # Clamp a/b to prevent wild colour from outlier noise values
            oklab[:, :, 1] = np.clip(oklab[:, :, 1], -0.35, 0.35)
            oklab[:, :, 2] = np.clip(oklab[:, :, 2], -0.35, 0.35)

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
