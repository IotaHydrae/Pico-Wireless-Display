cmake_minimum_required(VERSION 3.13)

# ══ M0b：官方 iperf 例子的**客户端模式**（设备→PC 方向），带**两处最小 delta** ══
#
# 为什么不能"零 delta"：上游那条 `#if CLIENT_TEST` 分支坏了两处（详见
# ../../notes/wifi-link-pico-w.md）——
#   1) 用了 `xstr(IPERF_SERVER_IP)`，但 `xstr` 在 pico-examples 全仓与 SDK 里都没定义 ⇒ 编不过；
#   2) 唯一的启动调用写在 `assert(...)` 里，Release 的 `-DNDEBUG` 把整句删掉 ⇒ 静默不发一个包。
#
# 所以这里：官方 `picow_iperf.c` **逐字不动**，delta 全在本目录的两个文件里 ——
#   * `client_compat.h`（`-include`）：把 `xstr` 定义成恒等宏，于是 CMake 传**带引号**的 IP，
#     写法与隔壁官方 `tcp_client` 例子一致；
#   * `-UNDEBUG`：只对这个源文件生效，让那句 assert 真的求值。
# 两个选项都**只作用于这一个源文件** —— SDK 的 `lwiperf.c` 也被编进同一个 target，
# 全局 `-include` 会把 `lwiperf.c` 里同名函数的**定义**也宏替换掉 ✗。

set(PICO_BOARD pico_w CACHE STRING "")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pwd_baseline_iperf_client C CXX ASM)
pico_sdk_init()

if("${WIFI_SSID}" STREQUAL "" OR "${WIFI_PASSWORD}" STREQUAL "")
    message(FATAL_ERROR "缺 WIFI_SSID / WIFI_PASSWORD：用 -C <wifi-config.cmake> 传")
endif()
if("${PWD_PC_IP}" STREQUAL "")
    message(FATAL_ERROR "缺 PWD_PC_IP：PC 的地址（iperf 服务端跑在那里），用 -DPWD_PC_IP=<PC-IP> 传")
endif()

set(PWD_PICO_EXAMPLES "${CMAKE_CURRENT_LIST_DIR}/../../third_party/pico-examples" CACHE PATH "")
set(SRC "${PWD_PICO_EXAMPLES}/pico_w/wifi/iperf/picow_iperf.c")
if(NOT EXISTS "${SRC}")
    message(FATAL_ERROR "找不到官方 iperf 例子：先跑 scripts/fetch-deps.sh")
endif()

add_executable(pwd_iperf_client "${SRC}")
target_compile_definitions(pwd_iperf_client PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
    CLIENT_TEST=1
    IPERF_SERVER_IP=\"${PWD_PC_IP}\"      # 带引号：xstr 是恒等宏（见 client_compat.h）
)
target_include_directories(pwd_iperf_client PRIVATE
    "${PWD_PICO_EXAMPLES}/pico_w/wifi/iperf"
    "${PWD_PICO_EXAMPLES}/pico_w/wifi"   # 官方那份公共 lwipopts
)
target_link_libraries(pwd_iperf_client
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico_lwip_iperf
)
set_source_files_properties("${SRC}" PROPERTIES
    COMPILE_OPTIONS "-UNDEBUG;-include;${CMAKE_CURRENT_LIST_DIR}/client_compat.h")
pico_add_extra_outputs(pwd_iperf_client)
