# SDK 2.x 时钟 API 的坑（RP2040）

> **结论**：`clock_get_hz()` **不读寄存器，只返回内部缓存** ✓ ⇒ 任何"读回实际频率"的判断都必须
> 读 `clocks_hw->clk[x].div`；而 `set_sys_clock_khz()` 会**顺带把 `clk_peri` 指到 `clk_sys`** ✓
> ⇒ 抬频之后所有按旧 `clk_peri` 编译期算死的分频（PIO/SPI）立即失准 ✗。

**适用**：pico-sdk **2.3.1**、RP2040（`~/<pico-sdk>` 实测 ✓）。行号为该版本 `clocks.c` ✓。

## TL;DR

- `clock_get_hz(clock)` = `return configured_freq[clock];` ✓ —— **纯缓存**，不是寄存器读数 ✗。
  缓存只在 `clock_configure*()` 里更新 ⇒ 别人用寄存器直接改了时钟，缓存就是**陈旧的** ✗。
- 读回真值：`volatile uint32_t *d = &clocks_hw->clk[clk_peri].div;` ⇒ 8.8 定点（`0x100` = ÷1，
  `0x266` = ÷2.4，`0` = 不分频）✓。
- `set_sys_clock_khz()` / `set_sys_clock_pll()` 内部会用 `clock_configure_undivided(clk_peri, …)` ✓
  ⇒ **`clk_peri` 被设成"不分频地跟着 `clk_sys`"** ⇒ 抬到 300 MHz 时 peri 也变 300 MHz ✗。
- `clock_configure(clock, src, auxsrc, src_freq, freq)`：`assert(src_freq >= freq)` ✓、返回 `bool` ✓、
  分频是 8.8 定点 ✓、RP2040 会把 < 2.0 的商钳到 1.0 ✓。`src_freq` 必须给**真实**频率 ——
  拿 `clock_get_hz()` 的缓存值当输入，算错也不会报错 ✗。

## 改 clk_sys 后必须一起核对的编译期分频 ✓

| 位置 | 常量 | 后果 |
| --- | --- | --- |
| `pico-display-lib` 的 i80 总线 | `DEFAULT_PIO_CLK_KHZ`（配置里 125000 ✓，`pico_i80.c` 用它算 PIO 分频 ✓） | 总线时钟随 `clk_peri` 上涨 ✗ |
| CYW43 无线 SPI | `CYW43_PIO_CLOCK_DIV_INT 2` + `CYW43_PIO_CLOCK_DIV_DYNAMIC 0` ✓ | WiFi 芯片起不来 ✗（见 [`pico-turbo-overclock.md`](pico-turbo-overclock.md)）|
| 其它 | 写死的 SPI 波特率、PWM wrap、按周期校准的循环 ✓ | 同类失准 ✗ |

`CYW43_PIO_CLOCK_DIV_DYNAMIC=1` 时驱动才提供 `cyw43_set_pio_clkdiv_int_frac8()` 可在运行期改 ✓（默认 0 ✗）。

## 未验证 ✗

`clock_configure()` 在板上出现过"**返回成功但分频寄存器没变**"：它走了 `div64 >> 32`（`div = 0`）分支，
用的是 `__wrap___aeabi_uldivmod`（`clocks.c:99` 那份实现 ✓）。疑点在那步 64 位除法，**未证实** ✗。
详见 [`pico-turbo-overclock.md`](pico-turbo-overclock.md) 的"未解"一节 ✓。

## 出处

- `<pico-sdk>/src/rp2_common/hardware_clocks/clocks.c`：`clock_get_hz()` / `clock_configure()` /
  `clock_configure_internal()` / `set_sys_clock_pll()` ✓
- `<pico-sdk>/src/rp2_common/hardware_clocks/include/hardware/clocks.h`：API 注释（`src_freq >= freq` 等）✓
- `<pico-sdk>/src/rp2_common/pico_cyw43_driver/include/pico/cyw43_driver.h`：`CYW43_PIO_CLOCK_DIV_*` ✓
