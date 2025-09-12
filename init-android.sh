#!/bin/bash
set -euo pipefail

# ================== 固定分支/版本 ==================
FFMPEG_BRANCH="release/8.0"     # 固定 FFmpeg 分支
OPENSSL_BRANCH="openssl-3.4"

# ================== 三方库名及地址 ==================
LIB_NAMES=("ffmpeg" "x264" "SoundTouch" "libmp3lame" \
  "libass" "freetype2" "fribidi" "harfbuzz" "libiconv" "zlib" "libsoxr" \
  "openssl" "dav1d" "libunibreak")

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
  "https://github.com/adah1972/libunibreak.git"
)

# ================== 目录准备 ==================
EXTRA_DIR="$(pwd)/extra"
ANDROID_DIR="$(pwd)/android"
mkdir -p "$EXTRA_DIR" "$ANDROID_DIR"

ARCHS=("arm64-v8a" "armeabi-v7a")

echo ">>> 开始拉取三方源码到 $EXTRA_DIR ..."

# ================== 函数：克隆/更新 Git 仓库 ==================
clone_branch_or_default() {
  local name="$1" url="$2" dst="$3" branch="$4"

  if [ -d "$dst/.git" ]; then
    echo ">>> [$name] 已存在，跳过克隆。"
    return 0
  fi

  if [ -n "$branch" ]; then
    echo ">>> 正在克隆 $name 分支/标签: $branch ..."
    git clone --depth=1 --branch "$branch" "$url" "$dst" || {
      echo "!!! 克隆 $name 分支/标签 '$branch' 失败，请检查网络或仓库地址。"
      exit 1
    }
  else
    echo ">>> 正在克隆 $name 默认分支 ..."
    git clone --depth=1 "$url" "$dst" || {
      echo "!!! 克隆 $name 失败，请检查网络或仓库地址。"
      exit 1
    }
  fi
  echo ">>> [$name] 克隆完成"
}

# ================== 拉取源码 ==================
for i in "${!LIB_NAMES[@]}"; do
  name="${LIB_NAMES[$i]}"
  url="${LIB_URLS[$i]}"
  SRC_PATH="$EXTRA_DIR/$name"

  case "$name" in
    libiconv)
      if [ -d "$SRC_PATH" ]; then
        echo ">>> [libiconv] 已存在，跳过。"
      else
        echo ">>> 正在下载 libiconv 官方 release 包 ..."
        pushd "$EXTRA_DIR" >/dev/null
          curl -LO "$url"
          tar -zxf "libiconv-1.18.tar.gz"
          mv libiconv-1.18 libiconv
          rm -f libiconv-1.18.tar.gz
        popd >/dev/null
        echo ">>> [libiconv] 解压完成"
      fi
      ;;

    openssl)
      clone_branch_or_default "openssl" "$url" "$SRC_PATH" "$OPENSSL_BRANCH"
      ;;

    x264)
      clone_branch_or_default "x264" "$url" "$SRC_PATH" "stable"
      ;;

    ffmpeg)
      clone_branch_or_default "ffmpeg" "$url" "$SRC_PATH" "$FFMPEG_BRANCH"
      ;;

    *)
      # 其余仓库走默认分支浅克隆
      if [ -d "$SRC_PATH/.git" ] || [ -d "$SRC_PATH/.svn" ]; then
        echo ">>> [$name] 已存在，跳过。"
      else
        echo ">>> 正在克隆 $name ..."
        git clone --depth=1 "$url" "$SRC_PATH" || {
          echo "!!! 克隆 $name 失败，请检查网络或仓库地址：$url"
          exit 1
        }
        echo ">>> [$name] 克隆完成"
      fi
      ;;
  esac
done

echo ">>> 三方源码全部准备完毕！"

# ================== 按 ABI 拷贝源码 ==================
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

    rsync -a "$SRC_PATH/" "$DST_PATH/"
    echo ">>> [$name] $ABI 拷贝完成: $DST_PATH"
  done
done

echo ">>> 所有三方源码已为各 ABI 拷贝完毕！"

# ====== 独立处理 SoundTouch：复制到 MediaCore/soundtouch（其余逻辑不变） ======
MEDIA_CORE_DIR="$(pwd)/MediaCore"
DST_SOUNDTOUCH="$MEDIA_CORE_DIR/soundtouch"
SRC_SOUNDTOUCH="$EXTRA_DIR/SoundTouch"

mkdir -p "$MEDIA_CORE_DIR"
if [ -d "$SRC_SOUNDTOUCH" ]; then
  echo ">>> 同步 SoundTouch 源码到 $DST_SOUNDTOUCH"
  # 使用 rsync，排除无关目录
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete       --exclude=".git" --exclude=".github" --exclude="build" --exclude="Build*"       --exclude="cmake*" --exclude="CMake*" --exclude="docs" --exclude="examples"       --exclude="projects" --exclude="tests"       "$SRC_SOUNDTOUCH/" "$DST_SOUNDTOUCH/"
  else
    # 兜底：tar 管道复制
    rm -rf "$DST_SOUNDTOUCH"
    mkdir -p "$DST_SOUNDTOUCH"
    (cd "$SRC_SOUNDTOUCH" && tar cf - .       --exclude .git --exclude .github --exclude build --exclude Build*       --exclude cmake* --exclude CMake* --exclude docs --exclude examples       --exclude projects --exclude tests) | (cd "$DST_SOUNDTOUCH" && tar xpf -)
  fi
  echo ">>> SoundTouch 同步完成：$DST_SOUNDTOUCH"
else
  echo "!!! 未找到 $SRC_SOUNDTOUCH ，请检查是否已完成拉取"
fi
