cmake_minimum_required(VERSION 3.13)

# ══ M0 基线：**官方** pico_w/wifi/iperf 例子，零 delta ══
#
# 官方源码**逐字不动**（在 third_party/ 里，gitignore；scripts/fetch-deps.sh 取）。
# 这层 wrapper 只做三件事，都不碰官方代码：
#   ① 选板子 `pico_w`；
#   ② 校验 WiFi 凭据**存在** —— 官方那份 CMakeLists 不校验，缺了会静默编出空凭据的固件 ✗；
#   ③ `add_subdirectory` 官方那份 CMakeLists，拿到它的两个服务端 target。
#
# 凭据：`-C <wifi-config.cmake>`（scripts/make-wifi-config.sh 生成，chmod 600，gitignore）。
# 它建出 `picow_iperf_server_background`（中断后台）与 `picow_iperf_server_poll`。

set(PICO_BOARD pico_w CACHE STRING "")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pwd_baseline_iperf C CXX ASM)
pico_sdk_init()

if("${WIFI_SSID}" STREQUAL "" OR "${WIFI_PASSWORD}" STREQUAL "")
    message(FATAL_ERROR
        "缺 WIFI_SSID / WIFI_PASSWORD：用 -C <wifi-config.cmake> 传（scripts/make-wifi-config.sh 生成）")
endif()

set(PWD_PICO_EXAMPLES "${CMAKE_CURRENT_LIST_DIR}/../third_party/pico-examples" CACHE PATH "")
if(NOT EXISTS "${PWD_PICO_EXAMPLES}/pico_w/wifi/iperf/picow_iperf.c")
    message(FATAL_ERROR "找不到官方 iperf 例子：先跑 scripts/fetch-deps.sh（当前 PWD_PICO_EXAMPLES=${PWD_PICO_EXAMPLES}）")
endif()

add_subdirectory("${PWD_PICO_EXAMPLES}/pico_w/wifi/iperf" iperf)
