# 超频接入：pico-turbo 与"编译期分频"的冲突

> **结论**：升频本身在这块板上**成功** ✓（300 MHz @1.20 V 实测 `reached=1`、寄存器也对得上 ✓），
> 但 **CYW43 起不来** ✗ ⇒ WiFi 连不上、`main()` 走失败分支返回。原因是所有**按 125 MHz
> 编译期算死**的总线分频不会跟着 `clk_peri` 走 ✓。**目前只有 240 MHz 档（`safe`）可用** ✓。

## TL;DR

| 档位（`lib/pico-turbo/boards/pico_w.cmake` ✓） | 频率 / 电压 | 本仓实测 |
| --- | --- | --- |
| `safe` | 240 MHz / stock 1.10 V / flash DIV 4 | **能用** ✓（在线、`[stat]` 正常）|
| `fast` | 300 MHz / 1.20 V / DIV 4 | **起不来** ✗（已逐步验证）|
| `turbo` / `extreme` | 360 / 440 MHz / 1.20 / 1.30 V / DIV 4 | 同 `fast`（未逐个复测 ✗）|

- ⚠️ **`-DPICO_BOARD=pico_w` 不给 `-DPICO_TURBO_PROFILE` 时，pico-turbo 默认选 `extreme`（440 MHz）**
  ✗ ⇒ 本仓在 `CMakeLists.txt` 里把默认档改成 `safe` ✓（要快就显式给档位 ✓）。
- 失败**不是**时钟不稳、**不是** panic ✗：300 MHz 档下 `s_state = reached=1, sys=300000 kHz,
  vreg_sel=13`，寄存器 `CLK_SYS div=1.0`、`VREG VSEL=13 ROK=1` ✓ —— 升频与调压都到位了 ✓。
- 真正的失败点：**CYW43 芯片起不来** ✗（控制台唯一一行错误是 `[CYW43] Failed to start CYW43`）。
- 机制：面板 i80 与 CYW43 的 PIO/SPI 分频都是**编译期**按 `clk_peri = 125 MHz` 算死的 ✓，
  而 `pico_turbo_init()` 把 `clk_peri` 一并指到了 `clk_sys`（`s_state peri=sys` ✓）。
- 本仓已尝试在 turbo 之后把 `clk_peri` 拨回 125 MHz，**未生效** ✗（详见文末"未解"）。

## 现场与判据（怎么认出"没起来"而不是"挂了" ✓）

| 现象 | 判据 / 说明 |
| --- | --- |
| PC 停在 `_exit` 的 `__breakpoint` | **不是 panic** ✗：`nm` 定位到 `_exit`（`pico/platform.h` 的 `__breakpoint` ✓）+ LR 在 `exit` 里 ⇒ 是 `main()` 的失败分支 `return 1`，crt0 调 `exit()` ✓ |
| 控制台只有 `Connecting to Wi-Fi...` | USB CDC 的读者**必须先带 DTR 就位** ✓，否则开机那几行直接丢 ✗（见 [`../AGENTS.md`](../AGENTS.md) 硬件纪律 9）|
| ping 不通、DHCP 无地址 | 与"连上后掉线"区分：`[stat]` 一行都没出现 ⇒ 主循环没进去 ✓ |
| SWD 直读 `s_state` | `nm build/pwd.elf` 取地址后 `mdw` ✓ —— 直接看它跑到哪一档（含 `reached`/`sys`/`vreg_sel`/`usb_ok` ✓）|

## 为什么：编译期分频（实测注入值 ✓）

| 谁 | 分频从哪来 | 300 MHz 档下的结果 |
| --- | --- | --- |
| 面板 i80 | `pico_i80.c`：`DEFAULT_PIO_CLK_KHZ / 2 / TFT_BUS_CLK_KHZ` ✓，而 `DEFAULT_PIO_CLK_KHZ=125000` 是**编译期注入**的 ✓ | PIO 时钟 = `300/2.5 = 120 MHz` ✗（远超 ILI9488 的余量）|
| CYW43 | `cyw43_bus_pio_spi.c`：`CYW43_PIO_CLOCK_DIV_INT 2`，且 `CYW43_PIO_CLOCK_DIV_DYNAMIC 0` ⇒ 运行期不可改 ✓ | PIO 时钟 = `300/2 = 150 MHz` ✗ ⇒ **芯片起不来** ✓ |

⇒ 这正是 pico-turbo 自己的 README「调用之后：还有哪些东西必须自己跟上」警告的那类问题 ✓：
**凡是编译期从 `clk_sys`/`clk_peri` 推导出来的时序，抬频后全部作废** ✓。
（`peri/分频` 只是 PIO 时钟；实际 SPI 位率还取决于 PIO 程序的每比特周期数 ✗ 未核对。）

顺带记住：240 MHz 档下 CYW43 的 PIO 时钟是 `240/2 = 120 MHz`，**它却是能用的** ✓
⇒ "CYW43 的 PIO 分频上限"这条边界落在 120–150 MHz 之间 ✗（只测了两个点，未二分定界 ✗）。

## 未解 ✗（未验证，不许当结论）

把 `clk_peri` 拨回 125 MHz 的调用**没有生效**：

- 观测：`clock_configure(clk_peri, 0, CLK_SYS, 300 MHz, 125 MHz)` **返回成功** ✓、参数在生成物里核对无误 ✓
  （调用点反汇编：`r0=6`(clk_peri)、`r1=r2=0`、`r3=sys_khz*1000`、第 5 个参数常量 `0x07735940` = 125000000 ✓），
  但 `CLK_PERI_DIV` 寄存器**仍是 `0`**（`0` = 不分频 ⇒ peri = clk_sys）✗。
- 线索：链接进来的 `clock_configure`（`clocks.c:99` ✓）反汇编显示它走了 `div64 >> 32` 分支
  （该分支把 `div` 置 0 = 最大分频），而它用的 64 位除法是 `__wrap___aeabi_uldivmod`。
- 下一步：在板上**单测那一步 64 位除法**（`(300e6<<8)/125e6` 应为 614，高字为 0）✗；
  或改用"直接写 `clocks_hw->clk[clk_peri].div` + 回读比对"绕开该路径 ✓。
- 也待定：面板总线到底该跟着 `clk_sys` 走（那就得在运行期按 `clock_get_hz(clk_sys)` 重算 PIO 分频 ✓）
  还是钉在 125 MHz ✓ —— 取决于上面那条修不修得好 ✓。

## 可复用的诊断手法（本次都实际用上 ✓）

- **SWD 读 RAM 里的状态**（`nm` 现取符号地址 ⇒ `mdw`）：不看控制台也能知道固件跑到哪 ✓。
- **栈上的返回地址 + `addr2line`**：判"panic 还是正常 `exit`" ✓（本仓曾把 `_exit` 的 `bkpt` 误读成 panic ✗）。
- **回读 flash 比对镜像**：确认烧进去的确实是刚构建的那份 ✓（对照 ELF 里的字符串地址 ✓）。
- **USB CDC 读者要带 DTR**，否则早期日志全丢 ✓。

## 相关

- [`sdk2-clocks.md`](sdk2-clocks.md) —— SDK 2.x 时钟 API 的两个坑（`clock_get_hz()` 只读缓存 ✓、`set_sys_clock_khz()` 会改 `clk_peri` ✓）
- [`design.md`](design.md) —— 面板总线的编译期分频、M3 的解码预算
- `lib/pico-turbo/README_zh.md` —— 档位表、"调用之后"、故障排查（权威 ✓）
