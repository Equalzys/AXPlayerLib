#!/bin/bash
set -e

AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
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

ANDROID_API=24
SRC_DIR="$AXPLAYER_ROOT/extra/libiconv"
BUILD_BASE="$AXPLAYER_ROOT/android/build/libiconv"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        TARGETS=("${ARCHS[@]}")
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

# 兼容 macOS/Linux 并发编译
if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
else
    JOBS=$(sysctl -n hw.ncpu)
fi

for ABI in "${TARGETS[@]}"; do
    if [ "$1" = "clean" ]; then
        echo ">>> 清理 $ABI ..."
        rm -rf "$SRC_DIR/build_$ABI" "$BUILD_BASE/$ABI"
        continue
    fi

    echo ">>> [libiconv] 正在编译 $ABI ..."
    BUILD_DIR="$SRC_DIR/build_$ABI"
    INSTALL_DIR="$BUILD_BASE/$ABI"
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$SRC_DIR"
    [ -f Makefile ] && make distclean || true

    case "$ABI" in
        "arm64-v8a")
            HOST="aarch64-linux-android"
            CC="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/${HOST}${ANDROID_API}-clang"
            ;;
        "armeabi-v7a")
            HOST="arm-linux-androideabi"
            CC="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            ;;
        *)
            echo "不支持的 ABI: $ABI"
            exit 1
            ;;
    esac

    cd "$BUILD_DIR"
    "$SRC_DIR"/configure \
        --host="$HOST" \
        --prefix="$INSTALL_DIR" \
        --enable-static \
        --disable-shared \
        --with-pic \
        CC="$CC"

    make -j$JOBS
    make install
done

if [ "$1" != "clean" ]; then
    echo ">>> libiconv 全部 ABI 静态库编译完成！"
fi