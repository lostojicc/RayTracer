#!/usr/bin/env python3
"""M10 analysis generator.

Reads the committed M9 benchmark dataset (Benchmark/Results/benchmark_results.csv)
and produces the derived tables and plots referenced by
Analysis/M10-BVH-Analysis.md.

This script performs no rendering, traversal, or BVH construction of its own.
It only reads the already-collected CSV and computes derived quantities
(differences, ratios, coefficients of variation, theoretical reference
curves) directly from the numbers already present in that file. Nothing is
invented, extrapolated beyond the measured range, or manually entered.

Usage:
    python3 Analysis/Source/generate_analysis.py \
        [--input Benchmark/Results/benchmark_results.csv] \
        [--output-dir Analysis/Results]

No third-party dependencies (stdlib only: csv, math, statistics), consistent
with this project's general preference for dependency-free tooling for
narrow, one-off scope (see specs/milestones.md M4's OBJ-importer decision).
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from svgplot import Chart, Series, next_color, save_chart  # noqa: E402


def to_opt_float(value: str) -> Optional[float]:
    if value is None or value == "" or value == "NA":
        return None
    return float(value)


def to_opt_int(value: str) -> Optional[int]:
    f = to_opt_float(value)
    return int(f) if f is not None else None


def to_bool(value: str) -> bool:
    return value.strip().lower() == "true"


@dataclass
class Row:
    processor: str
    scene: str
    triangle_count: int
    traversal_mode: str
    leaf_size: Optional[int]
    resolution_width: Optional[int]
    resolution_height: Optional[int]
    samples_per_pixel: Optional[int]
    max_path_depth: Optional[int]
    ray_count: Optional[int]
    iterations: int
    warmup_iterations: int
    model_load_ms: Optional[float]
    bvh_build_ms: Optional[float]
    gpu_triangle_upload_ms: Optional[float]
    gpu_bvh_node_upload_ms: Optional[float]
    gpu_bvh_primitive_upload_ms: Optional[float]
    measurement_mean_ms: float
    measurement_min_ms: float
    measurement_max_ms: float
    measurement_stddev_ms: float
    node_count: Optional[int]
    leaf_count: Optional[int]
    max_depth: Optional[int]
    min_leaf_size: Optional[int]
    max_leaf_size: Optional[int]
    average_leaf_size: Optional[float]
    correctness_checked: bool
    correctness_passed: bool
    mismatch_count: Optional[int]
    tested_ray_count: Optional[int]
    gpu_pixel_check_performed: bool
    gpu_pixel_check_passed: bool
    gpu_pixel_max_abs_diff: Optional[float]
    gpu_pixel_mismatch_count: Optional[int]
    status: str
    status_reason: str
    gl_vendor: Optional[str]
    gl_renderer: Optional[str]
    gl_version: Optional[str]
    run_timestamp_utc: str


def load_rows(path: str) -> List[Row]:
    rows: List[Row] = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for r in reader:
            rows.append(Row(
                processor=r["processor"],
                scene=r["scene"],
                triangle_count=int(r["triangle_count"]),
                traversal_mode=r["traversal_mode"],
                leaf_size=to_opt_int(r["leaf_size"]),
                resolution_width=to_opt_int(r["resolution_width"]),
                resolution_height=to_opt_int(r["resolution_height"]),
                samples_per_pixel=to_opt_int(r["samples_per_pixel"]),
                max_path_depth=to_opt_int(r["max_path_depth"]),
                ray_count=to_opt_int(r["ray_count"]),
                iterations=int(r["iterations"]),
                warmup_iterations=int(r["warmup_iterations"]),
                model_load_ms=to_opt_float(r["model_load_ms"]),
                bvh_build_ms=to_opt_float(r["bvh_build_ms"]),
                gpu_triangle_upload_ms=to_opt_float(r["gpu_triangle_upload_ms"]),
                gpu_bvh_node_upload_ms=to_opt_float(r["gpu_bvh_node_upload_ms"]),
                gpu_bvh_primitive_upload_ms=to_opt_float(r["gpu_bvh_primitive_upload_ms"]),
                measurement_mean_ms=float(r["measurement_mean_ms"]),
                measurement_min_ms=float(r["measurement_min_ms"]),
                measurement_max_ms=float(r["measurement_max_ms"]),
                measurement_stddev_ms=float(r["measurement_stddev_ms"]),
                node_count=to_opt_int(r["node_count"]),
                leaf_count=to_opt_int(r["leaf_count"]),
                max_depth=to_opt_int(r["max_depth"]),
                min_leaf_size=to_opt_int(r["min_leaf_size"]),
                max_leaf_size=to_opt_int(r["max_leaf_size"]),
                average_leaf_size=to_opt_float(r["average_leaf_size"]),
                correctness_checked=to_bool(r["correctness_checked"]),
                correctness_passed=to_bool(r["correctness_passed"]),
                mismatch_count=to_opt_int(r["mismatch_count"]),
                tested_ray_count=to_opt_int(r["tested_ray_count"]),
                gpu_pixel_check_performed=to_bool(r["gpu_pixel_check_performed"]),
                gpu_pixel_check_passed=to_bool(r["gpu_pixel_check_passed"]),
                gpu_pixel_max_abs_diff=to_opt_float(r["gpu_pixel_max_abs_diff"]),
                gpu_pixel_mismatch_count=to_opt_int(r["gpu_pixel_mismatch_count"]),
                status=r["status"],
                status_reason=r["status_reason"],
                gl_vendor=r["gl_vendor"] if r["gl_vendor"] != "NA" else None,
                gl_renderer=r["gl_renderer"] if r["gl_renderer"] != "NA" else None,
                gl_version=r["gl_version"] if r["gl_version"] != "NA" else None,
                run_timestamp_utc=r["run_timestamp_utc"],
            ))
    return rows


# Real-world (imported) scenes vs synthetic (generated) scenes, purely for
# labelling. This mirrors Benchmark/Source/Scenes.cpp's SceneDescriptor
# (IsSynthetic), it does not redefine anything.
REAL_WORLD_SCENES = {"Bunny", "Armadillo"}

# Canonical ascending-triangle-count scene order used throughout the tables
# and plots below, derived directly from each scene's own triangle_count
# column (not hardcoded assumptions about the CSV's row order).
def scene_order(rows: List[Row]) -> List[str]:
    seen: Dict[str, int] = {}
    for r in rows:
        seen[r.scene] = r.triangle_count
    return [s for s, _ in sorted(seen.items(), key=lambda kv: kv[1])]


def find(rows: List[Row], **kwargs) -> Optional[Row]:
    for r in rows:
        if all(getattr(r, k) == v for k, v in kwargs.items()):
            return r
    return None


def cov(mean: float, stddev: float) -> Optional[float]:
    if mean == 0:
        return None
    return stddev / mean


def write_csv(path: str, header: List[str], data_rows: List[List[object]]) -> None:
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(header)
        for row in data_rows:
            writer.writerow(["NA" if v is None else v for v in row])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    parser.add_argument("--input", default=os.path.join(repo_root, "Benchmark", "Results", "benchmark_results.csv"))
    parser.add_argument("--output-dir", default=os.path.join(repo_root, "Analysis", "Results"))
    args = parser.parse_args()

    rows = load_rows(args.input)
    tables_dir = os.path.join(args.output_dir, "tables")
    plots_dir = os.path.join(args.output_dir, "plots")
    os.makedirs(tables_dir, exist_ok=True)
    os.makedirs(plots_dir, exist_ok=True)

    order = scene_order(rows)
    print(f"Loaded {len(rows)} rows from {args.input}")
    print(f"Scenes (ascending triangle_count): {order}")

    # ------------------------------------------------------------------
    # 1. CPU BruteForce vs BVH: absolute diff, speedup, relative reduction, CoV
    # ------------------------------------------------------------------
    cpu_table: List[List[object]] = []
    for scene in order:
        bf = find(rows, processor="CPU", scene=scene, traversal_mode="BruteForce")
        bvh = find(rows, processor="CPU", scene=scene, traversal_mode="BVH")
        if not bf or not bvh:
            continue
        if bf.status != "ok" or bvh.status != "ok":
            continue
        abs_diff = bf.measurement_mean_ms - bvh.measurement_mean_ms
        speedup = bf.measurement_mean_ms / bvh.measurement_mean_ms if bvh.measurement_mean_ms > 0 else None
        rel_reduction = abs_diff / bf.measurement_mean_ms * 100.0 if bf.measurement_mean_ms > 0 else None
        cpu_table.append([
            scene, bf.triangle_count,
            round(bf.measurement_mean_ms, 4), round(bf.measurement_stddev_ms, 4),
            round(cov(bf.measurement_mean_ms, bf.measurement_stddev_ms) * 100.0, 3) if cov(bf.measurement_mean_ms, bf.measurement_stddev_ms) is not None else None,
            round(bvh.measurement_mean_ms, 4), round(bvh.measurement_stddev_ms, 4),
            round(cov(bvh.measurement_mean_ms, bvh.measurement_stddev_ms) * 100.0, 3) if cov(bvh.measurement_mean_ms, bvh.measurement_stddev_ms) is not None else None,
            round(abs_diff, 4), round(speedup, 3) if speedup else None,
            round(rel_reduction, 3) if rel_reduction is not None else None,
            bvh.tested_ray_count,
        ])
    write_csv(
        os.path.join(tables_dir, "cpu_bruteforce_vs_bvh.csv"),
        ["scene", "triangle_count", "bf_mean_ms", "bf_stddev_ms", "bf_cov_pct",
         "bvh_mean_ms", "bvh_stddev_ms", "bvh_cov_pct",
         "abs_diff_ms", "speedup_x", "relative_reduction_pct", "tested_ray_count"],
        cpu_table,
    )

    # ------------------------------------------------------------------
    # 2. GPU BruteForce vs BVH -- ONLY where BruteForce actually completed
    #    ("ok"), never for "skipped" rows.
    # ------------------------------------------------------------------
    gpu_valid_table: List[List[object]] = []
    gpu_bvh_only_table: List[List[object]] = []
    for scene in order:
        bf = find(rows, processor="GPU", scene=scene, traversal_mode="BruteForce")
        bvh = find(rows, processor="GPU", scene=scene, traversal_mode="BVH")
        if not bvh:
            continue
        if bf and bf.status == "ok" and bvh.status == "ok":
            abs_diff = bf.measurement_mean_ms - bvh.measurement_mean_ms
            speedup = bf.measurement_mean_ms / bvh.measurement_mean_ms if bvh.measurement_mean_ms > 0 else None
            rel_reduction = abs_diff / bf.measurement_mean_ms * 100.0 if bf.measurement_mean_ms > 0 else None
            gpu_valid_table.append([
                scene, bf.triangle_count,
                round(bf.measurement_mean_ms, 4), round(bf.measurement_stddev_ms, 4),
                round(cov(bf.measurement_mean_ms, bf.measurement_stddev_ms) * 100.0, 3) if cov(bf.measurement_mean_ms, bf.measurement_stddev_ms) is not None else None,
                round(bvh.measurement_mean_ms, 4), round(bvh.measurement_stddev_ms, 4),
                round(cov(bvh.measurement_mean_ms, bvh.measurement_stddev_ms) * 100.0, 3) if cov(bvh.measurement_mean_ms, bvh.measurement_stddev_ms) is not None else None,
                round(abs_diff, 4), round(speedup, 3) if speedup else None,
                round(rel_reduction, 3) if rel_reduction is not None else None,
                bvh.gpu_pixel_check_performed, bvh.gpu_pixel_check_passed, bvh.gpu_pixel_max_abs_diff,
            ])
        else:
            reason = bf.status_reason if bf else "no BruteForce row recorded"
            gpu_bvh_only_table.append([
                scene, bvh.triangle_count, round(bvh.measurement_mean_ms, 4),
                round(bvh.measurement_stddev_ms, 4), bf.status if bf else "missing", reason,
            ])
    write_csv(
        os.path.join(tables_dir, "gpu_bruteforce_vs_bvh.csv"),
        ["scene", "triangle_count", "bf_mean_ms", "bf_stddev_ms", "bf_cov_pct",
         "bvh_mean_ms", "bvh_stddev_ms", "bvh_cov_pct",
         "abs_diff_ms", "speedup_x", "relative_reduction_pct",
         "gpu_pixel_check_performed", "gpu_pixel_check_passed", "gpu_pixel_max_abs_diff"],
        gpu_valid_table,
    )
    write_csv(
        os.path.join(tables_dir, "gpu_bvh_only_brute_force_skipped.csv"),
        ["scene", "triangle_count", "bvh_mean_ms", "bvh_stddev_ms", "brute_force_status", "brute_force_status_reason"],
        gpu_bvh_only_table,
    )

    # ------------------------------------------------------------------
    # 3. BVH structural statistics vs a theoretical median-split reference
    #    (leaf_size fixed at 4 for every row in this dataset).
    # ------------------------------------------------------------------
    struct_table: List[List[object]] = []
    for scene in order:
        bvh = find(rows, processor="CPU", scene=scene, traversal_mode="BVH")
        if not bvh:
            continue
        leaf_size = bvh.leaf_size or 4
        theoretical_min_leaves = math.ceil(bvh.triangle_count / leaf_size)
        theoretical_max_depth = math.ceil(math.log2(max(bvh.triangle_count / leaf_size, 1)))
        struct_table.append([
            scene, bvh.triangle_count, leaf_size,
            bvh.node_count, bvh.leaf_count, bvh.max_depth,
            bvh.min_leaf_size, bvh.max_leaf_size, round(bvh.average_leaf_size, 4),
            theoretical_min_leaves,
            round(bvh.leaf_count / theoretical_min_leaves, 4) if theoretical_min_leaves else None,
            theoretical_max_depth,
            bvh.max_depth - theoretical_max_depth,
            2 * bvh.leaf_count - 1 == bvh.node_count,
        ])
    write_csv(
        os.path.join(tables_dir, "bvh_structure.csv"),
        ["scene", "triangle_count", "leaf_size", "node_count", "leaf_count", "max_depth",
         "min_leaf_size", "max_leaf_size", "average_leaf_size",
         "theoretical_min_leaf_count_ceil_N_over_leafsize", "leaf_count_ratio_to_theoretical_min",
         "theoretical_max_depth_ceil_log2_N_over_leafsize", "measured_minus_theoretical_depth",
         "node_count_equals_2xleaf_minus_1"],
        struct_table,
    )

    # ------------------------------------------------------------------
    # 4. Construction + upload overhead vs single-frame GPU render time,
    #    plus a frame-count amortization estimate (derived, not measured
    #    directly -- computed purely from the measured one-time and
    #    per-frame numbers already in the CSV).
    # ------------------------------------------------------------------
    overhead_table: List[List[object]] = []
    for scene in order:
        bvh = find(rows, processor="GPU", scene=scene, traversal_mode="BVH")
        if not bvh or bvh.status != "ok":
            continue
        onetime = (bvh.bvh_build_ms or 0.0) + (bvh.gpu_triangle_upload_ms or 0.0) \
            + (bvh.gpu_bvh_node_upload_ms or 0.0) + (bvh.gpu_bvh_primitive_upload_ms or 0.0)
        per_frame = bvh.measurement_mean_ms
        ratio = onetime / per_frame if per_frame > 0 else None
        frames_10pct = onetime * 9.0 / per_frame if per_frame > 0 else None
        frames_1pct = onetime * 99.0 / per_frame if per_frame > 0 else None
        overhead_table.append([
            scene, bvh.triangle_count,
            round(bvh.bvh_build_ms, 4), round(bvh.gpu_triangle_upload_ms, 4),
            round(bvh.gpu_bvh_node_upload_ms, 4), round(bvh.gpu_bvh_primitive_upload_ms, 4),
            round(onetime, 4), round(per_frame, 4),
            round(ratio, 3) if ratio is not None else None,
            math.ceil(frames_10pct) if frames_10pct is not None else None,
            math.ceil(frames_1pct) if frames_1pct is not None else None,
        ])
    write_csv(
        os.path.join(tables_dir, "gpu_construction_upload_overhead.csv"),
        ["scene", "triangle_count", "bvh_build_serialize_ms", "gpu_triangle_upload_ms",
         "gpu_bvh_node_upload_ms", "gpu_bvh_primitive_upload_ms",
         "total_onetime_overhead_ms", "single_frame_render_mean_ms",
         "overhead_to_one_frame_ratio",
         "frames_until_overhead_leq_10pct_of_cumulative_time",
         "frames_until_overhead_leq_1pct_of_cumulative_time"],
        overhead_table,
    )

    # ------------------------------------------------------------------
    # 5. CPU vs GPU per-primitive / per-ray normalized cost (helps compare
    #    synthetic vs real-world model efficiency independent of raw N).
    # ------------------------------------------------------------------
    normalized_table: List[List[object]] = []
    for scene in order:
        bf = find(rows, processor="CPU", scene=scene, traversal_mode="BruteForce")
        bvh = find(rows, processor="CPU", scene=scene, traversal_mode="BVH")
        if not bf or not bvh:
            continue
        ray_count = bvh.tested_ray_count or 1
        us_per_ray_bf = bf.measurement_mean_ms * 1000.0 / ray_count
        us_per_ray_bvh = bvh.measurement_mean_ms * 1000.0 / ray_count
        us_per_tri_build = (bvh.bvh_build_ms or 0.0) * 1000.0 / bf.triangle_count
        normalized_table.append([
            scene, bf.triangle_count,
            "real" if scene in REAL_WORLD_SCENES else "synthetic",
            round(us_per_ray_bf, 4), round(us_per_ray_bvh, 4), round(us_per_tri_build, 5),
        ])
    write_csv(
        os.path.join(tables_dir, "normalized_cpu_cost.csv"),
        ["scene", "triangle_count", "kind", "cpu_bruteforce_us_per_ray",
         "cpu_bvh_us_per_ray", "cpu_bvh_build_us_per_triangle"],
        normalized_table,
    )

    # ------------------------------------------------------------------
    # Plots
    # ------------------------------------------------------------------

    def scene_points(processor: str, mode: str, y_field: str) -> List[tuple]:
        pts = []
        for scene in order:
            r = find(rows, processor=processor, scene=scene, traversal_mode=mode)
            if not r or r.status != "ok":
                continue
            pts.append((r.triangle_count, getattr(r, y_field)))
        return pts

    # Plot 1: CPU traversal time vs triangle count (BruteForce vs BVH)
    save_chart(Chart(
        title="CPU Traversal Time vs Triangle Count",
        note="CPU, mean of 12 sweeps over 3000 deterministic test rays each (3 warm-up sweeps discarded); leaf size 4. Log-log axes.",
        x_label="Triangle count (N, log scale)",
        y_label="Mean traversal time per full ray sweep (ms, log scale)",
        x_log=True, y_log=True,
        series=[
            Series("CPU BruteForce (measured)", scene_points("CPU", "BruteForce", "measurement_mean_ms"), next_color(0)),
            Series("CPU BVH (measured)", scene_points("CPU", "BVH", "measurement_mean_ms"), next_color(1)),
        ],
    ), os.path.join(plots_dir, "cpu_traversal_time_vs_triangle_count.svg"))

    # Plot 2: CPU speedup vs triangle count
    speedup_pts = [(row[1], row[9]) for row in cpu_table if row[9] is not None]
    save_chart(Chart(
        title="CPU Speedup (BruteForce mean / BVH mean) vs Triangle Count",
        note="Speedup = brute-force mean / BVH mean, both measured on the same ray set. Log x, linear y.",
        x_label="Triangle count (N, log scale)",
        y_label="Speedup ratio (x, unitless)",
        x_log=True, y_log=False,
        series=[Series("Measured CPU speedup", speedup_pts, next_color(2), marker_shape="square")],
    ), os.path.join(plots_dir, "cpu_speedup_vs_triangle_count.svg"))

    # Plot 3: GPU BruteForce vs BVH, only for scenes where both completed
    gpu_bf_pts = [(row[1], row[2]) for row in gpu_valid_table]
    gpu_bvh_valid_pts = [(row[1], row[5]) for row in gpu_valid_table]
    save_chart(Chart(
        title="GPU Render Time vs Triangle Count (BruteForce vs BVH, completed configurations only)",
        note="GPU, mean of 10 measured frames (320x240, 1 spp, max_path_depth=50), 3 warm-up frames discarded. "
             "Only scenes where GPU BruteForce completed (triangle_count <= 20000 ceiling) are shown here.",
        x_label="Triangle count (N, log scale)",
        y_label="Mean GPU render time per frame (ms, log scale)",
        x_log=True, y_log=True,
        series=[
            Series("GPU BruteForce (measured, completed only)", gpu_bf_pts, next_color(0)),
            Series("GPU BVH (measured, same scenes)", gpu_bvh_valid_pts, next_color(1)),
        ],
    ), os.path.join(plots_dir, "gpu_bruteforce_vs_bvh.svg"))

    # Plot 4: GPU BVH across ALL scenes (including those where BF was
    # skipped), with skipped-BF scenes visually distinguished, never
    # connected to a fabricated brute-force value.
    gpu_bvh_all_pts = scene_points("GPU", "BVH", "measurement_mean_ms")
    skipped_scenes = {row[0] for row in gpu_bvh_only_table}
    gpu_bvh_completed_bf = [(x, y) for (x, y), s in zip(gpu_bvh_all_pts, order) if s not in skipped_scenes]
    gpu_bvh_skipped_bf = [(x, y) for (x, y), s in zip(gpu_bvh_all_pts, order) if s in skipped_scenes]
    save_chart(Chart(
        title="GPU BVH Render Time vs Triangle Count (all scenes)",
        note="Triangles marker = scenes where GPU BruteForce was skipped (triangle_count exceeds the "
             "20000-triangle ceiling); no brute-force value exists or is plotted for these.",
        x_label="Triangle count (N, log scale)",
        y_label="Mean GPU BVH render time per frame (ms, log scale)",
        x_log=True, y_log=True,
        series=[
            Series("GPU BVH (BruteForce also measured)", gpu_bvh_completed_bf, next_color(1), line=False),
            Series("GPU BVH (BruteForce skipped, ceiling)", gpu_bvh_skipped_bf, next_color(4), line=False, marker_shape="triangle"),
        ],
    ), os.path.join(plots_dir, "gpu_bvh_all_scenes.svg"))

    # Plot 5: BVH build time vs triangle count (CPU-only construction time),
    # plus theoretical O(N) and O(N log N) reference curves normalized to
    # match the measured value at the smallest N, drawn dashed and clearly
    # labelled as theoretical, not measured.
    build_pts = scene_points("CPU", "BVH", "bvh_build_ms")
    if build_pts:
        n0, t0 = build_pts[0]
        on_pts = [(n, t0 * (n / n0)) for n, _ in build_pts]
        onlogn_pts = [(n, t0 * (n * math.log2(max(n, 2))) / (n0 * math.log2(max(n0, 2)))) for n, _ in build_pts]
        save_chart(Chart(
            title="CPU BVH Construction Time vs Triangle Count",
            note="Measured: Core::BVH construction only (excludes model load, upload, traversal). "
                 "Dashed lines are O(N) and O(N log N) references normalized to the smallest measured point -- "
                 "theoretical, not measured.",
            x_label="Triangle count (N, log scale)",
            y_label="BVH construction time (ms, log scale)",
            x_log=True, y_log=True,
            series=[
                Series("Measured build time", build_pts, next_color(0)),
                Series("O(N) reference (theoretical)", on_pts, next_color(3), marker=False, dashed=True),
                Series("O(N log N) reference (theoretical)", onlogn_pts, next_color(5), marker=False, dashed=True),
            ],
        ), os.path.join(plots_dir, "bvh_build_time_vs_triangle_count.svg"))

    # Plot 6: BVH node/leaf count vs triangle count, plus theoretical
    # 2*ceil(N/leafSize)-1 reference (leaf size 4 throughout this dataset).
    node_pts = scene_points("CPU", "BVH", "node_count")
    leaf_pts = scene_points("CPU", "BVH", "leaf_count")
    theoretical_node_pts = [(n, 2 * math.ceil(n / 4) - 1) for n, _ in node_pts]
    save_chart(Chart(
        title="BVH Node/Leaf Count vs Triangle Count (leaf size = 4)",
        note="Dashed line: theoretical node count if every leaf held exactly leaf_size=4 triangles "
             "(2*ceil(N/4)-1) -- theoretical, not measured. Median-split leaves are frequently smaller than 4, "
             "so the measured node count is consistently higher.",
        x_label="Triangle count (N, log scale)",
        y_label="Count (log scale)",
        x_log=True, y_log=True,
        series=[
            Series("Measured node count", node_pts, next_color(0)),
            Series("Measured leaf count", leaf_pts, next_color(1)),
            Series("Theoretical node count (all leaves = 4)", theoretical_node_pts, next_color(3), marker=False, dashed=True),
        ],
    ), os.path.join(plots_dir, "bvh_node_count_vs_triangle_count.svg"))

    # Plot 7: overhead amortization -- fraction of cumulative GPU time spent
    # on one-time build+upload overhead, as a function of frame count.
    # Purely derived from the already-measured onetime/per-frame numbers.
    amort_series = []
    frame_counts = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000]
    for i, row in enumerate(overhead_table):
        scene = row[0]
        onetime = row[6]
        per_frame = row[7]
        pts = [(f, onetime / (onetime + f * per_frame) * 100.0) for f in frame_counts]
        amort_series.append(Series(scene, pts, next_color(i), marker=True))
    save_chart(Chart(
        title="One-Time BVH Build + GPU Upload Overhead as a Fraction of Cumulative Render Time",
        note="Derived from measured one-time overhead (BVH build+serialize + 3 GPU upload times) and measured "
             "single-frame BVH render time, assuming that many frames are rendered from the same static upload "
             "(specs/gpu-integration.md 'GPU Data Lifecycle'). Not a new measurement.",
        x_label="Number of rendered frames (log scale)",
        y_label="Overhead as % of cumulative time (log scale)",
        x_log=True, y_log=True,
        series=amort_series,
        width=900, height=560,
    ), os.path.join(plots_dir, "overhead_amortization_vs_frame_count.svg"))

    print(f"Wrote tables to {tables_dir}")
    print(f"Wrote plots to {plots_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
