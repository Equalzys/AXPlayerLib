#!/bin/bash
set -e

ANDROID_API=24
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="zlib"
BUILD_BASE="$AXPLAYER_ROOT/android/build/zlib"
SRC_BASE="$AXPLAYER_ROOT/android"

ARCHS=("arm64-v8a" "armeabi-v7a")
NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
JOBS=

usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 zlib 各 ABI 目录 ..."
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
        done
        echo ">>> 清理完成！"
        exit 0
        ;;
    arm64)  TARGETS=("arm64-v8a");;
    armv7a) TARGETS=("armeabi-v7a");;
    all)    TARGETS=("${ARCHS[@]}");;
    *)      usage;;
esac

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

if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
else
    JOBS=$(sysctl -n hw.ncpu)
fi

for ABI in "${TARGETS[@]}"; do
    echo ">>> [zlib] 正在编译 $ABI ..."
    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    INSTALL_DIR="$BUILD_BASE/$ABI"
    rm -rf "$INSTALL_DIR"
    mkdir -p "$INSTALL_DIR"
    BUILD_DIR="$SRC_DIR/build"

    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ABI" \
        -DANDROID_PLATFORM=android-$ANDROID_API \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

    make -j"$JOBS"
    make install
done

echo ">>> zlib 全部 ABI 静态库编译完成！"