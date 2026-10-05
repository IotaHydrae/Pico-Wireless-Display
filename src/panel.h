/* 面板层（PWD）：bring-up 自检 + 把收到的带（band）画上去。
 *
 * M2 v1 不做解码：载荷就是**原始 RGB565**，直接进面板窗口 ✓。
 */
#ifndef PWD_PANEL_H
#define PWD_PANEL_H

#include <stdbool.h>
#include <stdint.h>

/* 初始化面板与背光；返回 false 表示面板没起来（库的 `tft_driver_init()` 失败 ✓）。 */
bool panel_bringup(void);

/* 主循环里周期调用：bring-up 测试图现在是一次性的 ✓，这个钩子留给以后要加的动效。 */
void panel_tick(void);

/* 把一个带画上去（`xs/ys/xe/ye` 闭区间 ✓，像素为原始 RGB565 ✓）。
 * 只应在主循环里调用 ✓ —— 走总线要等硬件，放 lwIP 回调里会长时间占着中断 ✗。 */
void panel_show_band(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye,
                     const void *pixels, uint32_t len);

/* 已上屏的带数 / 被兜底拒掉的带数（给统计行用 ✓）。 */
uint32_t panel_bands_drawn(void);
uint32_t panel_bands_rejected(void);

#endif /* PWD_PANEL_H */
