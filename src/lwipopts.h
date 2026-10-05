/* PWD 的 lwIP 选项 —— 与官方例子同构：**不自己写一套选项**，只 include 官方那份公共文件 ✓
 * （`pico-examples/pico_w/wifi/lwipopts_examples_common.h`，与本目录 `pico_w/wifi/udp_beacon/`
 * 的 `lwipopts.h` 内容一致；该目录在 CMakeLists 里作为 include 路径加进来）。
 *
 * 之所以要有一份：lwIP 是按 `#include "lwipopts.h"` 找配置的，官方做法就是"每个例子自带一个
 * 只做 include 的壳"；PWD 沿用同一套，改动 lwIP 选项时才算 delta，必须写明原因 ✗。
 */
#ifndef PWD_LWIPOPTS_H
#define PWD_LWIPOPTS_H

#include "lwipopts_examples_common.h"

#endif /* PWD_LWIPOPTS_H */

/* ── 本仓对官方公共配置的显式 delta（每一处都写理由 ✓）─────────────────────────
 * 官方那份 `lwipopts_examples_common.h` 是给**轻流量例子**调的 ✗；PWD 要持续收 1400 B 的
 * UDP 分片，实测（M2 v1）在 **2.5 Mbit/s** 下就丢了一半分片 ✗（240 片只到 120 片 ✓），
 * 而同板 TCP 上行能跑到 17.4 Mbit/s ✓ ⇒ 瓶颈在接收侧的缓冲池，不在链路 ✓。
 *
 * 覆盖方式：官方那份里 `PBUF_POOL_SIZE` 是**裸 `#define`** ✗（不是 `#ifndef`），
 * 所以只能**先 include、再 `#undef` 重定义** ✓ —— 这是唯一能生效又不动官方源码的做法 ✓。
 */
#undef PBUF_POOL_SIZE
#define PBUF_POOL_SIZE 64      /* 24 ⇒ 64：突发时 pbuf 不够会静默丢包 ✗ */

#undef MEMP_NUM_PBUF
#define MEMP_NUM_PBUF 64       /* 16 ⇒ 64：同上（UDP 分片是短包高峰 ✓）*/

#undef MEMP_NUM_UDP_PCB
#define MEMP_NUM_UDP_PCB 8     /* 4 ⇒ 8：留余量给将来多端口/统计回发 ✓ */
