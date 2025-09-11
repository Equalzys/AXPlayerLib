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
    Darwin*) HOST_OS=darwin-x86_64 ;;  # Apple Silicon 一般也在此目录
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
    # 不能加引号，否则通配符不展开
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

for ABI in "${TARGETS[@]}"; do
  echo ">>> [openssl-3.4.1] 正在编译 $ABI ..."

  SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
  INSTALL_DIR="$BUILD_BASE/$ABI"

  case "$ABI" in
    arm64-v8a)
      TARGET="android-arm64"
      HOST_TRIPLET="aarch64-linux-android"
      CC_BIN="${HOST_TRIPLET}${ANDROID_API}-clang"
      ;;
    armeabi-v7a)
      TARGET="android-arm"
      HOST_TRIPLET="armv7a-linux-androideabi"   # 注意 armv7a 前缀
      CC_BIN="${HOST_TRIPLET}${ANDROID_API}-clang"
      ;;
    *) echo "不支持的 ABI: $ABI"; exit 1 ;;
  esac

  rm -rf "$INSTALL_DIR"
  mkdir -p "$INSTALL_DIR"

  cd "$SRC_DIR"

  # 环境：让 Configure 使用 NDK/clang 工具链
  export ANDROID_NDK="$ANDROID_NDK_HOME"
  export PATH="$TOOLCHAINS/bin:$PATH"

  export CC="$TOOLCHAINS/bin/$CC_BIN"
  export AR="$TOOLCHAINS/bin/llvm-ar"
  export RANLIB="$TOOLCHAINS/bin/llvm-ranlib"
  export STRIP="$TOOLCHAINS/bin/llvm-strip"
  export NM="$TOOLCHAINS/bin/llvm-nm"
  export AS="$CC"   # clang 充当汇编器

  # 确保静态库可被装入 .so：-fPIC
  export CFLAGS="-fPIC -O2"
  export CXXFLAGS="-fPIC -O2"
  export LDFLAGS=""

  # OpenSSL 3.4.x 配置：
  # - no-shared：只产出 .a
  # - no-tests：不编单测
  # - -D__ANDROID_API__：显式指定 API level
  perl ./Configure \
    "$TARGET" \
    -D__ANDROID_API__="$ANDROID_API" \
    --prefix="$INSTALL_DIR" \
    no-shared \
    no-tests \
    -fPIC

  # 只编库，避免编 apps/openssl
  if make -n build_libs >/dev/null 2>&1; then
    make -j"$JOBS" build_libs
  else
    make -j"$JOBS"
  fi

  # 只安装开发所需（头文件 + 静态库）
  if make -n install_dev >/dev/null 2>&1; then
    make install_dev
  else
    make install_sw
  fi

  echo ">>> [$ABI] 输出目录: $INSTALL_DIR"
  echo ">>> [$ABI] 产物应包含: $INSTALL_DIR/lib/libcrypto.a 与 $INSTALL_DIR/lib/libssl.a"
done

echo ">>> OpenSSL 全部 ABI 静态库编译完成！"