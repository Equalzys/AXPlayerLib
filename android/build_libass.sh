#!/bin/bash
set -euo pipefail

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="libass"
BUILD_BASE="$AXPLAYER_ROOT/android/build/libass"
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

# 清理
if [ "$1" = "clean" ]; then
  echo ">>> 清理 libass 各 ABI 目录 ..."
  for ABI in "${TARGETS[@]}"; do
    SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    if [ -f "$SRC/Makefile" ]; then
      (cd "$SRC" && make clean 2>/dev/null || true)
      (cd "$SRC" && make distclean 2>/dev/null || true)
    fi
    rm -rf "$BUILD_BASE/$ABI"
    # 不能加引号，否则通配符不展开
    rm -rf $SRC/*.o $SRC/*.so $SRC/*.lo $SRC/*.la 2>/dev/null || true
  done
  echo ">>> 清理完成！"
  exit 0
fi

for ABI in "${TARGETS[@]}"; do
  echo ">>> [libass] 正在编译 $ABI ..."

  SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
  BUILD_DIR="$SRC_DIR/build_$ABI"
  INSTALL_DIR="$BUILD_BASE/$ABI"
  rm -rf "$BUILD_DIR" "$INSTALL_DIR"
  mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

  SYSROOT="$TOOLCHAIN/sysroot"

  case "$ABI" in
    arm64-v8a)
      HOST="aarch64-linux-android"
      CC_BIN="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
      CPU_MARCH="armv8-a"
      ABI_EXTRA_CFLAGS=""
      ;;
    armeabi-v7a)
      HOST="arm-linux-androideabi"
      CC_BIN="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
      CPU_MARCH="armv7-a"
      ABI_EXTRA_CFLAGS="-mthumb -mfloat-abi=softfp -mfpu=neon"
      ;;
    *) echo "不支持的 ABI: $ABI"; exit 1 ;;
  esac

  # 依赖 .pc 路径
  ICONV_PKG="$AXPLAYER_ROOT/android/build/libiconv/$ABI/lib/pkgconfig"
  FREETYPE_PKG="$AXPLAYER_ROOT/android/build/freetype2/$ABI/lib/pkgconfig"
  FRIBIDI_PKG="$AXPLAYER_ROOT/android/build/fribidi/$ABI/lib/pkgconfig"
  HARFBUZZ_PKG="$AXPLAYER_ROOT/android/build/harfbuzz/$ABI/lib/pkgconfig"
  ZLIB_PKG="$AXPLAYER_ROOT/android/build/zlib/$ABI/lib/pkgconfig"
  LIBUNIBREAK_PKG="$AXPLAYER_ROOT/android/build/libunibreak/$ABI/lib/pkgconfig"

  PKG_LIST=("$ICONV_PKG" "$FREETYPE_PKG" "$FRIBIDI_PKG" "$HARFBUZZ_PKG" "$ZLIB_PKG" "$LIBUNIBREAK_PKG")
  PKG_CONFIG_PATH="$(IFS=:; echo "${PKG_LIST[*]}")"
  export PKG_CONFIG_PATH
  export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"

  # 工具链
  export PATH="$TOOLCHAIN/bin:$PATH"
  export CC="$CC_BIN"
  export AR="$TOOLCHAIN/bin/llvm-ar"
  export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
  export NM="$TOOLCHAIN/bin/llvm-nm"
  export STRIP="$TOOLCHAIN/bin/llvm-strip"
  export LD="$TOOLCHAIN/bin/ld.lld"

  COMMON_CFLAGS="-Os -fPIC -ffunction-sections -fdata-sections -fdeclspec -march=$CPU_MARCH $ABI_EXTRA_CFLAGS --sysroot=$SYSROOT"
  export CFLAGS="${CFLAGS:-} $COMMON_CFLAGS"
  export CPPFLAGS="${CPPFLAGS:-} $COMMON_CFLAGS"
  export LDFLAGS="${LDFLAGS:-} --sysroot=$SYSROOT"

  # 让 AC_SEARCH_LIBS 找到 libiconv
  ICONV_PREFIX="$AXPLAYER_ROOT/android/build/libiconv/$ABI"
  export CPPFLAGS="${CPPFLAGS} -I$ICONV_PREFIX/include"
  export LDFLAGS="${LDFLAGS} -L$ICONV_PREFIX/lib"
  export LIBS="${LIBS:-} -liconv -lcharset"

  echo "PKG_CONFIG_PATH: $PKG_CONFIG_PATH"
  pkg-config --modversion freetype2 || true
  pkg-config --modversion fribidi || true
  pkg-config --modversion harfbuzz || true
  pkg-config --modversion libunibreak || true

  # 源目录准备（若无 configure 则跑 autogen）
  cd "$SRC_DIR"
  [ -f Makefile ] && make distclean || true
  [ -f configure ] || ./autogen.sh

  # out-of-tree 配置与构建
  cd "$BUILD_DIR"
  "$SRC_DIR/configure" \
    --host="$HOST" \
    --prefix="$INSTALL_DIR" \
    --enable-static \
    --disable-shared \
    --with-pic \
    --disable-fontconfig \
    --disable-require-system-font-provider \
    --disable-coretext \
    --disable-directwrite \
    CC="$CC" AR="$AR" RANLIB="$RANLIB" LD="$LD" NM="$NM" STRIP="$STRIP"

  make -j"$JOBS"
  make install

  echo ">>> [$ABI] 输出: $INSTALL_DIR/lib/libass.a"
done

echo ">>> libass 全部 ABI 静态库编译完成！"