Pico Wireless Display (PWD)
===========================

> **一句话**：让一块 **Pico W**（RP2040 + CYW43439）当**无线显示设备** —— 主机把画面压成
> 一帧一帧的图片，经 WiFi 推给它，它自己解码上屏。
> **现状**：**仓刚立起来，还没有可烧的固件** ✗。已完成的只有一件事：用**官方 iperf 例子**
> 把这块板子的无线链路量清楚了（见 [`notes/wifi-link-pico-w.md`](notes/wifi-link-pico-w.md)）。

## 为什么单独一个仓

`Pico-USB-Display`（PUD）走 **USB** 把画面送到设备；PWD 换 **WiFi**。设备侧的显示与解码
（面板驱动、总线、QOI/JPEG 解码）已经是独立库 [`pico-display-lib`](https://github.com/embeddedboys/pico-display-lib)，
两边都能用；差异全在**传输与帧调度**上，所以 PWD 独立成仓，不往 PUD 里塞第二套链路。

与 PUD 的关系一句话：**共用显示/解码库，另立传输层**。PUD 的固件、协议与踩坑记录仍然是最
直接的参考（`../Pico-USB-Display/`，接口与判据见那边的 `AGENTS.md` 与 `notes/`）。

## 链路预算（实测，2026-10）

| 项 | 实测 | 含义 |
| --- | --- | --- |
| 上行 PC→设备（设备收） | **17.4 Mbit/s ≈ 2.2 MB/s** | **投屏用的就是这条** ✓ |
| 下行 设备→PC | **≈6.7 Mbit/s ≈ 0.83 MB/s** | 若将来让 Pico W 当发送端，别用上行数字外推 |
| 480×320 JPEG q85 单帧 | ≈22 KB | 30 fps 需 **0.66 MB/s**，约占上行 **30%** ✓ |

条件与证据链见 [`notes/wifi-link-pico-w.md`](notes/wifi-link-pico-w.md)（都是官方例子 + PC 侧
`iperf 2.2.1`，两个独立 oracle 逐次吻合）。

## 硬件

| 件 | 型号 / 事实 |
| --- | --- |
| 板子 | 官方 **Pico W**：RP2040（B2）+ CYW43439（**只支持 2.4G**）+ 2 MB flash（W25Q16JV） |
| 调试器 | XV-Link / CMSIS-DAP（`1a86:7021`）接 SWD ✓；UART0(GP0/1) → 它的 UART 桥（`/dev/ttyACM0` @115200）—— **当前这段串口线是断开的** ✗，所以读数走 **USB stdio**（插 Pico W 自己的 USB ✓）或 **SWD 直接读 RAM 里的计数器** ✓，见 [`notes/udp-ingress.md`](notes/udp-ingress.md) |
| 烧写 | `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c 'program <elf> verify reset exit'` ✓（不用按 BOOTSEL） |
| 面板 | **Z350IT008 模组**（用户按屏上丝印确认 ✓）：**驱动 ILI9488** ✓、触摸 **GT911** ✓、**8080 16-bit**、原生 320×480 + rotation 1 ⇒ 逻辑 480×320 ✓ |
| 面板接线 | 数据 `GP0..GP15`、`WR 19`、`RS 20`、`RST 22`、**CS 硬件下拉**、背光 `28`、触摸 `CTP_RST 18`/`CTP_IRQ 21`/`I2C 26·27` ✓（配置见 [`notes/design.md`](notes/design.md)）|

## 路线

| 里程碑 | 内容 | 判据 |
| --- | --- | --- |
| **M0 基线** | 官方 `pico-examples` 的 `pico_w/wifi/iperf` 在本仓里能编、能烧、能测（`baseline/`，零 delta） | 设备自己打印的速率与服务端 `iperf -s` 逐次吻合 ✓（已达成，见 notes） |
| **M1 收帧** ✓ | 连 WiFi + UDP 分片组帧 + 丢片/丢帧/带宽统计，**不上屏** | **已达成** ✓：发 100 帧 × 22000 B @30 fps ⇒ 设备 `frags_rx=1600`、`frames_complete=100`、`incomplete=0`、`bytes_complete=2 200 000`，与发送侧**逐项对账一致** ✓；0.66 MB/s = 5.33 Mbit/s（占上行 31%）✓。见 [`notes/udp-ingress.md`](notes/udp-ingress.md) |
| **M2 上屏** ✓(v1) | 面板 = **Z350IT008（ILI9488 + GT911）** ✓；v1 走**原始 RGB565 分带**（不解码 ✓） | **已达成** ✓：120 带全部 `bands=120` / `reject=0` / `bytes` 逐字节对账 ✓（40 带/s，即 **1 整图/s** ✓）；解码（QOI ✓）是下一步 |
| **M3 实时视频** | 主机侧 ffmpeg → JPEG → UDP，目标 **30 fps @480×320** | 设备侧 `0 坏帧`、``KB/帧`` 与预测值一致 |

**开发顺序是强制的**：先跑通官方例子、再写自己的代码（工作区守则，见 [`AGENTS.md`](AGENTS.md) §上手顺序）。

## 构建与烧写（占位，随 M0 落地补齐）

```bash
export PICO_SDK_PATH=<pico-sdk>
scripts/fetch-deps.sh                 # 取 pico-examples（与 SDK 同 tag）
cmake -S baseline -B build-baseline -DPICO_BOARD=pico_w -C <wifi-config.cmake>
cmake --build build-baseline -j
```

WiFi 凭据**只放在被 gitignore 的文件里**（`scripts/make-wifi-config.sh` 生成）✗ 不进仓。

## 文档

- 知识库索引：[`notes/README.md`](notes/README.md)
- 本仓规则：[`AGENTS.md`](AGENTS.md)（含工作区四份 skill 的强制前置与"先跑通官方例子"）
- 参考实现：[`../Pico-USB-Display/`](../Pico-USB-Display/)（USB 版，同一套显示/解码库）
