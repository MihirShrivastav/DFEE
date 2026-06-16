import numpy as np

class RenderPlanSolver:
    def __init__(self):
        pass

    def solve(self, feature_dict, stock_profile, scan_profile, user_controls=None):
        """
        Combines feature metrics with stock/scanner behavior to compute the render plan.
        
        Args:
            feature_dict (dict): The output of ImageStateAnalyzer
            stock_profile (FilmStockProfile): Selected stock behavior profile
            scan_profile (ScanPrintProfile): Selected scan/print finish profile
            user_controls (dict): Optional user overrides (adaptation_strength, grain_amount, etc.)
        """
        tonal = feature_dict["tonal_distribution"]
        color = feature_dict["hue_saturation_state"]
        spatial = feature_dict["spatial_frequency"]
        channel = feature_dict["channel_behavior"]
        
        # Merge user controls with defaults
        controls = {
            "adaptation_strength": 1.0,
            "exposure_intent": "Preserve",
            "color_cast_handling": "Auto",
            "grain_amount": "Auto",
            "grain_strength": -1.0,
            "grain_size": -1.0,
            "grain_roughness": -1.0,
            "halation_amount": "Auto",
            "output_finish": "Natural",
            "sharpness": 0.0,
            "sharpness_mask": 0.5,
            "film_color": 100.0,
            "print_stock": None,      # PrintStockProfile or None
            "print_strength": 1.0,    # 0.0–2.0 user slider
            "print_c": 0.0,
            "print_m": 0.0,
            "print_y": 0.0,
            "print_contrast": 0.0,
            "print_black_point": 0.0,
        }
        if user_controls:
            controls.update(user_controls)
            
        adaptation_mult = float(controls["adaptation_strength"])
        
        # 1. Input Diagnosis Summary
        input_diagnosis = {
            "tonal_state": tonal["tonal_skew"],
            "dynamic_range_stops": tonal["dynamic_range_stops"],
            "shadow_cast": "normal" if abs(tonal["shadow_depth"]) < 0.05 else "deep",
            "midtone_anchor": tonal["midtone_anchor"],
            "highlight_headroom": tonal["highlight_headroom"],
            "neon_risk": color["neon_risk"],
            "specular_candidate_strength": spatial["specular_point_ratio"]
        }
        
        warnings = []
        
        # Determine warnings
        if tonal["tonal_skew"] == "highlight_stressed":
            warnings.append("HIGH_CHANNEL_CLIPPING")
        if tonal["shadow_depth"] < 0.02:
            warnings.append("SHADOW_NOISE_RISK")
        if color["neon_risk"] > 0.05:
            warnings.append("NEON_CHROMA_RISK")
        if feature_dict.get("camera_input_bias", {}).get("neutral_confidence", 1.0) < 0.25:
            warnings.append("LOW_NEUTRAL_CONFIDENCE")
        if spatial["large_highlight_area_ratio"] > 0.10:
            warnings.append("DIFFUSE_HIGHLIGHT_SUPPRESSION")
            
        # 2. Pre-Film Normalization
        # Solve Exposure Compensation
        # EV offset: log2(target_gray / midtone_anchor)
        target_gray = 0.18
        exposure_comp = 0.0
        
        # Calculate raw alignment to middle gray (baseline)
        raw_comp = float(np.log2(target_gray / max(tonal["midtone_anchor"], 1e-4)))
        
        # ── Intelligent Stock-Specific Exposure & Contrast Correction ────
        stock_type = stock_profile.stock_type
        stock_id = stock_profile.stock_id
        
        # 1. Base Stock Bias (mimicking how photographers expose these stocks in the field)
        stock_bias = 0.0
        if stock_type == "color_negative":
            if stock_id in ["portra_400", "portra_160", "portra_800", "pro_400h", "cinestill_50d", "vision3_500t", "vision3_250d", "fuji_eterna_250d"]:
                # Professional high-latitude negatives are shot overexposed for soft, pastel tones
                stock_bias = 0.65
            elif stock_id in ["ektar_100", "gold_200", "ultramax_400", "superia_400", "colorplus_200"]:
                # Consumer negatives get a minor lift to open up shadows slightly
                stock_bias = 0.35
            else:
                stock_bias = 0.40
        elif stock_type == "color_reversal":
            # Slide films are exposed for highlights (under-exposed to keep rich saturation and avoid blowing highlights)
            # If the scene has bright/contrasty highlights, we pull exposure down to protect them
            p95 = tonal["luma_p95"]
            if p95 > 0.80:
                # Protect highlights by pulling them down to safe range [0.75, 0.80]
                stock_bias = float(np.log2(0.78 / p95))
            else:
                stock_bias = -0.15
        elif stock_type == "monochrome":
            if stock_id == "delta_3200":
                # Delta 3200 is very low contrast, we give it a minor lift to avoid muddy midtones
                stock_bias = 0.25
            else:
                stock_bias = 0.10
                
        # 2. Scene-Adaptive Correction
        if controls["exposure_intent"] == "Auto":
            exposure_comp = raw_comp + stock_bias
        elif controls["exposure_intent"] == "Lift":
            exposure_comp = raw_comp + stock_bias + 0.5
        elif controls["exposure_intent"] == "Darken":
            exposure_comp = raw_comp + stock_bias - 0.5
        else: # "Preserve"
            # Mix raw_comp and stock_bias based on film type behavior
            if stock_type == "color_reversal":
                # Slide film contrast requires strict scene highlight protection
                exposure_comp = raw_comp * 0.45 + stock_bias
            else:
                # Negatives can drift, so we let the exposure float more toward the stock bias
                exposure_comp = raw_comp * 0.25 + stock_bias
                
        # Limit exposure compensation to safe bounds [-2.5, 2.5]
        exposure_comp = float(np.clip(exposure_comp * adaptation_mult, -2.5, 2.5))
        
        # Color Cast Compensation
        bias_info = feature_dict.get("camera_input_bias", {})
        neutral_conf = bias_info.get("neutral_confidence", 0.8)
        comp_sensitivity = stock_profile.adaptation.get("camera_cast_compensation_sensitivity", 0.7)
        
        cast_correction_mult = 0.0
        if controls["color_cast_handling"] in ["Auto", "Neutralize", "Strong neutralize"]:
            cast_correction_mult = neutral_conf * comp_sensitivity * adaptation_mult
            if controls["color_cast_handling"] == "Strong neutralize":
                cast_correction_mult = max(cast_correction_mult, 0.8)
        elif controls["color_cast_handling"] == "Preserve warmth":
            # Correct cool cast but keep warm cast
            if bias_info.get("warm_cool_bias", 0.0) < 0.0:  # cool cast
                cast_correction_mult = neutral_conf * comp_sensitivity * 0.8
            else:
                cast_correction_mult = neutral_conf * comp_sensitivity * 0.2
                
        # Calculate pre-film normalization offsets in OKLab space
        shadow_blue_norm = float(bias_info.get("blue_excess_index", 0.0) * cast_correction_mult)
        green_mag_stab = float(abs(bias_info.get("green_magenta_bias", 0.0)) * cast_correction_mult)
        
        # Highlight channel recovery
        highlight_channel_recovery = 0.0
        if max(channel["clipping_ratios"].values()) > 0.0:
            highlight_channel_recovery = float(np.clip(max(channel["clipping_ratios"].values()) * 5.0, 0.1, 0.9))
            
        # Stock-and-scene-dependent pre-film adjustments
        contrast_comp = 0.0
        highlights_comp = 0.0
        shadows_comp = 0.0
        blacks_comp = 0.0
        whites_comp = 0.0
        midtones_comp = 0.0

        toe_strength = float(stock_profile.tone_response.get("toe_strength", 0.40))

        # Contrast and tonal adaptations
        if stock_type == "color_reversal":
            # slide film is contrasty, compress if scene is harsh
            if tonal["dynamic_range_stops"] > 10.0:
                contrast_comp = -12.0 * (tonal["dynamic_range_stops"] - 10.0)
            # slide highlight protect
            p95 = tonal["luma_p95"]
            if p95 > 0.80:
                highlights_comp = -30.0 * ((p95 - 0.80) / 0.20)
            # slide shadow lift (to keep shadows from going pitch black immediately)
            shadows_comp = 12.0 * (1.0 + toe_strength)
        elif stock_type == "color_negative":
            # negative film can look flat if scene is flat
            if tonal["dynamic_range_stops"] < 7.0:
                contrast_comp = 15.0 * (7.0 - tonal["dynamic_range_stops"])
            if tonal["tonal_skew"] == "low_key":
                shadows_comp = 15.0
        elif stock_type == "monochrome":
            if stock_id == "delta_3200":
                # Delta 3200 is flat by default, give it a tiny contrast boost
                contrast_comp = 8.0
            else:
                # Tri-X and HP5 like punchy midtones
                contrast_comp = 5.0
                
        # Midtones anchor protection
        if tonal["midtone_anchor"] < 0.12:
            midtones_comp = float(np.clip((0.15 - tonal["midtone_anchor"]) * 100.0, 0.0, 30.0))
        elif tonal["midtone_anchor"] > 0.35:
            midtones_comp = float(np.clip((0.25 - tonal["midtone_anchor"]) * 100.0, -25.0, 0.0))

        # Clamp offsets to reasonable slider ranges
        contrast_comp = float(np.clip(contrast_comp * adaptation_mult, -40.0, 40.0))
        highlights_comp = float(np.clip(highlights_comp * adaptation_mult, -50.0, 30.0))
        shadows_comp = float(np.clip(shadows_comp * adaptation_mult, -20.0, 50.0))
        blacks_comp = float(np.clip(blacks_comp * adaptation_mult, -30.0, 30.0))
        whites_comp = float(np.clip(whites_comp * adaptation_mult, -30.0, 30.0))
        midtones_comp = float(np.clip(midtones_comp * adaptation_mult, -40.0, 40.0))

        pre_film_normalization = {
            "exposure_compensation_stops": exposure_comp,
            "shadow_blue_normalization": shadow_blue_norm,
            "green_magenta_stabilization": green_mag_stab,
            "highlight_channel_recovery": highlight_channel_recovery,
            "contrast_compensation": contrast_comp,
            "highlights_compensation": highlights_comp,
            "shadows_compensation": shadows_comp,
            "blacks_compensation": blacks_comp,
            "whites_compensation": whites_comp,
            "midtones_compensation": midtones_comp
        }
        
        # 3. Film Response
        tone = stock_profile.tone_response
        color_resp = stock_profile.color_response
        hsv_resp = stock_profile.hue_saturation_response
        
        # Adjust curve parameters based on dynamic range and headroom
        toe_strength = float(tone["toe_strength"])
        shoulder_strength = float(tone["shoulder_strength"])
        highlight_rolloff_start = float(tone["highlight_rolloff_start"])
        # black_density_floor: the inherent film base + fog (FB+F) — always present,
        # but kept small. This is NOT the scene-driven shadow lift.
        black_density = float(tone["black_density_floor"])

        # Adapt tone curve to scene dynamic range
        if tonal["dynamic_range_stops"] > 11.5:
            # High DR: soften shoulder and toe to preserve detail
            shoulder_strength *= 0.9
            toe_strength *= 0.85
        elif tonal["dynamic_range_stops"] < 5.0:
            # Low DR: punch up contrast
            toe_strength *= 1.15
            
        # Adapt shoulder to highlight headroom
        if tonal["highlight_headroom"] < 0.15:
            # Low headroom: roll off sooner and stronger
            highlight_rolloff_start = max(highlight_rolloff_start - 0.05, 0.5)
            shoulder_strength = min(shoulder_strength + 0.1, 0.95)
            
        # Saturation adjustments
        highlight_desat = float(hsv_resp["highlight_desaturation"])
        if tonal["highlight_headroom"] < 0.10:
            highlight_desat = min(highlight_desat + 0.15, 0.95)
            
        film_response = {
            "toe_strength":            toe_strength,
            "toe_length":              float(tone["toe_length"]),
            "midtone_density":         float(tone["midtone_contrast"]),
            "shoulder_strength":       shoulder_strength,
            "highlight_rolloff_start": highlight_rolloff_start,
            "black_density_floor":     black_density,  # Inherent film FB+F from stock profile
            "highlight_desaturation":  highlight_desat,
            "blue_cyan_compression":   float(hsv_resp["cyan_blue_highlight_compression"]),
            "red_orange_compression":  float(hsv_resp["red_orange_midtone_compression"]),
            "neon_compression":        float(hsv_resp.get("neon_compression", 0.0)),
            "chroma_boost":            float(hsv_resp.get("saturation_boost", 1.0)),
            # Per-channel tone curve differential (B layer has less latitude on real film)
            "channel_toe_mult":        tone.get("channel_toe_mult",      [1.0, 1.0, 1.0]),
            "channel_shoulder_mult":   tone.get("channel_shoulder_mult", [1.0, 1.0, 1.0]),
            "channel_midtone_mult":    tone.get("channel_midtone_mult",  [1.0, 1.0, 1.0]),
            # Per-zone color biases from stock profile YAML (used by color_response stage)
            "shadow_bias_lab":    color_resp.get("shadow_bias_lab",    [0.0, 0.0, 0.0]),
            "midtone_bias_lab":   color_resp.get("midtone_bias_lab",   [0.0, 0.0, 0.0]),
            "highlight_bias_lab": color_resp.get("highlight_bias_lab", [0.0, 0.0, 0.0]),
            # Panchromatic weights for monochrome stocks (Tri-X spectral sensitivity)
            "pan_weight_r": 0.25,
            "pan_weight_g": 0.55,
            "pan_weight_b": 0.20,
            # Luminance-chroma coupling (new)
            "chroma_coupling":     stock_profile.chroma_coupling,
            # Cross-channel dye contamination (new)
            "dye_contamination":   stock_profile.dye_contamination,
            # stock_type needed by chroma coupling step
            "stock_type":          stock_profile.stock_type,
            # Film color density multiplier (0-200, 100=stock default)
            "film_color":          float(controls.get("film_color", 100.0)),
        }

        # 4. Material Effects (Grain, Halation, Bloom)
        g_cfg = stock_profile.grain
        grain_strength = float(g_cfg["strength"])
        grain_size     = float(g_cfg["size"])
        grain_roughness = float(g_cfg.get("roughness", 0.5))

        # Manual grain overrides
        u_strength  = float(controls.get("grain_strength", -1.0))
        u_size      = float(controls.get("grain_size", -1.0))
        u_roughness = float(controls.get("grain_roughness", -1.0))

        # Determine size and roughness
        if u_size >= 0.0:
            grain_size = u_size
        else:
            # Use profile default, and check if we are in "Auto" mode to apply ISO adaptation
            if controls["grain_amount"] in ("Auto",):
                raw_meta  = feature_dict.get("raw_metadata", {})
                shot_iso  = raw_meta.get("iso", None)
                base_iso  = float(stock_profile.adaptation.get("base_iso", 400))
                if shot_iso and shot_iso > 0:
                    push_stops = np.log2(shot_iso / base_iso)
                    if push_stops > 0:
                        grain_size *= (1.0 + 0.10 * push_stops)

        if u_roughness >= 0.0:
            grain_roughness = u_roughness
        else:
            grain_roughness = float(g_cfg.get("roughness", 0.5))

        # Determine strength
        if u_strength >= 0.0:
            grain_strength = u_strength
        else:
            # Check old grain_amount selector override first
            if controls["grain_amount"] == "Off":
                grain_strength = 0.0
            elif controls["grain_amount"] == "Low":
                grain_strength *= 0.5
            elif controls["grain_amount"] == "High":
                grain_strength *= 1.5
                if u_size < 0.0:
                    grain_size *= 1.2
            else:
                # "Auto" or default
                if controls["grain_amount"] in ("Auto",):
                    raw_meta  = feature_dict.get("raw_metadata", {})
                    shot_iso  = raw_meta.get("iso", None)
                    base_iso  = float(stock_profile.adaptation.get("base_iso", 400))
                    if shot_iso and shot_iso > 0:
                        push_stops = np.log2(shot_iso / base_iso)
                        if push_stops > 0:
                            grain_strength *= (1.0 + 0.20 * push_stops)
                        elif push_stops < 0:
                            grain_strength *= max(0.6, 1.0 + 0.08 * push_stops)

        # Shadow noise risk — suppress grain to avoid compounding with digital noise
        if "SHADOW_NOISE_RISK" in warnings:
            noise_sensitivity = stock_profile.adaptation.get("shadow_noise_sensitivity", 0.6)
            grain_strength *= (1.0 - 0.20 * noise_sensitivity)

        # Halation & Bloom
        h_cfg = stock_profile.halation
        halation_strength = float(h_cfg["strength"])
        bloom_strength = 0.10 # default bloom base
        
        # Scale halation by control
        if controls["halation_amount"] == "Off":
            halation_strength = 0.0
            bloom_strength = 0.0
        elif controls["halation_amount"] == "Low":
            halation_strength *= 0.5
            bloom_strength *= 0.5
        elif controls["halation_amount"] == "High":
            halation_strength *= 1.5
            bloom_strength *= 1.5
            
        # Suppress halation if diffuse highlight is high
        if "DIFFUSE_HIGHLIGHT_SUPPRESSION" in warnings:
            halation_strength *= 0.3
            bloom_strength *= 1.2  # Increase soft bloom instead
            
        # Edge softening (acutance)
        edge_softening = float(stock_profile.scanner.get("output_sharpening", 0.15)) * 0.5
        
        material_effects = {
            "grain_strength": grain_strength,
            "grain_size": grain_size,
            "grain_roughness": grain_roughness,
            "grain_chroma_strength": float(g_cfg["chroma_strength"]),
            "halation_strength": halation_strength,
            "bloom_strength": bloom_strength,
            "edge_softening": edge_softening,
            "sharpness": float(controls.get("sharpness", 0.0)),
            "sharpness_mask": float(controls.get("sharpness_mask", 0.5))
        }
        
        # Scanner finish: both contrast fields are now 1.0-centred slope multipliers.
        # scan_profile.contrast  = base scan characteristic  (frontier_soft=0.92, noritsu=1.0, darkroom=1.12)
        # stock.scanner.contrast = stock-specific modifier   (Portra=1.0, Kodachrome=1.05, Tri-X=1.08)
        # Product: 1.0 × 1.0 = 1.0 (neutral), 0.92 × 1.05 = 0.97 (nearly flat), etc.
        stock_scanner_contrast = float(stock_profile.scanner.get("contrast", 1.0))
        scan_contrast = float(np.clip(float(scan_profile.contrast) * stock_scanner_contrast, 0.5, 1.8))
        scan_warmth = float(scan_profile.warmth) + float(stock_profile.scanner.get("warmth", 0.0)) * 0.5

        scanner_finish = {
            "scan_contrast": scan_contrast,
            "scan_warmth": scan_warmth,
            "black_point": float(scan_profile.black_point),
            "white_point": float(scan_profile.white_point),
            "color_separation": float(scan_profile.color_separation)
        }
        
        # Print stock finish — optional second-stage theatrical emulsion
        print_finish = None
        print_stock_profile = controls.get("print_stock")
        if print_stock_profile is not None:
            pt = print_stock_profile.tone
            pc = print_stock_profile.color
            pg = print_stock_profile.grain
            print_strength = float(controls.get("print_strength", 1.0))
            print_finish = {
                "strength":            print_strength,
                "print_c":             float(controls.get("print_c", 0.0)),
                "print_m":             float(controls.get("print_m", 0.0)),
                "print_y":             float(controls.get("print_y", 0.0)),
                "print_contrast":      float(controls.get("print_contrast", 0.0)),
                "print_black_point":   float(controls.get("print_black_point", 0.0)),
                "shadow_lift":         float(pt.get("shadow_lift",         0.02)),
                "contrast_boost":      float(pt.get("contrast_boost",      1.10)),
                "highlight_rolloff":   float(pt.get("highlight_rolloff",    0.78)),
                "highlight_rolloff_rate": float(pt.get("highlight_rolloff_rate", 2.0)),
                "toe_depth":           float(pt.get("toe_depth",           0.85)),
                "shadow_bias_lab":     pc.get("shadow_bias_lab",    [0.0, 0.0, 0.0]),
                "midtone_bias_lab":    pc.get("midtone_bias_lab",   [0.0, 0.0, 0.0]),
                "highlight_bias_lab":  pc.get("highlight_bias_lab", [0.0, 0.0, 0.0]),
                "blue_suppression":    float(pc.get("blue_suppression",   0.0)),
                "red_boost":           float(pc.get("red_boost",          0.0)),
                "green_shift":         float(pc.get("green_shift",        0.0)),
                "saturation_scale":    float(pc.get("saturation_scale",   1.0)),
                "grain_strength":      float(pg.get("strength",           0.0)),
                "grain_size":          float(pg.get("size",               0.3)),
            }

        # Package and return Render Plan
        render_plan = {
            "stock_type":             stock_profile.stock_type,
            "input_diagnosis":        input_diagnosis,
            "pre_film_normalization": pre_film_normalization,
            "film_response":          film_response,
            "material_effects":       material_effects,
            "scanner_finish":         scanner_finish,
            "print_finish":           print_finish,
            "warnings":               warnings
        }
        
        return render_plan
