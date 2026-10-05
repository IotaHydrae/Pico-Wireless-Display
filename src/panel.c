/* 面板 bring-up 实现：点亮 → 背光自检 → 铺四角方向测试图。
 *
 * 为什么不是纯色轮换 ✗：纯色只证明"点亮了"，看不出旋转、镜像与覆盖范围；
 * 四角各一块不同颜色的方块，一眼同时读出三件事 ✓：
 *   * 哪个角是什么颜色 ⇒ 旋转方向（0/90/180/270）
 *   * 四角都出现、边缘无留白 ⇒ 逻辑分辨率与面板原生分辨率对齐
 *   * 左右/上下是否镜像 ⇒ 交换两角即可判定
 * 这是固件仓/移植仓里"方向测试图"的老做法 ✓。
 */
#include "panel.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "tft.h"
#include "backlight.h"

/* 逻辑分辨率：**直接用库给的值**，不要再自己按 rotation 换一次 ✗。
 * 凭据（实测 ✓）：配置写的是原生 320×480 + rotation 1，而运行期从 RAM 里读驱动那个静态
 * `struct tft_display`，前两个 int 是 **480 / 320** ✓ ⇒ 库在**配置期**就把旋转算进去了，
 * `TFT_HOR_RES/VER_RES` 到这里已经是逻辑尺寸 ✓。
 * 我第一版按 rotation 奇偶又换了一次 ⇒ 角块坐标算错 ✗（屏幕看着"方向不对"就是它 ✓）。 */
#define PANEL_W TFT_X_RES
#define PANEL_H TFT_Y_RES

#define BLOCK 80                 /* 每块角块的边长（像素） */
#define BLOCK_PX (BLOCK * BLOCK)

/* RGB565 */
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_BLUE  0x001F
#define C_WHITE 0xFFFF
#define C_BLACK 0x0000

/* 背光自检：开机闪 3 下。这条结论本身是被它逼出来的 ✓ —— 把 BLK 改到别的脚时，用户报
 * "背光都没亮" ✓ ⇒ 闪不闪是有效的判据：闪了说明供电、背光脚、以及"固件在控制这块模块"
 * 都成立 ✓；不闪就先查供电/引脚 ✗。 */
#define BLINK_COUNT 3
#define BLINK_MS 150

static bool s_ready;
static bool s_frame_mode;
static uint32_t s_bands_drawn;
static uint32_t s_bands_rejected;



static u16 s_block[BLOCK_PX];

/* 把一块纯色方块贴到 (x, y)。用窗口 + 一次 bulk 写（`tft_video_flush`）✓，
 * 而不是逐像素 —— 这也是 M2 显示整帧要走的那条路 ✓。 */
static void put_block(int x, int y, u16 color) {
    for (int i = 0; i < BLOCK_PX; i++) {
        s_block[i] = color;
    }
    tft_video_flush(x, y, x + BLOCK - 1, y + BLOCK - 1, s_block, sizeof(s_block));
    printf("[panel] 角块 (%3d,%3d) 颜色 %04X\n", x, y, color);
}

static void draw_direction_test(void) {
    tft_fill_color(C_BLACK);

    put_block(0,               0,               C_RED);     /* 左上 */
    put_block(PANEL_W - BLOCK,  0,              C_GREEN);   /* 右上 */
    put_block(0,               PANEL_H - BLOCK, C_BLUE);    /* 左下 */
    put_block(PANEL_W - BLOCK, PANEL_H - BLOCK, C_WHITE);   /* 右下 */

    printf("[panel] 方向测试图：逻辑 %dx%d（原生 %dx%d, rotation %d）"
           "，左上红 右上绿 左下蓝 右下白\n",
           PANEL_W, PANEL_H, TFT_HOR_RES, TFT_VER_RES, TFT_ROTATION);
}

bool panel_bringup(void) {
    if (tft_driver_init() != 0) {
        printf("[panel] ✗ tft_driver_init() 失败\n");
        return false;
    }

    backlight_driver_init();
    for (int i = 0; i < BLINK_COUNT; i++) {
        backlight_set_level(0);
        sleep_ms(BLINK_MS);
        backlight_set_level(100);
        sleep_ms(BLINK_MS);
    }
    backlight_set_level(100);

    draw_direction_test();
    s_ready = true;
    printf("[panel] ✓ 面板已初始化（8080 16-bit, ILI9488 初始化序列）\n");
    return true;
}

void panel_tick(void) {
    /* 测试图是一次性的 ✓；这里的钩子留给以后要动的动效，现在什么都不做。 */
    (void)s_ready;
}

void panel_show_band(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye,
                     const void *pixels, uint32_t len) {
    if (!s_ready) {
        return;
    }
    /* **越界窗口绝不交给库** ✗：实测有一次固件进 HardFault，PC 落在库的 `tft_video_flush`
     * 上 ✓，而当时正把"载荷前 8 字节"当窗口用的压测帧发进来 ✓ —— 那种窗口可能是任意巨大坐标 ✓。
     * 边界检查放在**我们这层**：库的接口不做范围校验 ✗，而面板尺寸是我们知道的 ✓。 */
    if (xs > xe || ys > ye || xe >= PANEL_W || ye >= PANEL_H ||
        (uint32_t)(xe - xs + 1) * (uint32_t)(ye - ys + 1) * 2u != len) {
        if (s_bands_rejected < 3) {   /* 只打前几条，避免刷屏 ✓ */
            printf("[panel] ✗ 拒带 xs=%u ys=%u xe=%u ye=%u len=%u（期望 %u，面板 %ux%u）\n",
                   (unsigned)xs, (unsigned)ys, (unsigned)xe, (unsigned)ye, (unsigned)len,
                   (unsigned)((uint32_t)(xe - xs + 1) * (uint32_t)(ye - ys + 1) * 2u),
                   (unsigned)PANEL_W, (unsigned)PANEL_H);
        }
        s_bands_rejected++;
        return;
    }
    if (!s_frame_mode) {
        s_frame_mode = true;         /* 第一帧到了 ⇒ 退出 bring-up 测试图 ✓ */
        tft_fill_color(C_BLACK);
        printf("[panel] 收到第一帧 ⇒ 退出测试图，转显示帧（%ux%u 窗口, %u B）\n",
               (unsigned)(xe - xs + 1), (unsigned)(ye - ys + 1), (unsigned)len);
    }
    tft_video_flush(xs, ys, xe, ye, (void *)pixels, len);
    s_bands_drawn++;
}

uint32_t panel_bands_drawn(void) {
    return s_bands_drawn;
}

uint32_t panel_bands_rejected(void) {
    return s_bands_rejected;
}

