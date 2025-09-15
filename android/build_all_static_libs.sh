#!/bin/bash
set -euo pipefail

# 用法校验
usage() {
  echo "用法: $0 [clean|arm64|armv7a|all]"
  exit 1
}

if [ $# -ne 1 ]; then
  usage
fi

MODE="$1"
case "$MODE" in
  clean|arm64|armv7a|all) ;;
  *) usage ;;
esac

echo ">>> 批量编译全部三方静态库... (mode: $MODE)"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"


LIB_LIST=(
  build_zlib.sh
  build_libmp3lame.sh
  build_x264.sh
  build_dav1d.sh
  build_libsoxr.sh
  build_openssl.sh

  build_libunibreak.sh
  build_freetype2.sh
  build_harfbuzz.sh
  build_iconv.sh
  build_fribidi.sh
  build_libass.sh
)

# 出错定位：打印失败脚本名与行号
trap 'echo "!!! 失败：$CURRENT_SCRIPT (mode: $MODE)"; exit 1' ERR

for script in "${LIB_LIST[@]}"; do
  CURRENT_SCRIPT="$script"
  full="$SCRIPT_DIR/$script"
  echo "========== $script (mode: $MODE) =========="
  if [ -x "$full" ]; then
    "$full" "$MODE"
    echo
  else
    echo "!!! 未找到或无执行权限: $full"
    exit 1
  fi
done

echo ">>> 全部三方库静态库编译完成！（mode: $MODE）"