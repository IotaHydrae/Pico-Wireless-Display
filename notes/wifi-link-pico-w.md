# Pico W 无线链路实测（官方例子 + iperf2）

> **一句话**：这块 Pico W 的 **PC→设备** 吞吐 **17.4 Mbit/s ≈ 2.2 MB/s**（投屏用的就是这个方向），
> **设备→PC** 只有 **≈6.7 Mbit/s ≈ 0.83 MB/s**。
> 480×320 JPEG q85（≈22 KB/帧）跑 30 fps 需要 0.66 MB/s ⇒ **占上行约 30%** ✓。

**结论的强度**：上行/下行都有**两个独立 oracle**（PC 侧 `iperf` 与设备侧官方例子的 report）
逐次吻合 ✓，各 3–4 次采样 ✓。**不是**单次采样。

## 测量条件

| 项 | 值 |
| --- | --- |
| 板子 | 官方 **Pico W**：RP2040（B2）+ CYW43439（2.4G only）+ 2 MB flash（W25Q16JV） |
| 频率 | **原厂 133 MHz**（本组数字**未**超频 ✓） |
| SDK / 例子 | **本机 `pico-sdk` = 2.3.1**；量测时用的**例子**是 `pico-examples @ tag sdk-2.2.0`（官方源码逐字不动，只注入 SSID/PSK）。⚠️ 注意 2.2.0 是**例子**的 tag，**不是** SDK 版本 —— 早期记录把两者写成同一个版本 ✗，已改。 |
| 固件 | 上行 = 官方 `picow_iperf_server_background`；下行 = 官方 `picow_iperf.c` 的 `CLIENT_TEST` 分支（见下面"上游 bug"） |
| 无线 | 家用 2.4G AP `<your-ssid>`，**chan 11**，同房间；设备 IP 由固件打印为 `<device-ip>` |
| PC | 同一 AP 的 2.4/5G 客户端 `<PC-IP>`，`iperf 2.2.1`（**iperf2 协议**，与 lwIP 的 lwiperf 对口；`iperf3` 不兼容 ✗） |
| 串口 | `/dev/ttyACM0` = 调试器 UART 桥（GP0/GP1，115200 8N1） |

## 结果

| 方向 | 每次 | 中位 | 对应字节/秒 |
| --- | --- | --- | --- |
| **上行 PC→设备**（设备收）10 s ×3 | 17.4 / 17.4 / 16.0 Mbit/s | **17.4** | **≈2.2 MB/s** |
| **下行 设备→PC**（设备发）10 s ×4 | 6.2 / 6.8 / 6.6 / 6.9 Mbit/s | **≈6.7** | **≈0.83 MB/s** |

- 下行的设备侧 report 与 PC 侧 `iperf -s` 汇总**逐次吻合** ✓：
  `6.2↔6.21`、`6.8↔6.76`、`6.6↔6.59`、`6.9↔(第 4 次同时间窗)` Mbit/s。每次传输 7–8 MB、时长 10.008 s。
- **方向不对称** ✗：下行只有上行的 ~38%。原因**未验证**（lwIP 发送路径 / CYW43 SPI 写路径 /
  省电模式都还没排除）。⇒ **不要拿上行数字去外推下行** ✓。

### 同时测到的其它事实

| 项 | 结果 | 备注 |
| --- | --- | --- |
| ping RTT | min 14.3 / avg **77.0** / max 160.8 ms ✗ | 抖动大；**假设**（未验证）：CYW43 省电模式 |
| `iperf -R` / `-d`（要服务端发） | **0 字节** ✗ | 本版 lwiperf **服务端不实现 iperf2 的反向请求**；这是**仪器能力限制**，不是链路限制 |
| UDP（`iperf -u -b 20M`） | **不可信** ✗ | 客户端自报 21 Mbit/s，但 `did not receive ack of last datagram after 10 tries`；这版没有可用的 UDP 服务端 |
| 与 P4 的 C6 对照 | P4 ≈ **4.95–5.03 MB/s**（~40 Mbit/s） | 同一 AP、同一 PC ⇒ Pico W 上行 ≈ 它的 **44%** |

## 上游 bug：官方 iperf 例子的客户端模式坏了两处 ✗

两处都在 `pico_w/wifi/iperf/picow_iperf.c` 的 `#if CLIENT_TEST` 分支（第 77–81 行）：

```c
    printf("\nReady, running iperf client\n");
    ip_addr_t clientaddr;
    ip4_addr_set_u32(&clientaddr, ipaddr_addr(xstr(IPERF_SERVER_IP)));
    assert(lwiperf_start_tcp_client_default(&clientaddr, &iperf_report, NULL) != NULL);
```

1. **`xstr` 宏不存在** ⇒ **编不过**。证据：`grep -rn 'define xstr' pico-examples/ ~/.pico-sdk/`
   无命中；报错 `implicit declaration of function 'xstr'`。旁证：官方 `iperf/CMakeLists.txt`
   只建 `picow_iperf_server_background` / `_server_poll`，**从没建过 client target**。
2. **唯一的启动调用写在 `assert()` 里** ⇒ Release 的 `-DNDEBUG` **把整句删掉** ✗
   （pico-examples 的 `CMAKE_BUILD_TYPE=Release`，编译行里确有 `-DNDEBUG`）。
   现象：客户端打印 `Ready, running iperf client` 之后**再无输出**、PC 侧 `ss` 四个端口全无连接 ——
   看起来像"网络不通"，实际是**客户端从没启动**。

**最小修法**（官方源码逐字不动，只对这一个源文件加两个编译选项）：

| delta | 作用 |
| --- | --- |
| `-include client_compat.h`（内容只有 `#define xstr(s) s`） | 让 CMake 传**带引号**的 IP 字面量（与隔壁官方 `tcp_client` 例子的写法一致） |
| `-UNDEBUG` | 让那句 `assert(...)` 真的求值 ⇒ 启动调用被执行 |

> 校验手段：把探针写进 `client_compat.h` 后，**只有加了 `-UNDEBUG` 时**探针才会被编进目标文件
> （`strings <obj> | grep '\[probe\]'` 可验）⇒ 直接证明了此前整句被删除。
> 注意 `-include` 要**只作用于** `picow_iperf.c`：SDK 的 `lwiperf.c` 也被编进同一个 target，
> 全局 `-include` 会把 `lwiperf.c` 里 `lwiperf_start_tcp_client_default` 的**定义**也宏替换掉 ✗。

## 本仓（PWD）里 M0 的现状

- `scripts/fetch-deps.sh` 把例子钉在**与本机 SDK 相同的 tag** ⇒ 现在取到的是 **`sdk-2.3.1`**
  （2026-09-03），**不是**上面那组数字用的 `sdk-2.2.0`。
  ⇒ **条件变了，旧数字不自动适用** ✗：M0 要在本仓重测一遍，把结果写回本文（并标注测得时的 tag ✓）。
- 今天已在 `sdk-2.3.1` 上核对：**两处上游 bug 仍然存在** ✓（`grep -rn 'define xstr'` 在
  pico-examples 全仓 0 命中；启动调用仍在 `assert(...)` 里；官方 `iperf/CMakeLists.txt` 里
  `CLIENT_TEST` 出现 0 次）⇒ 上面的分析对新 tag 同样成立 ✓。
- 今天已在 PWD 里编过两个基线 ✓：服务端 `build-baseline/iperf/picow_iperf_server_background.elf`、
  客户端 `build-baseline-client/pwd_iperf_client.elf`（两处 delta 只作用于 `picow_iperf.c` 一个文件 ✓）。

## 复现（最小步骤）

```bash
# 基线：官方服务端（零 delta）
cmake -S baseline -B build-baseline -DPICO_BOARD=pico_w -C <wifi-config.cmake>
cmake --build build-baseline -j
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c 'program build-baseline/iperf/picow_iperf_server_background.elf verify reset exit'
# PC 侧（受管后台作业 / 或另开终端；重定向到文件会被全缓冲，别拿它当 oracle ✗）
stdbuf -oL iperf -s -i 1
iperf -c <device-ip> -t 10          # 上行：设备收
```

- 设备 IP 由固件打印；下行要用 `CLIENT_TEST` 那个目标（见上"上游 bug"）。
- **判据**：PC 侧数字与设备串口 report 必须吻合；只看一侧不够 ✓。

## 仍然不知道的 ✗

- 我自己手写的探针固件（带 `pico-turbo` + vendored lwipopts）为什么 `[CYW43] Failed to start CYW43`。
  官方例子在同板同接线同 AP 上一切正常 ⇒ 这条**不必再查**：需要什么能力就在官方例子上加最小 delta ✓。
- 官方服务端空闲时偶见的 `[CYW43] STALL(0;200-200): timeout` / `send_ethernet failed: -2`
  的触发条件（出现过一次，未复现）。
- 2.2 MB/s **持续 UDP** 下的丢包率（iperf 那版没有可用的 UDP 服务端）。
