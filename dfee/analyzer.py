import numpy as np
import cv2
from .color_spaces import rgb_to_oklab, oklab_to_oklch

class ImageStateAnalyzer:
    def __init__(self):
        pass

    def analyze(self, rgb_linear, Y, clipping_masks, clipping_ratios):
        """
        Runs complete analysis on linear RGB array and luminance canvas Y.
        Returns a feature dictionary and spatial masks.
        """
        # 1. Tonal and Luminance Analysis
        tonal_dist = self._analyze_tonal(Y, clipping_ratios)
        
        # 2. Generate Continuous Zone Masks
        zone_masks = self._generate_zone_masks(Y, tonal_dist['midtone_anchor'])
        
        # 3. Hue, Saturation, and Color Volume Analysis
        color_features = self._analyze_color(rgb_linear, zone_masks)
        
        # 4. Spatial and Optical Features
        spatial_features, spatial_masks = self._analyze_spatial(Y)
        
        # Assemble Top-Level Feature Dictionary
        feature_dict = {
            "tonal_distribution": tonal_dist,
            "hue_saturation_state": color_features,
            "spatial_frequency": spatial_features,
            "channel_behavior": {
                "clipping_ratios": clipping_ratios,
                "clipping_masks": clipping_masks  # Included for solver/renderer usage
            }
        }
        
        # Combine masks into a separate dictionary for the renderer
        masks = {
            "luminance_zone_masks": zone_masks,
            **spatial_masks
        }
        
        return feature_dict, masks

    def _analyze_tonal(self, Y, clipping_ratios):
        # Calculate percentiles
        p01, p05, p25, p50, p75, p95, p99, p995 = np.percentile(
            Y, [1, 5, 25, 50, 75, 95, 99, 99.5]
        )
        
        # Epsilon-protected dynamic range in stops
        p01_safe = max(p01, 1e-6)
        dynamic_range_stops = float(np.log2(max(p99, p01_safe) / p01_safe))
        
        # Highlight headroom and shadow depth
        highlight_headroom = float(1.0 - p95)
        shadow_depth = float(p05)
        midtone_anchor = float(max(p50, 0.01))
        
        # Global contrast index
        contrast_index = float(p75 - p25)
        
        # Classify tonal skew
        tonal_skew = "normal"
        if midtone_anchor < 0.05:
            tonal_skew = "low_key"
        elif midtone_anchor > 0.45:
            tonal_skew = "high_key"
        elif dynamic_range_stops > 11.5:
            tonal_skew = "harsh_contrast"
        elif dynamic_range_stops < 5.0:
            tonal_skew = "flat"
            
        # Check if highlights are stressed (highly clipped)
        max_clip = max(clipping_ratios.values())
        if max_clip > 0.01:
            tonal_skew = "highlight_stressed"

        return {
            "luma_p01": float(p01),
            "luma_p05": float(p05),
            "luma_p25": float(p25),
            "luma_p50": float(p50),
            "luma_p75": float(p75),
            "luma_p95": float(p95),
            "luma_p99": float(p99),
            "luma_p995": float(p995),
            "dynamic_range_stops": dynamic_range_stops,
            "midtone_anchor": midtone_anchor,
            "highlight_headroom": highlight_headroom,
            "shadow_depth": shadow_depth,
            "tonal_skew": tonal_skew,
            "contrast_index": contrast_index,
            "black_point_actual": float(p01),
            "white_point_actual": float(p995)
        }

    def _generate_zone_masks(self, Y, midtone_anchor):
        """
        Generates 7 continuous exposure zone masks using partition of unity.
        Zones: Z0 (Deep Shadow) to Z6 (Specular)
        """
        # Calculate EV relative to the midtone anchor
        # Avoid log of zero
        Y_safe = np.maximum(Y, 1e-10)
        EV = np.log2(Y_safe / midtone_anchor)
        
        # Zone centers in EV space
        # Z0: -4.5, Z1: -3.0, Z2: -1.5, Z3: 0.0, Z4: 1.5, Z5: 3.0, Z6: 4.5
        centers = [-4.5, -3.0, -1.5, 0.0, 1.5, 3.0, 4.5]
        sigma = 0.85
        
        weights = []
        for i, c in enumerate(centers):
            if i == 0:
                # Deep shadow (Z0) extends infinitely to the negative side
                w = np.where(EV <= c, 1.0, np.exp(-((EV - c) ** 2) / (2 * (sigma ** 2))))
            elif i == 6:
                # Specular (Z6) extends infinitely to the positive side
                w = np.where(EV >= c, 1.0, np.exp(-((EV - c) ** 2) / (2 * (sigma ** 2))))
            else:
                # Mid zones (Z1 to Z5) are symmetric Gaussians
                w = np.exp(-((EV - c) ** 2) / (2 * (sigma ** 2)))
            weights.append(w)
            
        # Normalization to partition of unity (sum of weights = 1.0)
        weights = np.stack(weights, axis=0)
        weight_sum = np.sum(weights, axis=0)
        weight_sum = np.maximum(weight_sum, 1e-12) # avoid zero division
        
        zone_masks = {}
        for i in range(7):
            zone_masks[f'Z{i}'] = (weights[i] / weight_sum).astype(np.float32)
            
        return zone_masks

    def _analyze_color(self, rgb_linear, zone_masks):
        # Convert image to OKLab and OKLCH
        oklab = rgb_to_oklab(rgb_linear)
        oklch = oklab_to_oklch(oklab)
        
        chroma = oklch[:, :, 1]
        hue = oklch[:, :, 2]
        
        # Global mean chroma and percentiles
        mean_chroma = float(np.mean(chroma))
        sat_p95 = float(np.percentile(chroma, 95))
        
        # Tonal zone-weighted mean saturation
        # Z1: Shadow, Z3: Midtone, Z5: Highlight
        sat_shadow_mean = float(np.sum(chroma * zone_masks['Z1']) / max(np.sum(zone_masks['Z1']), 1e-5))
        sat_mid_mean = float(np.sum(chroma * zone_masks['Z3']) / max(np.sum(zone_masks['Z3']), 1e-5))
        sat_highlight_mean = float(np.sum(chroma * zone_masks['Z5']) / max(np.sum(zone_masks['Z5']), 1e-5))
        
        # Neon risk: ratio of pixels with chroma > 0.18 (very highly saturated in OKLCH)
        neon_risk = float(np.sum(chroma > 0.18) / (chroma.shape[0] * chroma.shape[1]))
        
        # Hue analysis: bucket hue angles (0 to 2*pi) into 12 bins
        # 12 bins = 30 degrees each
        hue_deg = np.degrees(hue)
        hist, bin_edges = np.histogram(hue_deg, bins=12, range=(0, 360))
        
        # Normalize histogram to get distribution
        hue_dist = hist / np.sum(hist)
        
        # Identify dominant hue families
        hue_names = ["Red", "Orange", "Yellow", "Yellow-Green", "Green", "Green-Cyan", 
                     "Cyan", "Cyan-Blue", "Blue", "Blue-Violet", "Violet", "Magenta"]
        
        sorted_indices = np.argsort(hue_dist)[::-1]
        dominant_hue_bins = [hue_names[idx] for idx in sorted_indices[:3]]
        
        # Compute entropy of hue distribution as a measure of palette complexity
        # Add epsilon to avoid log(0)
        hue_entropy = float(-np.sum(hue_dist * np.log2(hue_dist + 1e-6)))
        
        # Red-Orange occupancy (bin indices 0, 1, 11) and Green-Yellow occupancy (bin indices 2, 3, 4)
        red_orange_idx = [0, 1, 11]
        green_yellow_idx = [2, 3, 4]
        red_orange_density = float(np.sum(hue_dist[red_orange_idx]))
        green_yellow_density = float(np.sum(hue_dist[green_yellow_idx]))
        
        # Cyan-Blue ratio vs Warm ratio
        warm_bins = [0, 1, 2, 10, 11]  # Red, Orange, Yellow, Violet, Magenta
        cool_bins = [5, 6, 7, 8]       # Green-Cyan, Cyan, Cyan-Blue, Blue
        
        warm_sum = np.sum(hue_dist[warm_bins])
        cool_sum = np.sum(hue_dist[cool_bins])
        warm_cool_ratio = float(warm_sum / max(cool_sum, 1e-5))
        
        cyan_blue_ratio = float(np.sum(hue_dist[[6, 7, 8]]))

        return {
            "sat_shadow_mean": sat_shadow_mean,
            "sat_mid_mean": sat_mid_mean,
            "sat_highlight_mean": sat_highlight_mean,
            "sat_p95": sat_p95,
            "neon_risk": neon_risk,
            "dominant_hue_bins": dominant_hue_bins,
            "hue_entropy": hue_entropy,
            "red_orange_density": red_orange_density,
            "green_yellow_density": green_yellow_density,
            "warm_cool_ratio": warm_cool_ratio,
            "cyan_blue_ratio": cyan_blue_ratio,
            "mean_chroma": mean_chroma
        }

    def _analyze_spatial(self, Y):
        # Downsample Y for heavy spatial calculations to keep processing fast
        h, w = Y.shape
        scale = 1.0
        # If very large, compute variance on a smaller size for speed
        if h > 1000 or w > 1000:
            scale = 1000.0 / max(h, w)
            Y_small = cv2.resize(Y, (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
        else:
            Y_small = Y.copy()
            
        # 1. Texture density via local variance
        # Compute mean of image and square of image
        blur_k = 15
        if blur_k % 2 == 0: blur_k += 1
        
        mean_Y = cv2.boxFilter(Y_small, -1, (blur_k, blur_k))
        mean_Y_sq = cv2.boxFilter(Y_small**2, -1, (blur_k, blur_k))
        local_var = np.maximum(mean_Y_sq - mean_Y**2, 0.0)
        
        # Resize variance back to full resolution
        if scale != 1.0:
            local_var_full = cv2.resize(local_var, (w, h), interpolation=cv2.INTER_LINEAR)
        else:
            local_var_full = local_var
            
        texture_density = float(np.mean(local_var_full))
        
        # Smooth region mask: grain is visible where variance is low
        # Normalize local variance to range [0, 1] and invert
        max_var = np.percentile(local_var_full, 95)
        max_var = max(max_var, 1e-5)
        norm_var = np.clip(local_var_full / max_var, 0.0, 1.0)
        grain_receptivity_mask = (1.0 - norm_var).astype(np.float32)
        smooth_area_ratio = float(np.sum(norm_var < 0.1) / (h * w))
        
        # 2. Edge density via Sobel filter
        sobel_x = cv2.Sobel(Y_small, cv2.CV_32F, 1, 0, ksize=3)
        sobel_y = cv2.Sobel(Y_small, cv2.CV_32F, 0, 1, ksize=3)
        grad_mag = np.sqrt(sobel_x**2 + sobel_y**2)
        edge_density = float(np.mean(grad_mag))
        
        # 3. Specular and Diffuse Highlight Analysis
        # Create a blurred version of Y to measure local contrast
        blur_size = 25
        if blur_size % 2 == 0: blur_size += 1
        Y_blur = cv2.GaussianBlur(Y, (blur_size, blur_size), 0)
        
        # Specular candidate: Y is bright and locally high-contrast (much brighter than surroundings)
        specular_mask = (Y > 0.8) & ((Y - Y_blur) > 0.1)
        specular_point_ratio = float(np.sum(specular_mask) / (h * w))
        
        # Large diffuse highlight: Y is bright but flat (low local contrast)
        large_highlight_mask = (Y > 0.7) & ((Y - Y_blur) <= 0.1)
        large_highlight_area_ratio = float(np.sum(large_highlight_mask) / (h * w))
        
        # Halation source mask: high intensity, high contrast specular highlights
        halation_source_mask = specular_mask.astype(np.float32)
        
        # Halation receiver mask: areas surrounding sources that are darker (contrast boundary)
        # We can find this by dilating the specular sources and masking out the bright pixels
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (15, 15))
        dilated_sources = cv2.dilate(halation_source_mask, kernel)
        halation_receiver_mask = (dilated_sources * (1.0 - Y)).astype(np.float32)
        
        # Compute a simple digital sharpness score
        # (high-frequency energy divided by mid-frequency energy)
        digital_sharpness_score = float(edge_density / max(texture_density * 100.0, 1e-4))
        
        spatial_features = {
            "texture_density": texture_density,
            "smooth_area_ratio": smooth_area_ratio,
            "edge_density": edge_density,
            "digital_sharpness_score": digital_sharpness_score,
            "specular_point_ratio": specular_point_ratio,
            "large_highlight_area_ratio": large_highlight_area_ratio
        }
        
        spatial_masks = {
            "grain_receptivity_mask": grain_receptivity_mask,
            "halation_source_mask": halation_source_mask.astype(np.float32),
            "halation_receiver_mask": halation_receiver_mask
        }
        
        return spatial_features, spatial_masks
