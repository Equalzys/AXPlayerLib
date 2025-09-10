#!/bin/bash
set -e

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="dav1d"
BUILD_BASE="$AXPLAYER_ROOT/android/build/dav1d"
SRC_BASE="$AXPLAYER_ROOT/android"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

# 参数解析
usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 dav1d 各 ABI 目录 ..."
        for ABI in "${ARCHS[@]}"; do
            SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
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
    echo ">>> [dav1d] 正在编译 $ABI ..."

    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    BUILD_DIR="$SRC_DIR/build"
    INSTALL_DIR="$BUILD_BASE/$ABI-so"
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

    case "$ABI" in
        "arm64-v8a")
            HOST="aarch64-linux-android"
            CPU="armv8-a"
            CLANG="$TOOLCHAIN/bin/aarch64-linux-android${ANDROID_API}-clang"
            ;;
        "armeabi-v7a")
            HOST="armv7a-linux-androideabi"
            CPU="armv7-a"
            CLANG="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            ;;
        *)
            echo "不支持的 ABI: $ABI"
            exit 1
            ;;
    esac

    PKG_CONFIG_BIN=$(which pkg-config 2>/dev/null || echo "/usr/bin/false")
    AR_BIN="$TOOLCHAIN/bin/llvm-ar"
    STRIP_BIN="$TOOLCHAIN/bin/llvm-strip"

    # 生成 cross-file
    CROSS_FILE="$BUILD_DIR/meson-cross-$ABI.txt"
    cat > "$CROSS_FILE" <<EOF
[binaries]
c = '$CLANG'
ar = '$AR_BIN'
strip = '$STRIP_BIN'
pkg-config = '$PKG_CONFIG_BIN'

[host_machine]
system = 'android'
cpu_family = '${HOST%%-*}'
cpu = '$CPU'
endian = 'little'

[built-in options]
c_args = ['-DANDROID', '-fPIC']
cpp_args = ['-DANDROID', '-fPIC']
EOF

    cd "$BUILD_DIR"
    meson setup .. --cross-file="$CROSS_FILE" --prefix="$INSTALL_DIR" --default-library=shared --buildtype=release
    ninja -j$JOBS
    ninja install

    echo ">>> [dav1d] $ABI 编译完成，输出目录: $INSTALL_DIR"
done

echo ">>> dav1d 全部 ABI 静态库编译完成！"