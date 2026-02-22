#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys

def parse_drop_file(filename, w=8):
    """解析丢包日志文件，统计丢包数据，并使用 bitmap 记录丢包"""
    drop_counts = {1: 0, 2: 0, 3: 0}
    total_lines = 0
    bitmaps = []  # 存储所有 bitmap
    current_bitmap = [1] * w  # 活动 bitmap，初始化全 1
    current_start_idx = None  # 当前 bitmap 的起始序列号
    bitmap_length = w  # bitmap 逻辑表示的范围（起始序列号 + bitmap_length）

    try:
        with open(filename, "r") as fin:
            pkt_list = []
            for line in fin:
                parts = line.strip().split()
                if len(parts) < 6:
                    continue
                try:
                    pkt_idx = int(parts[4])  # 第五列是 pkt_idx
                    drop_type = int(parts[5])  # 第六列是丢包类型
                except ValueError:
                    continue

                pkt_list.append(pkt_idx)

                if drop_type in drop_counts:
                    drop_counts[drop_type] += 1
                total_lines += 1

            pkt_list.sort()  # 确保丢包序列是有序的

            i = 0
            while i < len(pkt_list):
                pkt_idx = pkt_list[i]
                
                if current_start_idx is None:
                    # 初始化新的 bitmap
                    current_start_idx = pkt_idx - 1
                    current_bitmap = [1] * w
                    bitmap_length = w  # 初始长度 w

                pos = pkt_idx - current_start_idx - 1  # 计算在 bitmap 内的相对位置

                if 0 <= pos < w:
                    # pkt_idx 在 bitmap 内
                    current_bitmap[pos] = 0
                elif pos == bitmap_length:
                    # pkt_idx 刚好是 bitmap 长度边界，检查是否可以扩展“表示长度”
                    extend_count = 0
                    while (i + extend_count < len(pkt_list) and 
                           pkt_list[i + extend_count] == pkt_idx + extend_count):
                        extend_count += 1
                    i += extend_count - 1

                    # 增加 bitmap "表示长度"，但不扩展实际 bitmap
                    bitmap_length += extend_count
                else:
                    # 存储当前 bitmap，并开启新 bitmap
                    bitmaps.append((current_start_idx, bitmap_length, current_bitmap[:]))  # 存储副本
                    current_start_idx = pkt_idx - 1
                    bitmap_length = w
                    current_bitmap = [1] * w
                    current_bitmap[0] = 0  # 记录新 bitmap 的第一个丢包

                i += 1

        # 存储最后一个 bitmap
        if current_start_idx is not None:
            bitmaps.append((current_start_idx, bitmap_length, current_bitmap[:]))

    except FileNotFoundError:
        print(f"错误：文件 '{filename}' 未找到！")
        sys.exit(1)
    except Exception as e:
        print(f"读取文件时出错: {e}")
        sys.exit(1)

    return drop_counts, total_lines, bitmaps

def main():
    if len(sys.argv) != 2:
        print("用法: python3 parse_drop.py <filename>")
        sys.exit(1)

    filename = sys.argv[1]
    
    
    w = 16  # 配置 bitmap 长度
    
    
    drop_counts, total_lines, bitmaps = parse_drop_file(filename, w)

    if total_lines == 0:
        print("文件为空或没有有效数据")
        return

    print(f"分析文件: {filename}")
    print("总行数（记录包总数）：", total_lines)
    print("各类型丢包统计及比例：")
    for dt in sorted(drop_counts.keys()):
        count = drop_counts[dt]
        percentage = (count / total_lines) * 100
        print(f"  类型 {dt}: {count} 个包, 占 {percentage:.2f}%")

    print("\nBitmap 记录:")
    for idx, (start_idx, length, bitmap) in enumerate(bitmaps):
        bitmap_str = ''.join(map(str, bitmap))  # 仅显示 bitmap
        print(f"  Bitmap {idx+1}: 起始序列号={start_idx - 1}, 长度={length}, Bitmap={bitmap_str}")

if __name__ == '__main__':
    main()
