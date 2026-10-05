#!/usr/bin/env bash
# 生成 WiFi 凭据文件（**只落在被 gitignore 的路径上，chmod 600** ✗ 绝不进仓）。
#
# 用法：
#   scripts/make-wifi-config.sh                       # 交互式询问 SSID / 口令
#   scripts/make-wifi-config.sh -o /tmp/pwd-wifi.cmake
#   scripts/make-wifi-config.sh --from-sdkconfig <path/to/sdkconfig>   # 从别处 sdkconfig 抽
#
# 生成的文件里只有两个 cache 变量，正好是官方例子要的那两个：
#   WIFI_SSID / WIFI_PASSWORD（CMake 会以 \"...\" 形式注入）
#
# 退出码：0 成功 / 2 用法错误 / 3 环境错误（源文件里抽不到凭据）
set -euo pipefail

OUT="/tmp/pwd-wifi.cmake"
SRC=""

while [ $# -gt 0 ]; do
    case "$1" in
        -o|--output)     OUT="${2:?}"; shift 2 ;;
        --from-sdkconfig) SRC="${2:?}"; shift 2 ;;
        -h|--help)       sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "✗ 未知参数：$1（用 --help）" >&2; exit 2 ;;
    esac
done

if [ -n "${SRC}" ]; then
    [ -f "${SRC}" ] || { echo "✗ 找不到 ${SRC}" >&2; exit 3; }
    # ESP-IDF 的 sdkconfig：CONFIG_EXAMPLE_WIFI_SSID="..." / CONFIG_EXAMPLE_WIFI_PASSWORD="..."
    SSID="$(sed -n 's/^CONFIG_[A-Z0-9_]*WIFI_SSID="\(.*\)"$/\1/p' "${SRC}" | head -1)"
    PSK="$(sed -n 's/^CONFIG_[A-Z0-9_]*WIFI_PASSWORD="\(.*\)"$/\1/p' "${SRC}" | head -1)"
    [ -n "${SSID}" ] && [ -n "${PSK}" ] || { echo "✗ ${SRC} 里抽不到 SSID/口令（键名不符？）" >&2; exit 3; }
    echo "✓ 从 ${SRC} 抽到 SSID 与口令（不回显 ✗）"
else
    read -r -p "WiFi SSID: " SSID
    read -r -s -p "WiFi 口令: " PSK; echo
    [ -n "${SSID}" ] && [ -n "${PSK}" ] || { echo "✗ SSID/口令不能为空" >&2; exit 2; }
fi

umask 077
cat > "${OUT}" <<EOF
# 由 scripts/make-wifi-config.sh 生成 —— **不要提交** ✗（.gitignore 已覆盖）
set(WIFI_SSID "${SSID}" CACHE STRING "")
set(WIFI_PASSWORD "${PSK}" CACHE STRING "")
EOF
chmod 600 "${OUT}"
echo "✓ 已写出 ${OUT}（$(stat -c '%a' "${OUT}")，SSID/口令不回显 ✗）"
