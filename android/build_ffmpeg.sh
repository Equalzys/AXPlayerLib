#!/bin/bash
set -euo pipefail

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="ffmpeg"
BUILD_BASE="$AXPLAYER_ROOT/android/build/ffmpeg"
THREE_BUILD_BASE="$AXPLAYER_ROOT/android/build"
SRC_BASE="$AXPLAYER_ROOT/android"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() { echo "用法: $0 [clean|arm64|armv7a|all]"; exit 1; }
[ $# -eq 0 ] && usage

case "$1" in
  clean)   TARGETS=("${ARCHS[@]}") ;;
  arm64)   TARGETS=("arm64-v8a") ;;
  armv7a)  TARGETS=("armeabi-v7a") ;;
  all)     TARGETS=("${ARCHS[@]}") ;;
  *)       usage ;;
esac

# 读取 NDK
NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
[ -f "$NDK_PROP_FILE" ] || { echo "!!! 未找到 $NDK_PROP_FILE"; exit 1; }
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
[ -d "$ANDROID_NDK_HOME" ] || { echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"; exit 1; }

TOOLCHAIN=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
if [ -z "${TOOLCHAIN:-}" ] || [ ! -d "$TOOLCHAIN" ]; then
  case "$(uname -s)" in
    Linux*)  HOST_OS=linux-x86_64 ;;
    Darwin*) HOST_OS=darwin-x86_64 ;;
    *) echo "!!! 不支持的主机系统：$(uname -s)"; exit 1 ;;
  esac
  TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_OS"
fi
[ -d "$TOOLCHAIN" ] || { echo "!!! TOOLCHAIN ($TOOLCHAIN) 路径无效"; exit 1; }

# 并发
if command -v nproc >/dev/null 2>&1; then JOBS=$(nproc); else JOBS=$(sysctl -n hw.ncpu); fi

# 清理
if [ "$1" = "clean" ]; then
  echo ">>> 清理 ffmpeg 各 ABI 目录 ..."
  for ABI in "${TARGETS[@]}"; do
    SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    if [ -f "$SRC/Makefile" ]; then
      (cd "$SRC" && make clean 2>/dev/null || true)
      (cd "$SRC" && make distclean 2>/dev/null || true)
    fi
    rm -rf "$SRC/build" "$BUILD_BASE/$ABI"
    rm -rf $SRC/*.o $SRC/*.so $SRC/*.lo $SRC/*.la 2>/dev/null || true
  done
  echo ">>> 清理完成！"
  exit 0
fi

for ABI in "${TARGETS[@]}"; do
  echo ">>> [ffmpeg] 正在编译 $ABI ..."

  case "$ABI" in
    arm64-v8a)
      ARCH="aarch64"; CPU="armv8-a"; HOST="aarch64-linux-android"
      CC="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
      CXX="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang++"
      ;;
    armeabi-v7a)
      ARCH="arm"; CPU="armv7-a"; HOST="arm-linux-androideabi"
      CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
      CXX="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang++"
      ;;
    *) echo "不支持的 ABI: $ABI"; exit 1 ;;
  esac

  # 依赖库前缀
  X264_PREFIX="$THREE_BUILD_BASE/x264/$ABI"
  LAME_PREFIX="$THREE_BUILD_BASE/libmp3lame/$ABI"
  ASS_PREFIX="$THREE_BUILD_BASE/libass/$ABI"
  FREETYPE_PREFIX="$THREE_BUILD_BASE/freetype2/$ABI"
  FRIBIDI_PREFIX="$THREE_BUILD_BASE/fribidi/$ABI"
  HARFBUZZ_PREFIX="$THREE_BUILD_BASE/harfbuzz/$ABI"
  ZLIB_PREFIX="$THREE_BUILD_BASE/zlib/$ABI"
  SOXR_PREFIX="$THREE_BUILD_BASE/libsoxr/$ABI"
  SOUNDTOUCH_PREFIX="$THREE_BUILD_BASE/SoundTouch/$ABI"
  OPENSSL_PREFIX="$THREE_BUILD_BASE/openssl/$ABI"
  LIBUNIBREAK_PREFIX="$THREE_BUILD_BASE/libunibreak/$ABI"
  DAV1D_PREFIX="$THREE_BUILD_BASE/dav1d/$ABI"
  EXPAT_PREFIX="$THREE_BUILD_BASE/expat/$ABI"
  ICONV_PREFIX="$THREE_BUILD_BASE/libiconv/$ABI"

  # 头文件
  EXTRA_CFLAGS=""
  for inc in \
    "$X264_PREFIX/include" "$LAME_PREFIX/include" "$ASS_PREFIX/include" \
    "$FREETYPE_PREFIX/include" "$FRIBIDI_PREFIX/include" "$HARFBUZZ_PREFIX/include" \
    "$ZLIB_PREFIX/include" "$SOXR_PREFIX/include" "$SOUNDTOUCH_PREFIX/include" \
    "$OPENSSL_PREFIX/include" "$EXPAT_PREFIX/include" "$DAV1D_PREFIX/include" \
    "$LIBUNIBREAK_PREFIX/include" "$ICONV_PREFIX/include"
  do
    [ -d "$inc" ] && EXTRA_CFLAGS="$EXTRA_CFLAGS -I$inc"
  done

  # 库搜索路径（供 configure 测试时使用）
  EXTRA_LDFLAGS=""
  for lib in \
    "$X264_PREFIX/lib" "$LAME_PREFIX/lib" "$ASS_PREFIX/lib" "$FREETYPE_PREFIX/lib" \
    "$FRIBIDI_PREFIX/lib" "$HARFBUZZ_PREFIX/lib" "$ZLIB_PREFIX/lib" "$SOXR_PREFIX/lib" \
    "$SOUNDTOUCH_PREFIX/lib" "$OPENSSL_PREFIX/lib" "$EXPAT_PREFIX/lib" "$DAV1D_PREFIX/lib" \
    "$LIBUNIBREAK_PREFIX/lib" "$ICONV_PREFIX/lib"
  do
    [ -d "$lib" ] && EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$lib"
  done
  [ "$ABI" = "armeabi-v7a" ] && EXTRA_LDFLAGS="$EXTRA_LDFLAGS -Wl,--fix-cortex-a8"

  # 源码与安装
  FFMPEG_SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
  BUILD_DIR="$FFMPEG_SRC_DIR/build"
  INSTALL_DIR="$BUILD_BASE/$ABI"
  mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
  cd "$BUILD_DIR"
  [ -f Makefile ] && make distclean || true

  # 仅使用我们提供的 .pc
  PKGCFG=""
  for d in \
    "$ICONV_PREFIX/lib/pkgconfig" \
    "$OPENSSL_PREFIX/lib/pkgconfig" \
    "$ZLIB_PREFIX/lib/pkgconfig" \
    "$X264_PREFIX/lib/pkgconfig" \
    "$SOUNDTOUCH_PREFIX/lib/pkgconfig" \
    "$SOXR_PREFIX/lib/pkgconfig" \
    "$LAME_PREFIX/lib/pkgconfig" \
    "$FREETYPE_PREFIX/lib/pkgconfig" \
    "$FRIBIDI_PREFIX/lib/pkgconfig" \
    "$HARFBUZZ_PREFIX/lib/pkgconfig" \
    "$DAV1D_PREFIX/lib/pkgconfig" \
    "$LIBUNIBREAK_PREFIX/lib/pkgconfig" \
    "$EXPAT_PREFIX/lib/pkgconfig" \
    "$ASS_PREFIX/lib/pkgconfig"
  do
    [ -d "$d" ] && PKGCFG="${PKGCFG:+$PKGCFG:}$d"
  done
  export PKG_CONFIG_PATH="$PKGCFG"
  export PKG_CONFIG_LIBDIR="$PKGCFG"

  # —— 诊断：确保能发现关键三方库 ——
  echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"
  echo ">>> libass:   $(pkg-config --modversion libass   2>&1 || true)"
  echo ">>> libsoxr:  $(pkg-config --libs soxr         2>&1 || true)"
  echo ">>> freetype: $(pkg-config --modversion freetype2 2>&1 || true)"

  export PATH="$TOOLCHAIN/bin:$PATH"
  export CC="$CC"; export CXX="$CXX"
  export AR="$TOOLCHAIN/bin/llvm-ar"
  export NM="$TOOLCHAIN/bin/llvm-nm"
  export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
  export STRINGS="$TOOLCHAIN/bin/llvm-strings"
  [ -x "$STRINGS" ] || STRINGS="$(which strings || true)"

  # —— 方案 A：为所有探测/链接补充数学库，避免 soxr 检测期因 -lm 缺失失败 ——
  # 若以后你把 libsoxr 用 OpenMP 编译，请把 EXTRA_LIBS 改成 "-lm -lomp"
  EXTRA_LIBS="-lm -lomp"

  "$FFMPEG_SRC_DIR/configure" \
    --prefix="$INSTALL_DIR" \
    --target-os=android \
    --arch="$ARCH" \
    --cpu="$CPU" \
    --cc="$CC" \
    --nm="$NM" \
    --ar="$AR" \
    --ranlib="$RANLIB" \
    --pkg-config=pkg-config \
    --pkg-config-flags="--static" \
    --sysroot="$TOOLCHAIN/sysroot" \
    --enable-cross-compile \
    --disable-shared --enable-static \
    --disable-programs --disable-doc \
    --disable-symver \
    --enable-pic \
    --enable-gpl --enable-version3 \
    --enable-pthreads \
    --enable-protocols \
    --enable-network \
    --enable-avfilter --enable-avformat --enable-avcodec \
    --enable-swresample --enable-swscale --enable-avdevice \
    --enable-jni --enable-mediacodec \
    --enable-libx264 \
    --enable-libmp3lame \
    --enable-libass \
    --enable-libfreetype \
    --enable-libfribidi \
    --enable-libharfbuzz \
    --enable-libsoxr \
    --enable-openssl \
    --enable-zlib \
    --extra-cflags="-Os -fPIC -DANDROID $EXTRA_CFLAGS" \
    --extra-ldflags="$EXTRA_LDFLAGS" \
    --extra-libs="$EXTRA_LIBS" \
    --disable-debug

  make -j"$JOBS"
  make install

  echo ">>> [$ABI] FFmpeg 静态库已安装到: $INSTALL_DIR"

  # ========= 链接成单一 libAXFCore.so =========
  OUT_SO="$INSTALL_DIR/libAXFCore.so"
  FFLIB="$INSTALL_DIR/lib"

  declare -a FF_A
  for a in libavcodec.a libavformat.a libavutil.a libswresample.a libswscale.a libavfilter.a libavdevice.a; do
    [ -f "$FFLIB/$a" ] && FF_A+=("$FFLIB/$a")
  done

  declare -a THIRD_A
  add_if_exist() { [ -f "$1" ] && THIRD_A+=("$1"); }
  add_if_exist "$X264_PREFIX/lib/libx264.a"
  add_if_exist "$LAME_PREFIX/lib/libmp3lame.a"
  add_if_exist "$ASS_PREFIX/lib/libass.a"
  add_if_exist "$FREETYPE_PREFIX/lib/libfreetype.a"
  add_if_exist "$FRIBIDI_PREFIX/lib/libfribidi.a"
  add_if_exist "$HARFBUZZ_PREFIX/lib/libharfbuzz.a"
  add_if_exist "$ZLIB_PREFIX/lib/libz.a"
  add_if_exist "$SOXR_PREFIX/lib/libsoxr.a"
  add_if_exist "$SOUNDTOUCH_PREFIX/lib/libSoundTouch.a"
  add_if_exist "$OPENSSL_PREFIX/lib/libssl.a"
  add_if_exist "$OPENSSL_PREFIX/lib/libcrypto.a"
  add_if_exist "$DAV1D_PREFIX/lib/libdav1d.a"
  add_if_exist "$EXPAT_PREFIX/lib/libexpat.a"
  add_if_exist "$LIBUNIBREAK_PREFIX/lib/libunibreak.a"

  if [ ${#FF_A[@]} -eq 0 ]; then
    echo "!!! 未找到 FFmpeg .a（检查 $FFLIB）"; exit 1
  fi

  echo ">>> [$ABI] 正在链接单一 so: $(basename "$OUT_SO")"

  EXTRA_LINK_FIX=""
  [ "$ABI" = "armeabi-v7a" ] && EXTRA_LINK_FIX="-Wl,--fix-cortex-a8"

  "$CC" -shared -o "$OUT_SO" \
    -Wl,-soname,libAXFCore.so \
    -Wl,--no-undefined -Wl,--gc-sections \
    -Wl,--exclude-libs,ALL \
    -Wl,--start-group \
      "${FF_A[@]}" \
      "${THIRD_A[@]}" \
    -Wl,--end-group \
    -llog -landroid -ldl -lm -lmediandk \
    -static-libstdc++ \
    $EXTRA_LINK_FIX

  "$TOOLCHAIN/bin/llvm-strip" --strip-unneeded "$OUT_SO" || true
  echo ">>> [$ABI] 产物: $OUT_SO"
done

echo ">>> FFmpeg （libAXFCore.so）全部 ABI 编译完成！"