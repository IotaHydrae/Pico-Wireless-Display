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

int main(void) {
    stdio_init_all();

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
