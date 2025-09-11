#!/bin/bash
set -euo pipefail

# ===================== 基本配置 =====================
ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$AXPLAYER_ROOT/extra/libiconv"
BUILD_BASE="$AXPLAYER_ROOT/android/build/libiconv"
ARCHS=("arm64-v8a" "armeabi-v7a")

usage() {
  echo "用法: $0 [clean|arm64|armv7a|all]"
  exit 1
}
[ $# -eq 1 ] || usage

MODE="$1"
case "$MODE" in
  clean)   TARGETS=("${ARCHS[@]}") ;;
  arm64)   TARGETS=("arm64-v8a") ;;
  armv7a)  TARGETS=("armeabi-v7a") ;;
  all)     TARGETS=("${ARCHS[@]}") ;;
  *)       usage ;;
esac

# ===================== 读取 NDK / Toolchain =====================
NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
[ -f "$NDK_PROP_FILE" ] || { echo "!!! 未找到 $NDK_PROP_FILE"; exit 1; }

ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
[ -d "$ANDROID_NDK_HOME" ] || { echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"; exit 1; }

TOOLCHAIN=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n' || true)
if [ -z "${TOOLCHAIN:-}" ] || [ ! -d "$TOOLCHAIN" ]; then
  case "$(uname -s)" in
    Linux*)  HOST_OS=linux-x86_64 ;;
    Darwin*) HOST_OS=darwin-x86_64 ;;
    *) echo "!!! 不支持的主机系统：$(uname -s)"; exit 1 ;;
  esac
  TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_OS"
fi
[ -d "$TOOLCHAIN" ] || { echo "!!! TOOLCHAIN ($TOOLCHAIN) 路径无效"; exit 1; }

# ===================== 并发数 =====================
if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
else
  JOBS=$(sysctl -n hw.ncpu)
fi

# ===================== 清理模式 =====================
if [ "$MODE" = "clean" ]; then
  echo ">>> 清理 libiconv 各 ABI 目录 ..."
  for ABI in "${TARGETS[@]}"; do
    rm -rf "$SRC_DIR/build_$ABI" "$BUILD_BASE/$ABI"
  done
  echo ">>> 清理完成！"
  exit 0
fi

# ===================== 辅助：生成 .pc =====================
gen_pc_files() {
  local install_dir="$1"
  local build_dir="$2"

  local pc_dir="$install_dir/lib/pkgconfig"
  mkdir -p "$pc_dir"

  # 从 config.status 抓取版本号；抓不到则 unknown
  local ver
  ver="$(sed -n 's/^S\["PACKAGE_VERSION"\]=\"\(.*\)\"/\1/p' "$build_dir/config.status" 2>/dev/null | head -n1 || true)"
  [ -z "$ver" ] && ver="unknown"

  cat > "$pc_dir/libiconv.pc" <<EOF
prefix=$install_dir
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: libiconv
Description: Character set conversion library
Version: $ver
Cflags: -I\${includedir}
Libs: -L\${libdir} -liconv
Libs.private: -lcharset
EOF

  cat > "$pc_dir/libcharset.pc" <<EOF
prefix=$install_dir
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include

Name: libcharset
Description: Portable character set determination library
Version: $ver
Cflags: -I\${includedir}
Libs: -L\${libdir} -lcharset
EOF

  echo ">>> 生成 pkg-config: $pc_dir/libiconv.pc, $pc_dir/libcharset.pc"
}

# ===================== 主循环：逐 ABI 构建 =====================
for ABI in "${TARGETS[@]}"; do
  echo ">>> [libiconv] 正在编译 $ABI ..."

  BUILD_DIR="$SRC_DIR/build_$ABI"
  INSTALL_DIR="$BUILD_BASE/$ABI"
  rm -rf "$BUILD_DIR" && mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

  case "$ABI" in
    arm64-v8a)
      HOST_TRIPLET="aarch64-linux-android"
      CC_BIN="${HOST_TRIPLET}${ANDROID_API}-clang"
      ;;
    armeabi-v7a)
      HOST_TRIPLET="armv7a-linux-androideabi"   # 与 NDK 三元组一致
      CC_BIN="${HOST_TRIPLET}${ANDROID_API}-clang"
      ;;
    *) echo "不支持的 ABI: $ABI"; exit 1 ;;
  esac

  export PATH="$TOOLCHAIN/bin:$PATH"
  export CC="$TOOLCHAIN/bin/$CC_BIN"
  export AR="$TOOLCHAIN/bin/llvm-ar"
  export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
  export STRIP="$TOOLCHAIN/bin/llvm-strip"
  export LD="$TOOLCHAIN/bin/ld.lld"

  # -fPIC 必须，后续要装入单一 so
  export CFLAGS="-O2 -fPIC"
  export CPPFLAGS="-O2 -fPIC"

  # 重新配置（源目录清理）
  cd "$SRC_DIR"
  [ -f Makefile ] && make distclean || true

  mkdir -p "$BUILD_DIR"
  cd "$BUILD_DIR"

  # 构建三元组（尽量准确）
  BUILD_TRIPLET="$("$SRC_DIR/build-aux/config.guess" 2>/dev/null || "$SRC_DIR/config.guess" 2>/dev/null || echo x86_64-pc-linux-gnu)"

  "$SRC_DIR/configure" \
    --build="$BUILD_TRIPLET" \
    --host="$HOST_TRIPLET" \
    --prefix="$INSTALL_DIR" \
    --enable-static \
    --disable-shared \
    --with-pic \
    CC="$CC" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP"

  make -j"$JOBS"
  make install

  # 生成 pkg-config
  gen_pc_files "$INSTALL_DIR" "$BUILD_DIR"

  # 简要校验
  ls -l "$INSTALL_DIR/lib"/libiconv.a "$INSTALL_DIR/lib"/libcharset.a >/dev/null
  echo ">>> [$ABI] 输出: $INSTALL_DIR/lib/libiconv.a, $INSTALL_DIR/lib/libcharset.a"
done

echo ">>> libiconv 全部 ABI 静态库编译完成！"