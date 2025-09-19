#!/bin/bash
set -euo pipefail

# ================== 固定分支/版本 ==================
FFMPEG_BRANCH="release/8.0"     # 固定 FFmpeg 分支
OPENSSL_BRANCH="openssl-3.4"
OBOE_VERSION="1.10.0"

# ================== 目录准备 ==================
ROOT_DIR="$(pwd)"
EXTRA_DIR="$ROOT_DIR/extra"            # 三方源码统一放这里
ANDROID_DIR="$ROOT_DIR/android"        # 针对各 ABI 的展开目录
MEDIA_CORE_DIR="$ROOT_DIR/MediaCore"   # 可直接编进 libAXPlayer.so 的源码

mkdir -p "$EXTRA_DIR" "$ANDROID_DIR" "$MEDIA_CORE_DIR"

ARCHS=("arm64-v8a" "armeabi-v7a")

# ================== 三方库名及地址（会为每个 ABI 拷贝到 android/*，不包含 soundtouch/libyuv；oboe 仅 extra→MediaCore） ==================
LIB_NAMES=("ffmpeg" "x264" "libmp3lame" \
  "libass" "freetype2" "fribidi" "harfbuzz" "libiconv" "zlib" "libsoxr" \
  "openssl" "dav1d" "libunibreak" "oboe")

LIB_URLS=(
  "https://github.com/FFmpeg/FFmpeg.git"
  "https://code.videolan.org/videolan/x264.git"
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
  "https://codeload.github.com/google/oboe/zip/refs/tags/${OBOE_VERSION}"
)

# ================== MediaCore 专用源码（仅拷贝到 MediaCore/*，不展开到 android/*） ==================
MC_NAMES=("soundtouch" "libyuv")
MC_URLS=(
  "https://codeberg.org/soundtouch/soundtouch"                 # -> extra/soundtouch
  "https://chromium.googlesource.com/external/libyuv"          # -> extra/libyuv
)

echo ">>> 开始拉取三方源码到 $EXTRA_DIR ..."

# ================== 公共函数 ==================
need_cmd() { command -v "$1" >/dev/null 2>&1 || { echo "!!! 需要命令：$1"; exit 1; }; }

fetch_file() {
  # $1=url  $2=dst_file
  local url="$1" dst="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -L --retry 3 -o "$dst" "$url"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$dst" "$url"
  else
    echo "!!! 需要 curl 或 wget 以下载：$url"
    exit 1
  fi
}

clone_branch_or_default() {
  local name="$1" url="$2" dst="$3" branch="${4:-}"

  if [ -d "$dst/.git" ]; then
    echo ">>> [$name] 已存在，跳过克隆。"
    return 0
  fi

  if [ -n "${branch}" ]; then
    echo ">>> 正在克隆 $name 分支/标签: $branch ..."
    git clone --depth=1 --branch "$branch" "$url" "$dst" || {
      echo "!!! 克隆 $name 分支/标签 '$branch' 失败，请检查网络或仓库地址。"
      exit 1
    }
  else
    echo ">>> 正在克隆 $name 默认分支 ..."
    git clone --depth=1 "$url" "$dst" || {
      echo "!!! 克隆 $name 失败，请检查网络或仓库地址：$url"
      exit 1
    }
  fi
  echo ">>> [$name] 克隆完成"
}

clone_default_if_absent() {
  local name="$1" url="$2" dst="$3"
  if [ -d "$dst/.git" ] || [ -d "$dst/.svn" ]; then
    echo ">>> [$name] 已存在，跳过。"
  else
    echo ">>> 正在克隆 $name ..."
    git clone --depth=1 "$url" "$dst" || {
      echo "!!! 克隆 $name 失败，请检查网络或仓库地址：$url"
      exit 1
    }
    echo ">>> [$name] 克隆完成"
  fi
}

sync_to_dir() {
  # 用 rsync（优先）或 tar 进行目录同步，并过滤不必要内容
  local src="$1" dst="$2"
  mkdir -p "$dst"
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete \
      --exclude=".git" --exclude=".github" \
      --exclude="build" --exclude="Build*" \
      --exclude="docs" --exclude="examples" \
      --exclude="projects" --exclude="tests" \
      "$src/" "$dst/"
  else
    rm -rf "$dst"
    mkdir -p "$dst"
    (cd "$src" && tar cf - . \
      --exclude .git --exclude .github \
      --exclude build --exclude 'Build*' \
      --exclude 'cmake*' --exclude 'CMake*' \
      --exclude docs --exclude examples \
      --exclude projects --exclude tests) | (cd "$dst" && tar xpf -)
  fi
}

# ================== 拉取“会展开到 android/* 的库”（oboe 仅下载，不展开到 android/*） ==================
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
          fetch_file "$url" "libiconv-1.18.tar.gz"
          tar -zxf "libiconv-1.18.tar.gz"
          mv libiconv-1.18 libiconv
          rm -f "libiconv-1.18.tar.gz"
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

    oboe)
      if [ -d "$SRC_PATH" ]; then
        echo ">>> [oboe] 已存在，跳过下载。"
      else
        echo ">>> 正在下载 Oboe v${OBOE_VERSION} 源码（zip）..."
        need_cmd unzip
        pushd "$EXTRA_DIR" >/dev/null
          local_zip="oboe-${OBOE_VERSION}.zip"
          fetch_file "$url" "$local_zip"
          unzip -q "$local_zip"
          mv "oboe-${OBOE_VERSION}" "oboe"
          rm -f "$local_zip"
        popd >/dev/null
        echo ">>> [oboe] 解压完成：$SRC_PATH"
      fi
      ;;

    *)
      clone_default_if_absent "$name" "$url" "$SRC_PATH"
      ;;
  esac
done

# ================== 拉取 MediaCore 专用源码：soundtouch & libyuv（只拷贝到 MediaCore） ==================
for i in "${!MC_NAMES[@]}"; do
  name="${MC_NAMES[$i]}"
  url="${MC_URLS[$i]}"
  SRC_PATH="$EXTRA_DIR/$name"
  clone_default_if_absent "$name" "$url" "$SRC_PATH"
done

echo ">>> 三方源码全部准备完毕！"

# ================== 按 ABI 拷贝源码到 android/*（不包含 soundtouch/libyuv/oboe） ==================
echo ">>> 开始为各 ABI 拷贝源码到 $ANDROID_DIR ..."

for name in "${LIB_NAMES[@]}"; do
  # 按要求：oboe 不展开到 android/*
  if [ "$name" = "oboe" ]; then
    echo ">>> [oboe] 不展开到 android/*（按要求跳过）"
    continue
  fi

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

echo ">>> 已为各 ABI 完成源码拷贝（soundtouch/libyuv/oboe 未拷贝到 android/*）。"

# ================== 仅将 soundtouch 与 libyuv + oboe 拷贝到 MediaCore/* ==================
echo ">>> 同步 MediaCore 专用源码 ..."

# soundtouch -> MediaCore/soundtouch
SRC_SOUNDTOUCH="$EXTRA_DIR/soundtouch"
DST_SOUNDTOUCH="$MEDIA_CORE_DIR/soundtouch"
if [ -d "$SRC_SOUNDTOUCH" ]; then
  echo ">>> 同步 SoundTouch 源码到 $DST_SOUNDTOUCH"
  sync_to_dir "$SRC_SOUNDTOUCH" "$DST_SOUNDTOUCH"
  echo ">>> SoundTouch 同步完成：$DST_SOUNDTOUCH"
else
  echo "!!! 未找到 $SRC_SOUNDTOUCH ，请检查是否已完成拉取"
fi

# libyuv -> MediaCore/libyuv
SRC_LIBYUV="$EXTRA_DIR/libyuv"
DST_LIBYUV="$MEDIA_CORE_DIR/libyuv"
if [ -d "$SRC_LIBYUV" ]; then
  echo ">>> 同步 libyuv 源码到 $DST_LIBYUV"
  sync_to_dir "$SRC_LIBYUV" "$DST_LIBYUV"
  echo ">>> libyuv 同步完成：$DST_LIBYUV"
else
  echo "!!! 未找到 $SRC_LIBYUV ，请检查是否已完成拉取"
fi

# oboe -> MediaCore/oboe  （仅同步到 MediaCore）
SRC_OBOE="$EXTRA_DIR/oboe"
DST_OBOE="$MEDIA_CORE_DIR/oboe"
if [ -d "$SRC_OBOE" ]; then
  echo ">>> 同步 Oboe 源码到 $DST_OBOE"
  sync_to_dir "$SRC_OBOE" "$DST_OBOE"
  echo ">>> Oboe 同步完成：$DST_OBOE"
else
  echo "!!! 未找到 $SRC_OBOE ，请检查是否已完成拉取"
fi

echo ">>> 全部完成：android/* 已展开公共库（不含 oboe/soundtouch/libyuv）；MediaCore/* 包含 soundtouch / libyuv / oboe。"