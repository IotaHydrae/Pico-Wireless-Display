# PWD 设计：把画面经 WiFi 送到 Pico W

> **一句话**：设备侧只做三件事 —— **收分片、组帧、解码上屏**；主机侧只做两件事 ——
> **把画面压成 JPEG、按分片协议发出去**。所有复杂度都在这两件事之间的**帧调度**上。
> **现状**：设计定稿到 M1（收帧统计）所需；显示相关的选择**等面板确定**（见文末 UNKNOWN）。

## TL;DR

- **传输**：UDP + 应用层分片；字段布局**沿用 P4 无线投屏那套**（PC 侧工具将来能两边通用）。
- **组帧策略**：只认**最新的完整帧**，缺片整帧丢并计数 —— 不重传、不无限等待。
- **设备侧只复用显示/解码库**（`pico-display-lib`）+ 移植 PUD 的流水线结论；
  **不复用 PUD 的 USB 端点协议**（那是另一条链路）。
- **判据来自设备自报的计数器**（收/丢/帧率/B/s），PC 侧另有一份独立统计，两边必须对得上。

## 数据流

```text
PC                                 Pico W (RP2040 + CYW43439)
──────────────                     ─────────────────────────────
 取画面 (ffmpeg / 截图)
      ↓ 编码 JPEG                    UDP 收分片
  分片 (≤1400 B)      ──WiFi──►       ↓ 按帧号组帧（缺片即丢整帧）
  发 UDP                               ↓ 帧槽（最新帧优先）
  统计（自报）                         ↓ 解码（QOI / JPEG）
                                       ↓ 面板刷屏（pico-display-lib）
```

反过来（设备→PC）现在**不需要**：下行实测只有上行的 ~38%（见 `wifi-link-pico-w.md`），
而且投屏的瓶颈在"送到设备"这一侧。

## 传输协议（M1 定稿）

沿用 P4 无线投屏已经验证过的布局，**字段只追加、不重排、不复用编号**：

```text
每片 = [u32 帧号][u16 片序][u16 片数][u32 帧总长] + ≤1400 B 载荷
```

- **1400 B 载荷**：留出 IP/UDP 头，避免 IP 分片（实测 MTU 内的单包最省事）。
- **帧号**单调递增；设备侧只接受**当前帧号**与更新的帧号，旧帧的分片直接计入"过期丢弃"。
- **一帧的片数**由 `帧总长 / 1400` 上取整；组帧完成即把整帧交给解码，**不等待**下一帧。
- **缺片的处理**：帧号前进就丢弃未完成的旧帧并计数 —— 显示端永远偏向"最新"，
  而不是"完整但过期"。

> 为什么不用 TCP：P4 上的实测是 TCP 114 fps / UDP + 丢帧策略 ~150 fps（同一套硬件条件）。
> Pico W 的上行只有 2.2 MB/s，30 fps @480×320 JPEG q85 只占 30% ⇒ **链路不是瓶颈时，
> 选行为可预测的那条**（UDP：丢帧可计数、延迟不累积；TCP：队头阻塞会把延迟越积越长）。

## 复用边界

| 复用 ✓ | 不复用 ✗ |
| --- | --- |
| `pico-display-lib`：面板驱动、SPI/PIO 总线、`tft_*_flush()`、输入 | PUD 的 USB 端点协议（EP1/EP2/EP4）、CherryUSB 栈 |
| PUD 踩过的**结论**：解码不在中断里、帧槽背压、缓冲区复用契约、看门狗自愈 | PUD 的定长帧槽尺寸（那是按 USB 的 64 KB 传输定的，RP2040 上不适用） |
| P4 无线投屏的 PC 侧 `pud_media.py`（ffmpeg → JPEG 参数映射）、分片发送端写法 | P4 的硬件 JPEG 解码路径（Pico W 没有硬件解码器） |
| `baseline/` 里官方例子的**构建配方**（wrapper + 凭据注入） | 官方例子的客户端模式源码（上游有 bug，见 `wifi-link-pico-w.md`） |

## 设备侧的关键约束（RP2040）

| 事实 | 影响 |
| --- | --- |
| RAM 只有 **264 KB**（RP2350 是 520 KB） | 帧槽不能照抄 PUD 的 64 KB；单帧缓冲 + 一块工作缓冲就够（JPEG 一帧约 22 KB） |
| **无 PSRAM** | 解码输出直接进面板窗口（band 刷屏），不做整屏 RGB565 缓冲（480×320×2 = 300 KB ✗） |
| CPU **133 MHz** 默认 | 480×320 JPEG 软解在 133 MHz 上会很紧 ✗ ⇒ 要么 QOI ✓，要么超频（见下） |
| 可超频到 **440 MHz**（1.30 V）✓ | 官方 Pico W 实测 440 MHz / 双核 30 次 soak 通过（`pico-turbo` 的 board 文件）；用之前先读 `pico-display-lib` 的"总线时钟是编译期常量"那条 ✗ |

## 面板与触摸：YT350S006（8080 16-bit）+ GT911

| 项 | 事实 | 凭据 |
| --- | --- | --- |
| 面板 | **YT350S006**，480×320，**8080 16-bit 并口** | 型号/总线来自接线约定（用户）；分辨率来自 PUD 的配置行 |
| 面板控制器 | **ST7796**，而且 `YT350S006` 是它的一个**初始化变体** ✓ | `pico-display-lib/drivers/display/CMakeLists.txt` 把 `TFT_MODEL_YT350S006` 映射成 `TFT_ST7796_VARIANT_YT350S006` |
| 触摸 | **GT911**（I2C，含 IRQ/RST） | PUD 的 `configs/pico_dm_ep4309n.cmake` 是现成的 GT911 写法（`INDEV_DRV_USE_GT911` + SCL/SDA/IRQ/RST） |

**PUD 里那份 `configs/pico_dm_yt350s006.cmake` 不能照抄** ✗：它是 **SPI** 变体
（`TFT_BUS_TYPE 0`、`TFT_COLOR_16_SWAP 1`、`INDEV_DRV_NOT_USED 1`），我们这块是 8080 16-bit +
带触摸。8080 的写法照 `configs/pico_dm_qd3503728.cmake`（`TFT_BUS_TYPE 1` +
`TFT_PIN_DB_BASE/COUNT` + `TFT_PIN_CS/WR/RS/RES`），触摸照 `configs/pico_dm_ep4309n.cmake`。
**PWD 自己的 `configs/` 等接线表确认后再建**（M2）。

**Pico W 的引脚约束** ✓（SDK 板级头文件 `boards/pico_w.h` 的 `CYW43_DEFAULT_PIN_*`）：

```text
WL_REG_ON  = GPIO23     WL_DATA_OUT/IN/HOST_WAKE = GPIO24
WL_CS      = GPIO25     WL_CLOCK = GPIO29
```

⇒ **GPIO 23/24/25/29 不可用**；剩下 0–22、26、27、28 共 **26 个**要给：16 位数据总线 +
`CS/WR/RS/RES` + 背光 + 触摸 I2C 两线（+ 可选 IRQ/RST）。**排布很紧**，写 `configs/` 前先算一遍 ✗。

## 已定项与 UNKNOWN

**已定** ✓：

1. 传输 = UDP + 分片，布局如上；只用最新完整帧。
2. 设备侧以 `pico-display-lib` 为显示/解码基础，不重写面板驱动。
3. M1 先做**不上屏**的收帧统计（没有面板也能测，判据是设备自报计数与 PC 侧一致）。
4. M0 基线 = 官方 `pico_w/wifi/iperf`（本仓 `baseline/`，零 delta）。

**UNKNOWN** ✗（要用硬件/实测回答，不许猜）：

| 问题 | 为什么必须先问 |
| --- | --- |
| **面板与触摸实际接在 Pico W 的哪些 GPIO 上**？ | 面板型号已定（见上节 ✓），但 PUD 的脚位是 Pico 2 那套板子的 ✗；没有真实接线表就写不出 `configs/`，也无法确认避开了 23/24/25/29 ✓ |
| CYW43439 在 2.2 MB/s 持续 UDP 下的**丢包率**？ | 上行 iperf 是 TCP 的数字；UDP 的丢片率决定分片大小与是否要 FEC |
| RP2040 @133/440 MHz 解 480×320 JPEG 的**单帧耗时**？ | 决定 M3 的帧率上限与"JPEG 还是 QOI"的选型 |
| 是否要超频？ | 超频会连带抬高面板总线时钟（`pico-display-lib` 的已知耦合点 ✗），要成对验证 |

## 相关

- [`wifi-link-pico-w.md`](wifi-link-pico-w.md) —— 链路实测与官方例子的两处上游 bug
- [`../AGENTS.md`](../AGENTS.md) —— 本仓规则（含"先跑通官方例子"与硬件纪律）
- `../Pico-USB-Display/notes/` —— USB 版的解码流水线、帧槽背压、看门狗自愈（结论可直接借鉴）
