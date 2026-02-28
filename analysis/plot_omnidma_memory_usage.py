#!/usr/bin/env python3
"""
Summarize OmniDMA memory usage across drop rates and distances, then plot heatmaps.

Input file format (out_memory_usage.txt):
  # time_ns,host_id,total_rxqp_linked_list_entries,total_rxqp_lookup_table_entries
  2000000000,1,0,0
  ...

For each run folder, this script aggregates per-time memory usage as:
  total_entries(t) = sum_host(linked_list_entries + lookup_table_entries)

It then computes avg/p95/p99/max over time for each (distance, drop_rate) run.
"""

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


RUN_DIR_RE = re.compile(
    r"^omnidma_(?P<topo>.+?)_omniDMA_flow_dropm(?P<drop_mode>[^_]+)"
    r"_drop(?P<drop_rate>[0-9.]+)_pfc(?P<pfc>\d+)_irn(?P<irn>\d+)$"
)
DIST_RE = re.compile(r"topo_simple_dumbbell_OS2_(?P<delay_us>\d+)us$")


def parse_run_dir_name(name: str) -> Optional[Dict[str, object]]:
    m = RUN_DIR_RE.match(name)
    if m is None:
        return None
    topo = m.group("topo")
    d = DIST_RE.match(topo)
    if d is None:
        return None
    delay_us = int(d.group("delay_us"))
    return {
        "topo": topo,
        "distance_us": delay_us,
        "distance_label": f"{delay_us}us",
        "drop_mode": m.group("drop_mode"),
        "drop_rate": float(m.group("drop_rate")),
        "pfc": int(m.group("pfc")),
        "irn": int(m.group("irn")),
    }


def calc_metrics(series: pd.Series) -> Tuple[float, float, float, float]:
    if series.empty:
        return 0.0, 0.0, 0.0, 0.0
    arr = series.to_numpy(dtype=np.float64)
    avg = float(np.mean(arr))
    p95 = float(np.percentile(arr, 95))
    p99 = float(np.percentile(arr, 99))
    maxv = float(np.max(arr))
    return avg, p95, p99, maxv


def format_tick(v: float) -> str:
    if abs(v - round(v)) < 1e-9:
        return str(int(round(v)))
    return f"{v:.4g}"


def plot_heatmaps(df: pd.DataFrame, out_png: Path) -> None:
    metrics = ["avg", "p95", "p99", "max"]
    metric_titles = {
        "avg": "Average",
        "p95": "P95",
        "p99": "P99",
        "max": "Max",
    }

    distance_order = sorted(df["distance_us"].unique().tolist())
    distance_labels = [f"{d}us" for d in distance_order]
    drop_order = sorted(df["drop_rate_pct"].unique().tolist())

    fig, axes = plt.subplots(2, 2, figsize=(14, 10), constrained_layout=True)
    axes = axes.flatten()

    for ax, metric in zip(axes, metrics):
        pivot = (
            df.pivot_table(
                index="drop_rate_pct",
                columns="distance_us",
                values=metric,
                aggfunc="first",
            )
            .reindex(index=drop_order, columns=distance_order)
        )
        data = pivot.to_numpy(dtype=float)
        masked = np.ma.masked_invalid(data)
        im = ax.imshow(masked, aspect="auto", origin="lower", cmap="viridis")

        ax.set_title(f"{metric_titles[metric]} Memory Entries")
        ax.set_xlabel("Distance")
        ax.set_ylabel("Drop Rate (%)")
        ax.set_xticks(np.arange(len(distance_labels)))
        ax.set_xticklabels(distance_labels)
        ax.set_yticks(np.arange(len(drop_order)))
        ax.set_yticklabels([format_tick(v) for v in drop_order])

        # annotate cells
        for i in range(masked.shape[0]):
            for j in range(masked.shape[1]):
                v = data[i, j]
                txt = "-" if math.isnan(v) else f"{v:.1f}"
                ax.text(j, i, txt, ha="center", va="center", color="white", fontsize=8)

        cbar = fig.colorbar(im, ax=ax, shrink=0.86)
        cbar.set_label("Entries")

    fig.suptitle("OmniDMA Memory Usage (sum over hosts per sample)", fontsize=14)
    fig.savefig(out_png, dpi=180)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot OmniDMA memory usage avg/p95/p99/max across drop rates and distances."
    )
    parser.add_argument(
        "--input-root",
        default="mix/output",
        help="Root directory containing run output directories (default: mix/output).",
    )
    parser.add_argument(
        "--out-dir",
        default="mix/output/memory_usage_merged",
        help="Directory to write summary CSV and plots.",
    )
    parser.add_argument(
        "--drop-mode",
        default="amazon",
        help="Only include runs with this drop mode in directory name (default: amazon).",
    )
    parser.add_argument(
        "--active-only",
        action="store_true",
        help="Compute stats only on samples where total memory entries > 0.",
    )
    args = parser.parse_args()

    input_root = Path(args.input_root)
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    rows: List[Dict[str, object]] = []
    for f in input_root.glob("omnidma_*_omniDMA_flow_dropm*_drop*_pfc0_irn0/out_memory_usage.txt"):
        run_info = parse_run_dir_name(f.parent.name)
        if run_info is None:
            continue
        if str(run_info["drop_mode"]).lower() != args.drop_mode.lower():
            continue

        try:
            data = pd.read_csv(
                f,
                comment="#",
                header=None,
                names=[
                    "time_ns",
                    "host_id",
                    "total_rxqp_linked_list_entries",
                    "total_rxqp_lookup_table_entries",
                ],
            )
        except Exception as e:
            print(f"[WARN] skip {f}: failed to read ({e})")
            continue

        if data.empty:
            print(f"[WARN] skip {f}: empty file")
            continue

        data["total_entries"] = (
            data["total_rxqp_linked_list_entries"] + data["total_rxqp_lookup_table_entries"]
        )
        # Aggregate across all hosts at each sampling time.
        ts = data.groupby("time_ns", sort=True)["total_entries"].sum()
        if args.active_only and (ts > 0).any():
            ts = ts[ts > 0]

        avg, p95, p99, maxv = calc_metrics(ts)
        rows.append(
            {
                "run_dir": f.parent.name,
                "distance_us": run_info["distance_us"],
                "distance_label": run_info["distance_label"],
                "drop_mode": run_info["drop_mode"],
                "drop_rate": run_info["drop_rate"],
                "drop_rate_pct": float(run_info["drop_rate"]) * 100.0,
                "samples": int(ts.shape[0]),
                "avg": avg,
                "p95": p95,
                "p99": p99,
                "max": maxv,
                "source_file": str(f),
            }
        )

    if not rows:
        raise SystemExit("No matching out_memory_usage.txt files found.")

    summary = pd.DataFrame(rows).sort_values(
        by=["distance_us", "drop_rate"], ascending=[True, True]
    )
    csv_path = out_dir / "omnidma_memory_usage_summary.csv"
    summary.to_csv(csv_path, index=False)

    png_path = out_dir / "omnidma_memory_usage_heatmaps.png"
    plot_heatmaps(summary, png_path)

    print(f"[OK] summary csv: {csv_path}")
    print(f"[OK] heatmap png: {png_path}")


if __name__ == "__main__":
    main()

