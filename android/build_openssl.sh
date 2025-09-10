#!/bin/bash
set -e

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="openssl"
BUILD_BASE="$AXPLAYER_ROOT/android/build/openssl"
SRC_BASE="$AXPLAYER_ROOT/android"

ARCHS=("arm64-v8a" "armeabi-v7a")

usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 openssl 各 ABI 目录 ..."
        for ABI in "${ARCHS[@]}"; do
            SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
            rm -rf "$BUILD_BASE/$ABI"
            # 1. 能执行 make clean 就执行
            if [ -f "$SRC/Makefile" ]; then
                (cd "$SRC" && make clean 2>/dev/null || true)
                (cd "$SRC" && make distclean 2>/dev/null || true)
            fi
            # 2. 通用删除常见临时文件和产物
            rm -rf "$SRC/*.o" "$SRC/*.so" "$SRC/*.lo" "$SRC/*.la"


        done
        echo ">>> 清理完成！"
        exit 0
        ;;
    arm64)  TARGETS=("arm64-v8a");;
    armv7a) TARGETS=("armeabi-v7a");;
    all)    TARGETS=("${ARCHS[@]}");;
    *)      usage;;
esac

NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
if [ ! -f "$NDK_PROP_FILE" ]; then
    echo "!!! 未找到 $NDK_PROP_FILE"
    exit 1
fi
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2-)
ANDROID_NDK_HOME="${ANDROID_NDK_HOME//[$'\r\n']}"
if [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"
    exit 1
fi

# 抽取工具链路径
TOOLCHAINS="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64"

if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
else
    JOBS=$(sysctl -n hw.ncpu)
fi

for ABI in "${TARGETS[@]}"; do
    echo ">>> [openssl] 正在编译 $ABI ..."

    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    INSTALL_DIR="$BUILD_BASE/$ABI"

    case "$ABI" in
        "arm64-v8a") TARGET="android-arm64";;
        "armeabi-v7a") TARGET="android-arm";;
        *) echo "不支持的 ABI: $ABI"; exit 1;;
    esac

    rm -rf "$INSTALL_DIR"
    mkdir -p "$INSTALL_DIR"

    cd "$SRC_DIR"

    export ANDROID_NDK_HOME
    export PATH="$TOOLCHAINS/bin:$PATH"

    ./Configure \
        $TARGET \
        -D__ANDROID_API__=$ANDROID_API \
        --prefix="$INSTALL_DIR" \
        no-shared \
        no-tests \
        no-unit-test \
        -fPIC

    make -j"$JOBS"
    make install_sw
done

echo ">>> openssl 全部 ABI 静态库编译完成！"