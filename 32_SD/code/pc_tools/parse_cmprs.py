#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
解析 SD 卡根目录下的 CMPRS 目录中的 .dat 录制文件，并导出图片与 MP4：
- 每帧为原始 8bit 灰度数据，分辨率默认为 188x120（MT9V03X）。
- 将每个 .dat 按帧切分，导出为 PNG 或 BMP 序列（首选 PNG，兼容性好、体积小）。
- 若缺少 Pillow，则自动退化为 PGM（几乎所有图像工具都能打开）。
- 支持把导出的帧序列打包为 MP4（优先 moviepy，其次 OpenCV；都缺失则提示如何安装）。

帧率设置：修改本文件顶部的 FPS 常量，或运行时用 --fps 参数覆盖。

用法示例：
    将本脚本拷贝到 SD 卡根目录（与 CMPRS 同级），双击运行或用 Python 执行。
    可选参数请运行：python parse_cmprs.py -h  查看。
"""

import os
import sys
import argparse
import json

# 配置文件路径（与脚本同目录）
CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'parse_cmprs_last.json')

WIDTH = 188
HEIGHT = 120
FRAME_BYTES = WIDTH * HEIGHT
FPS = 60  # 默认帧率，可在命令行用 --fps 覆盖

# 尝试导入 Pillow，用于保存 PNG/BMP；若无则退化到 PGM 输出
try:
    from PIL import Image  # type: ignore
    HAVE_PIL = True
except Exception:
    Image = None
    HAVE_PIL = False

# 可选视频打包依赖：优先 moviepy，其次 opencv
try:
    import moviepy.editor as mpy  # type: ignore
    HAVE_MOVIEPY = True
except Exception:
    mpy = None
    HAVE_MOVIEPY = False

try:
    import cv2  # type: ignore
    HAVE_CV2 = True
except Exception:
    cv2 = None
    HAVE_CV2 = False


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


def natural_key(s: str):
    import re
    return [int(text) if text.isdigit() else text.lower() for text in re.split(r'(\d+)', s)]


def make_video_from_folder(frames_dir: str, out_mp4_path: str, fps: float, fmt: str, step: int = 1):
    """将 frames_dir 下的序列帧打包为 MP4。
    - 优先使用 moviepy（编码 libx264），其次使用 OpenCV（编码 mp4v）。
    - fmt 用于过滤需要的扩展名（png/bmp/pgm）。
    """
    files = [os.path.join(frames_dir, f) for f in os.listdir(frames_dir)
             if f.lower().endswith('.' + fmt.lower())]
    if not files:
        # 当没有所选格式（如无 Pillow 输出为 pgm），尝试 pgm
        if fmt.lower() != 'pgm':
            files = [os.path.join(frames_dir, f) for f in os.listdir(frames_dir)
                     if f.lower().endswith('.pgm')]
            fmt = 'pgm'
    if not files:
        print(f"[INFO] 未找到用于打包的视频帧: {frames_dir}")
        return False

    files.sort(key=natural_key)
    # 采样（每 step 帧取一帧）
    if step > 1:
        files = files[::max(1, int(step))]
        if not files:
            print(f"[INFO] 取样后无帧可用，step={step}")
            return False

    if HAVE_MOVIEPY:
        try:
            clip = mpy.ImageSequenceClip(files, fps=fps)
            # 写入 mp4，使用 libx264，并强制像素格式 yuv420p 以提高 Windows/QuickTime 兼容性
            # 若缺少 ffmpeg 会报错
            clip.write_videofile(
                out_mp4_path,
                codec='libx264',
                audio=False,
                preset='fast',
                bitrate='3000k',
                ffmpeg_params=['-pix_fmt', 'yuv420p'],
                verbose=False,
                logger=None,
            )
            print(f"[VIDEO] 已生成: {out_mp4_path}")
            return True
        except Exception as e:
            print(f"[WARN] moviepy 生成 MP4 失败：{e}，尝试 OpenCV")

    if HAVE_CV2:
        try:
            # 读取首帧获取尺寸
            if fmt.lower() == 'pgm' and not HAVE_PIL:
                # OpenCV 通常也支持读 PGM
                frame0 = cv2.imread(files[0], cv2.IMREAD_GRAYSCALE)
            else:
                frame0 = cv2.imread(files[0], cv2.IMREAD_UNCHANGED)
            if frame0 is None:
                print("[ERROR] OpenCV 无法读取首帧，放弃视频生成")
                return False
            h, w = frame0.shape[:2]
            # 优先尝试 mp4v -> .mp4
            fourcc = cv2.VideoWriter_fourcc(*'mp4v')
            vw = cv2.VideoWriter(out_mp4_path, fourcc, fps, (w, h), isColor=True)
            if not vw.isOpened():
                print("[WARN] OpenCV mp4v 打开失败，改用 AVI(MJPG) 尝试")
                raise RuntimeError('mp4v_open_failed')
            for f in files:
                frame = cv2.imread(f, cv2.IMREAD_UNCHANGED)
                if frame is None:
                    continue
                if len(frame.shape) == 2:
                    # 灰度 -> BGR
                    frame = cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)
                elif frame.shape[2] == 4:
                    # BGRA -> BGR
                    frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
                # 尺寸安全检查
                if frame.shape[1] != w or frame.shape[0] != h:
                    frame = cv2.resize(frame, (w, h), interpolation=cv2.INTER_NEAREST)
                vw.write(frame)
            vw.release()
            print(f"[VIDEO] 已生成: {out_mp4_path}")
            return True
        except Exception as e:
            # 回退到 AVI(MJPG) 以提升 Windows 兼容性
            try:
                avi_path = os.path.splitext(out_mp4_path)[0] + '.avi'
                print(f"[INFO] 尝试回退为 AVI(MJPG): {avi_path}")
                frame0 = cv2.imread(files[0], cv2.IMREAD_UNCHANGED)
                if frame0 is None:
                    print("[ERROR] OpenCV 无法读取首帧，放弃视频生成")
                    return False
                h, w = frame0.shape[:2]
                fourcc = cv2.VideoWriter_fourcc(*'MJPG')
                vw = cv2.VideoWriter(avi_path, fourcc, fps, (w, h), isColor=True)
                if not vw.isOpened():
                    print("[ERROR] OpenCV 打开 AVI(MJPG) 失败")
                    return False
                for f in files:
                    frame = cv2.imread(f, cv2.IMREAD_UNCHANGED)
                    if frame is None:
                        continue
                    if len(frame.shape) == 2:
                        frame = cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)
                    elif frame.shape[2] == 4:
                        frame = cv2.cvtColor(frame, cv2.COLOR_BGRA2BGR)
                    if frame.shape[1] != w or frame.shape[0] != h:
                        frame = cv2.resize(frame, (w, h), interpolation=cv2.INTER_NEAREST)
                    vw.write(frame)
                vw.release()
                print(f"[VIDEO] 已生成: {avi_path}")
                return True
            except Exception as e2:
                print(f"[ERROR] OpenCV 生成视频失败：{e2}")

    print("[INFO] 未安装 moviepy 或 OpenCV，无法自动生成 MP4。可选择：\n"
          "  pip install moviepy  或  pip install opencv-python\n"
          "  或者手动用 ffmpeg 将帧序列打包为视频。")
    return False


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
    # 若无任何命令行参数（如双击运行），自动进入菜单模式
    injected_menu = False
    if argv is None and len(sys.argv) == 1:
        argv = ["--menu"]
        injected_menu = True

    parser = argparse.ArgumentParser(description="解析 CMPRS 目录下 .dat 原始灰度帧为图片")
    parser.add_argument("root", nargs="?", default=os.getcwd(), help="SD 根目录路径（默认当前目录）")
    parser.add_argument("--out", default="CMPRS_extracted", help="输出目录（默认 CMPRS_extracted）")
    parser.add_argument("--fmt", choices=["png", "bmp"], default="png", help="输出图片格式（默认 png；无 Pillow 时自动使用 pgm）")
    parser.add_argument("--fps", type=float, default=FPS, help=f"视频帧率（默认 {FPS}，可为小数；也可在脚本顶部修改 FPS 常量）")
    parser.add_argument("--fps-scale", type=float, default=1.0, help="帧率比例系数：实际写入帧率 = fps * fps_scale（<1 减速，>1 加速）")
    parser.add_argument("--no-video", action="store_true", help="只导出图片，不生成 MP4")
    parser.add_argument("--video-only", action="store_true", help="只重建视频，不重新导出图片（当图片已存在时很有用）")
    parser.add_argument("--menu", action="store_true", help="交互式菜单模式，在终端中逐步设置参数")
    parser.add_argument("--use-last", action="store_true", help="直接使用上次保存的配置运行（非交互）")
    parser.add_argument("--start", type=int, default=0, help="从第几帧开始（0 基）")
    parser.add_argument("--count", type=int, default=-1, help="导出帧数（-1 到末尾）")
    parser.add_argument("--take-every", type=int, default=1, help="帧采样步长：每 N 帧取一帧（用于加速，N>=1）")
    parser.add_argument("--file", default="", help="只解析指定 .dat（如 1.dat）；留空解析全部")

    args = parser.parse_args(argv)

    # 若指定直接使用上次配置
    if getattr(args, 'use_last', False):
        last = load_last_config()
        if last is None:
            print(f"[WARN] 未找到历史配置：{CONFIG_PATH}，将按当前参数继续")
        else:
            print(f"[CONF] 已加载历史配置：{CONFIG_PATH}")
            args = argparse.Namespace(**last)

    # 如果使用菜单模式，则通过交互方式获取参数
    if getattr(args, 'menu', False):
        new_args = interactive_menu(args)
        if new_args is None:
            # 在自动菜单模式下，为避免窗口秒关，等待用户确认
            if injected_menu:
                try:
                    input("\n按回车退出...")
                except Exception:
                    pass
            return 0
        args = new_args

    code = run_pipeline(args)
    # 保存此次运行配置
    try:
        save_last_config(args)
        print(f"[CONF] 已保存本次配置到：{CONFIG_PATH}")
    except Exception as e:
        print(f"[WARN] 保存配置失败：{e}")
    # 在自动菜单模式下，为避免窗口秒关，等待用户确认
    if injected_menu:
        try:
            input("\n按回车退出...")
        except Exception:
            pass
    return code


def save_last_config(args):
    data = {
        'root': args.root,
        'out': args.out,
        'fmt': args.fmt,
        'fps': float(args.fps),
        'fps_scale': float(getattr(args, 'fps_scale', 1.0)),
        'no_video': bool(args.no_video),
        'video_only': bool(args.video_only),
        'start': int(args.start),
        'count': int(args.count),
        'take_every': int(getattr(args, 'take_every', 1)),
        'file': args.file,
    }
    with open(CONFIG_PATH, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


def load_last_config():
    if not os.path.isfile(CONFIG_PATH):
        return None
    try:
        with open(CONFIG_PATH, 'r', encoding='utf-8') as f:
            data = json.load(f)
        # 基础字段校验与默认
        data.setdefault('root', os.getcwd())
        data.setdefault('out', 'CMPRS_extracted')
        data.setdefault('fmt', 'png')
        data.setdefault('fps', FPS)
        data.setdefault('fps_scale', 1.0)
        data.setdefault('no_video', False)
        data.setdefault('video_only', False)
        data.setdefault('start', 0)
        data.setdefault('count', -1)
        data.setdefault('take_every', 1)
        data.setdefault('file', '')
        return data
    except Exception:
        return None


def run_pipeline(args) -> int:
    """执行解析与打包流程，接收已解析好的 args。"""

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
        base = os.path.splitext(os.path.basename(dat))[0]
        frames_dir = os.path.join(args.out, base)
        out_mp4 = os.path.join(args.out, base + ".mp4")

        n = 0
        if not args.video_only:
            n = extract_dat(dat, args.out, fmt=args.fmt, start=args.start, count=args.count)
            total_frames += n
        else:
            # 只重建视频时，如果帧目录不存在则提示
            if not os.path.isdir(frames_dir):
                print(f"[WARN] 缺少帧目录，无法只重建视频：{frames_dir}")
                continue

        # 打包为 MP4/AVI
        if not args.no_video:
            # 若图片是 PGM（无 Pillow），fmt 需要回退 pgm
            chosen_fmt = args.fmt if HAVE_PIL else 'pgm'
            # 若 --video-only 且不存在该格式帧，则尝试自动探测常见格式
            if args.video_only:
                exts = [chosen_fmt]
                if chosen_fmt.lower() != 'png':
                    exts.append('png')
                if chosen_fmt.lower() != 'bmp':
                    exts.append('bmp')
                if 'pgm' not in [e.lower() for e in exts]:
                    exts.append('pgm')
                found = False
                for e in exts:
                    if any(name.lower().endswith('.' + e) for name in os.listdir(frames_dir)):
                        chosen_fmt = e
                        found = True
                        break
                if not found:
                    print(f"[WARN] 未在 {frames_dir} 找到可用帧文件，跳过视频重建")
                    continue
            eff_fps = float(getattr(args, 'fps', FPS)) * float(getattr(args, 'fps_scale', 1.0))
            step = max(1, int(getattr(args, 'take_every', 1)))
            make_video_from_folder(frames_dir, out_mp4, eff_fps, fmt=chosen_fmt, step=step)

    if not args.video_only:
        if HAVE_PIL:
            print(f"[DONE] 导出完成，合计帧数: {total_frames}。图片格式: {args.fmt}")
        else:
            print(f"[DONE] 导出完成（未安装 Pillow，已输出为 PGM）。合计帧数: {total_frames}")
            print("      如需 PNG/BMP，请先安装 Pillow：pip install pillow")
    return 0


def interactive_menu(args):
    """简单的终端交互菜单，返回更新后的 args（argparse.Namespace）。"""
    def prompt(text, default_str):
        s = input(f"{text} [{default_str}]: ").strip()
        return s if s else default_str

    def prompt_bool(text, default_bool):
        ds = 'y' if default_bool else 'n'
        s = input(f"{text} (y/n) [{ds}]: ").strip().lower()
        if not s:
            return default_bool
        return s.startswith('y')

    # 载入历史配置作为默认
    last = load_last_config()
    if last is None:
        ns = argparse.Namespace(**vars(args))
    else:
        # 历史配置 + 本次命令行参数（命令行优先）
        merged = {**last, **vars(args)}
        ns = argparse.Namespace(**merged)

    print("\n==== CMPRS 解析与打包 - 交互式菜单 ====")
    # 快速通道：是否直接使用上次配置
    if last is not None:
        use_last_now = prompt_bool("直接使用上次配置并执行?", True)
        if use_last_now:
            return argparse.Namespace(**{**last, **vars(args)})

    # 根目录
    while True:
        ns.root = prompt("SD 根目录", ns.root)
        try:
            cmprs = find_cmprs(ns.root)
            print(f"[OK] 找到 CMPRS 目录: {cmprs}")
            break
        except Exception as e:
            print(f"[ERROR] {e}")
            if not prompt_bool("重新输入根目录?", True):
                return None

    # 枚举 .dat 文件
    try:
        cmprs_dir = find_cmprs(ns.root)
        dats = list_dat_files(cmprs_dir)
    except Exception:
        dats = []
    if not dats:
        print("[INFO] 未发现 .dat 文件，稍后流程可能会直接结束。")
    else:
        print("可选择处理的 .dat 文件：")
        for i, p in enumerate(dats, 1):
            print(f"  {i}. {os.path.basename(p)}")
        sel = input("选择编号（回车=全部，或直接输入文件名）：").strip()
        if sel.isdigit():
            idx = int(sel)
            if 1 <= idx <= len(dats):
                ns.file = os.path.basename(dats[idx-1])
        elif sel:
            ns.file = sel

    # 输出目录
    ns.out = prompt("输出目录", ns.out)
    # 图片格式
    fmt_in = prompt("图片格式 png/bmp（无 Pillow 自动输出 pgm）", ns.fmt)
    if fmt_in.lower() in ("png", "bmp"):
        ns.fmt = fmt_in.lower()
    # 帧率
    while True:
        fps_in = prompt("视频帧率 (可为小数)", str(ns.fps))
        try:
            ns.fps = float(fps_in)
            if ns.fps <= 0:
                raise ValueError
            break
        except Exception:
            print("[WARN] 帧率必须为正数，请重试")

    # 帧率缩放（比例系数）
    while True:
        fps_scale_in = prompt("帧率比例系数 fps_scale (<1 减速, >1 加速)", str(getattr(ns, 'fps_scale', 1.0)))
        try:
            ns.fps_scale = float(fps_scale_in)
            if ns.fps_scale <= 0:
                raise ValueError
            break
        except Exception:
            print("[WARN] fps_scale 必须为正数，请重试")

    # 起始帧与帧数
    while True:
        start_in = prompt("起始帧 (0 基)", str(ns.start))
        try:
            ns.start = int(start_in)
            if ns.start < 0:
                raise ValueError
            break
        except Exception:
            print("[WARN] 起始帧必须为非负整数")
    while True:
        count_in = prompt("导出帧数 (-1 到末尾)", str(ns.count))
        try:
            ns.count = int(count_in)
            if ns.count == 0:
                print("[WARN] 导出帧数为 0 将不会输出任何帧")
            break
        except Exception:
            print("[WARN] 帧数必须为整数")

    # 采样步长（每 N 帧取一帧）
    while True:
        step_in = prompt("帧采样步长 take_every (>=1)", str(getattr(ns, 'take_every', 1)))
        try:
            ns.take_every = int(step_in)
            if ns.take_every < 1:
                raise ValueError
            break
        except Exception:
            print("[WARN] take_every 必须为 >=1 的整数")

    # 视频控制
    nv = prompt_bool("生成视频?", not ns.no_video)
    ns.no_video = not nv
    ns.video_only = prompt_bool("只重建视频(不重新导出图片)?", ns.video_only)

    print("\n配置确认：")
    print(f"  根目录: {ns.root}")
    print(f"  输出目录: {ns.out}")
    print(f"  目标文件: {'全部' if not ns.file else ns.file}")
    print(f"  图片格式: {ns.fmt}{'' if HAVE_PIL else ' (将改为 PGM)'}")
    print(f"  帧率: {ns.fps}")
    print(f"  帧率比例: {getattr(ns, 'fps_scale', 1.0)}  (实际写入={ns.fps * getattr(ns, 'fps_scale', 1.0)})")
    print(f"  起始/数量: {ns.start} / {ns.count}")
    print(f"  采样步长: {getattr(ns, 'take_every', 1)}  (每 N 帧取 1 帧)")
    print(f"  生成视频: {not ns.no_video}")
    print(f"  只重建视频: {ns.video_only}")
    if not prompt_bool("开始执行?", True):
        return None

    return ns


if __name__ == "__main__":
    sys.exit(main())
