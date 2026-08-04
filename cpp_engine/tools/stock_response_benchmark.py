"""Measure native stock separation without grain, halation, bloom, or print finish.

This is a calibration tool, not a perceptual-quality oracle. It makes profile
differences auditable before a stock is promoted, merged, or retired.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import cv2
import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw_filename", help="RAW filename under raw_files/")
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="Repository root. Defaults to the current DFEE checkout.",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=None,
        help="Native Release directory containing dfee_native.pyd.",
    )
    parser.add_argument(
        "--stocks",
        default="",
        help="Comma-separated stock IDs. Defaults to every native-listed stock.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("cpp_engine/out/benchmarks/stock_response.json"),
        help="JSON artifact path, relative to project root unless absolute.",
    )
    parser.add_argument(
        "--near-duplicate-threshold",
        type=float,
        default=0.01,
        help="Mean absolute RGB difference in normalized 0..1 display space.",
    )
    return parser.parse_args()


def resolve_build_dir(project_root: Path, requested: Path | None) -> Path:
    if requested is not None:
        return requested.resolve()
    candidate = project_root / "cpp_engine" / "out" / "build" / "windows-msvc-vcpkg" / "Release"
    if (candidate / "dfee_native.pyd").exists():
        return candidate
    raise FileNotFoundError("Could not find cpp_engine/out/build/windows-msvc-vcpkg/Release/dfee_native.pyd")


def configure_native_import(project_root: Path, build_dir: Path) -> None:
    for path in (project_root, build_dir):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    if os.name == "nt" and hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(build_dir))
        vcpkg_bin = build_dir.parent / "vcpkg_installed" / "x64-windows" / "bin"
        if vcpkg_bin.exists():
            os.add_dll_directory(str(vcpkg_bin))


def luminance_metrics(image_bgr: np.ndarray) -> dict[str, float]:
    rgb = image_bgr[:, :, ::-1].astype(np.float32) / 255.0
    luminance = 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]
    return {
        "luma_p05": float(np.quantile(luminance, 0.05)),
        "luma_p25": float(np.quantile(luminance, 0.25)),
        "luma_p50": float(np.quantile(luminance, 0.50)),
        "luma_p75": float(np.quantile(luminance, 0.75)),
        "luma_p95": float(np.quantile(luminance, 0.95)),
        "luma_stddev": float(np.std(luminance)),
        "contrast_p95_p05": float(np.quantile(luminance, 0.95) - np.quantile(luminance, 0.05)),
        "mean_red": float(np.mean(rgb[:, :, 0])),
        "mean_green": float(np.mean(rgb[:, :, 1])),
        "mean_blue": float(np.mean(rgb[:, :, 2])),
    }


def main() -> int:
    args = parse_args()
    project_root = args.project_root.resolve()
    build_dir = resolve_build_dir(project_root, args.build_dir)
    configure_native_import(project_root, build_dir)

    import dfee_native_bridge as native

    raw_path = project_root / "raw_files" / args.raw_filename
    if not raw_path.is_file():
        raise FileNotFoundError(f"RAW fixture does not exist: {raw_path}")

    session = native.create_session(project_root)
    session.select_file(args.raw_filename)
    available = [stock.stock_id for stock in session.list_profiles().stocks]
    requested = [stock.strip() for stock in args.stocks.split(",") if stock.strip()]
    stocks = requested or available
    missing = sorted(set(stocks) - set(available))
    if missing:
        raise ValueError(f"Unknown stock IDs: {', '.join(missing)}")

    renders: dict[str, np.ndarray] = {}
    results: list[dict[str, Any]] = []
    for stock in stocks:
        request = native.NativePreviewRenderRequest(
            filename=args.raw_filename,
            stock=stock,
            effect_pipeline_version="filmic_v3",
            exposure_placement="as_shot",
            adaptive=False,
            grain="Off",
            halation="Off",
            bloom=0.0,
            print_stock="none",
        )
        preview = session.render_preview(request)
        image = cv2.imdecode(np.frombuffer(preview.jpeg_bytes, dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise RuntimeError(f"Could not decode native JPEG preview for {stock}")
        renders[stock] = image
        results.append(
            {
                "stock": stock,
                "request": asdict(request),
                "metrics": luminance_metrics(image),
                "render_preview_ms": next(
                    (float(item.milliseconds) for item in preview.engine.timings if item.stage == "render_preview_total"),
                    None,
                ),
            }
        )

    pairs: list[dict[str, Any]] = []
    for left_index, left_stock in enumerate(stocks):
        for right_stock in stocks[left_index + 1 :]:
            mae = float(np.mean(np.abs(renders[left_stock].astype(np.float32) - renders[right_stock].astype(np.float32))) / 255.0)
            if mae <= args.near_duplicate_threshold:
                pairs.append({"left": left_stock, "right": right_stock, "mean_absolute_rgb_difference": mae})

    output_path = args.output if args.output.is_absolute() else project_root / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "raw_filename": args.raw_filename,
        "method": {
            "pipeline": "filmic_v3",
            "scene_placement": "as_shot",
            "adaptive": False,
            "grain": "Off",
            "halation": "Off",
            "bloom": 0.0,
            "print_stock": "none",
            "purpose": "isolate static stock image formation from material finish and automatic scene placement",
        },
        "near_duplicate_threshold": args.near_duplicate_threshold,
        "stocks": results,
        "near_duplicate_pairs": pairs,
    }
    output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output_path}")
    print(f"Measured {len(results)} stocks; {len(pairs)} pairs are at or below {args.near_duplicate_threshold:.4f} MAE.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
