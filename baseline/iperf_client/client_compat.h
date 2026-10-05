/* 让官方 picow_iperf.c 的 CLIENT_TEST 分支能编、能跑（官方源码逐字不动 ✓）。
 *
 * 上游问题 1：picow_iperf.c 用 `xstr(IPERF_SERVER_IP)`，而 `xstr` 在 pico-examples 全仓与
 *   ~/.pico-sdk 里都没有定义（`grep -rn 'define xstr'` 无命中），官方 CMakeLists 也只建了
 *   两个 server target ⇒ 这条分支长期没人编。
 *   这里把 `xstr` 定义成**恒等宏**，于是 CMake 直接传带引号的 IP 字面量 ——
 *   与隔壁官方 tcp_client 例子（CMake 传 `TEST_TCP_SERVER_IP=\"...\"`）的写法一致。
 *
 * 上游问题 2（在 CMakeLists.txt 里用 -UNDEBUG 处理）：官方把唯一的启动调用写在 `assert(...)` 里，
 *   Release 的 `-DNDEBUG` 会把整个表达式删掉 ⇒ 客户端从不启动、一个包都不发。
 */
#ifndef PWD_IPERF_CLIENT_COMPAT_H
#define PWD_IPERF_CLIENT_COMPAT_H

#define xstr(s) s

#endif /* PWD_IPERF_CLIENT_COMPAT_H */
