from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import time
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the documented DFEE native export benchmark scenario and write a JSON artifact."
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="DFEE repository root. Defaults to the current repo layout.",
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=None,
        help="Native build output directory that contains dfee_native.pyd. Defaults to the vcpkg Release build.",
    )
    parser.add_argument(
        "--raw-filename",
        type=str,
        default="",
        help="RAW filename under raw_files/. Defaults to the first .ARW file found.",
    )
    parser.add_argument("--stock", default="portra_400")
    parser.add_argument("--print-stock", default="none")
    parser.add_argument("--export-format", default="png8", choices=("png8", "png16", "tiff"))
    parser.add_argument("--cold-runs", type=int, default=1)
    parser.add_argument("--warm-runs", type=int, default=2)
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Benchmark JSON output path. Defaults to cpp_engine/out/benchmarks/native_export_benchmark.json.",
    )
    parser.add_argument(
        "--keep-exports",
        action="store_true",
        help="Keep exported image/report artifacts instead of deleting them after each run.",
    )
    parser.add_argument(
        "--label",
        default="default",
        help="Short label stored in the benchmark artifact for this scenario or revision.",
    )
    parser.add_argument(
        "--baseline",
        type=Path,
        default=None,
        help="Optional benchmark JSON artifact to compare against. Comparison deltas are written into the new artifact.",
    )
    return parser.parse_args()


def resolve_build_dir(project_root: Path, build_dir: Path | None) -> Path:
    if build_dir is not None:
        return build_dir.resolve()

    candidates = [
        project_root / "cpp_engine" / "out" / "build" / "windows-msvc-vcpkg" / "Release",
        project_root / "cpp_engine" / "out" / "build" / "windows-msvc" / "Release",
        project_root / "cpp_engine" / "out" / "build" / "windows-msvc-vcpkg" / "Debug",
        project_root / "cpp_engine" / "out" / "build" / "windows-msvc" / "Debug",
    ]
    for candidate in candidates:
        if (candidate / "dfee_native.pyd").exists():
            return candidate.resolve()
    raise FileNotFoundError("Could not locate a native build directory containing dfee_native.pyd.")


def configure_python_paths(project_root: Path, build_dir: Path) -> None:
    repo_root = project_root.resolve()
    if str(repo_root) not in sys.path:
        sys.path.insert(0, str(repo_root))
    if str(build_dir) not in sys.path:
        sys.path.insert(0, str(build_dir))

    if os.name == "nt" and hasattr(os, "add_dll_directory"):
        os.add_dll_directory(str(build_dir))
        vcpkg_bin = build_dir.parent / "vcpkg_installed" / "x64-windows" / "bin"
        vcpkg_debug_bin = build_dir.parent / "vcpkg_installed" / "x64-windows" / "debug" / "bin"
        if vcpkg_bin.exists():
            os.add_dll_directory(str(vcpkg_bin))
        if vcpkg_debug_bin.exists():
            os.add_dll_directory(str(vcpkg_debug_bin))


def resolve_raw_filename(project_root: Path, requested_name: str) -> str:
    raw_dir = project_root / "raw_files"
    if requested_name:
        candidate = raw_dir / requested_name
        if not candidate.exists():
            raise FileNotFoundError(f"Requested RAW file was not found: {candidate}")
        return requested_name

    for entry in sorted(raw_dir.iterdir()):
        if entry.is_file() and entry.suffix.lower() == ".arw":
            return entry.name
    raise FileNotFoundError(f"No .ARW files were found under {raw_dir}")


def stage_timings_to_dict(engine: Any) -> dict[str, float]:
    return {timing.stage: float(timing.milliseconds) for timing in engine.timings}


def select_summary_timings(stage_timings: dict[str, float]) -> dict[str, float]:
    keys = [
        "export_image_total",
        "export_image_render",
        "export_image_render_prefilm_fullres",
        "export_image_render_film_fullres",
        "export_image_render_stage_tone_response",
        "export_image_render_stage_color_response",
        "export_image_write_output",
        "export_image_ensure_full_cache",
    ]
    return {key: stage_timings[key] for key in keys if key in stage_timings}


def build_export_request(dfee_native_bridge: Any, raw_filename: str, args: argparse.Namespace) -> Any:
    return dfee_native_bridge.NativeExportRequest(
        filename=raw_filename,
        stock=args.stock,
        print_stock=args.print_stock,
        export_format=args.export_format,
        grain="Off",
        halation="Off",
        bloom=0.0,
        clarity=0.0,
        texture=0.0,
        dehaze=0.0,
    )


def artifact_output_path(project_root: Path, requested_output: Path | None) -> Path:
    if requested_output is not None:
        return requested_output.resolve()
    return (project_root / "cpp_engine" / "out" / "benchmarks" / "native_export_benchmark.json").resolve()


def summarize_numeric_values(values: list[float]) -> dict[str, float]:
    if not values:
        return {}
    return {
        "count": float(len(values)),
        "min": float(min(values)),
        "max": float(max(values)),
        "mean": float(statistics.fmean(values)),
        "median": float(statistics.median(values)),
    }


def summarize_runs_by_phase(runs: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    grouped: dict[str, list[dict[str, Any]]] = {}
    for run in runs:
        grouped.setdefault(str(run["phase"]), []).append(run)

    phase_summary: dict[str, dict[str, Any]] = {}
    for phase, phase_runs in grouped.items():
        wall_values = [float(run["wall_ms"]) for run in phase_runs]
        metric_names: set[str] = set()
        for run in phase_runs:
            metric_names.update(run["summary_timings_ms"].keys())

        metric_summary: dict[str, dict[str, float]] = {}
        for metric_name in sorted(metric_names):
            values = [
                float(run["summary_timings_ms"][metric_name])
                for run in phase_runs
                if metric_name in run["summary_timings_ms"]
            ]
            if values:
                metric_summary[metric_name] = summarize_numeric_values(values)

        phase_summary[phase] = {
            "run_count": len(phase_runs),
            "wall_ms": summarize_numeric_values(wall_values),
            "metrics_ms": metric_summary,
        }
    return phase_summary


def compute_summary_delta(current: dict[str, float], baseline: dict[str, float]) -> dict[str, float]:
    current_median = current.get("median")
    baseline_median = baseline.get("median")
    if current_median is None or baseline_median is None:
        return {}
    delta_ms = current_median - baseline_median
    delta_pct = 0.0 if baseline_median == 0.0 else (delta_ms / baseline_median) * 100.0
    return {
        "current_median_ms": float(current_median),
        "baseline_median_ms": float(baseline_median),
        "delta_ms": float(delta_ms),
        "delta_percent": float(delta_pct),
    }


def compare_against_baseline(
    current_phase_summary: dict[str, dict[str, Any]],
    baseline_phase_summary: dict[str, dict[str, Any]],
) -> dict[str, dict[str, Any]]:
    comparison: dict[str, dict[str, Any]] = {}
    for phase, current_summary in current_phase_summary.items():
        baseline_summary = baseline_phase_summary.get(phase)
        if baseline_summary is None:
            continue

        phase_comparison: dict[str, Any] = {}
        wall_delta = compute_summary_delta(
            current_summary.get("wall_ms", {}),
            baseline_summary.get("wall_ms", {}),
        )
        if wall_delta:
            phase_comparison["wall_ms"] = wall_delta

        current_metrics = current_summary.get("metrics_ms", {})
        baseline_metrics = baseline_summary.get("metrics_ms", {})
        metric_comparison: dict[str, dict[str, float]] = {}
        for metric_name, metric_summary in current_metrics.items():
            baseline_metric_summary = baseline_metrics.get(metric_name)
            if baseline_metric_summary is None:
                continue
            delta = compute_summary_delta(metric_summary, baseline_metric_summary)
            if delta:
                metric_comparison[metric_name] = delta
        if metric_comparison:
            phase_comparison["metrics_ms"] = metric_comparison

        if phase_comparison:
            comparison[phase] = phase_comparison
    return comparison


def phase_summary_from_payload(payload: dict[str, Any]) -> dict[str, dict[str, Any]]:
    phase_summary = payload.get("phase_summary")
    if isinstance(phase_summary, dict) and phase_summary:
        return phase_summary
    runs = payload.get("runs", [])
    if isinstance(runs, list):
        return summarize_runs_by_phase(runs)
    return {}


def run_export_benchmark(args: argparse.Namespace) -> dict[str, Any]:
    project_root = args.project_root.resolve()
    build_dir = resolve_build_dir(project_root, args.build_dir)
    configure_python_paths(project_root, build_dir)

    import dfee_native_bridge

    raw_filename = resolve_raw_filename(project_root, args.raw_filename)
    output_path = artifact_output_path(project_root, args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    session = dfee_native_bridge.create_session(project_root)
    session.select_file(raw_filename)
    request = build_export_request(dfee_native_bridge, raw_filename, args)

    runs: list[dict[str, Any]] = []
    total_runs = max(args.cold_runs, 0) + max(args.warm_runs, 0)
    for run_index in range(total_runs):
        phase = "cold" if run_index < max(args.cold_runs, 0) else "warm"
        wall_start = time.perf_counter()
        result = session.export_image(request)
        wall_ms = (time.perf_counter() - wall_start) * 1000.0
        stage_timings = stage_timings_to_dict(result.engine)
        run_record = {
            "run_index": run_index + 1,
            "phase": phase,
            "wall_ms": wall_ms,
            "output_path": str(result.output_path),
            "report_path": str(result.report_path) if result.report_path is not None else "",
            "summary_timings_ms": select_summary_timings(stage_timings),
            "all_timings_ms": stage_timings,
            "engine_version": result.engine.engine_version,
            "libraw_enabled": result.engine.libraw_enabled,
            "cuda_mode": result.engine.cuda_status.mode,
        }
        runs.append(run_record)

        if not args.keep_exports:
            result.output_path.unlink(missing_ok=True)
            if result.report_path is not None:
                result.report_path.unlink(missing_ok=True)

    phase_summary = summarize_runs_by_phase(runs)
    payload = {
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "label": args.label,
        "project_root": str(project_root),
        "build_dir": str(build_dir),
        "raw_filename": raw_filename,
        "request": asdict(request),
        "cold_runs": max(args.cold_runs, 0),
        "warm_runs": max(args.warm_runs, 0),
        "runs": runs,
        "phase_summary": phase_summary,
        "method_reference": "cpp_engine/migration_docs/PERFORMANCE_METHOD.md",
    }

    if args.baseline is not None:
        baseline_path = args.baseline.resolve()
        baseline_payload = json.loads(baseline_path.read_text(encoding="utf-8"))
        baseline_phase_summary = phase_summary_from_payload(baseline_payload)
        payload["baseline_comparison"] = {
            "baseline_path": str(baseline_path),
            "baseline_label": str(baseline_payload.get("label", "")),
            "phase_deltas": compare_against_baseline(phase_summary, baseline_phase_summary),
        }

    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    payload["artifact_path"] = str(output_path)
    return payload


def main() -> int:
    args = parse_args()
    try:
        payload = run_export_benchmark(args)
    except Exception as exc:
        print(f"export benchmark failed: {exc}", file=sys.stderr)
        return 1

    print(f"wrote benchmark artifact: {payload['artifact_path']}")
    for phase, summary in payload.get("phase_summary", {}).items():
        wall_summary = summary.get("wall_ms", {})
        median_wall = wall_summary.get("median", 0.0)
        metrics = summary.get("metrics_ms", {})
        total_median = metrics.get("export_image_total", {}).get("median", 0.0)
        render_median = metrics.get("export_image_render", {}).get("median", 0.0)
        print(
            f"summary [{phase}] median_total={total_median:.3f}ms "
            f"median_render={render_median:.3f}ms median_wall={median_wall:.3f}ms"
        )
    for run in payload["runs"]:
        total_ms = run["summary_timings_ms"].get("export_image_total", run["wall_ms"])
        render_ms = run["summary_timings_ms"].get("export_image_render", 0.0)
        print(
            f"run {run['run_index']} [{run['phase']}] total={total_ms:.3f}ms "
            f"render={render_ms:.3f}ms wall={run['wall_ms']:.3f}ms"
        )
    baseline_comparison = payload.get("baseline_comparison", {})
    for phase, phase_delta in baseline_comparison.get("phase_deltas", {}).items():
        total_delta = phase_delta.get("metrics_ms", {}).get("export_image_total")
        render_delta = phase_delta.get("metrics_ms", {}).get("export_image_render")
        if total_delta or render_delta:
            total_text = ""
            render_text = ""
            if total_delta:
                total_text = (
                    f" total_delta={total_delta['delta_ms']:.3f}ms "
                    f"({total_delta['delta_percent']:.2f}%)"
                )
            if render_delta:
                render_text = (
                    f" render_delta={render_delta['delta_ms']:.3f}ms "
                    f"({render_delta['delta_percent']:.2f}%)"
                )
            print(f"baseline [{phase}]{total_text}{render_text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
