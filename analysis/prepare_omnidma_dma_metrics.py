#!/usr/bin/env python3
"""
Prepare OmniDMA RNIC-DMA plotting metrics from simulation outputs.

This script scans OmniDMA experiment folders and generates CSV metrics for:
1) Stacked op-count bars (x=lossrate, grouped by delay)
2) Queue-depth boxplots (x=lossrate, grouped by delay)

Input:
  - out_rnic_dma_ops.txt

Output CSVs (under --output-dir):
  - dma_ops_count_by_run.csv
  - dma_queue_depth_box_by_run.csv
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


# Keep a stable op order for stacked bars.
KNOWN_OPS: Sequence[str] = (
    "LL_APPEND_WRITE",
    "LL_TO_TABLE_WRITE",
    "LL_PREFETCH_READ",
    "LL_MISS_READ",
    "TABLE_MISS_READ",
)

DELAY_RE = re.compile(r"_OS2_([0-9]+(?:\.[0-9]+)?)us_", re.IGNORECASE)
LOSS_RE = re.compile(r"_drop([0-9]+(?:\.[0-9]+)?(?:e[+-]?[0-9]+)?)_pfc", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare OmniDMA DMA metrics CSVs for stacked-bar + boxplot figures."
    )
    parser.add_argument(
        "--input-root",
        default="mix/output",
        help="Root directory containing simulation output folders (default: mix/output).",
    )
    parser.add_argument(
        "--output-dir",
        default="mix/output/omnidma_dma_metrics",
        help="Directory to write CSVs (default: mix/output/omnidma_dma_metrics).",
    )
    return parser.parse_args()


def parse_delay_and_loss(path: Path) -> Tuple[Optional[str], Optional[str]]:
    text = str(path.parent)
    delay_m = DELAY_RE.search(text)
    loss_m = LOSS_RE.search(text)
    delay = delay_m.group(1) if delay_m else None
    loss = loss_m.group(1) if loss_m else None
    return delay, loss


def percentile(sorted_values: Sequence[int], p: float) -> float:
    if not sorted_values:
        return 0.0
    if p <= 0:
        return float(sorted_values[0])
    if p >= 1:
        return float(sorted_values[-1])

    n = len(sorted_values)
    idx = (n - 1) * p
    lo = int(math.floor(idx))
    hi = int(math.ceil(idx))
    if lo == hi:
        return float(sorted_values[lo])
    frac = idx - lo
    return float(sorted_values[lo] * (1.0 - frac) + sorted_values[hi] * frac)


def to_float_or_inf(value: str) -> float:
    try:
        return float(value)
    except ValueError:
        return float("inf")


def collect_metrics(
    input_root: Path,
) -> Tuple[List[Dict[str, object]], List[Dict[str, object]], int]:
    ops_rows: List[Dict[str, object]] = []
    box_rows: List[Dict[str, object]] = []
    skipped = 0

    ops_files = sorted(input_root.rglob("out_rnic_dma_ops.txt"))
    for ops_file in ops_files:
        # Restrict to OmniDMA-mode outputs.
        if "omnidma" not in str(ops_file).lower():
            continue

        delay, loss = parse_delay_and_loss(ops_file)
        if delay is None or loss is None:
            print(
                f"[warn] skip {ops_file}: cannot parse delay/lossrate from folder name",
                file=sys.stderr,
            )
            skipped += 1
            continue

        op_counter: Counter[str] = Counter()
        queue_depth_values: List[int] = []

        with ops_file.open("r", encoding="utf-8", errors="ignore") as f:
            reader = csv.DictReader(f)
            required_cols = {"op_name", "queue_depth_after_submit"}
            if reader.fieldnames is None or not required_cols.issubset(set(reader.fieldnames)):
                print(
                    f"[warn] skip {ops_file}: missing required columns {sorted(required_cols)}",
                    file=sys.stderr,
                )
                skipped += 1
                continue

            for rec in reader:
                op_name = rec.get("op_name", "").strip()
                if op_name:
                    op_counter[op_name] += 1
                qd_raw = rec.get("queue_depth_after_submit", "").strip()
                if qd_raw:
                    try:
                        queue_depth_values.append(int(qd_raw))
                    except ValueError:
                        # Ignore malformed value but keep processing.
                        pass

        run_name = ops_file.parent.name
        run_path = str(ops_file.parent)

        # Write op-count rows with stable op names first, then any unknown ops.
        all_ops = list(KNOWN_OPS) + sorted(op for op in op_counter.keys() if op not in KNOWN_OPS)
        for op_name in all_ops:
            ops_rows.append(
                {
                    "delay_us": delay,
                    "lossrate": loss,
                    "run_name": run_name,
                    "run_path": run_path,
                    "op_name": op_name,
                    "ops_count": op_counter.get(op_name, 0),
                }
            )

        # Box-plot statistics of queue depth for this run.
        if queue_depth_values:
            queue_depth_values.sort()
            n = len(queue_depth_values)
            q_min = float(queue_depth_values[0])
            q1 = percentile(queue_depth_values, 0.25)
            q2 = percentile(queue_depth_values, 0.50)
            q3 = percentile(queue_depth_values, 0.75)
            q_max = float(queue_depth_values[-1])
            q_mean = float(sum(queue_depth_values)) / n
        else:
            n = 0
            q_min = q1 = q2 = q3 = q_max = q_mean = 0.0

        box_rows.append(
            {
                "delay_us": delay,
                "lossrate": loss,
                "run_name": run_name,
                "run_path": run_path,
                "queue_depth_n": n,
                "queue_depth_min": f"{q_min:.6f}",
                "queue_depth_q1": f"{q1:.6f}",
                "queue_depth_median": f"{q2:.6f}",
                "queue_depth_q3": f"{q3:.6f}",
                "queue_depth_max": f"{q_max:.6f}",
                "queue_depth_mean": f"{q_mean:.6f}",
            }
        )

    def sort_key(d: Dict[str, object]) -> Tuple[float, float, str]:
        return (
            to_float_or_inf(str(d["delay_us"])),
            to_float_or_inf(str(d["lossrate"])),
            str(d["run_name"]),
        )

    ops_rows.sort(key=lambda d: (sort_key(d), str(d["op_name"])))
    box_rows.sort(key=sort_key)
    return ops_rows, box_rows, skipped


def write_csv(path: Path, rows: Iterable[Dict[str, object]], header: Sequence[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(header))
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def main() -> int:
    args = parse_args()
    input_root = Path(args.input_root)
    output_dir = Path(args.output_dir)

    if not input_root.exists():
        print(f"[error] input root does not exist: {input_root}", file=sys.stderr)
        return 1

    ops_rows, box_rows, skipped = collect_metrics(input_root)

    ops_csv = output_dir / "dma_ops_count_by_run.csv"
    box_csv = output_dir / "dma_queue_depth_box_by_run.csv"

    write_csv(
        ops_csv,
        ops_rows,
        header=(
            "delay_us",
            "lossrate",
            "run_name",
            "run_path",
            "op_name",
            "ops_count",
        ),
    )
    write_csv(
        box_csv,
        box_rows,
        header=(
            "delay_us",
            "lossrate",
            "run_name",
            "run_path",
            "queue_depth_n",
            "queue_depth_min",
            "queue_depth_q1",
            "queue_depth_median",
            "queue_depth_q3",
            "queue_depth_max",
            "queue_depth_mean",
        ),
    )

    print(
        f"Prepared metrics from OmniDMA ops files.\n"
        f"  op-count CSV : {ops_csv}\n"
        f"  box-stat CSV : {box_csv}\n"
        f"  runs         : {len(box_rows)}\n"
        f"  skipped      : {skipped}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
