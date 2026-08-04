"""Measure RAW-to-rendered-reference agreement through the native preview pipeline.

This is a calibration harness, not an automatic look matcher. The rendered file
must be the same capture developed without creative Lightroom edits; otherwise
the numbers identify an input mismatch rather than a RAW-development defect.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import cv2
import numpy as np

DEFAULT_BASELINE_POWER = 1.28


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw_path", type=Path, help="Absolute or project-relative RAW path.")
    parser.add_argument("rendered_path", type=Path, help="Matching TIFF/JPEG/PNG developed reference.")
    parser.add_argument("--stock", default="none", help="Native stock ID, or 'none' for baseline-only.")
    parser.add_argument(
        "--baseline-power",
        type=float,
        default=None,
        help="Offline calibration override for DFEE_RAW_BASELINE_POWER (0.90 through 1.40).",
    )
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--build-dir", type=Path, default=None)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("cpp_engine/out/benchmarks/raw_rendered_pair.json"),
        help="JSON artifact path, relative to the project root unless absolute.",
    )
    return parser.parse_args()


def resolve_path(project_root: Path, path: Path) -> Path:
    resolved = path if path.is_absolute() else project_root / path
    resolved = resolved.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"Fixture does not exist: {resolved}")
    return resolved


def resolve_build_dir(project_root: Path, requested: Path | None) -> Path:
    candidate = requested or project_root / "cpp_engine/out/build/windows-msvc-vcpkg/Release"
    candidate = candidate.resolve()
    if not (candidate / "dfee_native.pyd").is_file():
        raise FileNotFoundError(f"Native module not found in {candidate}")
    return candidate


def configure_native_import(project_root: Path, build_dir: Path) -> None:
    for path in (project_root, build_dir):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    if os.name == "nt" and hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(build_dir))
        vcpkg_bin = build_dir.parent / "vcpkg_installed" / "x64-windows" / "bin"
        if vcpkg_bin.exists():
            os.add_dll_directory(str(vcpkg_bin))


def image_metrics(image_bgr: np.ndarray) -> dict[str, float]:
    rgb = image_bgr[:, :, ::-1].astype(np.float32) / 255.0
    luma = 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]
    hsv = cv2.cvtColor(rgb, cv2.COLOR_RGB2HSV)
    result = {f"luma_p{percentile:02d}": float(np.quantile(luma, percentile / 100.0)) for percentile in (1, 5, 25, 50, 75, 95, 99)}
    result.update(
        {
            "luma_stddev": float(np.std(luma)),
            "contrast_p95_p05": float(np.quantile(luma, 0.95) - np.quantile(luma, 0.05)),
            "mean_saturation": float(np.mean(hsv[:, :, 1])),
            "saturation_p95": float(np.quantile(hsv[:, :, 1], 0.95)),
        }
    )
    return result


def render(session: Any, native: Any, filename: Path, stock: str) -> tuple[np.ndarray, dict[str, Any]]:
    session.select_file(str(filename))
    preview = session.render_preview(
        native.NativePreviewRenderRequest(
            filename=str(filename),
            stock=stock,
            effect_pipeline_version="filmic_v3",
            exposure_placement="auto_balanced",
            adaptive=False,
            grain="Off",
            halation="Off",
            bloom=0.0,
            print_stock="none",
        )
    )
    image = cv2.imdecode(np.frombuffer(preview.jpeg_bytes, dtype=np.uint8), cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"Could not decode native preview for {filename}")
    timing = next((float(item.milliseconds) for item in preview.engine.timings if item.stage == "render_preview_total"), None)
    return image, {"metrics": image_metrics(image), "render_preview_ms": timing}


def main() -> int:
    args = parse_args()
    project_root = args.project_root.resolve()
    raw_path = resolve_path(project_root, args.raw_path)
    rendered_path = resolve_path(project_root, args.rendered_path)
    build_dir = resolve_build_dir(project_root, args.build_dir)
    if args.baseline_power is not None:
        if not 0.90 <= args.baseline_power <= 1.40:
            raise ValueError("--baseline-power must be between 0.90 and 1.40")
        os.environ["DFEE_RAW_BASELINE_POWER"] = str(args.baseline_power)
    configure_native_import(project_root, build_dir)
    import dfee_native_bridge as native

    session = native.create_session(project_root)
    available = {profile.stock_id for profile in session.list_profiles().stocks}
    if args.stock != "none" and args.stock not in available:
        raise ValueError(f"Unknown stock: {args.stock}")

    raw_image, raw_result = render(session, native, raw_path, args.stock)
    rendered_image, rendered_result = render(session, native, rendered_path, args.stock)
    if raw_image.shape[:2] != rendered_image.shape[:2]:
        rendered_image = cv2.resize(rendered_image, (raw_image.shape[1], raw_image.shape[0]), interpolation=cv2.INTER_AREA)
        rendered_result["resized_to_raw_preview"] = True

    mae = float(np.mean(np.abs(raw_image.astype(np.float32) - rendered_image.astype(np.float32))) / 255.0)
    payload = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "raw_path": str(raw_path),
        "rendered_path": str(rendered_path),
        "method": {
            "pipeline": "filmic_v3",
            "scene_placement": "auto_balanced",
            "adaptive": False,
            "grain": "Off",
            "halation": "Off",
            "bloom": 0.0,
            "print_stock": "none",
            "stock": args.stock,
            "baseline_midtone_power": args.baseline_power if args.baseline_power is not None else DEFAULT_BASELINE_POWER,
        },
        "raw": raw_result,
        "rendered": rendered_result,
        "mean_absolute_rgb_difference": mae,
        "note": "Compare baseline-only (stock=none) before attributing a mismatch to a film profile.",
    }
    output_path = args.output if args.output.is_absolute() else project_root / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output_path}")
    print(f"RAW/reference preview MAE: {mae:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
