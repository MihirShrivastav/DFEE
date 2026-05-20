import os
import argparse
from dfee import DFEEEngine, BatchProcessor

def main():
    parser = argparse.ArgumentParser(
        description="Deterministic Film Emulation Engine (DFEE) Command-Line Interface"
    )
    
    parser.add_argument(
        "--input", "-i", required=True,
        help="Path to input .ARW raw file or a directory containing .ARW files for batch processing."
    )
    parser.add_argument(
        "--output", "-o", required=True,
        help="Path to output rendered .tif file or output folder for batch processing."
    )
    parser.add_argument(
        "--stock", "-s", default="kodachrome_64",
        help="Film stock profile name (e.g., kodachrome_64, portra_400, superia_400, tri_x_400) or path to custom stock YAML file."
    )
    parser.add_argument(
        "--scanner", "-c", default="frontier_soft",
        help="Scanner profile name (e.g., frontier_soft, noritsu_smooth, darkroom_print) or path to custom scanner YAML file."
    )
    parser.add_argument(
        "--adaptation", "-a", type=float, default=1.0,
        help="Adaptation strength (0.0 to 1.0) controlling solver compensation."
    )
    parser.add_argument(
        "--draft", "-d", action="store_true",
        help="Enable draft mode (fast downsampled decoding for testing)."
    )
    parser.add_argument(
        "--exposure-intent", "-e", choices=["Preserve", "Auto", "Lift", "Darken"], default="Preserve",
        help="Exposure intent guiding tonal placement."
    )
    parser.add_argument(
        "--cast-handling", choices=["Auto", "Preserve warmth", "Neutralize", "Strong neutralize"], default="Auto",
        help="Pre-film camera color cast correction policy."
    )
    parser.add_argument(
        "--grain", choices=["Auto", "Off", "Low", "Medium", "High"], default="Auto",
        help="Grain strength selector."
    )
    parser.add_argument(
        "--halation", choices=["Auto", "Off", "Low", "Medium", "High"], default="Auto",
        help="Specular halation strength selector."
    )
    parser.add_argument(
        "--consistency", default="",
        help="Comma-separated consistency locks for batch mode (e.g., 'exposure,color_cast')."
    )
    
    args = parser.parse_args()
    
    # Resolve Stock Profile Path
    base_dir = os.path.dirname(os.path.abspath(__file__))
    
    if os.path.exists(args.stock):
        stock_path = args.stock
    else:
        stock_path = os.path.join(base_dir, "profiles", "stocks", f"{args.stock}.yaml")
        if not os.path.exists(stock_path):
            # Try stock folder fallback
            stock_path_alt = os.path.join(base_dir, "profiles", "stocks", args.stock)
            if os.path.exists(stock_path_alt):
                stock_path = stock_path_alt
            else:
                parser.error(f"Stock profile '{args.stock}' not found locally or in default profile path.")
                
    # Resolve Scanner Profile Path
    if os.path.exists(args.scanner):
        scanner_path = args.scanner
    else:
        scanner_path = os.path.join(base_dir, "profiles", "scanners", f"{args.scanner}.yaml")
        if not os.path.exists(scanner_path):
            scanner_path_alt = os.path.join(base_dir, "profiles", "scanners", args.scanner)
            if os.path.exists(scanner_path_alt):
                scanner_path = scanner_path_alt
            else:
                parser.error(f"Scanner profile '{args.scanner}' not found locally or in default profile path.")
                
    # Package overrides
    overrides = {
        "exposure_intent": args.exposure_intent,
        "color_cast_handling": args.cast_handling,
        "grain_amount": args.grain,
        "halation_amount": args.halation
    }
    
    # Check if input path is directory or file
    is_dir = os.path.isdir(args.input)
    
    if is_dir:
        print(f"Batch processing directory: {args.input}")
        
        # Parse consistency locks
        locks = []
        if args.consistency:
            locks = [l.strip() for l in args.consistency.split(",") if l.strip()]
            
        processor = BatchProcessor(
            stock_path, scanner_path, 
            adaptation_strength=args.adaptation, 
            output_bit_depth=16
        )
        
        processor.process_folder(
            args.input, args.output, 
            draft_mode=args.draft, 
            consistency_lock=locks
        )
    else:
        print(f"Processing single file: {args.input}")
        
        engine = DFEEEngine(
            stock_path, scanner_path,
            adaptation_strength=args.adaptation,
            output_bit_depth=16
        )
        
        # Sidecar report path
        output_dir = os.path.dirname(args.output)
        if not output_dir:
            output_dir = "."
        os.makedirs(output_dir, exist_ok=True)
        
        basename = os.path.splitext(os.path.basename(args.output))[0]
        report_path = os.path.join(output_dir, f"{basename}_report.json")
        
        result = engine.render(
            args.input, args.output, 
            report_path=report_path, 
            draft_mode=args.draft, 
            user_overrides=overrides
        )
        print(f"Successfully rendered: {result.output_path}")
        print(f"Warnings reported: {result.render_plan['warnings']}")

if __name__ == "__main__":
    main()
