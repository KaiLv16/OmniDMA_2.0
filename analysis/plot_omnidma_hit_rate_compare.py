#!/usr/bin/env python3
"""
Compare OmniDMA hit-rate summary CSV files across firstN/table settings.

Inputs:
  - Files like: omnidma_hit_rate_summary_firstnX_tableY.csv
    with columns:
      delay,lossrate,sid,did,flow_id,ll_access,ll_hit,table_access,table_hit

Outputs:
  - one heatmap figure (firstN x table)
  - one lossrate curve figure
  - two CSV summaries
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


FILE_RE = re.compile(r"firstn(?P<firstn>\d+)_table(?P<table>\d+)", re.IGNORECASE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare omnidma_hit_rate_summary_firstn*_table*.csv and plot 2 figures."
    )
    parser.add_argument(
        "--input-glob",
        default="mix/output/omnidma_hit_rate_summary_firstn*_table*.csv",
        help="Glob pattern of summary csv files.",
    )
    parser.add_argument(
        "--out-dir",
        default="mix/output/omnidma_hit_rate_compare",
        help="Output directory for plots and csv summaries.",
    )
    parser.add_argument(
        "--high-loss-threshold",
        type=float,
        default=0.02,
        help="Threshold for high-loss subset used in heatmap metric (default: 0.02).",
    )
    parser.add_argument(
        "--top-k-curves",
        type=int,
        default=4,
        help="Number of best configs to show in lossrate curve figure (baseline included separately).",
    )
    parser.add_argument(
        "--show-all-curves",
        action="store_true",
        help="Show all configs in curve figure instead of top-k.",
    )
    parser.add_argument(
        "--curve-cfgs",
        default="",
        help="Comma-separated cfg list for curve figure, e.g. F2-T2,F1-T2,F3-T2.",
    )
    return parser.parse_args()


def parse_cfg_from_filename(path: Path) -> Optional[Tuple[int, int]]:
    m = FILE_RE.search(path.name)
    if m is None:
        return None
    return int(m.group("firstn")), int(m.group("table"))


def load_and_derive(path: Path) -> Optional[pd.DataFrame]:
    try:
        df = pd.read_csv(path)
    except Exception as e:
        print(f"[WARN] skip {path}: failed to read ({e})")
        return None

    required = {
        "delay",
        "lossrate",
        "ll_access",
        "ll_hit",
        "table_access",
        "table_hit",
    }
    if not required.issubset(df.columns):
        print(f"[WARN] skip {path}: missing required columns {sorted(required)}")
        return None

    for col in ["delay", "lossrate", "ll_access", "ll_hit", "table_access", "table_hit"]:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=["delay", "lossrate", "ll_access", "ll_hit", "table_access", "table_hit"]).copy()
    if df.empty:
        print(f"[WARN] skip {path}: no valid numeric rows")
        return None

    total_access = df["ll_access"] + df["table_access"]
    total_hit = df["ll_hit"] + df["table_hit"]

    df["ll_hit_rate"] = df["ll_hit"] / df["ll_access"].replace(0, np.nan)
    df["table_hit_rate"] = df["table_hit"] / df["table_access"].replace(0, np.nan)
    df["overall_hit_rate"] = total_hit / total_access.replace(0, np.nan)
    df["table_access_share"] = df["table_access"] / total_access.replace(0, np.nan)
    return df


def build_summaries(
    paths: List[Path], high_loss_threshold: float
) -> Tuple[pd.DataFrame, pd.DataFrame]:
    file_rows: List[Dict[str, float]] = []
    loss_rows: List[pd.DataFrame] = []

    for p in paths:
        cfg = parse_cfg_from_filename(p)
        if cfg is None:
            print(f"[WARN] skip {p}: cannot parse firstn/table from filename")
            continue
        firstn, table = cfg
        df = load_and_derive(p)
        if df is None:
            continue

        cfg_name = f"F{firstn}-T{table}"
        high_loss = df[df["lossrate"] >= high_loss_threshold]
        if high_loss.empty:
            high_loss_mean = float("nan")
        else:
            high_loss_mean = float(high_loss["overall_hit_rate"].mean())

        file_rows.append(
            {
                "file": p.name,
                "firstn": firstn,
                "table": table,
                "cfg": cfg_name,
                "rows": int(len(df)),
                "overall_hit_rate_mean": float(df["overall_hit_rate"].mean()),
                "overall_hit_rate_high_loss_mean": high_loss_mean,
                "ll_hit_rate_mean": float(df["ll_hit_rate"].mean()),
                "table_hit_rate_mean": float(df["table_hit_rate"].mean()),
                "table_access_share_mean": float(df["table_access_share"].mean()),
                "ll_access_sum": float(df["ll_access"].sum()),
                "ll_hit_sum": float(df["ll_hit"].sum()),
                "table_access_sum": float(df["table_access"].sum()),
                "table_hit_sum": float(df["table_hit"].sum()),
            }
        )

        agg = (
            df.groupby(["lossrate"], as_index=False)
            .agg(
                {
                    "overall_hit_rate": "mean",
                    "ll_hit_rate": "mean",
                    "table_hit_rate": "mean",
                    "table_access_share": "mean",
                }
            )
            .sort_values("lossrate")
            .copy()
        )
        agg["firstn"] = firstn
        agg["table"] = table
        agg["cfg"] = cfg_name
        loss_rows.append(agg)

    if not file_rows:
        raise SystemExit("No valid input files found.")

    file_summary = pd.DataFrame(file_rows).sort_values(["firstn", "table"]).reset_index(drop=True)
    by_loss = pd.concat(loss_rows, ignore_index=True).sort_values(["firstn", "table", "lossrate"])
    return file_summary, by_loss


def annotate_heatmap(ax: plt.Axes, data: np.ndarray, fmt: str = "{:.3f}") -> None:
    for i in range(data.shape[0]):
        for j in range(data.shape[1]):
            v = data[i, j]
            txt = "-" if np.isnan(v) else fmt.format(v)
            ax.text(j, i, txt, ha="center", va="center", color="white", fontsize=10)


def plot_heatmap(file_summary: pd.DataFrame, out_png: Path) -> None:
    firstn_vals = sorted(file_summary["firstn"].unique().tolist())
    table_vals = sorted(file_summary["table"].unique().tolist())
    pivot = (
        file_summary.pivot_table(
            index="firstn",
            columns="table",
            values="overall_hit_rate_high_loss_mean",
            aggfunc="first",
        )
        .reindex(index=firstn_vals, columns=table_vals)
    )
    data = pivot.to_numpy(dtype=float)
    masked = np.ma.masked_invalid(data)

    fig, ax = plt.subplots(figsize=(6.5, 4.8), constrained_layout=True)
    im = ax.imshow(masked, aspect="auto", cmap="viridis", origin="lower")

    ax.set_title("Overall Hit Rate (High Loss Subset)")
    ax.set_xlabel("Lookup Table Size (table)")
    ax.set_ylabel("FirstN")
    ax.set_xticks(np.arange(len(table_vals)))
    ax.set_xticklabels([str(v) for v in table_vals])
    ax.set_yticks(np.arange(len(firstn_vals)))
    ax.set_yticklabels([str(v) for v in firstn_vals])

    annotate_heatmap(ax, data, fmt="{:.4f}")

    cbar = fig.colorbar(im, ax=ax, shrink=0.9)
    cbar.set_label("overall_hit_rate_high_loss_mean")

    fig.savefig(out_png, dpi=180)
    plt.close(fig)


def pick_curve_cfgs(
    file_summary: pd.DataFrame, top_k: int, show_all: bool, curve_cfgs: str
) -> List[str]:
    available_cfgs = file_summary.sort_values("overall_hit_rate_mean", ascending=False)["cfg"].tolist()
    available_set = set(available_cfgs)

    if curve_cfgs.strip():
        requested = [x.strip() for x in curve_cfgs.split(",") if x.strip()]
        valid = [x for x in requested if x in available_set]
        invalid = [x for x in requested if x not in available_set]
        if invalid:
            print(f"[WARN] ignore unknown cfg(s): {', '.join(invalid)}")
        if not valid:
            raise SystemExit("No valid cfg in --curve-cfgs.")
        return valid

    if show_all:
        return available_cfgs

    picked: List[str] = []
    baseline = "F1-T1"
    if baseline in available_set:
        picked.append(baseline)

    for cfg in available_cfgs:
        if cfg in picked:
            continue
        picked.append(cfg)
        if len(picked) >= max(1, top_k):
            break

    if baseline in available_set and baseline not in picked:
        picked.append(baseline)
    return picked


def plot_loss_curves(
    file_summary: pd.DataFrame,
    by_loss: pd.DataFrame,
    out_png: Path,
    top_k: int,
    show_all: bool,
    curve_cfgs: str,
) -> None:
    cfgs = pick_curve_cfgs(
        file_summary, top_k=top_k, show_all=show_all, curve_cfgs=curve_cfgs
    )
    cmap = plt.get_cmap("tab10")

    fig, ax = plt.subplots(figsize=(7.2, 4.8), constrained_layout=True)
    for idx, cfg in enumerate(cfgs):
        d = by_loss[by_loss["cfg"] == cfg].sort_values("lossrate")
        if d.empty:
            continue
        y = d["overall_hit_rate"] * 100.0  # percentage
        style = "--" if cfg == "F1-T1" else "-"
        lw = 2.8 if cfg == "F1-T1" else 2.0
        ax.plot(
            d["lossrate"],
            y,
            marker="o",
            linestyle=style,
            linewidth=lw,
            markersize=4.5,
            color=cmap(idx % 10),
            label=cfg,
        )

    ax.set_title("Overall Hit Rate vs Loss Rate")
    ax.set_xlabel("Loss Rate")
    ax.set_ylabel("Overall Hit Rate (%)")
    ax.grid(alpha=0.3)
    ax.legend(title="Config (F=firstN, T=table)", ncol=2, fontsize=9)

    fig.savefig(out_png, dpi=180)
    plt.close(fig)


def main() -> None:
    args = parse_args()
    paths = [Path(p) for p in sorted(Path().glob(args.input_glob))]
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not paths:
        raise SystemExit(f"No files matched: {args.input_glob}")

    file_summary, by_loss = build_summaries(paths, high_loss_threshold=args.high_loss_threshold)

    file_summary_csv = out_dir / "hitrate_compare_file_summary.csv"
    by_loss_csv = out_dir / "hitrate_compare_by_lossrate.csv"
    file_summary.to_csv(file_summary_csv, index=False)
    by_loss.to_csv(by_loss_csv, index=False)

    heatmap_png = out_dir / "hitrate_compare_heatmap.png"
    curves_png = out_dir / "hitrate_compare_lossrate_curves.png"
    plot_heatmap(file_summary, heatmap_png)
    plot_loss_curves(
        file_summary,
        by_loss,
        curves_png,
        top_k=args.top_k_curves,
        show_all=args.show_all_curves,
        curve_cfgs=args.curve_cfgs,
    )

    print(f"[OK] file summary csv : {file_summary_csv}")
    print(f"[OK] by-lossrate csv   : {by_loss_csv}")
    print(f"[OK] heatmap figure    : {heatmap_png}")
    print(f"[OK] curve figure      : {curves_png}")


if __name__ == "__main__":
    main()
