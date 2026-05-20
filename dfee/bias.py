import numpy as np
from .color_spaces import rgb_to_oklab, oklab_to_oklch

class CameraBiasEstimator:
    def __init__(self):
        pass

    def estimate_bias(self, rgb_linear, Y, clipping_masks, zone_masks):
        """
        Estimates the neutral axis and color casts in OKLab space.
        """
        # Convert RGB to OKLab and OKLCH
        oklab = rgb_to_oklab(rgb_linear)
        oklch = oklab_to_oklch(oklab)
        
        L = oklab[:, :, 0]
        a = oklab[:, :, 1]
        b = oklab[:, :, 2]
        chroma = oklch[:, :, 1]
        
        # 1. Identify neutral-axis candidates:
        # - non-clipped in all channels
        # - non-shadow, non-highlight: L in [0.15, 0.75]
        # - low chroma: chroma < 0.035
        not_clipped = ~(clipping_masks['R'] | clipping_masks['G'] | clipping_masks['B'])
        neutral_mask = not_clipped & (L >= 0.15) & (L <= 0.75) & (chroma < 0.035)
        
        total_pixels = L.shape[0] * L.shape[1]
        num_neutral_pixels = np.sum(neutral_mask)
        
        # Calculate confidence based on the abundance of neutral pixels
        # 0.5% of pixels is a solid baseline for confidence
        neutral_confidence = float(np.clip(num_neutral_pixels / (total_pixels * 0.005), 0.0, 1.0))
        
        # Compute global cast (average a, b of neutral candidates)
        if num_neutral_pixels > 100:
            global_cast_lab = [
                float(np.mean(L[neutral_mask])),
                float(np.mean(a[neutral_mask])),
                float(np.mean(b[neutral_mask]))
            ]
        else:
            global_cast_lab = [0.5, 0.0, 0.0]
            
        # 2. Compute casts separately for shadows (Z1), midtones (Z3), and highlights (Z5)
        # Using zone weights for a soft average
        def get_weighted_cast(zone_mask):
            weight_sum = np.sum(zone_mask)
            if weight_sum > 1e-4:
                mean_L = np.sum(L * zone_mask) / weight_sum
                mean_a = np.sum(a * zone_mask) / weight_sum
                mean_b = np.sum(b * zone_mask) / weight_sum
                return [float(mean_L), float(mean_a), float(mean_b)]
            return [0.5, 0.0, 0.0]

        shadow_cast_lab = get_weighted_cast(zone_masks['Z1'])
        midtone_cast_lab = get_weighted_cast(zone_masks['Z3'])
        highlight_cast_lab = get_weighted_cast(zone_masks['Z5'])
        
        # 3. Compute bias indices
        # Warm-cool: positive b is warm (yellow), negative b is cool (blue)
        # Green-magenta: positive a is magenta, negative a is green
        warm_cool_bias = midtone_cast_lab[2]
        green_magenta_bias = midtone_cast_lab[1]
        
        # Blue excess index: cool bias in shadows
        blue_excess_index = max(-shadow_cast_lab[2], 0.0)
        
        bias_features = {
            "neutral_confidence": neutral_confidence,
            "global_cast_lab": global_cast_lab,
            "shadow_cast_lab": shadow_cast_lab,
            "midtone_cast_lab": midtone_cast_lab,
            "highlight_cast_lab": highlight_cast_lab,
            "blue_excess_index": blue_excess_index,
            "green_magenta_bias": green_magenta_bias,
            "warm_cool_bias": warm_cool_bias
        }
        
        return bias_features
