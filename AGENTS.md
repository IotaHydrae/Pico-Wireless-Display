# AGENTS.md — Pico-Wireless-Display (PWD)

**PWD = 让 Pico W 当无线显示设备**：主机把画面压成一帧一帧的图片，经 WiFi 推过来，设备自己
解码上屏。本文件是接手须知；细节知识在 [`notes/`](notes/README.md)。

## 接手前必读（**强制**）

> 动这个仓库的任何代码、配置或文档**之前**，先读工作区 `../skills/` 里的四份 skill
> （**全文随仓携带**在 [`skills/`](skills/)，与工作区逐字节相同；改任何一份都要把全部副本一起
> 同步，见工作区 `AGENTS.md` §0 的校验命令），并按其中的规则做事。
> 它们**不是参考资料，是强制流程与验收标准**；不符合其中规则的产出视为未完成。

| skill | 一句话 | 本仓最容易踩的 |
|---|---|---|
| Repository Exploration | 先理解再修改；证据优先于直觉 | 未确认就写 `PROBABLY`；拿"通常如此"替代本仓代码事实 |
| Knowledge | 首屏结论、事实分级、信息预算、**漂移检查** | 把研究过程写进知识库；`cat >>` 追加"更新于某日" |
| Testing | `tests/` 解释观察、`tools/` 只出事实；oracle 显式声明 | 拿观测值当期望值；硬件缺失报成 FAIL（应为 ENVIRONMENT_ERROR）|
| Code Quality | **能跑 ≠ 完成**；可读性有硬标准 | 注释写"是什么"；过时注释不删；机制/策略混在一起 |

工作区级约定（四行闸门、全局纪律、优先级）见 [`../AGENTS.md`](../AGENTS.md)。

### 上手顺序：先跑通官方例子（**强制**）

碰新平台、新外设、新协议栈、没做过的事 —— **先把上游官方例子跑通**（厂商 SDK / 官方仓库里那个
demo），用它自己的输出证明工具链、烧写、接线、时钟、供电、链路都是好的，**再**动自己的代码。

- **复用官方那份构建配方**：`CMakeLists` / `Kconfig` / `sdkconfig`、官方引脚与初始化顺序照搬；
  官方例子源码**逐字不动**（放在 `third_party/`，不改写），需要差异就另开 wrapper 或独立文件承载。
- **自己的代码 = 官方例子 + 最小 delta**：每处 delta 都能指认"改了哪一行 / 哪个选项、为什么"，
  并单独验证。不许凭经验把官方初始化流程重写一遍。
- **官方例子跑不通时先怀疑仪器**（串口读者、量具、烧写、接线、常驻进程的生死），按证据分层记录；
  **不许据此宣布"硬件坏了"** —— 先让量具自证：读者要自报读到的字节数、先开读者再复位、常驻监听
  要用受管后台作业（`setsid … &` 会随调用一起被杀）。
- **官方例子本身也要验**：长期没人编的分支可能根本编不过（缺宏），看起来正常的调用可能从未执行
  （`assert()` 在 Release / `NDEBUG` 下被整句删掉）。判据是**观察到的行为**，不是"代码长这样"。

本仓的落地就是 `baseline/`：它**原样** `add_subdirectory` 官方 `pico_w/wifi/iperf`，
凭据从 `-C <wifi-config.cmake>` 传（该文件被 gitignore）。

## 1. 仓库约定

- **未经明确指令，不要 `git commit` / `git push`**；提交用 `git commit -s`（带 `Signed-off-by`），
  摘要 kernel 风格 `子系统: 祈使句`（如 `wifi: 收下分片后按帧号组帧`），一个逻辑改动一个提交。
- **一轮只做一件事**；诊断用的探针代码**提交前必须清干净**（临时开关、额外日志、面包屑都算），
  或者收进显式开关。
- 仓库内**不写**绝对主机路径 / 内网 IP / 口令 / SSID / 私有设备名：用 `<pico-sdk>`、`<PC-IP>`、
  `<your-ssid>` 这类占位符；WiFi 凭据只存在于被 gitignore 的文件里。
- **一次只动一个变量，每次都留判据**；不许盲目 `sleep`、不许 blanket 超时 —— 用**轮询就绪 + 秒级超时**。

## 2. 分层与目录

| 路径 | 是什么 | 规则 |
| --- | --- | --- |
| `baseline/` | **官方例子的 wrapper**（零 delta 优先） | 只放 wrapper 与说明，不改官方源码 |
| `third_party/` | 取来的上游源码（`pico-examples` 等） | **gitignore，逐字不改**；版本钉在 SDK tag |
| `notes/` | 知识库 | 按 developer-knowledge skill 维护（首屏结论、事实分级 ✓/✗、合并重写、漂移检查） |
| `tools/` | **只产出机器可读事实**的工具（`工具名 <子命令>`，`--json/--timeout`） | 不内嵌项目结论；退出码 `0/1/2/3/4/5` |
| `tests/` | **解释观察**的验证脚本（显式 oracle ⇒ PASS/FAIL/INCONCLUSIVE） | 调用 `tools/`，不反向；硬件缺失是 `ENVIRONMENT_ERROR` 不是 `FAIL` |
| `scripts/` | 构建/取依赖/凭据一类的一次性脚本 | 幂等、可重跑；不藏状态 |
| `skills/` | 工作区四份 skill 的**副本** | 与工作区逐字节相同（见上） |
| `src/`（M1 之后） | 本仓自己的固件代码 | 传输与帧调度在这里；显示/解码用 `pico-display-lib` |

## 3. 与其他仓的关系

- **共用显示与解码**：`lib/pico-display-lib`（子模块）= 面板驱动 / 总线 / 输入 / QOI·RLE 解码。
  **不要在 PWD 里长第二套面板驱动**，也不要把面板参数写死 —— 面板配置走 `configs/`（M2 时建立，
  照 `Pico-USB-Display/configs/` 的写法）。
  本仓面板 = **YT350S006**（480×320、**8080 16-bit**、控制器 ST7796 的 `YT350S006` 变体）+ **GT911** 触摸；
  **PUD 那份 `pico_dm_yt350s006.cmake` 是 SPI 变体，不能照抄** ✗；而且 Pico W 上
  **GPIO 23/24/25/29 归无线模组** ✓（`boards/pico_w.h` 的 `CYW43_DEFAULT_PIN_*`）⇒ 引脚排布要重算，
  见 `notes/design.md` 的"面板与触摸"。
- **PUD 是最直接的参考**（`../Pico-USB-Display/`）：它的解码流水线、帧槽背压、看门狗自愈、
  以及"不要在中断里解码"这类结论同样适用；但**它的协议是 USB 的**，PWD 不复用那套端点协议。
- **PC 侧工具**：`pico_dm_qd3503728_esp32p4_idf/wireless/p4_wireless_display/tools/` 里的
  `pud_media.py`（ffmpeg → JPEG）与 `pudnet.py`（发送端）是现成的起点，能复用就复用，
  不要重写编码参数那套已经踩过坑的映射。

## 4. 传输设计（M1 定稿前先读 `notes/design.md`）

- 帧走 **UDP + 分片组帧**；字段布局沿用 P4 无线投屏那套（`[u32 帧号][u16 片序][u16 片数][u32 总长]`
  + ≤1400 B 载荷），**字段只追加、不重排、不复用编号** —— 将来 PC 侧工具要能两边通用。
- 组帧策略：**只显示最新的完整帧**，缺片就整帧丢并计数（不做无限等待、不做重传）。
- 统计必须自报：收到的片/帧、丢的片/帧、当前帧率与 B/s —— 这些是 M1 唯一的判据来源。

## 5. 硬件纪律（Pico W，都是这轮踩出来的）

1. **先开读者，再复位/烧写** ✓：串口日志只在"读者已经在听"之后才算数；读者要**自报读到的字节数**
   以自证工作。`/dev/ttyACM0` 是调试器 UART 桥（GP0/GP1，**115200 8N1 raw**，要显式 `stty`）。
2. **常驻监听/服务端要用受管后台作业** ✓；`setsid … &` 起的进程会随调用结束被杀 ✗ ——
   曾因此把"没有监听者"误读成"设备连不上"。
3. **烧写用 openocd**：`program <elf> verify reset exit`（Pico W 是 RP2040 ⇒ `target/rp2040.cfg`）；
   写完以复位收尾，**不要把核留在 halt** ✗（bootrom 的 USB 会一起下线）。
4. **别用 `pkill -f '名字'`** ✗（会杀掉自己的 shell）；用 `pkill -x 名字` 或按 PID 杀。
5. **`-DNDEBUG` 会删掉 `assert(expr)` 里的整个表达式** ✗：若调用的唯一副作用写在 `assert()` 里，
   Release 构建下它**不会发生**。官方例子里就有这种代码（见 `notes/wifi-link-pico-w.md`）。
6. **驱动版本、宏定义、文件是否真的被编译**，三件事都要在相信任何数字之前核实（编译行 / 生成物
   `strings` / 唯一字符串，别用会自匹配的 `grep`）。

## 6. 构建 / 烧写 / 验证入口

```bash
export PICO_SDK_PATH=<pico-sdk>            # 本机装的是 2.3.1（`pico_sdk_version.cmake` 为准 ✓）
scripts/fetch-deps.sh                      # 取 pico-examples，tag 自动钉到 SDK 版本（现在是 sdk-2.3.1）
scripts/make-wifi-config.sh -o /tmp/pwd-wifi.cmake   # 凭据（600，gitignore）

# ① 服务端基线（上行：PC → 设备）
cmake -S baseline -B build-baseline -DPICO_BOARD=pico_w -C /tmp/pwd-wifi.cmake
cmake --build build-baseline -j
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg \
        -c 'program build-baseline/iperf/picow_iperf_server_background.elf verify reset exit'

# ② 客户端基线（下行：设备 → PC；两处 delta 只作用于 picow_iperf.c）
cmake -S baseline/iperf_client -B build-baseline-client -DPICO_BOARD=pico_w \
      -C /tmp/pwd-wifi.cmake -DPWD_PC_IP=<PC-IP>
cmake --build build-baseline-client -j
```

- 凭据：`scripts/make-wifi-config.sh` 生成 `<wifi-config.cmake>`（`chmod 600`，**gitignore**）。
- 验证顺序：**宿主机/离线 → 真机**；真机上"读日志先于烧写"，一次只改一个变量。
- 收尾自查：构建无新增 warning；`notes/` 与代码无漂移；统计判据写进 `notes/`。

## 7. 提交与身份

- `user.name` = `Wooden Chair`，`user.email` = `hua.zheng@embeddedboys.com`；一律 `git commit -s`。
- 默认分支 `main`；**提交与推送是两件事**，推送需要明确指令。
