/* PWD 收帧实现（M1 统计 + M2 v1 原始 RGB565 分带）。
 *
 * 并发：udp_recv 回调跑在 cyw43 的后台上下文（中断侧），组帧/统计都在那里；**上屏在主循环**
 * （走总线要等硬件，放中断里会长时间占着 lwIP/cyw43 ✗ —— PUD 的实测结论 ✓）。
 * 两边共享：s_frame（组好的帧）用 take/release 交接 ✓；s_stats 取快照时关中断拷贝 ✓。
 */
#include "pwd_net.h"

#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

/* ── 线上字段 ───────────────────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t frame_id;
    uint16_t frag_idx;
    uint16_t frag_cnt;
    uint32_t frame_len;
} pwd_frag_hdr_t;

_Static_assert(sizeof(pwd_frag_hdr_t) == PWD_FRAG_HDR, "分片头必须是 12 字节");

/* ── 状态 ───────────────────────────────────────────────────────────────── */
static pwd_stats_t s_stats;
static uint8_t s_frame[PWD_FRAME_MAX];

static uint32_t s_frame_id;
static uint32_t s_frame_len;
static uint32_t s_frag_cnt;
static uint32_t s_frag_mask;
static uint16_t s_frag_len[PWD_FRAG_MAX];
static absolute_time_t s_frame_first_rx;

static bool s_frame_ready;      /* 组好一帧，等主循环取走 ✓ */
static uint32_t s_ready_len;    /* 那一帧的长度 —— **必须单独存** ✗：
                                 * `on_frame_complete()` 结尾会 `reset_assembly()` 把
                                 * `s_frame_len` 清零 ✓，主循环稍后再读就得到 0 ⇒ 传给库的长度
                                 * 变成 `0-8 = 0xFFFFFFF8` ⇒ 库去写 4 GB 像素 ⇒ HardFault ✗
                                 * （真机日志：`[panel] ✗ 拒带 … len=4294967288` ✓）。 */
static bool s_frame_in_use;     /* 主循环正在用它上屏 ⇒ 中断侧别再写 ✓ */

static uint32_t rd_u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd_u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }

static void reset_assembly(void) {
    s_frame_id = 0;
    s_frame_len = 0;
    s_frag_cnt = 0;
    s_frag_mask = 0;
}

static void abandon_incomplete(void) {
    if (s_frag_cnt > 0) {
        s_stats.frames_incomplete++;
    }
    reset_assembly();
}

/* 一帧收齐了：先按窗口头自证"像素数与窗口对得上" ✓，再交给主循环上屏 ✓。 */
static void on_frame_complete(void) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < s_frag_cnt; i++) {
        sum += s_frag_len[i];
    }

    bool ok = (sum == s_frame_len) && (s_frame_len >= PWD_WINDOW_HDR);
    if (ok) {
        const uint16_t xs = rd_u16(s_frame + 0), ys = rd_u16(s_frame + 2);
        const uint16_t xe = rd_u16(s_frame + 4), ye = rd_u16(s_frame + 6);
        const uint32_t want = (uint32_t)(xe - xs + 1) * (uint32_t)(ye - ys + 1) * 2u;
        if (xe < xs || ye < ys || want != s_frame_len - PWD_WINDOW_HDR) {
            ok = false;     /* 窗口自相矛盾（宽高为负，或像素数与窗口不符）✗ */
        }
    }

    if (!ok) {
        s_stats.frags_bad++;        /* 拼错/长度不符：按坏帧计，不上屏 ✗ */
    } else if (s_frame_in_use || s_frame_ready) {
        s_stats.frames_busy_drop++; /* 上一帧还没上完 ⇒ 丢新帧（显示端永远偏向最新 ✓）*/
    } else {
        s_stats.frames_complete++;
        s_stats.bytes_complete += s_frame_len;
        s_ready_len = s_frame_len;   /* 先存下来：reset_assembly() 会清 s_frame_len ✗ */
        s_frame_ready = true;
    }
    reset_assembly();
}

static void udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)addr; (void)port;

    if (p == NULL || p->tot_len < PWD_FRAG_HDR) {
        s_stats.frags_bad++;
        if (p) pbuf_free(p);
        return;
    }

    uint8_t hdr[PWD_FRAG_HDR];
    pbuf_copy_partial(p, hdr, sizeof(hdr), 0);
    const uint32_t frame_id = rd_u32(hdr + 0);
    const uint32_t frag_idx = rd_u16(hdr + 4);
    const uint32_t frag_cnt = rd_u16(hdr + 6);
    const uint32_t frame_len = rd_u32(hdr + 8);
    const uint32_t payload_len = p->tot_len - PWD_FRAG_HDR;

    /* 字段自相矛盾的一律不接受：片数/下标越界、帧长超缓冲、片数与帧长对不上。
     * 最后两条是**片数与帧长的自洽**（拿 frag_cnt 比 frame_len，不是拿 frag_idx 比 ✗ ——
     * 曾经写错方向，结果每个非末片都被判成坏片 ✓）。 */
    if (frag_cnt == 0 || frag_cnt > PWD_FRAG_MAX || frag_idx >= frag_cnt ||
        frame_len == 0 || frame_len > PWD_FRAME_MAX ||
        payload_len > PWD_FRAG_PAYLOAD ||
        frame_len > frag_cnt * PWD_FRAG_PAYLOAD ||
        frame_len <= (frag_cnt - 1u) * PWD_FRAG_PAYLOAD) {
        s_stats.frags_bad++;
        pbuf_free(p);
        return;
    }

    if (frame_id != s_frame_id) {
        if (s_frame_id != 0 && frame_id < s_frame_id) {
            s_stats.frags_stale++;
            pbuf_free(p);
            return;
        }
        abandon_incomplete();
        s_frame_id = frame_id;
        s_frame_len = frame_len;
        s_frag_cnt = frag_cnt;
        s_frag_mask = 0;
        memset(s_frag_len, 0, sizeof(s_frag_len));
        s_frame_first_rx = get_absolute_time();
    }

    const uint32_t bit = 1u << frag_idx;
    if (s_frag_mask & bit) {
        s_stats.frags_dup++;
        pbuf_free(p);
        return;
    }

    const uint32_t offset = frag_idx * PWD_FRAG_PAYLOAD;
    if (offset + payload_len > PWD_FRAME_MAX) {
        s_stats.frags_bad++;
        pbuf_free(p);
        return;
    }
    if (!s_frame_in_use) {      /* 正在上屏的那块缓冲别动 ✗ */
        pbuf_copy_partial(p, s_frame + offset, payload_len, PWD_FRAG_HDR);
    }
    s_frag_mask |= bit;
    s_frag_len[frag_idx] = (uint16_t)payload_len;
    s_stats.frags_rx++;

    const bool complete = (s_frag_mask == ((1u << s_frag_cnt) - 1u));
    pbuf_free(p);
    if (complete) {
        on_frame_complete();
    }
}

void pwd_net_start(void) {
    reset_assembly();
    memset(&s_stats, 0, sizeof(s_stats));
    s_frame_ready = false;
    s_frame_in_use = false;

    cyw43_arch_lwip_begin();
    struct udp_pcb *pcb = udp_new();
    if (pcb == NULL || udp_bind(pcb, IP_ADDR_ANY, PWD_UDP_PORT) != ERR_OK) {
        cyw43_arch_lwip_end();
        printf("[pwd] ✗ UDP 绑定失败（端口 %u）\n", (unsigned)PWD_UDP_PORT);
        return;
    }
    udp_recv(pcb, udp_recv_cb, NULL);
    cyw43_arch_lwip_end();

    printf("[pwd] UDP 接收端已就绪：端口 %u，单帧上限 %u B（%u 片），载荷 = 8 B 窗口头 + 原始 RGB565\n",
           (unsigned)PWD_UDP_PORT, (unsigned)PWD_FRAME_MAX, (unsigned)PWD_FRAG_MAX);
}

void pwd_net_tick(void) {
    /* 兜底：最后那一帧如果一直缺片，就没人来把它结账 ⇒ 计数器对不上 ✓ */
    if (s_frag_cnt > 0 && absolute_time_diff_us(s_frame_first_rx, get_absolute_time()) > PWD_FRAME_TIMEOUT_MS * 1000) {
        abandon_incomplete();
    }
}

void pwd_net_snapshot(pwd_stats_t *out) {
    const uint32_t save = save_and_disable_interrupts();
    *out = s_stats;
    restore_interrupts(save);
}

bool pwd_net_take_frame(pwd_frame_t *out) {
    const uint32_t save = save_and_disable_interrupts();
    bool ok = s_frame_ready;
    if (ok) {
        out->xs = rd_u16(s_frame + 0);
        out->ys = rd_u16(s_frame + 2);
        out->xe = rd_u16(s_frame + 4);
        out->ye = rd_u16(s_frame + 6);
        out->pixels = s_frame + PWD_WINDOW_HDR;
        out->pixels_len = s_ready_len - PWD_WINDOW_HDR;
        s_frame_ready = false;
        s_frame_in_use = true;      /* 上屏期间中断侧不再写这块缓冲 ✓ */
    }
    restore_interrupts(save);
    return ok;
}

void pwd_net_release_frame(void) {
    const uint32_t save = save_and_disable_interrupts();
    s_frame_in_use = false;
    restore_interrupts(save);
}
