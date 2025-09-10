#!/bin/bash
set -e

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="x264"
BUILD_BASE="$AXPLAYER_ROOT/android/build/x264"
SRC_BASE="$AXPLAYER_ROOT/android"

ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

# 解析参数
usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"; exit 1;
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 x264 各 ABI 目录 ..."
        for ABI in "${ARCHS[@]}"; do
            SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
            # 1. 能执行 make clean 就执行
            if [ -f "$SRC/Makefile" ]; then
                (cd "$SRC" && make clean 2>/dev/null || true)
                (cd "$SRC" && make distclean 2>/dev/null || true)
            fi
            # 2. 通用删除常见临时文件和产物
            rm -rf "$SRC/*.o" "$SRC/*.so" "$SRC/*.lo" "$SRC/*.la"

            rm -rf "$BUILD_BASE/$ABI"
            rm -rf "$BUILD_BASE/$ABI-so"
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

#获取ndk路径
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2-)
ANDROID_NDK_HOME="${ANDROID_NDK_HOME//[$'\r\n']}"
# 获取 Toolchains 路径
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


# 获取 CPU 核数
if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
else
    JOBS=$(sysctl -n hw.ncpu)
fi



for ABI in "${ARCHS[@]}"; do
    echo ">>> [x264] 正在编译 $ABI ..."

    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    INSTALL_DIR="$BUILD_BASE/$ABI-so"
    rm -rf "$INSTALL_DIR"
    mkdir -p "$INSTALL_DIR"

    case "$ABI" in
        "arm64-v8a")
            HOST="aarch64-linux-android"
            CROSS_PREFIX="$TOOLCHAIN/bin/${HOST}-"
            CC="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
            CXX="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang++"
            OPTIMIZE_CFLAGS="-march=armv8-a"
            ;;
        "armeabi-v7a")
            HOST="armv7a-linux-androideabi"
            CROSS_PREFIX="$TOOLCHAIN/bin/${HOST}-"
            CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            CXX="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang++"
            OPTIMIZE_CFLAGS="-march=armv7-a"
            ;;
        *)
            echo "不支持的 ABI: $ABI"
            exit 1
            ;;
    esac

    cd "$SRC_DIR"
    make distclean || true

    # 导出新版 NDK 所需工具链环境变量
    export PATH="$TOOLCHAIN/bin:$PATH"
    export CC="$CC"
    export CXX="$CXX"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export NM="$TOOLCHAIN/bin/llvm-nm"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRINGS="$TOOLCHAIN/bin/llvm-strings"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
#    export EXTRA_CFLAGS="-O2 -g0"
#    export EXTRA_CXXFLAGS="-O2 -g0"
    if [ ! -x "$STRINGS" ]; then
        export STRINGS="$(which strings)"
    fi

    export LDFLAGS="-Wl,--hash-style=sysv -Wl,-z,max-page-size=16384"

    ./configure \
        --host="$HOST" \
        --cross-prefix="$CROSS_PREFIX" \
        --sysroot="$TOOLCHAIN/sysroot" \
        --prefix="$INSTALL_DIR" \
        --enable-shared \
        --disable-static \
        --disable-cli \
        --disable-asm \
        --enable-pic \
        --extra-cflags="-fPIC $OPTIMIZE_CFLAGS -D__ANDROID_API__=21 -DANDROID" \

    make -j$JOBS
    make install
done

echo ">>> x264 全部 ABI so库编译完成！"