#!/usr/bin/env bash
# 取本仓需要的外部源码（幂等，可重复跑 ✓）。
#
# 目前只有一样：pico-examples —— 但**只要它的官方例子**（baseline 用），
# 整个 pico-examples 树在本机 SDK 上会因其它例子的配置报错 ✗，所以整树取来、
# 只用其中的子目录（wrapper 里 add_subdirectory 指定的那一个 ✓）。
#
# 版本钉在 **与本机 SDK 相同的 tag**（sdk-X.Y.Z），避免"例子跟着新版 SDK 漂" ✗。
#
# 退出码：0 成功 / 3 环境错误（缺 SDK、缺 git、网络不通）
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${ROOT}/third_party/pico-examples"

: "${PICO_SDK_PATH:?需要设 PICO_SDK_PATH（SDK 根目录）}"
[ -f "${PICO_SDK_PATH}/pico_sdk_version.cmake" ] || { echo "✗ PICO_SDK_PATH 不像 SDK 根：${PICO_SDK_PATH}" >&2; exit 3; }
command -v git >/dev/null || { echo "✗ 缺 git" >&2; exit 3; }

# SDK 版本 → tag。注意 pico_sdk_version.cmake 里**没有**现成的版本字符串 ✗：
# 它是 set(PICO_SDK_VERSION_MAJOR 2) / _MINOR 3 / _REVISION 1 三行拼出来的，
# 照字面去抓 PICO_SDK_VERSION_STRING 只会抓到 `${MAJOR}.${MINOR}.${REVISION}` 这串变量名 ✗。
SDKVER_FILE="${PICO_SDK_PATH}/pico_sdk_version.cmake"
sdknum() { sed -n "s/.*set(${1}[[:space:]]*\([0-9][0-9]*\)).*/\1/p" "${SDKVER_FILE}" | head -1; }
MAJ="$(sdknum PICO_SDK_VERSION_MAJOR)"; MIN="$(sdknum PICO_SDK_VERSION_MINOR)"; REV="$(sdknum PICO_SDK_VERSION_REVISION)"
[ -n "${MAJ}" ] && [ -n "${MIN}" ] || { echo "✗ 读不出 SDK 版本号（${SDKVER_FILE}）" >&2; exit 3; }
VER="${MAJ}.${MIN}.${REV:-0}"

URL="https://github.com/raspberrypi/pico-examples.git"
TAG="sdk-${VER}"
# tag 必须真的存在：拼错的名字不检查只会得到 "Remote branch … not found" 这种误导性报错 ✗
if ! git ls-remote --tags --exit-code "${URL}" "refs/tags/${TAG}" >/dev/null 2>&1; then
    { echo "✗ ${URL} 上没有 tag ${TAG}（本机 SDK ${VER}）。最近可用的 sdk-* tag："
      git ls-remote --tags "${URL}" 2>/dev/null | grep -o 'refs/tags/sdk-[0-9.]*$' \
          | sed 's|refs/tags/||' | sort -V | tail -5; } >&2
    exit 3
fi
echo "SDK 版本 ${VER} ⇒ pico-examples tag ${TAG}"

if [ -d "${DEST}/.git" ]; then
    CUR="$(git -C "${DEST}" describe --tags --exact-match 2>/dev/null || echo '(非 tag)')"
    if [ "${CUR}" = "${TAG}" ]; then echo "✓ 已就位：${DEST} @ ${TAG}"; exit 0; fi
    echo "  已存在但停在 ${CUR} ⇒ 重新 checkout 到 ${TAG}"
    git -C "${DEST}" fetch --tags --depth 1 origin "refs/tags/${TAG}:refs/tags/${TAG}" >/dev/null 2>&1 || true
    git -C "${DEST}" checkout --quiet "${TAG}"
    echo "✓ 已切到 ${TAG}"
    exit 0
fi

mkdir -p "${ROOT}/third_party"
echo "  克隆 pico-examples @ ${TAG} …"
if ! git clone --quiet --branch "${TAG}" --depth 1 \
        https://github.com/raspberrypi/pico-examples.git "${DEST}"; then
    echo "✗ 克隆失败（网络？代理？）—— 直连不通时可试 https_proxy=<proxy> 只放在环境变量里 ✗ 别写进仓" >&2
    exit 3
fi
echo "✓ 已取到 ${DEST} @ ${TAG}"
