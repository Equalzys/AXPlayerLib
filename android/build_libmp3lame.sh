#!/bin/bash
set -e

AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="libmp3lame"
BUILD_BASE="$AXPLAYER_ROOT/android/build/$SRC_BASENAME"
SRC_BASE="$AXPLAYER_ROOT/android"

ANDROID_API=23

NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"

# 检查 NDK
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

ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 $SRC_BASENAME 各 ABI 目录 ..."
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

if command -v nproc >/dev/null 2>&1; then
    JOBS=$(nproc)
else
    JOBS=$(sysctl -n hw.ncpu)
fi

for ABI in "${TARGETS[@]}"; do
    echo ">>> [$SRC_BASENAME] 正在编译 $ABI ..."

    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    INSTALL_DIR="$BUILD_BASE/$ABI"
    INSTALL_PKGCONFIG_DIR="$BUILD_BASE/$ABI/lib/pkgconfig"
    mkdir -p "$INSTALL_DIR"
    mkdir -p "$INSTALL_PKGCONFIG_DIR"

    case "$ABI" in
        "arm64-v8a")
            HOST="aarch64-linux-android"
            CC="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
            CROSS_PREFIX="$TOOLCHAIN/bin/${HOST}-"
            CXX="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang++"
            CPU="armv8-a"
            ;;
        "armeabi-v7a")
            HOST="arm-linux-androideabi"
            CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            CROSS_PREFIX="$TOOLCHAIN/bin/$HOST-"
            CXX="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang++"
            CPU="armv7-a"
            ;;
        *)
            echo "不支持的 ABI: $ABI"
            exit 1
            ;;
    esac

    cd "$SRC_DIR"
    make distclean || true

    export PATH="$TOOLCHAIN/bin:$PATH"
    export CC="$CC"
    export CXX="$CXX"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export NM="$TOOLCHAIN/bin/llvm-nm"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRINGS="$TOOLCHAIN/bin/llvm-strings"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"

    OPTIMIZE_CFLAGS="-march=$CPU"
    export CFLAGS="-Os -fpic -fdeclspec $OPTIMIZE_CFLAGS"
    export CPPFLAGS="-Os -fpic -fdeclspec $OPTIMIZE_CFLAGS"
    export EXTRA_CFLAGS="-O2 -g0"
    export EXTRA_CXXFLAGS="-O2 -g0"

    ./configure \
        --host=$HOST \
        --prefix="$INSTALL_DIR" \
        --target=android \
        --enable-static \
        --disable-shared \
        --disable-frontend

    make -j$JOBS
    make install

    file $INSTALL_DIR/lib/libmp3lame.a # 一定要确认架构

# 生成 libmp3lame.pc
PC_FILE="$INSTALL_PKGCONFIG_DIR/libmp3lame.pc"
cat > "$PC_FILE" <<EOF
prefix=$INSTALL_DIR
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: libmp3lame
Description: LAME MP3 encoder library
Version: 3.100
Requires:
Conflicts:
Libs: -L\${libdir} -lmp3lame -lm
Libs.private:
Cflags: -I\${includedir}
EOF

done

echo ">>> $SRC_BASENAME 全部 ABI 静态库编译完成！"