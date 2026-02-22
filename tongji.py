#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys

def main():
    if len(sys.argv) != 2:
        print("用法: python tongji.py 要统计的字符串")
        sys.exit(1)

    # 获取命令行传入的目标字符串
    target = sys.argv[1]
    
    try:
        with open("mix/output/omnidma/omnidma_snd_rcv_record_file.txt", "r", encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        print("错误: 未找到 input.txt 文件。")
        sys.exit(1)

    # 统计字符串出现次数（注意统计的是不重叠的匹配）
    count = text.count(target)
    print(f"字符串 \"{target}\" 出现了 {count} 次。")

if __name__ == '__main__':
    main()
