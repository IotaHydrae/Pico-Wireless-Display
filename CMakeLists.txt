cmake_minimum_required(VERSION 3.13)

# ══ PWD 固件（M1：连 WiFi 收帧并统计，不上屏）══
#
# 构建配方**照官方例子**：同一块板子（`pico_w`）、同一种 cyw43 架构
# （`pico_cyw43_arch_lwip_threadsafe_background`）、**复用官方例子的公共 lwipopts**
# （`pico_w/wifi/lwipopts.h`，不自己写第二份 ✗）—— 与 `pico_w/wifi/udp_beacon` 的
# CMakeLists 逐项对应，只多了"凭据非空"的校验与自己的源文件。

set(PICO_BOARD pico_w CACHE STRING "")

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pwd C CXX ASM)
pico_sdk_init()

if("${WIFI_SSID}" STREQUAL "" OR "${WIFI_PASSWORD}" STREQUAL "")
    message(FATAL_ERROR "缺 WIFI_SSID / WIFI_PASSWORD：用 -C <wifi-config.cmake> 传（scripts/make-wifi-config.sh 生成）")
endif()

set(PWD_PICO_EXAMPLES "${CMAKE_CURRENT_LIST_DIR}/third_party/pico-examples" CACHE PATH "")
# 官方把公共 lwIP 选项放在 `pico_w/wifi/lwipopts_examples_common.h`，每个例子自带一个只做
# include 的 `lwipopts.h` 壳 —— PWD 的壳在 `src/lwipopts.h`，所以这里要加上官方那个目录。
if(NOT EXISTS "${PWD_PICO_EXAMPLES}/pico_w/wifi/lwipopts_examples_common.h")
    message(FATAL_ERROR "找不到官方例子的公共 lwipopts：先跑 scripts/fetch-deps.sh（当前 PWD_PICO_EXAMPLES=${PWD_PICO_EXAMPLES}）")
endif()

# ── 面板配置：**照 PUD 的模式** ──
# `configs/` 是指向库配置目录（`lib/pico-display-lib/configs/`）的软链接，配置文件的**归属地
# 就在库里** ✓；选择靠 `-DPUD_CONFIG=<名字>`，由库自己 `include()` ✓ —— 所以这里只设变量，
# 不自己 include ✗，也不在 PWD 里长第二套配置 ✗。
set(PUD_CONFIG "pico_dm_z350it008-8080" CACHE STRING
    "面板配置名（见 configs/，即 lib/pico-display-lib/configs/）")
set(PUD_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/configs" CACHE PATH
    "面板配置目录（软链接到库的 configs/）")
if(NOT EXISTS "${PUD_CONFIG_DIR}/${PUD_CONFIG}.cmake")
    message(FATAL_ERROR "找不到面板配置 configs/${PUD_CONFIG}.cmake")
endif()

# ── 显示/解码库（子模块；**与 PUD 用同一个 commit** ✓）──
set(PICO_DISPLAY_LIB_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico-display-lib")
if(NOT EXISTS "${PICO_DISPLAY_LIB_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "缺 lib/pico-display-lib 子模块：git submodule update --init --recursive")
endif()
add_subdirectory("${PICO_DISPLAY_LIB_PATH}")

add_executable(pwd
    src/main.c
    src/pwd_net.c
    src/panel.c
)

target_compile_definitions(pwd PRIVATE
    WIFI_SSID=\"${WIFI_SSID}\"
    WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
)

target_include_directories(pwd PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src"
    "${PWD_PICO_EXAMPLES}/pico_w/wifi"   # 官方例子的公共 lwipopts（与 udp_beacon 同一份）
)

target_link_libraries(pwd
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    pico-display-lib                     # 面板/总线/背光的唯一出处（头文件由它 PUBLIC 导出 ✓）
)

pico_add_extra_outputs(pwd)

# 控制台：**只走 USB stdio**
#   本接线表里没有留给 UART 的脚 ✗ —— GP0..GP15 是 8080 数据总线，GP16 被库要求的 CS 占位拿走
#   （模块 CS 是硬件下拉的，那个脚其实不接东西，但库的 i80 路径每次写都要动它 ✓），
#   其余脚（18/19/20/21/22/26/27/28）都归面板与触摸 ✓。
#   ⇒ 插上 Pico W 自己的 USB 就有一条控制台 ✓；`pico_w` 的 SDK **默认只开 UART** ✗，
#   所以这里显式关 UART、开 USB。
pico_enable_stdio_uart(pwd 0)
pico_enable_stdio_usb(pwd 1)
