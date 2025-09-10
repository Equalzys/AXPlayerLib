#!/bin/bash
set -e

echo ">>> 批量编译全部三方静态库..."

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

LIB_LIST=(
    build_x264.sh
    build_libmp3lame.sh
    build_libass.sh
    build_freetype2.sh
    build_fribidi.sh
    build_harfbuzz.sh
    build_libiconv.sh
    build_zlib.sh
    build_libsoxr.sh
    build_SoundTouch.sh
)

for script in "${LIB_LIST[@]}"; do
    if [ -x "$SCRIPT_DIR/$script" ]; then
        echo "========== $script =========="
        "$SCRIPT_DIR/$script"
        echo
    else
        echo "!!! 未找到 $SCRIPT_DIR/$script，或无可执行权限"
        exit 1
    fi
done

echo ">>> 全部三方库静态库编译完成！"