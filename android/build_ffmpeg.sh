#!/bin/bash
set -e

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="ffmpeg"
BUILD_BASE="$AXPLAYER_ROOT/android/build/ffmpeg"
THREE_BUILD_BASE="$AXPLAYER_ROOT/android/build"
SRC_BASE="$AXPLAYER_ROOT/android"
ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() {
    echo "用法: $0 [clean|arm64|armv7a|all]"
    exit 1
}
if [ $# -eq 0 ]; then usage; fi

case "$1" in
    clean)
        echo ">>> 清理 ffmpeg 各 ABI 目录 ..."
        for ABI in "${ARCHS[@]}"; do
            SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
            if [ -f "$SRC/Makefile" ]; then
                (cd "$SRC" && make clean 2>/dev/null || true)
                (cd "$SRC" && make distclean 2>/dev/null || true)
            fi
            rm -rf "$SRC/*.o" "$SRC/*.so" "$SRC/*.lo" "$SRC/*.la"
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
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2-)
ANDROID_NDK_HOME="${ANDROID_NDK_HOME//[$'\r\n']}"
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
    echo ">>> [ffmpeg] 正在编译 $ABI ..."
EXTRA_CFLAGS=
EXTRA_LDFLAGS=
    # ABI相关参数
    case "$ABI" in
        "arm64-v8a")
            ARCH="aarch64"
            CPU="armv8-a"
            HOST="aarch64-linux-android"
            CROSS_PREFIX="$TOOLCHAIN/bin/${HOST}-"
            SYSROOT="$TOOLCHAIN/sysroot"
            CC="$TOOLCHAIN/bin/${HOST}${ANDROID_API}-clang"
            ;;
        "armeabi-v7a")
            ARCH="arm"
            CPU="armv7-a"
            HOST="arm-linux-androideabi"
            CROSS_PREFIX="$TOOLCHAIN/bin/${HOST}-"
            SYSROOT="$TOOLCHAIN/sysroot"
            CC="$TOOLCHAIN/bin/armv7a-linux-androideabi${ANDROID_API}-clang"
            EXTRA_LDFLAGS="$EXTRA_LDFLAGS -Wl,--fix-cortex-a8"
            ;;
        *)
            echo "不支持的 ABI: $ABI"
            exit 1
            ;;
    esac

    # 依赖三方库
    X264_PREFIX="$THREE_BUILD_BASE/x264/$ABI"
    LAME_PREFIX="$THREE_BUILD_BASE/libmp3lame/$ABI"
    ASS_PREFIX="$THREE_BUILD_BASE/libass/$ABI"
    FREETYPE_PREFIX="$THREE_BUILD_BASE/freetype2/$ABI"
    FRIBIDI_PREFIX="$THREE_BUILD_BASE/fribidi/$ABI"
    HARFBUZZ_PREFIX="$THREE_BUILD_BASE/harfbuzz/$ABI"
    ICONV_PREFIX="$THREE_BUILD_BASE/libiconv/$ABI"
    ZLIB_PREFIX="$THREE_BUILD_BASE/zlib/$ABI"
    SOXR_PREFIX="$THREE_BUILD_BASE/libsoxr/$ABI"
    SOUNDTOUCH_PREFIX="$THREE_BUILD_BASE/SoundTouch/$ABI"
    OPENSSL_PREFIX="$THREE_BUILD_BASE/openssl/$ABI"
    FONTCOFIG_PREFIX="$THREE_BUILD_BASE/fontconfig/$ABI"
    LIBUNIBREAK_PREFIX="$THREE_BUILD_BASE/libunibreak/$ABI"
    DAV1D_PREFIX="$THREE_BUILD_BASE/dav1d/$ABI"
    EXPAT_PREFIX="$THREE_BUILD_BASE/expat/$ABI"

    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$X264_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$LAME_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$ASS_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$FREETYPE_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$FRIBIDI_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$HARFBUZZ_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$ICONV_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$ZLIB_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$SOXR_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$SOUNDTOUCH_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$OPENSSL_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$EXPAT_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$DAV1D_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$FONTCOFIG_PREFIX/include"
    EXTRA_CFLAGS="$EXTRA_CFLAGS -I$LIBUNIBREAK_PREFIX/include"

    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$X264_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$LAME_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$ASS_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$FREETYPE_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$FRIBIDI_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$HARFBUZZ_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$ICONV_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$ZLIB_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$SOXR_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$SOUNDTOUCH_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$OPENSSL_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$EXPAT_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$DAV1D_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$FONTCOFIG_PREFIX/lib"
    EXTRA_LDFLAGS="$EXTRA_LDFLAGS -L$LIBUNIBREAK_PREFIX/lib"

    # out-of-tree build，防止污染源码
    FFMPEG_SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    BUILD_DIR="$FFMPEG_SRC_DIR/build"
    INSTALL_DIR="$BUILD_BASE/$ABI"
    mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
    cd "$BUILD_DIR"
    # 清理
    [ -f Makefile ] && make distclean || true

    THREE_CONFIG_PATH="$THREE_BUILD_BASE/openssl/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/zlib/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/x264/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/SoundTouch/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/libsoxr/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/libmp3lame/$ABI-so/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/freetype2/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/fribidi/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/fontconfig/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/harfbuzz/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/dav1d/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/libunibreak/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/expat/$ABI/lib/pkgconfig:"
    THREE_CONFIG_PATH="$THREE_CONFIG_PATH$THREE_BUILD_BASE/libass/$ABI/lib/pkgconfig"

    export PKG_CONFIG_PATH="$THREE_CONFIG_PATH"

    echo "------- PKG_CONFIG_PATH for $ABI: -------"
    echo "$PKG_CONFIG_PATH"
    ls $THREE_BUILD_BASE/libass/$ABI/lib/pkgconfig/
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --modversion libass
    cat $THREE_BUILD_BASE/libass/$ABI/lib/pkgconfig/libass.pc


    echo "------- PKG_CONFIG_PATH ls-------"
#    cat /Users/admin/WorkSpace/AndroidGitCode/Gitee/AXPlayerLib/android/build/libmp3lame/arm64-v8a-so/lib/pkgconfig/libmp3lame.pc


    ls $THREE_BUILD_BASE/freetype2/$ABI/lib/pkgconfig/freetype2.pc
    ls $THREE_BUILD_BASE/fribidi/$ABI/lib/pkgconfig/fribidi.pc
    ls $THREE_BUILD_BASE/fontconfig/$ABI/lib/pkgconfig/fontconfig.pc
    ls $THREE_BUILD_BASE/libunibreak/$ABI/lib/pkgconfig/libunibreak.pc
    ls $THREE_BUILD_BASE/libmp3lame/$ABI/lib/pkgconfig/libmp3lame.pc
    ls $THREE_BUILD_BASE/dav1d/$ABI/lib/pkgconfig/dav1d.pc
    ls $THREE_BUILD_BASE/libsoxr/$ABI/lib/pkgconfig/soxr.pc

    echo "------- PKG_CONFIG_PATH cflags-------"
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --modversion libmp3lame
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --modversion dav1d

    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags zlib
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags libass
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags freetype2
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags fribidi
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags fontconfig
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags libunibreak
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags libmp3lame
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags dav1d
    PKG_CONFIG_PATH="$PKG_CONFIG_PATH" pkg-config --libs --cflags soxr

    echo "----------pkg-config-----------"
    pkg-config --modversion libass # 0.17.4
    pkg-config --modversion libmp3lame
    pkg-config --modversion dav1d # 0.17.4
    pkg-config --libs --cflags libass #    -I/Users/admin/WorkSpace/AndroidGitCode/Gitee/AXPlayerLib/android/build/libass/arm64-v8a/include -I/opt/homebrew/Cellar/fontconfig/2.16.0/include -I/opt/homebrew/Cellar/libunibreak/6.1/include -I/opt/homebrew/Cellar/harfbuzz/11.2.1/include/harfbuzz -I/opt/homebrew/Cellar/glib/2.84.2/include/glib-2.0 -I/opt/homebrew/Cellar/glib/2.84.2/lib/glib-2.0/include -I/opt/homebrew/opt/gettext/include -I/opt/homebrew/Cellar/pcre2/10.45/include -I/opt/homebrew/Cellar/graphite2/1.3.14/include -I/opt/homebrew/Cellar/fribidi/1.0.16/include/fribidi -I/opt/homebrew/opt/freetype/include/freetype2 -I/opt/homebrew/opt/libpng/include/libpng16 -L/Users/admin/WorkSpace/AndroidGitCode/Gitee/AXPlayerLib/android/build/libass/arm64-v8a/lib -lass -lm -L/opt/homebrew/Cellar/fontconfig/2.16.0/lib -lfontconfig -L/opt/homebrew/Cellar/libunibreak/6.1/lib -lunibreak -L/opt/homebrew/Cellar/harfbuzz/11.2.1/lib -lharfbuzz -L/opt/homebrew/Cellar/fribidi/1.0.16/lib -lfribidi -L/opt/homebrew/opt/freetype/lib -lfreetype
    pkg-config --libs --cflags zlib
    pkg-config --libs --cflags freetype2
    pkg-config --libs --cflags fribidi
    pkg-config --libs --cflags fontconfig
    pkg-config --libs --cflags libunibreak
    pkg-config --libs --cflags libmp3lame
    pkg-config --libs --cflags dav1d
    pkg-config --libs --cflags soxr

    export PATH="$TOOLCHAIN/bin:$PATH"
    export CC="$CC"
    export CXX="$CXX"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export NM="$TOOLCHAIN/bin/llvm-nm"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRINGS="$TOOLCHAIN/bin/llvm-strings"
#    export EXTRA_CFLAGS="-O2 -g0"
#    export EXTRA_CXXFLAGS="-O2 -g0"
    if [ ! -x "$STRINGS" ]; then
        export STRINGS="$(which strings)"
    fi

    "$FFMPEG_SRC_DIR/configure" \
        --prefix="$INSTALL_DIR" \
        --target-os=android \
        --arch="$ARCH" \
        --cpu="$CPU" \
        --cross-prefix="$CROSS_PREFIX" \
        --cc="$CC" \
        --nm="$TOOLCHAIN/bin/llvm-nm" \
        --ar="$TOOLCHAIN/bin/llvm-ar" \
        --ranlib="$TOOLCHAIN/bin/llvm-ranlib" \
        --pkg-config="$THREE_CONFIG_PATH" \
        --pkg-config-flags="--static" \
        --sysroot="$SYSROOT" \
        --enable-cross-compile \
        --enable-shared \
        --disable-static \
        --disable-doc \
        --disable-programs \
        --disable-symver \
        --enable-pic \
        --enable-gpl \
        --enable-version3 \
        --enable-pthreads \
        --enable-protocols \
        --enable-avdevice \
        --enable-avfilter \
        --enable-network \
        --disable-muxers \
        --enable-muxer=mp4 \
        --enable-muxer=mpegts \
        --enable-muxer=mp3 \
        --enable-muxer=hls \
        --enable-encoder=mp3,aac,libx264 \
        --enable-openssl \
        --enable-libx264 \
        --enable-zlib \
        --enable-libsoxr \
        --enable-jni \
        --enable-mediacodec \
        --enable-hwaccels \
        --enable-omx \
        --enable-vulkan \
        --enable-libxml2 \
        --enable-libvorbis \
        --enable-libvpx \
        --extra-cflags="-Os -fPIC -DANDROID $EXTRA_CFLAGS" \
        --extra-ldflags="$EXTRA_LDFLAGS" \
        --disable-debug \
        --enable-libmp3lame \
        --enable-libass \
        --enable-libfreetype \
        --enable-libfribidi \
        --enable-libharfbuzz \

    make -j$JOBS
    make install

    echo ">>> FFmpeg so 编译完成: $ABI => $INSTALL_DIR"
done

echo ">>> FFmpeg 全部 ABI so 编译完成！"