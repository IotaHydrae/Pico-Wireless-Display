/* PWD 收帧协议与统计（M1 收帧统计 + M2 v1 原始 RGB565 分带上屏）。
 *
 * 分片（小端；**字段只追加、不重排、不复用编号** ✓）：
 *
 *     每片 = [u32 frame_id][u16 frag_idx][u16 frag_cnt][u32 frame_len] + 载荷(≤1400 B)
 *
 * 整帧载荷（M2 v1）：**窗口头 + 原始 RGB565 像素** ——
 *
 *     [u16 xs][u16 ys][u16 xe][u16 ye][像素…]     （xe/ye 是**闭区间**，与面板窗口语义一致 ✓）
 *
 * 为什么把窗口放在载荷里而不是加到分片头 ✗：那样组帧代码一行都不用改 ✓，也贴合 PUD 那种
 * "带窗口头 + 像素"的带（band）做法 ✓。**v1 不做解码**：像素原样进面板 ✓。
 *
 * 组帧策略是"只认最新"：帧号前进就丢弃未完成的旧帧并计数，不重传、不无限等待 ✓。
 */
#ifndef PWD_NET_H
#define PWD_NET_H

#include <stdbool.h>
#include <stdint.h>

#define PWD_UDP_PORT 4444u          /* 与官方 udp_beacon 同端口，便于复用现成工具 */
#define PWD_FRAG_HDR 12u            /* 4+2+2+4 */
#define PWD_FRAG_PAYLOAD 1400u      /* 留出 IP/UDP 头，避免 IP 分片 */
#define PWD_FRAME_MAX 32768u        /* 单帧（=一个带）上限；480×8 的 RGB565 带是 7680 B ✓ */
#define PWD_FRAG_MAX (PWD_FRAME_MAX / PWD_FRAG_PAYLOAD + 1u)
#define PWD_FRAME_TIMEOUT_MS 1000u  /* 未完成帧的兜底丢弃时间，保证计数器能对上账 */
#define PWD_WINDOW_HDR 8u           /* [u16 xs][u16 ys][u16 xe][u16 ye] */

typedef struct {
    uint32_t frags_rx;          /* 收到的合法分片 */
    uint32_t frags_dup;         /* 重复分片（忽略并计数） */
    uint32_t frags_stale;       /* 属于更旧帧号的分片 */
    uint32_t frags_bad;         /* 字段越界/自相矛盾/长度对不上的分片或帧 */
    uint32_t frames_complete;   /* 组好的完整帧 */
    uint32_t frames_incomplete; /* 因帧号前进或超时而丢弃的未完成帧 */
    uint32_t frames_busy_drop;  /* 上一帧还没被上屏消费，又来了新帧 ⇒ 丢新帧并计数 */
    uint32_t bytes_complete;    /* 完整帧的字节数合计 */
} pwd_stats_t;

/* 一帧（一个带）：窗口 + 原始 RGB565 像素。 */
typedef struct {
    uint16_t xs, ys, xe, ye;
    const uint8_t *pixels;
    uint32_t pixels_len;
} pwd_frame_t;

void pwd_net_start(void);

/* 主循环里周期调用：处理超时丢弃、按间隔打印统计行。 */
void pwd_net_tick(void);

/* 取一份统计快照（关中断拷贝，避免读到撕裂的值）。 */
void pwd_net_snapshot(pwd_stats_t *out);

/* 取一帧去上屏；返回 false 表示当前没有待显示的帧 ✓。
 * 取到之后缓冲区被"占住"，直到 pwd_net_release_frame() ✓ —— 这样上屏（慢、走总线）
 * 就不会和中断侧的组帧抢同一块缓冲 ✓。 */
bool pwd_net_take_frame(pwd_frame_t *out);
void pwd_net_release_frame(void);

#endif /* PWD_NET_H */
