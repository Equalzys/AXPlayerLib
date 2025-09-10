#!/bin/bash
set -euo pipefail

AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
if [ ! -f "$NDK_PROP_FILE" ]; then
    echo "!!! 未找到 $NDK_PROP_FILE"
    exit 1
fi

# 从 local.properties 读取 NDK 与 Toolchain
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
TOOLCHAIN=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')

if [ -z "${ANDROID_NDK_HOME}" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"
    exit 1
fi

# 若未在 local.properties 指定 ndk_toolchains，则按系统推断 prebuilt 目录
if [ -z "${TOOLCHAIN}" ] || [ ! -d "$TOOLCHAIN" ]; then
    case "$(uname -s)" in
        Linux*)   HOST_OS=linux-x86_64 ;;
        Darwin*)  HOST_OS=darwin-x86_64 ;;
        *)        echo "!!! 不支持的主机系统：$(uname -s)"; exit 1 ;;
    esac
    TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_OS"
fi

if [ ! -d "$TOOLCHAIN" ]; then
    echo "!!! TOOLCHAIN ($TOOLCHAIN) 路径无效"
    exit 1
fi

ANDROID_API=23
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
    clean)   TARGETS=("${ARCHS[@]}") ;;
    arm64)   TARGETS=("arm64-v8a") ;;
    armv7a)  TARGETS=("armeabi-v7a") ;;
    all)     TARGETS=("${ARCHS[@]}") ;;
    *)       usage ;;
esac

# 并发
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

    case "$ABI" in
        arm64-v8a)
            HOST="aarch64-linux-android"
            CC="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
            ;;
        armeabi-v7a)
            HOST="arm-linux-androideabi"
            CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            ;;
        *)
            echo "不支持的 ABI: $ABI"; exit 1 ;;
    esac

    # 工具与 flags
    export PATH="$TOOLCHAIN/bin:$PATH"
    export CC="$CC"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
    export LD="$TOOLCHAIN/bin/ld.lld"

    # 适当的 CFLAGS（libiconv 纯 C）
    export CFLAGS="-O2 -fPIC"
    export CPPFLAGS="-O2 -fPIC"

    # out-of-tree 构建
    cd "$SRC_DIR"
    [ -f Makefile ] && make distclean || true

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # 显式 build 三元组，减少误判
    BUILD_TRIPLET="$("$SRC_DIR/build-aux/config.guess" 2>/dev/null || "$SRC_DIR/config.guess" 2>/dev/null || echo x86_64-pc-linux-gnu)"

    "$SRC_DIR/configure" \
        --build="$BUILD_TRIPLET" \
        --host="$HOST" \
        --prefix="$INSTALL_DIR" \
        --enable-static \
        --disable-shared \
        --with-pic \
        CC="$CC" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP"

    make -j"$JOBS"
    make install
done

if [ "$1" != "clean" ]; then
    echo ">>> libiconv 全部 ABI 静态库编译完成！"
fi