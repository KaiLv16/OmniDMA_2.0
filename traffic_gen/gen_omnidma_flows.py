#!/usr/bin/env python3
"""Generate OmniDMA flow files for incast/dumbbell experiments.

Output format:
  First line: number of flows
  Following lines: "src dst 3 size start_time"

Start times are generated in seconds. The first flow starts at `base_time_s`,
and each subsequent flow time is formed by adding a sampled inter-arrival.
To support independently configurable inter-arrival mean/variance, this script
samples inter-arrivals from a Gamma distribution (which matches the requested
mean and variance exactly in expectation). When variance is 0, the inter-arrival
is deterministic (= mean).
"""

import argparse
import math
import os
import random


DEFAULT_COUNTS = [1, 2, 4, 8, 16, 32, 64, 100]


def parse_counts(text):
    if not text:
        return list(DEFAULT_COUNTS)
    counts = []
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        value = int(item)
        if value <= 0:
            raise ValueError("flow count must be > 0")
        counts.append(value)
    if not counts:
        raise ValueError("counts cannot be empty")
    return counts


def fmt_label_ms(value):
    if float(value).is_integer():
        return str(int(value)) + "ms"
    s = ("%.6f" % float(value)).rstrip("0").rstrip(".")
    return s + "ms"


def sample_interval_seconds(mean_ms, var_ms):
    if mean_ms < 0:
        raise ValueError("mean must be >= 0")
    if var_ms < 0:
        raise ValueError("variance must be >= 0")

    mean_s = mean_ms / 1000.0
    var_s2 = var_ms / 1000.0 / 1000.0  # treat input numerically as ms^2 -> s^2

    if var_ms == 0:
        return mean_s
    if mean_ms == 0:
        raise ValueError("mean must be > 0 when variance > 0")

    shape = (mean_s * mean_s) / var_s2
    scale = var_s2 / mean_s
    return random.gammavariate(shape, scale)


def build_start_times(nflows, mean_ms, var_ms, base_time_s):
    if nflows <= 0:
        return []
    times = [base_time_s]
    for _ in range(1, nflows):
        times.append(times[-1] + sample_interval_seconds(mean_ms, var_ms))
    return times


def write_flow_file(path, flows):
    with open(path, "w") as f:
        f.write("%d\n" % len(flows))
        for src, dst, size, start_time in flows:
            f.write("%d %d 3 %d %.9f\n" % (src, dst, size, start_time))


def build_incast_flows(nflows, flow_size, mean_ms, var_ms, base_time_s):
    flows = []
    start_times = build_start_times(nflows, mean_ms, var_ms, base_time_s)
    for src, t in enumerate(start_times):
        flows.append((src, 100, flow_size, t))
    return flows


def build_dumbbell_flows(nflows, flow_size, mean_ms, var_ms, base_time_s):
    flows = []
    start_times = build_start_times(nflows, mean_ms, var_ms, base_time_s)
    for src, t in enumerate(start_times):
        flows.append((src, 100 + src, flow_size, t))
    return flows


def ensure_dir(path):
    if not os.path.isdir(path):
        os.makedirs(path)


def main():
    parser = argparse.ArgumentParser(
        description="Generate OmniDMA incast/dumbbell flow files."
    )
    parser.add_argument(
        "--mode",
        choices=["incast", "dumbbell", "all"],
        default="all",
        help="which pattern(s) to generate (default: all)",
    )
    parser.add_argument(
        "--counts",
        default="1,2,4,8,16,32,64,100",
        help="comma-separated flow counts to generate",
    )
    parser.add_argument(
        "--avg-ms",
        type=float,
        default=1.0,
        help="mean inter-arrival time in ms (default: 1.0)",
    )
    parser.add_argument(
        "--var-ms",
        type=float,
        default=1.0,
        help="inter-arrival variance in ms^2 (default: 1.0)",
    )
    parser.add_argument(
        "--flow-size",
        type=int,
        default=1000000000,
        help="flow size in bytes (default: 1000000000)",
    )
    parser.add_argument(
        "--base-time-s",
        type=float,
        default=2.0,
        help="base time offset in seconds (default: 2.0)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=None,
        help="random seed (default: unset)",
    )
    parser.add_argument(
        "--output-dir",
        default=os.path.join(os.path.dirname(os.path.dirname(__file__)), "config"),
        help="directory for generated flow files (default: repo config/)",
    )
    args = parser.parse_args()

    if args.flow_size <= 0:
        raise ValueError("flow-size must be > 0")

    counts = parse_counts(args.counts)
    if any(c > 100 for c in counts):
        raise ValueError("this generator expects x <= 100")

    if args.seed is not None:
        random.seed(args.seed)

    ensure_dir(args.output_dir)

    avg_label = fmt_label_ms(args.avg_ms)
    var_label = fmt_label_ms(args.var_ms)

    generated = []

    if args.mode in ("incast", "all"):
        for x in counts:
            flows = build_incast_flows(
                x, args.flow_size, args.avg_ms, args.var_ms, args.base_time_s
            )
            name = "flow_omni_%dflows_incast_avg%s_var%s.txt" % (
                x,
                avg_label,
                var_label,
            )
            path = os.path.join(args.output_dir, name)
            write_flow_file(path, flows)
            generated.append(path)

    if args.mode in ("dumbbell", "all"):
        for x in counts:
            flows = build_dumbbell_flows(
                x, args.flow_size, args.avg_ms, args.var_ms, args.base_time_s
            )
            name = "flow_omni_%dflows_dumbbell_avg%s_var%s.txt" % (
                x,
                avg_label,
                var_label,
            )
            path = os.path.join(args.output_dir, name)
            write_flow_file(path, flows)
            generated.append(path)

    for path in generated:
        print(path)


if __name__ == "__main__":
    main()
