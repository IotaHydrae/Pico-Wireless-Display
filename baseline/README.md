# `baseline/` —— 官方例子的基线（M0）

> **为什么有这一层**：本仓规则第一条是**先把上游官方例子跑通**再写自己的代码（[`../AGENTS.md`](../AGENTS.md)
> 的"上手顺序"）。这个目录就是把那条规则落成可执行的东西：**官方源码逐字不动**，delta 全在
> wrapper 与一个小兼容头里，每一处都能指认。

| 目标 | 是什么 | delta |
| --- | --- | --- |
| `baseline/`（本目录） | 官方 `pico_w/wifi/iperf` 的**服务端**（`picow_iperf_server_background` / `_poll`） | **零** ✓（官方 `CMakeLists.txt` 原样 `add_subdirectory`；只多了一句"凭据是否为空"的校验，写在我们的文件里） |
| `baseline/iperf_client/` | 官方 `picow_iperf.c` 的 **`CLIENT_TEST` 客户端模式**（设备→PC，用来量下行） | 两处，都只作用于 `picow_iperf.c` 一个文件：`-include client_compat.h`（补上游缺失的 `xstr`）+ `-UNDEBUG`（否则那句 `assert(...)` 连同启动调用被整句删掉 ✗） |

上游为什么坏、怎么验证、以及实测数字，全部记在 [`../notes/wifi-link-pico-w.md`](../notes/wifi-link-pico-w.md)。

## 跑起来

```bash
export PICO_SDK_PATH=<pico-sdk>
scripts/fetch-deps.sh                                   # 取 pico-examples @ sdk-<SDK 版本>
scripts/make-wifi-config.sh -o /tmp/pwd-wifi.cmake       # 生成凭据（600，gitignore）

# ① 服务端（上行：PC → 设备）
cmake -S baseline -B build-baseline -DPICO_BOARD=pico_w -C /tmp/pwd-wifi.cmake
cmake --build build-baseline -j
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c 'program build-baseline/iperf/picow_iperf_server_background.elf verify reset exit'
stdbuf -oL iperf -s -i 1          # PC 侧（**受管后台作业**，别用 setsid & ✗）
iperf -c <device-ip> -t 10        # 设备 IP 由它自己的串口打印出来

# ② 客户端（下行：设备 → PC）
cmake -S baseline/iperf_client -B build-baseline-client -DPICO_BOARD=pico_w \
      -C /tmp/pwd-wifi.cmake -DPWD_PC_IP=<PC-IP>
cmake --build build-baseline-client -j
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c 'program build-baseline-client/pwd_iperf_client.elf verify reset exit'
# PC 侧 iperf 服务端要**先起来**；设备侧的 report 会自己打印速率 ✓
```

**判据**：PC 侧数字与设备串口 report **必须吻合**（两个独立 oracle ✓）；只看一侧不算数 ✗。

## 烧写与读日志的纪律

- **先开串口读者，再烧写** ✓（读者要自报读到的字节数）；`/dev/ttyACM0` = 调试器 UART 桥，115200 8N1。
- 常驻的服务端（`iperf -s`）要用**受管后台作业**；`setsid … &` 起的进程会随命令结束被杀 ✗ ——
  曾因此把"没有监听者"误读成"设备连不上"。
- 烧写以复位收尾，**不要把核留在 halt** ✗。
