#!/bin/bash
set -euo pipefail

ANDROID_API=23
AXPLAYER_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC_BASENAME="SoundTouch"
BUILD_BASE="$AXPLAYER_ROOT/android/build/SoundTouch"
SRC_BASE="$AXPLAYER_ROOT/android"

ARCHS=("arm64-v8a" "armeabi-v7a")
NDK_PROP_FILE="$AXPLAYER_ROOT/local.properties"
JOBS=

usage() {
  echo "用法: $0 [clean|arm64|armv7a|all]"
  exit 1
}
[ $# -eq 0 ] && usage

case "$1" in
  clean)
    echo ">>> 清理 SoundTouch 各 ABI 目录 ..."
    for ABI in "${ARCHS[@]}"; do
      SRC="$SRC_BASE/${SRC_BASENAME}-${ABI}"
      if [ -f "$SRC/Makefile" ]; then
        (cd "$SRC" && make clean 2>/dev/null || true)
        (cd "$SRC" && make distclean 2>/dev/null || true)
      fi
      # 注意：不要加引号，否则通配符不展开
      rm -rf $SRC/*.o $SRC/*.so $SRC/*.lo $SRC/*.la 2>/dev/null || true
      rm -rf "$SRC/build" "$BUILD_BASE/$ABI"
    done
    echo ">>> 清理完成！"
    exit 0
    ;;
  arm64)   TARGETS=("arm64-v8a") ;;
  armv7a)  TARGETS=("armeabi-v7a") ;;
  all)     TARGETS=("${ARCHS[@]}") ;;
  *)       usage ;;
esac

[ -f "$NDK_PROP_FILE" ] || { echo "!!! 未找到 $NDK_PROP_FILE"; exit 1; }

# 读取 NDK & 预编译工具链（TOOLCHAIN 仅用于校验；实际走官方 CMake toolchain）
ANDROID_NDK_HOME=$(grep '^ndk.dir=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
TOOLCHAIN=$(grep '^ndk_toolchains=' "$NDK_PROP_FILE" | cut -d'=' -f2- | tr -d '\r\n')
[ -d "$ANDROID_NDK_HOME" ] || { echo "!!! ANDROID_NDK_HOME ($ANDROID_NDK_HOME) 路径无效"; exit 1; }
[ -d "$TOOLCHAIN" ] || { echo "!!! TOOLCHAIN ($TOOLCHAIN) 路径无效"; exit 1; }

# 并发
if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
else
  JOBS=$(sysctl -n hw.ncpu)
fi

# 统一编译选项：强制 PIC，禁 PIE，隐藏可见性（减少可抢占符号）
PIC_DEFS="-fPIC -fvisibility=hidden -fno-pie -fno-PIE"
ASM_PIC="-fPIC -DPIC"

for ABI in "${TARGETS[@]}"; do
  echo ">>> [SoundTouch] 正在编译 $ABI ..."
  SRC_DIR="$SRC_BASE/${SRC_BASENAME}-${ABI}"
  BUILD_DIR="$SRC_DIR/build"
  INSTALL_DIR="$BUILD_BASE/$ABI"

  rm -rf "$BUILD_DIR"
  mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
  cd "$BUILD_DIR"

  cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-${ANDROID_API} \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_C_FLAGS_RELEASE="$PIC_DEFS" \
    -DCMAKE_CXX_FLAGS_RELEASE="$PIC_DEFS" \
    -DCMAKE_ASM_FLAGS="$ASM_PIC" \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--max-page-size=16384"

  make -j"$JOBS"
  make install

  # 小提示：产物通常在 $INSTALL_DIR/lib 下，例如 libSoundTouch.a
  echo ">>> [$ABI] 安装到: $INSTALL_DIR"
done

echo ">>> SoundTouch 全部 ABI 静态库（-fPIC）编译完成！"