# M1：UDP 收帧统计（真机验收）

> **结论：通过** ✓ —— 主机发 **100 帧 × 22000 B @30 fps**（1600 片），设备侧
> `frags_rx=1600` ✓、`frames_complete=100` ✓、`frames_incomplete=0` ✓、`dup/stale/bad=0` ✓、
> `bytes_complete=2 200 000` ✓ ⇒ **收下的片/帧/字节与发送侧逐项对账一致** ✓。
> 持续 **0.66 MB/s（5.33 Mbit/s）**，约占上行预算的 **31%** ✓（与设计里的估算吻合 ✓）。
> 同一组数字在两个不同二进制上各跑一次，**完全复现** ✓。

## TL;DR

- 里程碑 M1（**不上屏**的收帧统计）达成 ✓；协议与计数器的定义见 [`design.md`](design.md) §传输设计。
- 判据是**关系型 oracle**：`frames_complete + frames_incomplete == 发送帧数` ✓，外加
  `frags_rx == 发送片数`、`bytes_complete == 帧数 × 帧长` ✓ —— 都是"两边必须对上账"的关系，
  不是"上次测出来是 100 所以这次也得是 100" ✗。
- 读数通道：**串口不通**（见下）⇒ 这一轮的计数器是**用 SWD 直接读 RAM 里的 `s_stats`** ✓。

## 条件（照抄判据时要带上）

| 项 | 值 |
| --- | --- |
| 板子 | 官方 Pico W（RP2040 + CYW43439），**原厂 133 MHz**（未超频 ✓） |
| 固件 | 本仓 `src/`（`pico_cyw43_arch_lwip_threadsafe_background`，lwIP 选项用官方例子的公共 lwipopts ✓） |
| 发送端 | `tools/pwd_send.py`（PC 侧，标准库 ✓），确定性载荷 22000 B／帧，`--rate 30` |
| 网络 | 家用 2.4G AP，设备 `<device-ip>`，PC 同网段（AP/PC 细节见 [`wifi-link-pico-w.md`](wifi-link-pico-w.md)） |
| 单帧 | 22000 B ⇒ **16 片**（1400 B／片 + 12 B 头） |
| 读数 | SWD：`openocd … -c 'init' -c 'halt' -c 'mdw <&s_stats> 7' -c 'resume'`（符号地址用 `arm-none-eabi-nm build/pwd.elf` 取 ✓） |

## 结果

| 计数器 | 值 | 对账对象 | 结论 |
| --- | --- | --- | --- |
| `frags_rx` | 1600 | 发送 1600 片 | ✓ **零丢片** |
| `frags_dup` / `frags_stale` / `frags_bad` | 0 / 0 / 0 | —— | ✓ 没有重复、乱序旧帧、坏片 |
| `frames_complete` | 100 | 发送 100 帧 | ✓ |
| `frames_incomplete` | 0 | —— | ✓ 没有半截帧 |
| `bytes_complete` | 2 200 000 | 100 × 22000 | ✓ 逐字节对得上 |

**带宽**：`sent_bytes=2 219 200`（含每片 12 B 头 ✓）、用时 **3.333 s** ⇒ **5.33 Mbit/s**。
pc 侧发得动、设备侧一片不丢 ⇒ 在 30 fps／22000 B 这个工作点上**链路有余量** ✓
（更高速率下的丢包率还没测，见"遗留" ✗）。

## 踩坑：一个写错的校验，被计数器的"指纹"抓出来

第一次跑出来的指纹是 **每帧 15 片被拒、只有 1 片被收**（`frags_rx=100`、`frags_bad=1500`、
`frames_complete=0` ✓）：

- 真凶是这条**方向写反**的自洽校验 ✗：
  `(frag_idx + 1u) * PWD_FRAG_PAYLOAD < frame_len` —— 它对**每个非末片**都成立，
  于是把所有"还有后续"的分片全判成坏片 ✗。
- 正确的自洽校验应当拿 **片数** 比 **帧长**（而不是片下标 ✗）：
  `frame_len > frag_cnt * PWD_FRAG_PAYLOAD`（片数装不下帧长）或
  `frame_len <= (frag_cnt - 1) * PWD_FRAG_PAYLOAD`（多出来的末片是空的）✓。
- 教训（可复用）：**"15/16 被拒、恰好是除末片以外全部"这个比例本身就是指纹**，
  它把范围直接锁到"与 idx 有关的判断"上 ✓；而 `frags_rx=100`（每帧 1 片）排除了"链路丢包" ✗。

另外两个当场踩到的工具坑：

- **openocd 0.12 把 RP2040 两个核当 SMP 组** ✗：只 `halt` core0 就写内存会报
  `[rp2040.core1] not halted` / `resume failed` ⇒ 写之前**两个核都要 halt**（`-c 'halt' -c 'rp2040.core1 halt'`）✓。
- **别用会自匹配的 `grep`** ✗：验证"探针有没有被编进去"要用唯一字符串，或直接查 `.o`/`.elf` 的符号 ✓。

## 读数通道：串口目前不通 ✗（人为，不是故障 ✓）

- 现场把调试器的 UART 桥与设备之间的**串口线断开了**（为了接面板）⇒ 读者 90 s 读到 **0 字节** ✓，
  且**用 SWD 直接往 `UART0` 的 TX FIFO 写 `PWD-TEST\n` 也收不到** ✓ ⇒ 是**通路**不通，不是固件不打印 ✓。
- 因此别把"没看到日志"当成"设备没跑" ✗：这一轮是靠 SWD 读 PC（`timer_time_reached` = 在跑 ✓）
  与读 `s_stats` 拿到的结论 ✓。
- 现在固件**UART + USB 双 stdio** ✓（`CMakeLists.txt`）：插上 **Pico W 自己的 USB** 就有独立控制台 ✓
  （`pico_w` 的 SDK 默认只开 UART ✗，这是本仓的一处显式 delta ✓）。串口线接回去也能用 ✓。

## 遗留 ✗

- **更高速率 / 更长时间**下的丢包率（本轮只到 5.33 Mbit/s、3.3 s；上行余量按 iperf 还有 ~3 倍 ✓）。
- `dup`/`stale` 两条路径**没有被真实触发过** ✗（重发、乱序都要专门造场景才能验）。
- **M2 v1 已通** ✓（原始 RGB565 分带，见 [`design.md`](design.md) 的 M2 v1 一节）；**解码（QOI）** 还没做，解码必须搬到任务里做
  （不能在 lwIP 回调里解码 ✗，PUD 的实测结论 ✓）。

## 相关

- [`design.md`](design.md) —— 协议、组帧策略、复用边界、面板与引脚约束
- [`wifi-link-pico-w.md`](wifi-link-pico-w.md) —— 上下行链路实测与官方例子的两处上游 bug
- `tools/pwd_send.py`（发送端）、`tools/pwd_console.py`（串口读者，自报字节数 ✓）
