#!/bin/bash
set -e

echo ">>> 批量编译全部三方静态库..."

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

LIB_LIST=(
    build_zlib.sh
    build_libmp3lame.sh
    build_SoundTouch.sh
    build_x264.sh
    build_dav1d.sh
    build_libsoxr.sh
    build_openssl.sh

    build_libunibreak.sh
    build_harfbuzz.sh
    build_iconv.sh
    build_expat.sh
    build_fribidi.sh
    build_freetype2.sh
    build_fontconfig.sh
    build_libass.sh
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