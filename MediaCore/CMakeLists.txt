cmake_minimum_required(VERSION 3.22)
project(AXPlayerCore)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===================== 目录结构 =====================
set(AX_MEDIA_CORE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(AX_PLAYER_DIR     ${AX_MEDIA_CORE_DIR}/player)
set(AX_SOUNDTOUCH_DIR ${AX_MEDIA_CORE_DIR}/soundtouch)
set(AX_JNI_DIR        ${AX_MEDIA_CORE_DIR}/jni)

# ===================== 外部参数 =====================
# AXFCORE_BASE 指向 FFmpeg 安装产物（含 include/ 与 libAXFCore.so）
# 默认为 ../android/build/ffmpeg
if(NOT DEFINED AXFCORE_BASE)
    set(AXFCORE_BASE "${CMAKE_SOURCE_DIR}/../android/build/ffmpeg")
endif()

message(STATUS "ANDROID_ABI=${ANDROID_ABI}")
message(STATUS "AXFCORE_BASE=${AXFCORE_BASE}")

# ===================== 头文件路径 =====================
# 仅引入必要头：播放器自身 + SoundTouch + FFmpeg 头
include_directories(
        ${AX_PLAYER_DIR}/include
        ${AX_SOUNDTOUCH_DIR}/include
        ${AX_SOUNDTOUCH_DIR}/source
        ${AXFCORE_BASE}/${ANDROID_ABI}/include        # FFmpeg 头 (libavcodec/libavformat/libavutil/libswresample/libswscale)
)

# ===================== 源文件收集 =====================
# 播放器核心
file(GLOB AXPLAYER_SRC
        ${AX_PLAYER_DIR}/src/*.cpp
        ${AX_PLAYER_DIR}/src/gl/*.cpp
        )

# JNI 桥
file(GLOB AXPLAYER_JNI_SRC
        ${AX_JNI_DIR}/*.cpp
        )

# SoundTouch 核心（通用实现；不引入 x86 SSE 文件）
# 如果你的 soundtouch 目录结构不同，请按实际调整下面路径
file(GLOB SOUNDTOUCH_SRC
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/AAFilter.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/BPMDetect.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/FIFOSampleBuffer.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/FIRFilter.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/InterpolateCubic.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/InterpolateLinear.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/InterpolateShannon.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/PeakFinder.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/RateTransposer.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/SoundTouch.cpp
        ${AX_SOUNDTOUCH_DIR}/source/SoundTouch/TDStretch.cpp
        )

add_library(axplayer SHARED
        ${AXPLAYER_SRC}
        ${AXPLAYER_JNI_SRC}
        ${SOUNDTOUCH_SRC}
        )

# ===================== 编译选项（体积/可见性/兼容性） =====================
# 更小体积与更少符号暴露；必要时可按模块调优
target_compile_options(axplayer PRIVATE
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        -fno-exceptions
        -fno-rtti
        )

# SoundTouch SIMD 开关（默认禁用，确保所有 ABI 稳定可编）
option(WITH_SOUNDTOUCH_SIMD "Enable SoundTouch SIMD (NEON/SSE) if available" OFF)
if(NOT WITH_SOUNDTOUCH_SIMD)
    target_compile_definitions(axplayer PRIVATE SOUNDTOUCH_DISABLE_SIMD)
endif()

# ===================== 导入 libAXFCore.so 并链接 =====================
# 仅需链接 axfcore，不需要把 libmp3lame/libass/harfbuzz/dav1d 等头文件加入本层
add_library(axfcore SHARED IMPORTED)

if(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set_target_properties(axfcore PROPERTIES IMPORTED_LOCATION
            "${AXFCORE_BASE}/arm64-v8a/libAXFCore.so")
elseif(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set_target_properties(axfcore PROPERTIES IMPORTED_LOCATION
            "${AXFCORE_BASE}/armeabi-v7a/libAXFCore.so")
else()
    message(FATAL_ERROR "Unsupported ABI: ${ANDROID_ABI}")
endif()

# ===================== 系统库链接 =====================
target_link_libraries(axplayer
        axfcore
        log
        android
        EGL
        GLESv3
        jnigraphics
        )

# 链接器优化
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--exclude-libs,ALL")

# ===================== 额外提示 =====================
# 1) 如需启用 SoundTouch NEON，可在外层传入 -DWITH_SOUNDTOUCH_SIMD=ON，
#    并根据官方实现添加 NEON 源（如果你的 soundtouch 版本带有 arm/neon 优化文件）。
# 2) 如果 FFmpeg 的头不在 ${AXFCORE_BASE}/${ANDROID_ABI}/include，
#    可在 Gradle 的 externalNativeBuild 里传入 -DAXFCORE_BASE=/your/path。