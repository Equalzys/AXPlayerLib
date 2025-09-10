#!/bin/bash
set -euo pipefail

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="openssl"
BUILD_BASE="$AXPLAYER_ROOT/android/build/openssl"
SRC_BASE="$AXPLAYER_ROOT/android"

ARCHS=("arm64-v8a" "armeabi-v7a")
TARGETS=()

usage() {
  echo "用法: $0 [clean|arm64|armv7a|all]"
  exit 1
}
[ $# -eq 0 ] && usage

case "$1" in
  clean)   TARGETS=("${ARCHS[@]}") ;;
  arm64)   TARGETS=("arm64-v8a") ;;
  armv7a)  TARGETS=("armeabi-v7a") ;;
  all)     TARGETS=("${ARCHS[@]}") ;;
  *)       usage ;;
esac

# 读取 NDK 路径
NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
if [ ! -f "$NDK_PROP_FILE" ]; then
  echo "!!! 未找到 $NDK_PROP_FILE"
  exit 1
fi
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
[ -d "$ANDROID_NDK_HOME" ] || { echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"; exit 1; }

# 优先读取 ndk_toolchains；若未设置则按主机系统推断
TOOLCHAINS=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n' || true)
if [ -z "${TOOLCHAINS:-}" ] || [ ! -d "$TOOLCHAINS" ]; then
  case "$(uname -s)" in
    Linux*)  HOST_OS=linux-x86_64 ;;
    Darwin*) HOST_OS=darwin-x86_64 ;;
    *) echo "!!! 不支持的主机系统：$(uname -s)"; exit 1 ;;
  esac
  TOOLCHAINS="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_OS"
fi
[ -d "$TOOLCHAINS" ] || { echo "!!! TOOLCHAINS ($TOOLCHAINS) 路径无效"; exit 1; }

# 并发
if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
else
  JOBS=$(sysctl -n hw.ncpu)
fi

# 清理
if [ "$1" = "clean" ]; then
  echo ">>> 清理 openssl 各 ABI 目录 ..."
  for ABI in "${TARGETS[@]}"; do
    SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
    rm -rf "$BUILD_BASE/$ABI"
    if [ -f "$SRC/Makefile" ]; then
      (cd "$SRC" && make clean 2>/dev/null || true)
      (cd "$SRC" && make distclean 2>/dev/null || true)
    fi
    # 注意：这里不能加引号，否则通配符不展开
    rm -rf $SRC/*.o $SRC/*.so $SRC/*.lo $SRC/*.la 2>/dev/null || true
  done
  echo ">>> 清理完成！"
  exit 0
fi

# 需要 perl
if ! command -v perl >/dev/null 2>&1; then
  echo "!!! 未安装 perl，请先安装：sudo apt-get install -y perl"
  exit 1
fi

# 开始构建
for ABI in "${TARGETS[@]}"; do
  echo ">>> [openssl] 正在编译 $ABI ..."

  SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
  INSTALL_DIR="$BUILD_BASE/$ABI"

  case "$ABI" in
    arm64-v8a)
      TARGET="android-arm64"
      HOST_TRIPLET="aarch64-linux-android"
      CC_BIN="${HOST_TRIPLET}${ANDROID_API}-clang"
      CXX_BIN="${HOST_TRIPLET}${ANDROID_API}-clang++"
      ;;
    armeabi-v7a)
      TARGET="android-arm"
      # 注意：前缀为 armv7a-linux-androideabi（带 v7a）
      HOST_TRIPLET="armv7a-linux-androideabi"
      CC_BIN="${HOST_TRIPLET}${ANDROID_API}-clang"
      CXX_BIN="${HOST_TRIPLET}${ANDROID_API}-clang++"
      ;;
    *) echo "不支持的 ABI: $ABI"; exit 1 ;;
  esac

  rm -rf "$INSTALL_DIR"
  mkdir -p "$INSTALL_DIR"

  cd "$SRC_DIR"

  # 环境变量：让 OpenSSL 的 Configure 能找到 NDK/工具
  export ANDROID_NDK="$ANDROID_NDK_HOME"
  export ANDROID_NDK_HOME="$ANDROID_NDK_HOME"
  export PATH="$TOOLCHAINS/bin:$PATH"

  # 显式指定工具，避免寻找 gcc
  export CC="$TOOLCHAINS/bin/$CC_BIN"
  export CXX="$TOOLCHAINS/bin/$CXX_BIN"
  export AR="$TOOLCHAINS/bin/llvm-ar"
  export RANLIB="$TOOLCHAINS/bin/llvm-ranlib"
  export STRIP="$TOOLCHAINS/bin/llvm-strip"
  export NM="$TOOLCHAINS/bin/llvm-nm"
  export LD="$TOOLCHAINS/bin/ld.lld"
  export AS="$CC"  # clang 充当汇编器
  export CFLAGS="-fPIC -O2"
  export LDFLAGS=""
  # OpenSSL 3 默认用 clang；-D__ANDROID_API__ 必须传
  perl ./Configure \
    "$TARGET" \
    -D__ANDROID_API__="$ANDROID_API" \
    --prefix="$INSTALL_DIR" \
    no-shared \
    no-tests \
    no-unit-test \
    -fPIC

  make -j"$JOBS"
  make install_sw

done

echo ">>> openssl 全部 ABI 静态库编译完成！"