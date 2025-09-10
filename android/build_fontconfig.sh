#!/bin/bash
set -e
# 适配 GNU libtool for macOS
if [ "$(uname)" = "Darwin" ]; then
    export LIBTOOL=$(which glibtool)
    export LIBTOOLIZE=$(which glibtoolize)
else
    export LIBTOOL=$(which libtool)
    export LIBTOOLIZE=$(which libtoolize)
fi

ANDROID_API=24
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="fontconfig"
BUILD_BASE="$AXPLAYER_ROOT/android/build/fontconfig"
SRC_BASE="$AXPLAYER_ROOT/android"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 fontconfig 各 ABI 目录 ..."
        for ABI in "${ARCHS[@]}"; do
            SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
            # 1. 能执行 make clean 就执行
            if [ -f "$SRC/Makefile" ]; then
                (cd "$SRC" && make clean 2>/dev/null || true)
                (cd "$SRC" && make distclean 2>/dev/null || true)
            fi
            # 2. 通用删除常见临时文件和产物
            rm -rf "$SRC/*.o" "$SRC/*.so" "$SRC/*.lo" "$SRC/*.la"

            rm -rf "$SRC/build" "$BUILD_BASE/$ABI"
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
    echo ">>> [fontconfig] 正在编译 $ABI ..."

    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    BUILD_DIR="$SRC_DIR/build_$ABI"
    INSTALL_DIR="$BUILD_BASE/$ABI"
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    mkdir -p "$BUILD_DIR"

    # ABI参数
    case "$ABI" in
        "arm64-v8a")
            HOST="aarch64-linux-android"
            CROSS_PREFIX="$TOOLCHAIN/bin/${HOST}-"
            CC="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
            CXX="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang++"
            ;;
        "armeabi-v7a")
            HOST="arm-linux-androideabi"
            CROSS_PREFIX="$TOOLCHAIN/bin/armv7a-linux-androideabi-"
            CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            CXX="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang++"
            ;;
        *)
            echo "不支持的 ABI: $ABI"
            exit 1
            ;;
    esac

    # fontconfig 依赖 expat, freetype2, zlib，需优先保证这几个都已交叉编译好并安装到 build 目录
    EXPAT_PREFIX="$AXPLAYER_ROOT/android/build/expat/$ABI"
    FREETYPE_PREFIX="$AXPLAYER_ROOT/android/build/freetype2/$ABI"
    ZLIB_PREFIX="$AXPLAYER_ROOT/android/build/zlib/$ABI"
    LIBUNIBREAK_PREFIX="$AXPLAYER_ROOT/android/build/libunibreak/$ABI"
    LIBICONV_PREFIX="$AXPLAYER_ROOT/android/build/libiconv/$ABI"


    export PKG_CONFIG_PATH="$FREETYPE_PREFIX/lib/pkgconfig:$ZLIB_PREFIX/lib/pkgconfig:$EXPAT_PREFIX/lib/pkgconfig:$LIBUNIBREAK_PREFIX/lib/pkgconfig"
    export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
    export LIBS="-lz"
    export LDFLAGS="-L$ZLIB_PREFIX/lib -lz"
    export PATH="$TOOLCHAIN/bin:$PATH"
    export CC="$CC"
    export CXX="$CXX"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRINGS="$TOOLCHAIN/bin/llvm-strings"
    [ ! -x "$STRINGS" ] && export STRINGS="$(which strings)"

    cd "$SRC_DIR"
    [ -f Makefile ] && make distclean || true
    [ -f configure ] || ./autogen.sh

    ./configure \
        --host="$HOST" \
        --prefix="$INSTALL_DIR" \
        --enable-static \
        --disable-shared \
        --with-pic \
        --with-freetype-config="pkg-config freetype2" \
        --with-expat="$EXPAT_PREFIX" \
        --with-zlib="$ZLIB_PREFIX" \
        --with-libiconv="$LIBICONV_PREFIX" \
        LIBS="-lz" \
        CC="$CC"

    make -j$JOBS
    make install
done

echo ">>> fontconfig 全部 ABI 静态库编译完成！"