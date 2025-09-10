#!/bin/bash
set -e

# 三方库名及对应git地址
LIB_NAMES=("ffmpeg" "x264" "SoundTouch" "libmp3lame" \
  "libass" "freetype2" "fribidi" "harfbuzz" "libiconv" "zlib" "libsoxr" \
  "openssl" "dav1d" "fontconfig" "expat" "libunibreak")

LIB_URLS=(
    "https://github.com/FFmpeg/FFmpeg.git"
    "https://code.videolan.org/videolan/x264.git"
    "https://codeberg.org/soundtouch/soundtouch"
    "https://github.com/rbrito/lame.git"
    "https://github.com/libass/libass.git"
    "https://gitlab.freedesktop.org/freetype/freetype.git"
    "https://github.com/fribidi/fribidi.git"
    "https://github.com/harfbuzz/harfbuzz.git"
    "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.18.tar.gz"
    "https://github.com/madler/zlib.git"
    "https://github.com/Equalzys/soxr"
    "https://github.com/openssl/openssl.git"
    "https://code.videolan.org/videolan/dav1d.git"
    "https://gitlab.freedesktop.org/fontconfig/fontconfig.git"
    "https://github.com/libexpat/libexpat.git"
    "https://github.com/adah1972/libunibreak.git"
)

# openssl
OPENSSL_BRANCH="openssl-3.4"
FONTCONFIG_BRANCH="2.16.2"

# 创建目录
EXTRA_DIR="$(pwd)/extra"
ANDROID_DIR="$(pwd)/android"
mkdir -p "$EXTRA_DIR"
mkdir -p "$ANDROID_DIR"

ARCHS=("arm64-v8a" "armeabi-v7a")

echo ">>> 开始拉取三方源码到 $EXTRA_DIR ..."

for i in "${!LIB_NAMES[@]}"; do
    name="${LIB_NAMES[$i]}"
    url="${LIB_URLS[$i]}"
    SRC_PATH="$EXTRA_DIR/$name"

    if [ "$name" = "libiconv" ]; then
        if [ -d "$SRC_PATH" ]; then
            echo ">>> [libiconv] 已存在，跳过。"
        else
            echo ">>> 正在下载 libiconv 官方 release 包 ..."
            cd "$EXTRA_DIR"
            curl -LO "$url"
            tar -zxf "libiconv-1.18.tar.gz"
            mv libiconv-1.18 libiconv
            rm -f libiconv-1.18.tar.gz
            echo ">>> [libiconv] 解压完成"
            cd - >/dev/null
        fi
    elif [ "$name" = "openssl" ]; then
        if [ -d "$SRC_PATH/.git" ]; then
            echo ">>> [openssl] 已存在，跳过。"
        else
            echo ">>> 正在克隆 openssl 分支: $OPENSSL_BRANCH ..."
            git clone --depth=1 --branch "$OPENSSL_BRANCH" "$url" "$SRC_PATH" || {
                echo "!!! 克隆 openssl 分支 $OPENSSL_BRANCH 失败，请检查网络或仓库地址。"
                exit 1
            }
            echo ">>> [openssl] $OPENSSL_BRANCH 分支克隆完成"
        fi
    elif [ "$name" = "fontconfig" ]; then
            if [ -d "$SRC_PATH/.git" ]; then
                echo ">>> [fontconfig] 已存在，跳过。"
            else
                echo ">>> 正在克隆 fontconfig 分支: $FONTCONFIG_BRANCH ..."
                git clone --depth=1 --branch "$FONTCONFIG_BRANCH" "$url" "$SRC_PATH" || {
                    echo "!!! 克隆 fontconfig 分支 $FONTCONFIG_BRANCH 失败，请检查网络或仓库地址。"
                    exit 1
                }
                echo ">>> [fontconfig] $FONTCONFIG_BRANCH 分支克隆完成"
            fi
    elif [ "$name" = "x264" ]; then
        if [ -d "$SRC_PATH/.git" ]; then
            echo ">>> [x264] 已存在，跳过。"
        else
            echo ">>> 正在克隆 x264 stable 分支 ..."
            git clone --depth=1 --branch stable "$url" "$SRC_PATH" || {
                echo "!!! 克隆 x264 stable 分支失败，请检查网络或仓库地址。"
                exit 1
            }
            echo ">>> [x264] stable 分支克隆完成"
        fi
    else
        if [ -d "$SRC_PATH/.git" ] || [ -d "$SRC_PATH/.svn" ]; then
            echo ">>> [$name] 已存在，跳过。"
        else
            echo ">>> 正在克隆 $name ..."
            git clone --depth=1 "$url" "$SRC_PATH" || {
                echo "!!! 克隆 $name 失败，请检查网络或仓库地址。"
                exit 1
            }
            echo ">>> [$name] 克隆完成"
        fi
    fi
done

echo ">>> 三方源码全部准备完毕！"

# 2. 每 ABI 拷贝一份源码到 android/xxx-abi
echo ">>> 开始为各 ABI 拷贝源码到 $ANDROID_DIR ..."

for name in "${LIB_NAMES[@]}"; do
    SRC_PATH="$EXTRA_DIR/$name"
    if [ ! -d "$SRC_PATH" ]; then
        echo "!!! 源码目录不存在: $SRC_PATH"
        exit 1
    fi
    for ABI in "${ARCHS[@]}"; do
        DST_PATH="$ANDROID_DIR/${name}-${ABI}"
        rm -rf "$DST_PATH"
        # 优化：避免拷贝二进制输出目录，减少杂项
#        rsync -a --exclude='bin' --exclude='build*' --exclude='test*' --exclude='.git' --exclude='.svn' "$SRC_PATH/" "$DST_PATH/"
        rsync -a "$SRC_PATH/" "$DST_PATH/"
        echo ">>> [$name] $ABI 拷贝完成: $DST_PATH"
    done
done

echo ">>> 所有三方源码已为各 ABI 拷贝完毕！"