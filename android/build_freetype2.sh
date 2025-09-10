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
SRC_DIR="$AXPLAYER_ROOT/extra/freetype2"
BUILD_BASE="$AXPLAYER_ROOT/android/build/freetype2"
ALL_ARCHS=("arm64-v8a" "armeabi-v7a")

usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
PARAM="${1:-all}"
case "$PARAM" in
    clean)   ARCHS=("${ALL_ARCHS[@]}");;
    arm64*)  ARCHS=("arm64-v8a");;
    armv7*)  ARCHS=("armeabi-v7a");;
    all|*)   ARCHS=("${ALL_ARCHS[@]}");;
esac

for ABI in "${ARCHS[@]}"; do
    BUILD_DIR="$SRC_DIR/build_$ABI"
    INSTALL_DIR="$BUILD_BASE/$ABI"

    if [ "$PARAM" = "clean" ]; then
        echo ">>> 清理 $ABI ..."
        rm -rf "$BUILD_DIR" "$INSTALL_DIR"
        continue
    fi

    echo ">>> [freetype2] 正在编译 $ABI ..."
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM=android-"$ANDROID_API" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--max-page-size=16384"

    make -j$(nproc || sysctl -n hw.ncpu)
    make install
done

echo ">>> freetype2 全部 ABI 静态库编译完成！"