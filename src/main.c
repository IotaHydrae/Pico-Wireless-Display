/* PWD M1 主程序：连 WiFi → 起 UDP 收帧 → 每 2 秒报一次统计（**不上屏**）。
 *
 * WiFi 与连接这一段**照官方例子写**（pico-examples `pico_w/wifi/udp_beacon` 的 main()，
 * 逐句同构），只把"发 beacon"换成"起接收端 + 报统计" —— 本仓规则：官方能用的部分不重写 ✓。
 *
 * 统计行是 M1 唯一的判据来源，格式固定、机器可读：
 *
 *     [stat] frags=%u dup=%u stale=%u bad=%u frames=%u incomplete=%u bytes=%u fps=%.1f kB_s=%.1f
 *
 * 其中 frames/bytes 是**累计值** ⇒ PC 侧发 N 帧后可直接对账：frames + incomplete == N ✓。
 */
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/netif.h"

#include "pwd_net.h"
#include "panel.h"

#include "pico_turbo.h"
#include "hardware/clocks.h"

#define REPORT_PERIOD_MS 2000

static void report(void) {
    static pwd_stats_t prev;

    pwd_stats_t s;
    pwd_net_snapshot(&s);

    const uint32_t d_frames = s.frames_complete - prev.frames_complete;
    const uint32_t d_bytes = s.bytes_complete - prev.bytes_complete;
    const float secs = REPORT_PERIOD_MS / 1000.0f;

    printf("[stat] frags=%u dup=%u stale=%u bad=%u frames=%u incomplete=%u busy=%u "
           "bytes=%u bands=%u reject=%u fps=%.1f kB_s=%.1f\n",
           (unsigned)s.frags_rx, (unsigned)s.frags_dup, (unsigned)s.frags_stale,
           (unsigned)s.frags_bad, (unsigned)s.frames_complete, (unsigned)s.frames_incomplete,
           (unsigned)s.frames_busy_drop, (unsigned)s.bytes_complete,
           (unsigned)panel_bands_drawn(), (unsigned)panel_bands_rejected(),
           d_frames / secs, d_bytes / 1024.0f / secs);

    prev = s;
}

/* 面板 i80 的 PIO 分频与 CYW43 的 SPI 分频都是**编译期**按这个 clk_peri 算死的：
 * `DEFAULT_PIO_CLK_KHZ=125000`（库的 drivers/clk，实测注入值 ✓）、
 * `CYW43_PIO_CLOCK_DIV_INT 2` 且 `CYW43_PIO_CLOCK_DIV_DYNAMIC 0`（cyw43_driver.h ✓）。
 * 它们**不会**跟着 clk_sys 走 ⇒ 谁改了 clk_peri，谁就得负责把它拨回来。 */
#define PERI_BASELINE_KHZ 125000u

/* pico-turbo 抬 clk_sys 时会把 clk_peri 一并指到 clk_sys ✗（实测 s_state: peri=sys ✓）。
 * 不拨回来的后果实测过（300 MHz 档）：CYW43 报 `[CYW43] Failed to start CYW43`、
 * WiFi 连不上 ⇒ main() 走失败分支返回 ⇒ crt0 调 exit() ⇒ 现场 PC 停在 `_exit` 的 bkpt
 * （看起来像 panic，其实不是 ✗）；同一档下面板总线也会涨到 300/2.5 = 120 MHz ✗。 */
static uint32_t peri_div(void) {
    volatile uint32_t *div = &clocks_hw->clk[clk_peri].div;
    return *div;
}

static void restore_peri_clock(void) {
    /* src_freq 取 pico-turbo 报告的实际频率：`clock_get_hz()` 在 SDK 2.x 里只返回
     * `configured_freq[]` **缓存**（clocks.c: ledger 一行 `return configured_freq[clock];` ✓），
     * 缓存过时就会算错分频、而且不会报错 ✗。 */
    const uint32_t sys_khz = pico_turbo_state().sys_clk_khz;
    const uint32_t sys_hz = sys_khz ? sys_khz * 1000u : clock_get_hz(clk_sys);
    const uint32_t div_before = peri_div();

    (void)clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, sys_hz,
                          PERI_BASELINE_KHZ * 1000u);

    /* 判据是**寄存器回读**，不是缓存 ✓（8.8 定点：div = clk_sys/125MHz × 256） */
    const uint32_t div_want = (uint32_t)(((uint64_t)sys_hz << 8) / (PERI_BASELINE_KHZ * 1000u));
    printf("[clk] clk_peri: div 0x%x → 0x%x（期望 0x%x，sys=%u kHz）\n", (unsigned)div_before,
           (unsigned)peri_div(), (unsigned)div_want, (unsigned)sys_khz);
}

int main(void) {
    /* pico-turbo 的契约：**最先**调用，在 stdio_init_all() 与任何外设初始化之前 ✓
     * （它的 README「调用之后」与 API 一节明确写了 ✓）。 */
    pico_turbo_init();

    stdio_init_all();

    /* 先接管时钟，再初始化面板与 CYW43（两者都吃 clk_peri ✓） */
    restore_peri_clock();
    {
        pico_turbo_state_t st = pico_turbo_state();
        printf("[clk] pico-turbo: enabled=%d reached=%d sys=%u kHz vreg_sel=%u peri=%u kHz flash=%u kHz usb_ok=%d\n",
               (int)st.enabled, (int)st.reached, (unsigned)st.sys_clk_khz,
               (unsigned)st.vreg_sel, (unsigned)st.peri_clk_khz,
               (unsigned)st.flash_clk_khz, (int)st.usb_ok);
    }

    /* 面板先起：屏幕不依赖 WiFi，起不来也能立刻从"有没有颜色"看出来 ✓ */
    if (!panel_bringup()) {
        printf("[pwd] ✗ 面板没起来（继续跑网络部分，方便分别定位 ✗）\n");
    }

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    }
    printf("Connected.\n");

    /* 先把地址打出来：PC 侧要往这里发，双方都得看得到它 ✓ */
    cyw43_arch_lwip_begin();
    const char *ip = ip4addr_ntoa(netif_ip4_addr(netif_default));
    printf("[pwd] 设备地址 %s\n", ip);
    cyw43_arch_lwip_end();

    pwd_net_start();
    printf("[pwd] ready\n");     /* 就绪标记：PC 侧工具/测试等这一行，不靠盲等 sleep ✓ */

    absolute_time_t next_report = make_timeout_time_ms(REPORT_PERIOD_MS);
    while (true) {
        pwd_net_tick();
        panel_tick();

        /* 上屏走总线要等硬件 ⇒ 只能在主循环里做，**不能在 lwIP 回调里做** ✗（PUD 的实测结论 ✓）*/
        pwd_frame_t frame;
        if (pwd_net_take_frame(&frame)) {
            panel_show_band(frame.xs, frame.ys, frame.xe, frame.ye, frame.pixels, frame.pixels_len);
            pwd_net_release_frame();
        }

        if (absolute_time_diff_us(get_absolute_time(), next_report) <= 0) {
            report();
            next_report = make_timeout_time_ms(REPORT_PERIOD_MS);
        }
        sleep_ms(1);   /* 带的消费粒度：40 带/帧时 1 ms 足够（链路才是瓶颈 ✓）*/
    }
}
