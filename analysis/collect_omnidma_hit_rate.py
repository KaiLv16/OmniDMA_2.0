#!/usr/bin/env python3
"""
Collect OmniDMA out_hit_rate.txt data into one CSV.

Output columns:
delay, lossrate, sid, did, flow_id, ll_access, ll_hit, table_access, table_hit
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path
from typing import List, Optional, Sequence, Tuple


DELAY_RE = re.compile(r"_OS2_([0-9]+(?:\.[0-9]+)?)us_", re.IGNORECASE)
LOSS_RE = re.compile(r"_drop([0-9]+(?:\.[0-9]+)?(?:e[+-]?[0-9]+)?)", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Collect OmniDMA cache-hit stats from out_hit_rate.txt files into one CSV."
    )
    parser.add_argument(
        "--input-root",
        default="mix/output",
        help="Root directory to scan (default: mix/output).",
    )
    parser.add_argument(
        "--output",
        default="mix/output/omnidma_hit_rate_summary.csv",
        help="Output CSV path (default: mix/output/omnidma_hit_rate_summary.csv).",
    )
    return parser.parse_args()


def parse_delay_and_loss(path: Path) -> Tuple[Optional[str], Optional[str]]:
    text = str(path.parent)
    delay_match = DELAY_RE.search(text)
    loss_match = LOSS_RE.search(text)
    delay = delay_match.group(1) if delay_match else None
    loss = loss_match.group(1) if loss_match else None
    return delay, loss


def tokenize_hit_rate_line(line: str) -> List[str]:
    # Supports either "a b c" or "a, b, c" style output.
    return line.replace(",", " ").split()


def to_float_or_inf(value: Optional[str]) -> float:
    if value is None:
        return float("inf")
    try:
        return float(value)
    except ValueError:
        return float("inf")


def collect_rows(input_root: Path) -> Tuple[List[List[str]], int]:
    rows: List[List[str]] = []
    skipped = 0

    for hit_file in sorted(input_root.rglob("out_hit_rate.txt")):
        # Keep OmniDMA-mode runs only.
        if "omnidma" not in str(hit_file).lower():
            continue

        delay, loss = parse_delay_and_loss(hit_file)
        if delay is None or loss is None:
            print(
                f"[warn] skip {hit_file}: cannot parse delay/lossrate from path",
                file=sys.stderr,
            )
            skipped += 1
            continue

        with hit_file.open("r", encoding="utf-8", errors="ignore") as f:
            for lineno, raw in enumerate(f, start=1):
                line = raw.strip()
                if not line:
                    continue
                tokens = tokenize_hit_rate_line(line)
                if len(tokens) < 7:
                    print(
                        f"[warn] skip {hit_file}:{lineno}: expected >=7 columns, got {len(tokens)}",
                        file=sys.stderr,
                    )
                    skipped += 1
                    continue

                rows.append([delay, loss] + tokens[:7])

    rows.sort(
        key=lambda r: (
            to_float_or_inf(r[0]),
            to_float_or_inf(r[1]),
            tuple(r[2:]),
        )
    )
    return rows, skipped


def write_csv(rows: Sequence[Sequence[str]], output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "delay",
                "lossrate",
                "sid",
                "did",
                "flow_id",
                "ll_access",
                "ll_hit",
                "table_access",
                "table_hit",
            ]
        )
        writer.writerows(rows)


def main() -> int:
    args = parse_args()
    input_root = Path(args.input_root)
    output_path = Path(args.output)

    if not input_root.exists():
        print(f"[error] input root does not exist: {input_root}", file=sys.stderr)
        return 1

    rows, skipped = collect_rows(input_root)
    write_csv(rows, output_path)
    print(
        f"Collected {len(rows)} rows from OmniDMA out_hit_rate files. "
        f"Skipped {skipped} lines/files. Output: {output_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
