import argparse
import os
import re

import matplotlib.font_manager as fm
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Plot per-flow send rate and CC window from snd_rcv_record_file"
    )
    parser.add_argument(
        "--base_folder",
        default="mix/output/Strack/FatTree_k4-topo_test-flow_test--map208-fl3",
        help="Folder containing snd_rcv_record_file.txt",
    )
    parser.add_argument(
        "--snd_rcv_file",
        default=None,
        help="Path to snd_rcv_record_file.txt (overrides --base_folder)",
    )
    parser.add_argument(
        "--bucket",
        type=float,
        default=10.0,
        help="Throughput bucket size in us",
    )
    parser.add_argument(
        "--flowids",
        default=None,
        help="Comma-separated flow IDs to plot, e.g. --flowids 1,2,3,4 (default: all)",
    )
    parser.add_argument(
        "--output_subdir",
        default="flow_rate_plots",
        help="Subdirectory (under snd_rcv file folder) to save generated PNGs",
    )
    parser.add_argument(
        "--x_min_us",
        type=float,
        default=None,
        help="Optional x-axis lower bound in us (relative to first send packet)",
    )
    parser.add_argument(
        "--x_max_us",
        type=float,
        default=None,
        help="Optional x-axis upper bound in us (relative to first send packet)",
    )
    return parser.parse_args()


def configure_font():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    font_path = os.path.join(script_dir, "times_simsun.ttf")
    if os.path.exists(font_path):
        fm.fontManager.addfont(font_path)
        font_prop = fm.FontProperties(fname=font_path)
        plt.rcParams["font.family"] = font_prop.get_name()
        plt.rcParams["axes.unicode_minus"] = False
        plt.rcParams["pdf.fonttype"] = 42
        plt.rcParams["ps.fonttype"] = 42
        plt.rcParams["mathtext.fontset"] = "custom"
        plt.rcParams["mathtext.rm"] = font_prop.get_name()
        print(f"成功加载并全局应用字体: {font_prop.get_name()}")
    else:
        print(f"警告：未找到字体文件 {font_path}，将使用系统默认字体")


NEW_FMT_RE = re.compile(
    r"^\s*(?P<ts>\d+):\s*host\s+(?P<host>\d+)\s+nic\s+(?P<nic>\d+)\s+flow\s+(?P<flow>-?\d+)\s+"
    r"(?P<action>send|recv)\s+a\s+(?P<pkt>[A-Za-z0-9_]+)\s+packet,\s+cc_win_size=(?P<win>\d+)"
    r".*?\bomniType=(?P<omni>-?\d+)\b.*?\bsize=(?P<size>\d+)\b.*?\bseq=(?P<seq>-?\d+)\b",
    re.IGNORECASE,
)

OLD_FMT_RE = re.compile(
    r"^\s*(?P<ts>\d+):\s*host\s+(?P<host>\d+)\s+NIC\s+(?P<nic>\d+)\s+(?P<action>send|recv)\s+a\s+"
    r"(?P<pkt>[A-Za-z0-9_]+)\s+pkt\.\s+omniType=(?P<omni>-?\d+)\s+size=(?P<size>\d+)\s+flowid=(?P<flow>-?\d+)\s+seq=(?P<seq>-?\d+)",
    re.IGNORECASE,
)


def parse_flowids(flowids_arg):
    if flowids_arg is None or flowids_arg.strip() == "":
        return None
    result = set()
    for item in flowids_arg.split(","):
        item = item.strip()
        if not item:
            continue
        result.add(int(item))
    return result if result else None


def read_snd_rcv_log(filepath):
    rows = []
    with open(filepath, "r") as f:
        for line_no, line in enumerate(f, 1):
            m = NEW_FMT_RE.match(line)
            if m is not None:
                rows.append(
                    {
                        "Time_ns": int(m.group("ts")),
                        "Host": int(m.group("host")),
                        "Nic": int(m.group("nic")),
                        "Flow": int(m.group("flow")),
                        "Action": m.group("action").lower(),
                        "PacketType": m.group("pkt"),
                        "CcWin": int(m.group("win")),
                        "OmniType": int(m.group("omni")),
                        "Size": int(m.group("size")),
                        "Seq": int(m.group("seq")),
                    }
                )
                continue

            m = OLD_FMT_RE.match(line)
            if m is not None:
                rows.append(
                    {
                        "Time_ns": int(m.group("ts")),
                        "Host": int(m.group("host")),
                        "Nic": int(m.group("nic")),
                        "Flow": int(m.group("flow")),
                        "Action": m.group("action").lower(),
                        "PacketType": m.group("pkt"),
                        "CcWin": 0,
                        "OmniType": int(m.group("omni")),
                        "Size": int(m.group("size")),
                        "Seq": int(m.group("seq")),
                    }
                )
                continue

            if line.strip():
                # Keep parser tolerant to unrelated debug output.
                pass

    if not rows:
        return pd.DataFrame(
            columns=[
                "Time_ns",
                "Host",
                "Nic",
                "Flow",
                "Action",
                "PacketType",
                "CcWin",
                "OmniType",
                "Size",
                "Seq",
            ]
        )

    df = pd.DataFrame(rows)
    df["Time_us"] = df["Time_ns"] / 1000.0
    return df.sort_values(["Host", "Flow", "Time_ns", "Seq"]).reset_index(drop=True)


def calculate_throughput(df_send, bucket_size_us):
    if df_send.empty:
        return pd.DataFrame(columns=["Time_us", "Throughput_Gbps"])

    bucket_ids = (df_send["Time_us"] // bucket_size_us).astype("int64")
    bucket_sum = df_send.groupby(bucket_ids)["Size"].sum().sort_index()
    if bucket_sum.empty:
        return pd.DataFrame(columns=["Time_us", "Throughput_Gbps"])

    all_buckets = np.arange(bucket_sum.index.min(), bucket_sum.index.max() + 1)
    bucket_sum = bucket_sum.reindex(all_buckets, fill_value=0)

    throughput_gbps = (bucket_sum.values * 8.0) / (bucket_size_us * 1000.0)
    return pd.DataFrame(
        {
            "Time_us": all_buckets.astype(float) * bucket_size_us,
            "Throughput_Gbps": throughput_gbps,
        }
    )


def build_window_series(df_send):
    if df_send.empty:
        return pd.DataFrame(columns=["Time_us", "CcWin"])

    # Use send-time samples; duplicate timestamps keep the latest observed window.
    df_win = (
        df_send[["Time_us", "CcWin"]]
        .sort_values("Time_us")
        .drop_duplicates(subset=["Time_us"], keep="last")
        .reset_index(drop=True)
    )
    return df_win


def plot_rate_and_window(host_id, flow_id, df_send, bucket_us, output_path, x_min_us=None, x_max_us=None):
    df_send = df_send.sort_values("Time_us").reset_index(drop=True)
    if df_send.empty:
        return False

    start_us = df_send["Time_us"].iloc[0]
    tput = calculate_throughput(df_send, bucket_us)
    win = build_window_series(df_send)

    if not tput.empty:
        tput = tput.copy()
        tput["Time_us"] = tput["Time_us"] - start_us
    if not win.empty:
        win = win.copy()
        win["Time_us"] = win["Time_us"] - start_us

    fig, ax1 = plt.subplots(figsize=(12, 7))
    ax2 = ax1.twinx()

    lines = []
    labels = []

    if not tput.empty:
        line1 = ax1.plot(
            tput["Time_us"],
            tput["Throughput_Gbps"],
            color="#1f77b4",
            linewidth=2.0,
            label="发送速率 (Gbps)",
        )[0]
        lines.append(line1)
        labels.append(line1.get_label())

    if not win.empty:
        line2 = ax2.step(
            win["Time_us"],
            win["CcWin"],
            where="post",
            color="#d62728",
            linewidth=1.8,
            linestyle="--",
            label="CC窗口 (Bytes)",
        )[0]
        lines.append(line2)
        labels.append(line2.get_label())

    ax1.set_xlabel("时间 (us, 相对首包发送)")
    ax1.set_ylabel("发送速率 (Gbps)", color="#1f77b4")
    ax2.set_ylabel("CC窗口大小 (Bytes)", color="#d62728")
    ax1.grid(True, linestyle=":", alpha=0.5)
    ax1.set_title(f"Host {host_id} Flow {flow_id} 发送速率与窗口变化")

    if not tput.empty:
        ax1.set_ylim(bottom=0)
    if not win.empty:
        ax2.set_ylim(bottom=0)
    if x_min_us is not None or x_max_us is not None:
        ax1.set_xlim(left=x_min_us, right=x_max_us)

    if lines:
        ax1.legend(lines, labels, loc="upper right")

    plt.tight_layout()
    plt.savefig(output_path, dpi=200)
    plt.close(fig)
    return True


def main():
    args = parse_arguments()
    configure_font()

    snd_rcv_file = args.snd_rcv_file or os.path.join(args.base_folder, "snd_rcv_record_file.txt")
    snd_rcv_file = os.path.abspath(snd_rcv_file)
    base_out_dir = os.path.dirname(snd_rcv_file)
    out_dir = os.path.join(base_out_dir, args.output_subdir)

    if not os.path.exists(snd_rcv_file):
        print(f"[Error] snd_rcv_record file not found: {snd_rcv_file}")
        return

    if args.bucket <= 0:
        print("[Error] --bucket must be > 0")
        return
    if (
        args.x_min_us is not None
        and args.x_max_us is not None
        and args.x_min_us > args.x_max_us
    ):
        print("[Error] --x_min_us must be <= --x_max_us")
        return

    os.makedirs(out_dir, exist_ok=True)
    print(f"[Info] 图像输出目录: {out_dir}")
    if args.x_min_us is not None or args.x_max_us is not None:
        print(f"[Info] 横轴范围: [{args.x_min_us}, {args.x_max_us}] us (相对首包发送)")

    target_flowids = parse_flowids(args.flowids)
    if target_flowids is not None:
        print(f"[Info] 仅分析 flowids: {sorted(target_flowids)}")
    else:
        print("[Info] 分析所有 flowid")

    print(f"[Info] 读取日志: {snd_rcv_file}")
    df = read_snd_rcv_log(snd_rcv_file)
    if df.empty:
        print("[Warning] 未解析到任何 snd/rcv 记录")
        return

    # Sender throughput/window analysis: host send-side UDP packets only.
    df_send = df[(df["Action"] == "send") & (df["PacketType"].str.upper() == "UDP") & (df["Flow"] >= 0)].copy()

    if target_flowids is not None:
        df_send = df_send[df_send["Flow"].isin(target_flowids)].copy()

    if df_send.empty:
        print("[Warning] 没有匹配到可分析的发送数据包（send + UDP + flowid）")
        return

    generated = 0
    for (host_id, flow_id), group in df_send.groupby(["Host", "Flow"], sort=True):
        output_path = os.path.join(out_dir, f"rate_host{host_id}_flow{flow_id}.png")
        ok = plot_rate_and_window(
            host_id,
            flow_id,
            group,
            args.bucket,
            output_path,
            x_min_us=args.x_min_us,
            x_max_us=args.x_max_us,
        )
        if ok:
            generated += 1
            print(f"[OK] 保存图像: {output_path}")

    if generated == 0:
        print("[Warning] 没有生成任何图像")
    else:
        print(f"[Done] 共生成 {generated} 张图")


if __name__ == "__main__":
    main()
