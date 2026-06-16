import os
import glob
import json
import csv
from .engine import DFEEEngine

class BatchProcessor:
    def __init__(self, stock_profile_path, print_profile_path=None,
                 adaptation_strength=1.0, output_bit_depth=16):
        self.engine = DFEEEngine(
            stock_profile_path, print_profile_path,
            adaptation_strength=adaptation_strength, 
            output_bit_depth=output_bit_depth
        )

    def process_folder(self, input_dir, output_dir, draft_mode=False, consistency_lock=None):
        """
        Processes all RAW files in input_dir and saves outputs to output_dir.
        
        Args:
            input_dir (str): Directory containing .ARW files
            output_dir (str): Directory to save TIFF outputs and reports
            draft_mode (bool): Run in fast low-resolution draft mode
            consistency_lock (dict): Optional locks for render parameters
        """
        if not os.path.exists(output_dir):
            os.makedirs(output_dir)
            
        # Search for .ARW files (case insensitive)
        arw_pattern = os.path.join(input_dir, "*.[aA][rR][wW]")
        raw_files = glob.glob(arw_pattern)
        
        if not raw_files:
            print(f"No RAW (.ARW) files found in {input_dir}")
            return []
            
        print(f"Found {len(raw_files)} RAW files to process.")
        results_summary = []
        
        # Track locked parameter values for consistency
        locked_overrides = {}
        if consistency_lock:
            # consistency_lock can contain keys like 'exposure_compensation_stops'
            pass

        for i, filepath in enumerate(raw_files):
            filename = os.path.basename(filepath)
            basename = os.path.splitext(filename)[0]
            
            output_path = os.path.join(output_dir, f"{basename}_dfee.tif")
            report_path = os.path.join(output_dir, f"{basename}_dfee_report.json")
            
            print(f"[{i+1}/{len(raw_files)}] Processing: {filename}...")
            
            try:
                # Apply consistency lock overrides if they have been computed from the first image
                current_overrides = {}
                if consistency_lock and i > 0:
                    current_overrides.update(locked_overrides)
                    
                result = self.engine.render(
                    filepath, output_path, report_path, 
                    draft_mode=draft_mode, user_overrides=current_overrides
                )
                
                # If this is the first image and consistency lock is enabled,
                # capture the parameters we want to lock for subsequent images
                if consistency_lock and i == 0:
                    plan = result.render_plan
                    if "exposure" in consistency_lock:
                        locked_overrides["exposure_compensation_stops"] = plan["pre_film_normalization"]["exposure_compensation_stops"]
                    if "color_cast" in consistency_lock:
                        locked_overrides["shadow_blue_normalization"] = plan["pre_film_normalization"]["shadow_blue_normalization"]
                        locked_overrides["green_magenta_stabilization"] = plan["pre_film_normalization"]["green_magenta_stabilization"]
                
                # Gather stats for summary
                diag = result.render_plan["input_diagnosis"]
                summary_entry = {
                    "input_filename": filename,
                    "profile": self.engine.stock_profile.stock_id,
                    "tonal_state": diag["tonal_state"],
                    "dynamic_range_stops": round(diag["dynamic_range_stops"], 2),
                    "warnings": ",".join(result.render_plan["warnings"]),
                    "output_path": output_path
                }
                results_summary.append(summary_entry)
                
            except Exception as e:
                print(f"Error processing {filename}: {e}")
                results_summary.append({
                    "input_filename": filename,
                    "profile": self.engine.stock_profile.stock_id,
                    "tonal_state": "ERROR",
                    "dynamic_range_stops": 0.0,
                    "warnings": f"FAILED: {str(e)}",
                    "output_path": "N/A"
                })
                
        # Export CSV summary
        summary_csv_path = os.path.join(output_dir, "batch_processing_summary.csv")
        self._write_csv_summary(results_summary, summary_csv_path)
        
        return results_summary

    def _write_csv_summary(self, summary_list, filepath):
        if not summary_list:
            return
            
        headers = list(summary_list[0].keys())
        try:
            with open(filepath, 'w', newline='', encoding='utf-8') as f:
                writer = csv.DictWriter(f, fieldnames=headers)
                writer.writeheader()
                writer.writerows(summary_list)
            print(f"Batch summary CSV written to: {filepath}")
        except Exception as e:
            print(f"Failed to write CSV summary: {e}")
