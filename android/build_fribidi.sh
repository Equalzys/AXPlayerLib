#!/bin/bash
set -e

AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
[ ! -f "$NDK_PROP_FILE" ] && { echo "!!! 未找到 $NDK_PROP_FILE"; exit 1; }

ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2-)
ANDROID_NDK_HOME="${ANDROID_NDK_HOME//[$'\r\n']}"
[ ! -d "$ANDROID_NDK_HOME" ] && { echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"; exit 1; }

TOOLCHAIN=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2-)
TOOLCHAIN="${TOOLCHAIN//[$'\r\n']}"
[ ! -d "$TOOLCHAIN" ] && { echo "!!! TOOLCHAIN ($TOOLCHAIN) 路径无效"; exit 1; }

SRC_BASENAME="fribidi"
SRC_BASE="$AXPLAYER_ROOT/android"
BUILD_BASE="$AXPLAYER_ROOT/android/build/fribidi"
ANDROID_API=23
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() { echo "用法: $0 [clean|arm64|armv7a|all]"; exit 1; }
[ $# -eq 0 ] && usage

case "$1" in
    clean)
        for ABI in "${ARCHS[@]}"; do
            rm -rf "$SRC_BASE/${SRC_BASENAME}-${ABI}/build"
            rm -rf "$BUILD_BASE/$ABI"
        done
        echo ">>> 清理完成！"
        exit 0
        ;;
    arm64) TARGETS=("arm64-v8a");;
    armv7a) TARGETS=("armeabi-v7a");;
    all) TARGETS=("${ARCHS[@]}");;
    *) usage;;
esac

if command -v nproc >/dev/null 2>&1; then JOBS=$(nproc); else JOBS=$(sysctl -n hw.ncpu); fi

for ABI in "${TARGETS[@]}"; do
    echo ">>> [fribidi] 正在编译 $ABI ..."

    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    BUILD_DIR="$SRC_DIR/build"
    INSTALL_DIR="$BUILD_BASE/$ABI"
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    mkdir -p "$BUILD_DIR"

    # 生成 meson cross-file
    CROSSFILE="$BUILD_DIR/meson-cross-$ABI.txt"
    case "$ABI" in
        "arm64-v8a")
            echo "[binaries]
c = '$TOOLCHAIN/bin/aarch64-linux-android${ANDROID_API}-clang'
ar = '$TOOLCHAIN/bin/llvm-ar'
strip = '$TOOLCHAIN/bin/llvm-strip'

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'
" > "$CROSSFILE"
            ;;
        "armeabi-v7a")
            echo "[binaries]
c = '$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang'
ar = '$TOOLCHAIN/bin/llvm-ar'
strip = '$TOOLCHAIN/bin/llvm-strip'

[host_machine]
system = 'android'
cpu_family = 'arm'
cpu = 'armv7-a'
endian = 'little'
" > "$CROSSFILE"
            ;;
    esac

    cd "$BUILD_DIR"
    meson setup .. \
        --cross-file "$CROSSFILE" \
        --prefix "$INSTALL_DIR" \
        --libdir "lib" \
        --default-library static \
        --buildtype release \
        -Ddocs=false

    ninja -j$JOBS
    ninja install
    echo ">>> [fribidi] $ABI 编译完成！输出目录: $INSTALL_DIR"
done

echo ">>> fribidi 全部 ABI 静态库编译完成！"