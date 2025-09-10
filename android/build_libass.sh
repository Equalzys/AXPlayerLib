#!/bin/bash
set -e

ANDROID_API=24
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="libass"
BUILD_BASE="$AXPLAYER_ROOT/android/build/libass"
SRC_BASE="$AXPLAYER_ROOT/android"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

# 参数解析
usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"; exit 1;
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 libass 各 ABI 目录 ..."
        for ABI in "${ARCHS[@]}"; do
            SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
            if [ -f "$SRC/Makefile" ]; then
                (cd "$SRC" && make clean 2>/dev/null || true)
                (cd "$SRC" && make distclean 2>/dev/null || true)
            fi
            rm -rf "$SRC/*.o" "$SRC/*.so" "$SRC/*.lo" "$SRC/*.la"
            rm -rf "$BUILD_BASE/$ABI"
        done
        echo ">>> 清理完成！"
        exit 0
        ;;
    arm64)
        TARGETS=("arm64-v8a")
        ;;
    armv7a)
        TARGETS=("armeabi-v7a")
        ;;
    all)
        TARGETS=("${ARCHS[@]}")
        ;;
    *)
        usage
        ;;
esac

NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
if [ ! -f "$NDK_PROP_FILE" ]; then
    echo "!!! 未找到 $NDK_PROP_FILE"
    exit 1
fi
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2-)
ANDROID_NDK_HOME="${ANDROID_NDK_HOME//[$'\r\n']}"
TOOLCHAIN=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2-)
TOOLCHAIN="${TOOLCHAIN//[$'\r\n']}"

if [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"
    exit 1
fi
if [ ! -d "$TOOLCHAIN" ]; then
    echo "!!! TOOLCHAIN ($TOOLCHAIN) 路径无效"
    exit 1
fi
if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
else
    JOBS=$(sysctl -n hw.ncpu)
fi

for ABI in "${TARGETS[@]}"; do
    echo ">>> [libass] 正在编译 $ABI ..."

    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    BUILD_DIR="$SRC_DIR/build_$ABI"
    INSTALL_DIR="$BUILD_BASE/$ABI"
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    mkdir -p "$BUILD_DIR"

    case "$ABI" in
        "arm64-v8a")
            HOST="aarch64-linux-android"
            CC="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
            CPU="armv8-a"
            ;;
        "armeabi-v7a")
            HOST="arm-linux-androideabi"
            CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            CPU="armv7-a"
            ;;
        *)
            echo "不支持的 ABI: $ABI"
            exit 1
            ;;
    esac

    # 指定依赖库的安装路径，确保只引用你自己交叉产物的 .pc 文件！
    FREETYPE_PKG="$AXPLAYER_ROOT/android/build/freetype2/$ABI/lib/pkgconfig"
    FRIBIDI_PKG="$AXPLAYER_ROOT/android/build/fribidi/$ABI/lib/pkgconfig"
    HARFBUZZ_PKG="$AXPLAYER_ROOT/android/build/harfbuzz/$ABI/lib/pkgconfig"
    ZLIB_PKG="$AXPLAYER_ROOT/android/build/zlib/$ABI/lib/pkgconfig"
    FONTCONFIG_PKG="$AXPLAYER_ROOT/android/build/fontconfig/$ABI/lib/pkgconfig"
    LIBUNIBREAK_PKG="$AXPLAYER_ROOT/android/build/libunibreak/$ABI/lib/pkgconfig"
    PKG_CONFIG_PATH="$FREETYPE_PKG:$FRIBIDI_PKG:$HARFBUZZ_PKG:$ZLIB_PKG:$FONTCONFIG_PKG:$LIBUNIBREAK_PKG"
    export PKG_CONFIG_PATH
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"

    echo "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
    echo "PKG_CONFIG_LIBDIR: $PKG_CONFIG_LIBDIR"
    pkg-config --modversion freetype2 || true
    pkg-config --modversion fribidi || true
    pkg-config --modversion harfbuzz || true
    pkg-config --modversion zlib || true
    pkg-config --modversion fontconfig || true
    pkg-config --modversion libunibreak || true

    pkg-config --libs --cflags zlib
    pkg-config --libs --cflags freetype2
    pkg-config --libs --cflags fribidi
    pkg-config --libs --cflags harfbuzz
    pkg-config --libs --cflags fontconfig
    pkg-config --libs --cflags libunibreak

    cd "$SRC_DIR"
    [ -f Makefile ] && make distclean || true
    [ -f configure ] || ./autogen.sh

    export PATH="$TOOLCHAIN/bin:$PATH"
    export CC="$CC"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRINGS="$TOOLCHAIN/bin/llvm-strings"
    [ ! -x "$STRINGS" ] && export STRINGS="$(which strings)"
    OPTIMIZE_CFLAGS="-march=$CPU"
    export CFLAGS="-Os -fpic -fdeclspec $OPTIMIZE_CFLAGS"
    export CPPFLAGS="-Os -fpic -fdeclspec $OPTIMIZE_CFLAGS"
    export EXTRA_CFLAGS="-O2 -g0"
    export EXTRA_CXXFLAGS="-O2 -g0"

    cd "$BUILD_DIR"
    "$SRC_DIR/configure" \
        --host="$HOST" \
        --prefix="$INSTALL_DIR" \
        --enable-static \
        --disable-shared \
        --with-pic \
        CC="$CC"

    make -j$JOBS
    make install
done

echo ">>> libass 全部 ABI 静态库编译完成！"