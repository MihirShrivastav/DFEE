import json
import os

class RenderReporter:
    def __init__(self, engine_version="1.0.0"):
        self.engine_version = engine_version

    def write_report(self, input_file, output_file, stock_profile, scan_profile, 
                     feature_dict, render_plan, report_path):
        """
        Generates and saves the sidecar JSON report containing diagnostic features
        and final solver parameters.
        """
        # Clean features for serializability (remove masks and numpy types)
        clean_tonal = {k: float(v) if isinstance(v, (np.float32, np.float64)) else v 
                       for k, v in feature_dict["tonal_distribution"].items()}
        
        clean_color = {k: float(v) if isinstance(v, (np.float32, np.float64)) else v 
                       for k, v in feature_dict["hue_saturation_state"].items()}
        # Handle lists/arrays
        if "dominant_hue_bins" in clean_color:
            clean_color["dominant_hue_bins"] = list(clean_color["dominant_hue_bins"])
            
        clean_spatial = {k: float(v) if isinstance(v, (np.float32, np.float64)) else v 
                         for k, v in feature_dict["spatial_frequency"].items()}
                         
        clean_channel = {
            "clipping_ratios": {k: float(v) for k, v in feature_dict["channel_behavior"]["clipping_ratios"].items()}
        }
        
        # Build clean features
        clean_features = {
            "tonal_distribution": clean_tonal,
            "hue_saturation_state": clean_color,
            "spatial_frequency": clean_spatial,
            "channel_behavior": clean_channel
        }
        
        if "camera_input_bias" in feature_dict:
            clean_bias = {}
            for k, v in feature_dict["camera_input_bias"].items():
                if isinstance(v, list):
                    clean_bias[k] = [float(x) for x in v]
                elif isinstance(v, (np.float32, np.float64, float)):
                    clean_bias[k] = float(v)
                else:
                    clean_bias[k] = v
            clean_features["camera_input_bias"] = clean_bias
            
        report_data = {
            "engine_version": self.engine_version,
            "input_file": os.path.basename(input_file),
            "output_file": os.path.basename(output_file),
            "stock_profile": stock_profile.stock_id,
            "scan_profile": scan_profile.scanner_id,
            "image_diagnosis": render_plan["input_diagnosis"],
            "feature_summary": clean_features,
            "render_plan": {
                "pre_film_normalization": render_plan["pre_film_normalization"],
                "film_response": render_plan["film_response"],
                "material_effects": render_plan["material_effects"],
                "scanner_finish": render_plan["scanner_finish"]
            },
            "warnings": render_plan["warnings"]
        }
        
        # Save JSON
        try:
            # Custom encoder to handle numpy data types safely
            class NumpyEncoder(json.JSONEncoder):
                def default(self, obj):
                    if isinstance(obj, (np.integer, np.int32, np.int64)):
                        return int(obj)
                    elif isinstance(obj, (np.floating, np.float32, np.float64)):
                        return float(obj)
                    elif isinstance(obj, np.ndarray):
                        return obj.tolist()
                    return super(NumpyEncoder, self).default(obj)
                    
            with open(report_path, 'w', encoding='utf-8') as f:
                json.dump(report_data, f, indent=2, cls=NumpyEncoder)
            print(f"Sidecar report written to: {report_path}")
        except Exception as e:
            print(f"Error writing sidecar report: {e}")

        return report_data

# Import numpy inside class helper scope to handle local imports cleanly
import numpy as np
