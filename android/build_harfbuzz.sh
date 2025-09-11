#!/bin/bash
set -euo pipefail

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="harfbuzz"
BUILD_BASE="$AXPLAYER_ROOT/android/build/harfbuzz"
SRC_BASE="$AXPLAYER_ROOT/android"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() { echo "用法: $0 [clean|arm64|armv7a|all]"; exit 1; }
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

if [ "$1" = "clean" ]; then
  echo ">>> 清理 harfbuzz 各 ABI 目录 ..."
  for ABI in "${TARGETS[@]}"; do
    SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    rm -rf "$SRC_DIR/build_$ABI" "$BUILD_BASE/$ABI"
  done
  echo ">>> 清理完成！"
  exit 0
fi

# 是否可用 Ninja
GEN_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GEN_ARGS+=("-G" "Ninja")
fi

for ABI in "${TARGETS[@]}"; do
  echo ">>> [harfbuzz] 正在编译 $ABI ..."

  SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
  BUILD_DIR="$SRC_DIR/build_$ABI"
  INSTALL_DIR="$BUILD_BASE/$ABI"

  rm -rf "$BUILD_DIR" "$INSTALL_DIR"
  mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

  case "$ABI" in
    arm64-v8a)
      ANDROID_ABI="arm64-v8a"
      CFLAGS_EXTRA="-march=armv8-a"
      ;;
    armeabi-v7a)
      ANDROID_ABI="armeabi-v7a"
      CFLAGS_EXTRA="-march=armv7-a"
      ;;
    *) echo "不支持的 ABI: $ABI"; exit 1 ;;
  esac

  # 仅使用我们交叉出来的 freetype/zlib
  FREETYPE_PREFIX="$AXPLAYER_ROOT/android/build/freetype2/$ABI"
  ZLIB_PREFIX="$AXPLAYER_ROOT/android/build/zlib/$ABI"
  FREETYPE_PKG="$FREETYPE_PREFIX/lib/pkgconfig"
  ZLIB_PKG="$ZLIB_PREFIX/lib/pkgconfig"

  export PKG_CONFIG_PATH="$FREETYPE_PKG:$ZLIB_PKG"
  export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"

  echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
  pkg-config --modversion freetype2
  pkg-config --modversion zlib

  cd "$BUILD_DIR"

  FT_LIB="$FREETYPE_PREFIX/lib/libfreetype.a"
  FT_INC="$FREETYPE_PREFIX/include/freetype2"

  cmake "$SRC_DIR" \
    "${GEN_ARGS[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ANDROID_ABI" \
    -DANDROID_PLATFORM=android-"$ANDROID_API" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_PREFIX_PATH="$FREETYPE_PREFIX;$ZLIB_PREFIX" \
    -DHB_HAVE_FREETYPE=ON \
    -DFREETYPE_LIBRARY="$FT_LIB" \
    -DFREETYPE_INCLUDE_DIRS="$FT_INC" \
    -DHB_HAVE_GLIB=OFF \
    -DHB_HAVE_GOBJECT=OFF \
    -DHB_HAVE_ICU=OFF \
    -DHB_HAVE_GRAPHITE2=OFF \
    -DHB_BUILD_TESTS=OFF \
    -DHB_BUILD_UTILS=OFF \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -fPIC $CFLAGS_EXTRA" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--gc-sections -Wl,--no-undefined -Wl,--max-page-size=16384"

  # 用 cmake --build 跨生成器
  cmake --build . -j"$JOBS"
  cmake --install .

  # 自检
  echo ">>> 自检头文件:"
  ls -l "$INSTALL_DIR/include/harfbuzz/hb-ft.h" || {
    echo "!!! 未安装 hb-ft.h（说明没有开启 hb-ft 或未找到 freetype2），请检查上面的 cmake 输出"; exit 1; }
  echo ">>> 自检 pkg-config:"
  grep -E '^(Requires|Cflags|Libs)' "$INSTALL_DIR/lib/pkgconfig/harfbuzz.pc" || true

  echo ">>> [$ABI] OK -> $INSTALL_DIR"
done

echo ">>> harfbuzz 全部 ABI 静态库编译完成！"