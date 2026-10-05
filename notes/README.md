# Pico-Wireless-Display 知识库

> 本目录是 PWD 的**设计与踩坑记录**，面向维护者：怎么把一帧画面经 WiFi 送到 Pico W 并上屏。
> 面向使用者的说明在根 [`README.md`](../README.md)。通用知识库/测试约定见工作区根
> [`../../AGENTS.md`](../../AGENTS.md)。

**范围**：设备侧固件（RP2040 + CYW43439、lwIP、PIO/SPI 面板、解码流水线）与**传输协议**。
显示与解码的**权威实现**在 `lib/pico-display-lib`（子模块），本仓只记录"我们怎么用它"。
USB 侧的协议（EP1/EP2/EP4）**不属于本仓**，权威定义在
`../PUD-kernel-drivers/notes/usb-protocol.md`。

## 文档索引

| 分类 | 文档 | 一句话内容 |
| --- | --- | --- |
| 设计 | [design.md](design.md) | PWD 是什么、传输协议与组帧策略、复用边界、**已定项与 UNKNOWN 清单** |
| 实测 | [wifi-link-pico-w.md](wifi-link-pico-w.md) | 官方 iperf 例子量出的上下行速率、条件、以及官方例子里**两处上游腐化** |
| 实测 | [udp-ingress.md](udp-ingress.md) | **M1 收帧统计的真机验收**（100 帧零丢片，逐项对账 ✓）、协议下的踩坑、读数通道 |
| 实测 | [pico-turbo-overclock.md](pico-turbo-overclock.md) | 超频接入：**升频成功但 CYW43 起不来** ✗（编译期分频的冲突），目前只到 240 MHz |
| 通用 | [sdk2-clocks.md](sdk2-clocks.md) | SDK 2.x 时钟 API 的两个坑：`clock_get_hz()` 只读缓存 ✗、`set_sys_clock_khz()` 会改 `clk_peri` |

（M0 之后新增的实测/踩坑文档都登记到这张表里。）

## 维护约定

- 按 developer-knowledge skill：**首屏给结论**（标题 → 一句话 → TL;DR）、**一文档一问题**、
  事实分级（已验证 ✓ / 实测否定 ✗ / 观察注明条件 / 假设显式标注）、常规 50–150 行、
  更新用**合并后重写**（不 `cat >>` 追加"更新于某日"）。
- 数字必须带**测量条件**（板子、频率、构建、工具版本），并指向能复现的那条命令。
- 面板参数、时钟分频、缓冲上限这类**运行时事实以代码/配置为准**；每次改代码都要做一次漂移检查，
  过时的**结论**删掉，历史**测量数据**保留并标注"在配置 X 下测得"。
