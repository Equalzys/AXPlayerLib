#!/bin/bash
set -euo pipefail

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="harfbuzz"
BUILD_BASE="$AXPLAYER_ROOT/android/build/harfbuzz"
SRC_BASE="$AXPLAYER_ROOT/android"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage(){ echo "用法: $0 [clean|arm64|armv7a|all]"; exit 1; }
[ $# -eq 1 ] || usage

case "$1" in
  clean)  TARGETS=("${ARCHS[@]}") ;;
  arm64)  TARGETS=("arm64-v8a") ;;
  armv7a) TARGETS=("armeabi-v7a") ;;
  all)    TARGETS=("${ARCHS[@]}") ;;
  *) usage ;;
esac

NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
[ -f "$NDK_PROP_FILE" ] || { echo "!!! 未找到 $NDK_PROP_FILE"; exit 1; }
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
TOOLCHAIN=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
[ -d "$ANDROID_NDK_HOME" ] || { echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"; exit 1; }
[ -d "$TOOLCHAIN" ] || { echo "!!! TOOLCHAIN ($TOOLCHAIN) 路径无效"; exit 1; }

# 并发
if command -v nproc >/dev/null 2>&1; then JOBS=$(nproc); else JOBS=$(sysctl -n hw.ncpu); fi


# 清理
if [ "$1" = "clean" ]; then
  echo ">>> 清理 harfbuzz 各 ABI 目录 ..."
  for ABI in "${TARGETS[@]}"; do
    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    rm -rf "$SRC_DIR/build_$ABI" "$BUILD_BASE/$ABI"
  done
  echo ">>> 清理完成！"
  exit 0
fi

# 需要 meson & ninja
command -v meson >/dev/null 2>&1 || { echo "!!! 需要 meson，请先安装：pip install meson"; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo "!!! 需要 ninja，请先安装：apt-get install -y ninja-build (或 pip install ninja)"; exit 1; }

for ABI in "${TARGETS[@]}"; do
  echo ">>> [harfbuzz] 正在编译 $ABI ..."

  case "$ABI" in
    arm64-v8a)
      HOST_TRIPLE="aarch64-linux-android"
      CC_BIN="${HOST_TRIPLE}${ANDROID_API}-clang"
      CPU_MARCH="armv8-a"
      ;;
    armeabi-v7a)
      HOST_TRIPLE="armv7a-linux-androideabi"
      CC_BIN="${HOST_TRIPLE}${ANDROID_API}-clang"
      CPU_MARCH="armv7-a"
      ;;
    *) echo "不支持的 ABI: $ABI"; exit 1 ;;
  esac

  SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
  BUILD_DIR="$SRC_DIR/build_$ABI"
  INSTALL_DIR="$BUILD_BASE/$ABI"

  rm -rf "$BUILD_DIR" "$INSTALL_DIR"
  mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

  export PATH="$TOOLCHAIN/bin:$PATH"
  export CC="$TOOLCHAIN/bin/$CC_BIN"
  export AR="$TOOLCHAIN/bin/llvm-ar"
  export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
  export CFLAGS="-O2 -fPIC -march=$CPU_MARCH"
  export LDFLAGS=""

  FREETYPE_PKG="$AXPLAYER_ROOT/android/build/freetype2/$ABI/lib/pkgconfig"
  ZLIB_PKG="$AXPLAYER_ROOT/android/build/zlib/$ABI/lib/pkgconfig"

  PKG_LIST=("$FREETYPE_PKG" "$ZLIB_PKG")
  PKG_CONFIG_PATH="$(IFS=:; echo "${PKG_LIST[*]}")"
  export PKG_CONFIG_PATH
  export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"

  # 诊断：确保能找到 freetype2
  echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
  pkg-config --modversion freetype2

  # Meson 配置（静态库 + 启用 freetype，禁用其它不需要的依赖）
  meson setup "$BUILD_DIR" "$SRC_DIR" \
    --prefix "$INSTALL_DIR" \
    --default-library=static \
    --buildtype=release \
    -Dtests=disabled \
    -Dbenchmark=disabled \
    -Ddocs=disabled \
    -Dintrospection=disabled \
    -Dfreetype=enabled \
    -Dglib=disabled \
    -Dgobject=disabled \
    -Dicu=disabled \
    -Dgraphite=disabled \


  meson compile -C "$BUILD_DIR" -j"$JOBS"
  meson install -C "$BUILD_DIR"

  # 自检：hb-ft.h 是否安装 & pc 是否带上 freetype2
  echo ">>> 自检头文件:"
  ls -l "$INSTALL_DIR/include/harfbuzz/hb-ft.h"
  echo ">>> 自检 pkg-config:"
  grep -E '^(Requires|Cflags|Libs)' "$INSTALL_DIR/lib/pkgconfig/harfbuzz.pc" || true
  echo ">>> 完成 $ABI ：输出 $INSTALL_DIR"
done

echo ">>> harfbuzz 全部 ABI 静态库编译完成！"