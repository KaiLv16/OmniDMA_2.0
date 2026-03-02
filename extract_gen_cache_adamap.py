#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from collections import Counter
from pathlib import Path


DEFAULT_DIR = (
    "mix/output/"
    "omnidma_topo_dumbbell_incast100_OS2_500us_"
    "flow_omni_64flows_dumbbell_avg1ms_var1ms_"
    "dropmnone_drop0.0_pfc0_irn0"
)
DEFAULT_INPUT_FILE = "omniDMA_event_output.txt"
DEFAULT_OUTPUT_FILE = "omniDMA_event_output_gen_and_cache_Adamap.txt"
DEFAULT_ANALYSIS_FILE = "omniDMA_event_output_gen_and_cache_Adamap_analysis.txt"
DEFAULT_PATTERN_PLOT_FILE = "omniDMA_event_output_pattern_loss_ratio.png"
DEFAULT_CDF_PLOT_FILE = "omniDMA_event_output_repr_length_cdf.png"
DEFAULT_KEYWORD = "gen and cache Adamap"

REP_LENGTH_RE = re.compile(r"Representation Length:\s*(\d+)")
BITMAP_RE = re.compile(r"Bitmap:\s*([01]+)\(size\(\)\s*=\s*(\d+)\)")


def split_event_blocks(lines: list[str]) -> list[list[str]]:
    blocks: list[list[str]] = []
    current: list[str] = []

    for line in lines:
        if line.startswith("[Event "):
            if current:
                blocks.append(current)
            current = [line]
        elif current:
            current.append(line)

    if current:
        blocks.append(current)

    return blocks


def count_switches(bitmap: str) -> int:
    if not bitmap:
        return 0
    return sum(1 for i in range(1, len(bitmap)) if bitmap[i] != bitmap[i - 1])


def parse_event_features(block_text: str) -> tuple[int, str, int]:
    rep_match = REP_LENGTH_RE.search(block_text)
    bitmap_match = BITMAP_RE.search(block_text)
    if not rep_match or not bitmap_match:
        raise ValueError("Cannot parse Representation Length or Bitmap from event block.")
    rep_length = int(rep_match.group(1))
    bitmap = bitmap_match.group(1)
    bitmap_size = int(bitmap_match.group(2))
    return rep_length, bitmap, bitmap_size


def calc_stat_representation_length(rep_length: int, bitmap: str, bitmap_size: int) -> int:
    # For stats only: if rep_length equals bitmap size, use the span between
    # first and last '0' in bitmap (inclusive); if no '0' exists, return 0.
    if rep_length == bitmap_size:
        first_zero = bitmap.find("0")
        if first_zero < 0:
            return 0
        last_zero = bitmap.rfind("0")
        return last_zero - first_zero + 1
    return rep_length


def classify_loss_pattern(
    rep_length: int,
    bitmap: str,
    bitmap_size: int,
    n_zeros_random: int,
    switch_high: int,
    n_zeros_continue: int,
    switch_continue: int,
) -> int:
    expanded = expand_adamap_sequence(rep_length, bitmap, bitmap_size)
    zeros = expanded.count("0")
    switches = count_switches(expanded)

    if rep_length == bitmap_size and zeros < n_zeros_random:
        return 1
    if rep_length >= bitmap_size and switches > switch_high:
        return 2

    is_pattern3 = (
        rep_length >= bitmap_size
        and zeros > n_zeros_continue
        and switches < switch_continue
    )
    if is_pattern3:
        return 3
    if rep_length > bitmap_size and not is_pattern3:
        return 4
    return 5


def calc_loss_units(rep_length: int, bitmap: str, bitmap_size: int) -> int:
    expanded = expand_adamap_sequence(rep_length, bitmap, bitmap_size)
    return expanded.count("0")


def expand_adamap_sequence(rep_length: int, bitmap: str, bitmap_size: int) -> str:
    overflow = max(rep_length - bitmap_size, 0)
    return bitmap + ("0" * overflow)


def build_analysis_text(
    length_counter: Counter[int],
    pattern_counter: Counter[int],
    pattern_loss_units: Counter[int],
    total_valid: int,
    total_matched: int,
    parse_failed: int,
    n_zeros_random: int,
    switch_high: int,
    n_zeros_continue: int,
    switch_continue: int,
) -> str:
    lines: list[str] = []
    lines.append("Analysis for 'gen and cache Adamap' events")
    lines.append("")
    lines.append("Parameters:")
    lines.append(f"n_zeros_random={n_zeros_random}")
    lines.append(f"switch_high={switch_high}")
    lines.append(f"n_zeros_continue={n_zeros_continue}")
    lines.append(f"switch_continue={switch_continue}")
    lines.append("")
    lines.append("Summary:")
    lines.append(f"matched_events={total_matched}")
    lines.append(f"parsed_events={total_valid}")
    lines.append(f"parse_failed_events={parse_failed}")
    lines.append("")
    lines.append("1) Representation Length Frequency")
    lines.append(
        "Rule note: if rep_len==bitmap_size, stat_len = span(first_zero,last_zero) in bitmap."
    )
    for length in sorted(length_counter):
        lines.append(f"length={length}\tcount={length_counter[length]}")
    lines.append("")
    lines.append("2) Loss Pattern Frequency")
    lines.append(
        "Rule note: zeros/switches are computed on expanded adamap = "
        "bitmap + trailing zeros for max(rep_len-bitmap_size, 0)."
    )
    lines.append(
        "pattern1(scattered): length==bitmap_size and zeros<n_zeros_random"
    )
    lines.append("pattern2(high-frequency): length>=bitmap_size and switches>switch_high")
    lines.append(
        "pattern3(consecutive): length>=bitmap_size and "
        "zeros>n_zeros_continue and switches<switch_continue"
    )
    lines.append("pattern4(len-gt-bitmap-and-not-pattern3): length>bitmap_size and not pattern3")
    lines.append("pattern5(other): not pattern1/2/3/4")
    for pattern_id in (1, 2, 3, 4, 5):
        count = pattern_counter.get(pattern_id, 0)
        loss_units_sum = pattern_loss_units.get(pattern_id, 0)
        ratio = 0.0 if total_valid == 0 else (count * 100.0 / total_valid)
        lines.append(
            f"pattern{pattern_id}\tcount={count}\tratio={ratio:.2f}%"
            f"\tloss_units_sum={loss_units_sum}"
        )

    return "\n".join(lines) + "\n"


def plot_pattern_loss_ratio(
    pattern_counter: Counter[int],
    pattern_loss_units: Counter[int],
    total_valid: int,
    output_path: Path,
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise RuntimeError("matplotlib is required for plotting.") from exc

    pattern_ids = [1, 2, 3, 4, 5]
    labels = [f"P{i}" for i in pattern_ids]
    loss_values = [pattern_loss_units.get(i, 0) for i in pattern_ids]
    ratio_values = [
        (pattern_counter.get(i, 0) * 100.0 / total_valid) if total_valid else 0.0
        for i in pattern_ids
    ]

    # Log-scale bars cannot render zero. Keep 0 in labels but plot a tiny placeholder.
    loss_plot_values = [v if v > 0 else 0.8 for v in loss_values]

    fig, ax1 = plt.subplots(figsize=(8, 5))
    bars = ax1.bar(labels, loss_plot_values, color="#4E79A7", alpha=0.9)
    ax1.set_yscale("log")
    ax1.set_ylabel("Loss Units Sum (log scale)")
    ax1.set_xlabel("Loss Pattern")
    ax1.set_title("Loss Pattern: Loss Units and Ratio")

    for bar, real_value in zip(bars, loss_values):
        ax1.text(
            bar.get_x() + bar.get_width() / 2.0,
            bar.get_height(),
            str(real_value),
            ha="center",
            va="bottom",
            fontsize=8,
        )

    ax2 = ax1.twinx()
    ax2.plot(labels, ratio_values, color="#E15759", marker="o", linewidth=2)
    ax2.set_ylabel("Ratio (%)")
    ax2.set_ylim(0, max(ratio_values + [1.0]) * 1.15)

    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    plt.close(fig)


def plot_representation_length_cdf(
    length_counter: Counter[int],
    output_path: Path,
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise RuntimeError("matplotlib is required for plotting.") from exc

    fig, ax = plt.subplots(figsize=(8, 5))

    total = sum(length_counter.values())
    if total == 0:
        ax.text(0.5, 0.5, "No data", ha="center", va="center", transform=ax.transAxes)
        ax.set_title("Representation Length CDF")
    else:
        zero_count = length_counter.get(0, 0)
        positive_counter = Counter({k: v for k, v in length_counter.items() if k > 0})
        lengths = sorted(positive_counter)
        cum = 0
        xs: list[int] = []
        ys: list[float] = []
        for length in lengths:
            cum += positive_counter[length]
            xs.append(length)
            ys.append(cum / total)
        ax.step(xs, ys, where="post", color="#59A14F", linewidth=2)
        ax.set_xscale("log")
        ax.set_ylim(0.0, 1.02)
        if zero_count > 0:
            ax.set_title(f"Representation Length CDF (x=log, zero_count={zero_count})")
        else:
            ax.set_title("Representation Length CDF (x=log)")
        ax.set_xlabel("Representation Length")
        ax.set_ylabel("CDF")
        ax.grid(True, linestyle="--", alpha=0.35)

    fig.tight_layout()
    fig.savefig(output_path, dpi=200)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Extract event records containing a keyword from omniDMA_event_output.txt "
            "and save them in the same directory."
        )
    )
    parser.add_argument(
        "--dir",
        default=DEFAULT_DIR,
        help="Directory containing the input event file.",
    )
    parser.add_argument(
        "--input",
        default=DEFAULT_INPUT_FILE,
        help="Input file name in the target directory.",
    )
    parser.add_argument(
        "--output",
        default=DEFAULT_OUTPUT_FILE,
        help="Output file name in the target directory.",
    )
    parser.add_argument(
        "--keyword",
        default=DEFAULT_KEYWORD,
        help="Keyword used to filter event records.",
    )
    parser.add_argument(
        "--analysis-output",
        default=DEFAULT_ANALYSIS_FILE,
        help="Analysis output file name in the target directory.",
    )
    parser.add_argument(
        "--pattern-plot-output",
        default=DEFAULT_PATTERN_PLOT_FILE,
        help="Output file name for pattern loss/ratio plot.",
    )
    parser.add_argument(
        "--cdf-plot-output",
        default=DEFAULT_CDF_PLOT_FILE,
        help="Output file name for representation length CDF plot.",
    )
    parser.add_argument(
        "--n-zeros-random",
        type=int,
        default=4,
        help="Threshold for pattern1: zeros < n_zeros_random.",
    )
    parser.add_argument(
        "--switch-high",
        type=int,
        default=8,
        help="Threshold for pattern2: switches > switch_high.",
    )
    parser.add_argument(
        "--n-zeros-continue",
        type=int,
        default=8,
        help="Threshold for pattern3: zeros > n_zeros_continue.",
    )
    parser.add_argument(
        "--switch-continue",
        type=int,
        default=4,
        help="Threshold for pattern3: switches < switch_continue.",
    )
    args = parser.parse_args()

    target_dir = Path(args.dir)
    input_path = target_dir / args.input
    output_path = target_dir / args.output
    analysis_path = target_dir / args.analysis_output
    pattern_plot_path = target_dir / args.pattern_plot_output
    cdf_plot_path = target_dir / args.cdf_plot_output

    if not input_path.exists():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    lines = input_path.read_text(encoding="utf-8", errors="replace").splitlines(
        keepends=True
    )
    blocks = split_event_blocks(lines)

    matched_blocks = [block for block in blocks if args.keyword in "".join(block)]

    output_text = ""
    if matched_blocks:
        output_text = "\n\n".join(
            "".join(block).rstrip("\n") for block in matched_blocks
        )
        output_text += "\n"

    output_path.write_text(output_text, encoding="utf-8")

    length_counter: Counter[int] = Counter()
    pattern_counter: Counter[int] = Counter()
    pattern_loss_units: Counter[int] = Counter()
    parse_failed = 0

    for block in matched_blocks:
        block_text = "".join(block)
        try:
            rep_length, bitmap, bitmap_size = parse_event_features(block_text)
        except ValueError:
            parse_failed += 1
            continue
        stat_length = calc_stat_representation_length(
            rep_length=rep_length, bitmap=bitmap, bitmap_size=bitmap_size
        )
        length_counter[stat_length] += 1
        pattern_id = classify_loss_pattern(
            rep_length=rep_length,
            bitmap=bitmap,
            bitmap_size=bitmap_size,
            n_zeros_random=args.n_zeros_random,
            switch_high=args.switch_high,
            n_zeros_continue=args.n_zeros_continue,
            switch_continue=args.switch_continue,
        )
        pattern_counter[pattern_id] += 1
        pattern_loss_units[pattern_id] += calc_loss_units(
            rep_length=rep_length, bitmap=bitmap, bitmap_size=bitmap_size
        )

    total_valid = len(matched_blocks) - parse_failed
    analysis_text = build_analysis_text(
        length_counter=length_counter,
        pattern_counter=pattern_counter,
        pattern_loss_units=pattern_loss_units,
        total_valid=total_valid,
        total_matched=len(matched_blocks),
        parse_failed=parse_failed,
        n_zeros_random=args.n_zeros_random,
        switch_high=args.switch_high,
        n_zeros_continue=args.n_zeros_continue,
        switch_continue=args.switch_continue,
    )
    analysis_path.write_text(analysis_text, encoding="utf-8")
    plot_pattern_loss_ratio(
        pattern_counter=pattern_counter,
        pattern_loss_units=pattern_loss_units,
        total_valid=total_valid,
        output_path=pattern_plot_path,
    )
    plot_representation_length_cdf(
        length_counter=length_counter,
        output_path=cdf_plot_path,
    )

    print(f"Input: {input_path}")
    print(f"Total event records: {len(blocks)}")
    print(f"Matched records ({args.keyword!r}): {len(matched_blocks)}")
    print(f"Output: {output_path}")
    print(f"Analysis output: {analysis_path}")
    print(f"Pattern plot: {pattern_plot_path}")
    print(f"CDF plot: {cdf_plot_path}")


if __name__ == "__main__":
    main()
