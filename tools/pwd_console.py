#!/usr/bin/env python3
"""读 PWD 设备的串口（调试器 UART 桥），把设备自己的话原样带出来。

存在的理由（都是踩过的坑）：
  * 串口读数**先验证读者本身** ✓：本工具会打印自己读到的字节数，读者不自证就等于没读 ✗；
  * 必须**显式设波特率**（115200 8N1 raw），否则拿到的是一堆乱码 ✗。

用法：
    pwd_console.py /dev/ttyACM0 --seconds 20            # 原样打印设备输出
    pwd_console.py /dev/ttyACM0 --seconds 20 --json      # 只把 [stat] 行解析成 JSON 打出来
    pwd_console.py /dev/ttyACM0 --until ready --seconds 30   # 等到 "[pwd] ready" 就退出

退出码：0 成功 / 2 用法错误 / 3 环境错误（打不开端口）/ 4 超时（没等到 --until 的条件）
"""

from __future__ import annotations

import argparse
import fcntl
import json
import os
import struct
import re
import sys
import termios
import time

EXIT_OK, EXIT_USAGE, EXIT_ENV, EXIT_TIMEOUT = 0, 2, 3, 4
BAUD = {115200: termios.B115200, 230400: termios.B230400}

STAT_RE = re.compile(
    r"\[stat\] frags=(?P<frags>\d+) dup=(?P<dup>\d+) stale=(?P<stale>\d+) bad=(?P<bad>\d+) "
    r"frames=(?P<frames>\d+) incomplete=(?P<incomplete>\d+) busy=(?P<busy>\d+) "
    r"bytes=(?P<bytes>\d+) bands=(?P<bands>\d+) "
    r"fps=(?P<fps>[\d.]+) kB_s=(?P<kbs>[\d.]+)"
)


def open_port(path: str, baud: int) -> int:
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    a[0] = a[1] = a[3] = 0                                  # raw：不加工、不回显、无流控
    a[2] = termios.CREAD | termios.CLOCAL | termios.CS8
    a[4] = a[5] = BAUD[baud]
    termios.tcsetattr(fd, termios.TCSANOW, a)
    # USB CDC 还要求**主机断言 DTR**（固件据此判断"有没有人在听" ✗）：不对它举手，
    # 端口打开了也收不到东西 ✓。TIOCM_* 在 Python 的 termios 里没有常量，直接用 Linux 数值 ✓。
    TIOCMBIS, TIOCM_DTR, TIOCM_RTS = 0x5416, 0x002, 0x004
    try:
        fcntl.ioctl(fd, TIOCMBIS, struct.pack("I", TIOCM_DTR | TIOCM_RTS))
    except OSError:
        pass    # 对普通 UART 节点可能不支持，无害 ✓
    return fd


def main() -> int:
    ap = argparse.ArgumentParser(description="PWD 串口读取器")
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=115200, choices=sorted(BAUD))
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--json", action="store_true", help="只输出解析好的 [stat] 行（JSON Lines）")
    ap.add_argument("--until", help="等到输出里出现这个子串就退出")
    args = ap.parse_args()

    try:
        fd = open_port(args.port, args.baud)
    except OSError as e:
        print(f"✗ 打不开 {args.port}：{e}", file=sys.stderr)
        return EXIT_ENV

    print(f"[reader] {args.port} @{args.baud} 已打开（先开读者再复位 ✓）", file=sys.stderr, flush=True)
    buf = b""
    total = 0
    deadline = time.monotonic() + args.seconds
    met = False
    last_report = 0.0

    try:
        while time.monotonic() < deadline:
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                chunk = b""
            if chunk:
                total += len(chunk)
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", "replace").strip("\r\0 ")
                    if not text:
                        continue
                    if not args.json:
                        print(text, flush=True)
                    m = STAT_RE.search(text)
                    if m and args.json:
                        print(json.dumps({k: (float(v) if "." in v else int(v)) for k, v in m.groupdict().items()},
                                         ensure_ascii=False), flush=True)
                    if args.until and args.until in text:
                        met = True
                        print(f"[reader] 命中等待条件：{args.until!r}", file=sys.stderr, flush=True)
                        break
                if met:
                    break
            now = time.monotonic()
            if now - last_report >= 5.0:      # 读者自证：读了多少字节
                print(f"[reader] 累计 {total} 字节", file=sys.stderr, flush=True)
                last_report = now
            time.sleep(0.02)
    finally:
        os.close(fd)

    print(f"[reader] 结束：共读 {total} 字节", file=sys.stderr, flush=True)
    if args.until and not met:
        print(f"✗ 超时：{args.seconds}s 内没等到 {args.until!r}", file=sys.stderr)
        return EXIT_TIMEOUT
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
