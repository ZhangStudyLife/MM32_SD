#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
解析 SD 卡根目录下的 CMPRS 目录中的 .dat 录制文件。
- 每帧为原始 8bit 灰度数据，分辨率默认为 188x120（MT9V03X）。
- 将每个 .dat 按帧切分，导出为 PNG 或 BMP 序列（首选 PNG，兼容性好、体积小）。
- 若缺少 Pillow，则自动退化为 PGM（几乎所有图像工具都能打开）。

用法示例：
    将本脚本拷贝到 SD 卡根目录（与 CMPRS 同级），双击运行或用 Python 执行。
    可选参数请运行：python parse_cmprs.py -h  查看。
"""

import os
import sys
import argparse

WIDTH = 188
HEIGHT = 120
FRAME_BYTES = WIDTH * HEIGHT

# 尝试导入 Pillow，用于保存 PNG/BMP；若无则退化到 PGM 输出
try:
    from PIL import Image  # type: ignore
    HAVE_PIL = True
except Exception:
    Image = None
    HAVE_PIL = False


def find_cmprs(root: str) -> str:
    """在 root 下寻找 CMPRS/cmprs 目录，返回路径；找不到则抛异常。"""
    candidates = [
        os.path.join(root, "CMPRS"),
        os.path.join(root, "cmprs"),
        os.path.join(root, "Cmprs"),
    ]
    for p in candidates:
        if os.path.isdir(p):
            return p
    raise FileNotFoundError("未找到 CMPRS 目录，请确认目录名为 CMPRS 并位于 SD 根目录下")


def list_dat_files(cmprs_dir: str):
    files = []
    for name in sorted(os.listdir(cmprs_dir)):
        if name.lower().endswith(".dat"):
            files.append(os.path.join(cmprs_dir, name))
    return files


def ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)


def save_frame_png_or_bmp(frame_bytes: bytes, w: int, h: int, out_path: str):
    if not HAVE_PIL:
        # 退化为 PGM（二进制），无需第三方库
        # P5\n<width> <height>\n<maxval>\n<raw data>
        with open(out_path.rsplit(".", 1)[0] + ".pgm", "wb") as f:
            header = f"P5\n{w} {h}\n255\n".encode("ascii")
            f.write(header)
            f.write(frame_bytes)
        return
    # 使用 Pillow 保存
    img = Image.frombytes("L", (w, h), frame_bytes)
    # 确保目录存在
    ensure_dir(os.path.dirname(out_path))
    img.save(out_path)


def extract_dat(dat_path: str, out_dir: str, fmt: str = "png", start: int = 0, count: int = -1):
    """
    将 dat 文件拆分为帧图片。
    - dat_path: .dat 文件路径
    - out_dir : 输出目录（会在其中创建同名子目录）
    - fmt     : png 或 bmp
    - start   : 从第几帧开始导出（0 基）
    - count   : 导出帧数，-1 表示导出到文件末尾
    """
    base = os.path.splitext(os.path.basename(dat_path))[0]
    out_subdir = os.path.join(out_dir, base)
    ensure_dir(out_subdir)

    size = os.path.getsize(dat_path)
    total = size // FRAME_BYTES
    leftover = size % FRAME_BYTES
    if leftover:
        print(f"[WARN] 文件大小不是帧大小的整数倍：{dat_path}，将忽略末尾 {leftover} 字节")
    if start >= total:
        print(f"[INFO] start({start}) >= total({total})，跳过 {dat_path}")
        return 0

    if count < 0:
        count = total - start
    end = min(start + count, total)

    print(f"[INFO] 解析 {dat_path} -> {out_subdir}，总帧数={total}，导出范围=[{start},{end})，格式={fmt}")

    with open(dat_path, "rb") as f:
        # 跳过起始帧
        f.seek(start * FRAME_BYTES)
        for i in range(start, end):
            buf = f.read(FRAME_BYTES)
            if len(buf) < FRAME_BYTES:
                print("[WARN] 提前到达文件末尾")
                break
            # 输出路径
            filename = f"{i+1:06d}.{fmt if HAVE_PIL else 'pgm'}"
            out_path = os.path.join(out_subdir, filename)
            save_frame_png_or_bmp(buf, WIDTH, HEIGHT, out_path)
    return end - start


def main(argv=None):
    parser = argparse.ArgumentParser(description="解析 CMPRS 目录下 .dat 原始灰度帧为图片")
    parser.add_argument("root", nargs="?", default=os.getcwd(), help="SD 根目录路径（默认当前目录）")
    parser.add_argument("--out", default="CMPRS_extracted", help="输出目录（默认 CMPRS_extracted）")
    parser.add_argument("--fmt", choices=["png", "bmp"], default="png", help="输出图片格式（默认 png；无 Pillow 时自动使用 pgm）")
    parser.add_argument("--start", type=int, default=0, help="从第几帧开始（0 基）")
    parser.add_argument("--count", type=int, default=-1, help="导出帧数（-1 到末尾）")
    parser.add_argument("--file", default="", help="只解析指定 .dat（如 1.dat）；留空解析全部")

    args = parser.parse_args(argv)

    try:
        cmprs_dir = find_cmprs(args.root)
    except Exception as e:
        print(f"[ERROR] {e}")
        return 2

    ensure_dir(args.out)

    dat_files = []
    if args.file:
        p = os.path.join(cmprs_dir, args.file)
        if not os.path.isfile(p):
            print(f"[ERROR] 未找到文件: {p}")
            return 2
        dat_files = [p]
    else:
        dat_files = list_dat_files(cmprs_dir)
        if not dat_files:
            print(f"[INFO] {cmprs_dir} 下未找到 .dat 文件")
            return 0

    total_frames = 0
    for dat in dat_files:
        total_frames += extract_dat(dat, args.out, fmt=args.fmt, start=args.start, count=args.count)

    if HAVE_PIL:
        print(f"[DONE] 导出完成，合计帧数: {total_frames}。图片格式: {args.fmt}")
    else:
        print(f"[DONE] 导出完成（未安装 Pillow，已输出为 PGM）。合计帧数: {total_frames}")
        print("      如需 PNG/BMP，请先安装 Pillow：pip install pillow")


if __name__ == "__main__":
    sys.exit(main())
