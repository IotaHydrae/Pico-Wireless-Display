#!/usr/bin/env python3
"""PWD 收帧协议的 PC 侧发送端。

两种模式（**协议分片头不变** ✓；窗口放在整帧载荷最前面 ✓）：

  * `--mode picture`（M2 v1，默认）：画一张 480×320 的测试图（8 条彩条 + 一个会移动的白方块 ✓），
    按水平**带**切开逐带发送 —— 每带 = 一帧，载荷 = `[u16 xs][u16 ys][u16 xe][u16 ye]` + 原始 RGB565 ✓。
    白方块会动 ⇒ 屏幕上"动起来"就是全链路（PC→WiFi→组帧→上屏）通的证据 ✓。
    设备侧**不做解码**（v1 ✓），所以带宽就是原始像素：480×320×2 = 300 KB/帧 ⇒ 上行 ~2.2 MB/s
    下理论 ~7 fps ✓ —— 能看能动，但这不是最终形态（QOI/JPEG 是下一步 ✓）。
  * `--mode blob`：发 N 个固定长度的确定性载荷（M1 验收用的就是它 ✓，便于复现那组数字 ✓）。

分片：`[u32 frame_id][u16 frag_idx][u16 frag_cnt][u32 frame_len]` + ≤1400 B 载荷（小端 ✓）。

用法：
    pwd_send.py --host <ip> --mode picture --rounds 20
    pwd_send.py --host <ip> --mode blob --size 22000 --frames 100 --rate 30 --json

退出码：0 成功 / 2 用法错误 / 3 环境错误（发不出去）/ 4 超时
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
import time

FRAG_HDR = 12
FRAG_PAYLOAD = 1400
FRAME_MAX = 32768
WINDOW_HDR = 8
DEFAULT_PORT = 4444

EXIT_OK, EXIT_USAGE, EXIT_ENV, EXIT_TIMEOUT = 0, 2, 3, 4

# 8 条彩条（RGB565：rrrrrggg gggbbbbb ✓）
BARS = [0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000]


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def build_fragments(frame_id: int, payload: bytes) -> list[bytes]:
    frag_cnt = (len(payload) + FRAG_PAYLOAD - 1) // FRAG_PAYLOAD
    return [
        struct.pack("<IHHI", frame_id, idx, frag_cnt, len(payload)) + payload[idx * FRAG_PAYLOAD:(idx + 1) * FRAG_PAYLOAD]
        for idx in range(frag_cnt)
    ]


def band_payload(width: int, y0: int, y1: int, square_x: int, square_y: int) -> bytes:
    """一带的载荷：8 字节窗口头 + 该带每行的 RGB565 像素。"""
    bar_w = max(1, width // len(BARS))
    rows = []
    for y in range(y0, y1 + 1):
        row = bytearray()
        for x in range(width):
            if square_x <= x < square_x + 40 and square_y <= y < square_y + 40:
                px = 0xFFFF                      # 会动的白方块 ✓
            elif x < 2 or x >= width - 2 or y < 2:
                px = 0xFFFF                      # 边框，方便看边界是否铺满 ✓
            else:
                px = BARS[min(x // bar_w, len(BARS) - 1)]
            row += struct.pack("<H", px)
        rows.append(bytes(row))
    return struct.pack("<HHHH", 0, y0, width - 1, y1) + b"".join(rows)


class Sender:
    def __init__(self, host: str, port: int):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.frames = self.frags = self.bytes = 0

    def send_frame(self, payload: bytes) -> None:
        for frag in build_fragments(self.frames + 1, payload):
            self.sock.sendto(frag, self.addr)
            self.frags += 1
            self.bytes += len(frag)
        self.frames += 1

    def close(self) -> None:
        self.sock.close()


def run_picture(sender: Sender, args) -> dict:
    band_rows = args.band_rows
    height = args.height
    t0 = time.monotonic()
    # **逐带限速**：一轮 40 带若全速连发，会把对端接收打爆（实测丢一半分片 ✗）；
    # 按 band_fps 均匀铺开才是可持续速率 ✓（blob 模式本来就是逐帧限速，所以它零丢 ✓）。
    band_period = (1.0 / args.band_fps) if args.band_fps > 0 else 0.0
    bands_sent = 0
    for rnd in range(args.rounds):
        if time.monotonic() - t0 > args.timeout:
            print("✗ 超过 --timeout", file=sys.stderr)
            sender.close()
            sys.exit(EXIT_TIMEOUT)
        # 白方块沿对角线移动，回到起点再重来 ✓
        step = (rnd * 24) % max(1, args.width)
        sq_x = min(step, args.width - 40)
        sq_y = min((rnd * 16) % max(1, height), height - 40)
        for y0 in range(0, height, band_rows):
            y1 = min(y0 + band_rows - 1, height - 1)
            sender.send_frame(band_payload(args.width, y0, y1, sq_x, sq_y))
            bands_sent += 1
            if band_period:
                time.sleep(max(0.0, t0 + bands_sent * band_period - time.monotonic()))
        if args.fps > 0:
            target = t0 + (rnd + 1) / args.fps
            time.sleep(max(0.0, target - time.monotonic()))
    dur = time.monotonic() - t0
    return {"rounds": args.rounds, "bands": bands_sent, "band_fps": args.band_fps, "frames": sender.frames, "frags": sender.frags,
            "bytes": sender.bytes, "duration_s": round(dur, 3),
            "fps": round(args.rounds / dur, 2) if dur > 0 else 0.0,
            "mbit_s": round(sender.bytes * 8 / dur / 1e6, 2) if dur > 0 else 0.0,
            "band_rows": band_rows, "picture": f"{args.width}x{args.height}"}


def run_blob(sender: Sender, args) -> dict:
    payload = bytes((i * 31 + 7) & 0xFF for i in range(args.size))
    t0 = time.monotonic()
    period = (1.0 / args.rate) if args.rate > 0 else 0.0
    for i in range(args.frames):
        if time.monotonic() - t0 > args.timeout:
            print("✗ 超过 --timeout", file=sys.stderr)
            sender.close()
            sys.exit(EXIT_TIMEOUT)
        sender.send_frame(payload)
        if period:
            time.sleep(max(0.0, t0 + (i + 1) * period - time.monotonic()))
    dur = time.monotonic() - t0
    return {"frames": sender.frames, "frags": sender.frags, "bytes": sender.bytes,
            "frame_bytes": len(payload), "duration_s": round(dur, 3),
            "fps": round(sender.frames / dur, 2) if dur > 0 else 0.0,
            "mbit_s": round(sender.bytes * 8 / dur / 1e6, 2) if dur > 0 else 0.0}


def main() -> int:
    ap = argparse.ArgumentParser(description="PWD 收帧协议发送端")
    ap.add_argument("--host", required=True, help="设备地址（固件会在串口/USB 控制台打印）")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--mode", choices=("picture", "blob"), default="picture")
    ap.add_argument("--rounds", type=int, default=20, help="picture：整图发多少轮")
    ap.add_argument("--fps", type=float, default=5.0, help="picture：目标轮帧率（整图 = 40 带）")
    ap.add_argument("--band-fps", type=float, default=80.0,
                    help="picture：**逐带**限速（默认 80 带/s ≈ 2 轮/s ≈ 0.62 MB/s ✓）；"
                         "设 0 = 一轮内全速连发（实测会丢一半分片 ✗，仅用于压测）")
    ap.add_argument("--band-rows", type=int, default=8, help="picture：每带多少行（480×8×2=7680 B ✓）")
    ap.add_argument("--width", type=int, default=480)
    ap.add_argument("--height", type=int, default=320)
    ap.add_argument("--frames", type=int, default=100, help="blob：发多少帧")
    ap.add_argument("--rate", type=float, default=30.0, help="blob：目标帧率（0 = 全速）")
    ap.add_argument("--size", type=int, default=22000, help="blob：每帧载荷字节数")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--timeout", type=float, default=120.0)
    args = ap.parse_args()

    if args.band_rows <= 0 or args.rounds <= 0 or args.width <= 0 or args.height <= 0:
        print("✗ 参数不合法（--rounds/--band-rows/--width/--height）", file=sys.stderr)
        return EXIT_USAGE
    if args.mode == "blob" and (args.frames <= 0 or args.size <= 0 or args.size > FRAME_MAX):
        print(f"✗ blob 模式参数不合法（--frames/--size，上限 {FRAME_MAX}）", file=sys.stderr)
        return EXIT_USAGE

    sender = Sender(args.host, args.port)
    try:
        result = run_picture(sender, args) if args.mode == "picture" else run_blob(sender, args)
    except OSError as e:
        print(f"✗ 发送失败：{e}", file=sys.stderr)
        return EXIT_ENV
    finally:
        sender.close()

    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        print("↑ " + "，".join(f"{k}={v}" for k, v in result.items()))
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
