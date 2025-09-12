cmake_minimum_required(VERSION 3.22)
project(AXPlayerCore)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== 项目结构 =====
set(AX_MEDIA_CORE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(AX_PLAYER_DIR     ${AX_MEDIA_CORE_DIR}/player)
set(AX_SOUNDTOUCH_DIR ${AX_MEDIA_CORE_DIR}/soundtouch)

# ===== 头文件路径 =====
include_directories(
        ${AX_PLAYER_DIR}/include
        ${AX_SOUNDTOUCH_DIR}/include        # SoundTouch 官方 include（通常为 include/SoundTouch.h）
        ${AX_SOUNDTOUCH_DIR}/source         # 有的版本头在 source 里也会引用
)

# ===== 核心播放器源码 =====
file(GLOB AXPLAYER_SRC
        ${AX_PLAYER_DIR}/src/*.cpp
        ${AX_PLAYER_DIR}/src/gl/*.cpp
        ${AX_MEDIA_CORE_DIR}/jni/*.cpp
        )

# ===== SoundTouch 源码收集（核心算法文件）=====
# 典型 SoundTouch 目录结构：include/  与  source/SoundTouch/*.cpp
# 如果你的版本路径不同，请按实际调整。
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

# 避免把 x86 SSE 文件编进 Android
# add_compile_definitions(SOUNDTOUCH_DISABLE_SIMD)  # 也可用这个宏来禁用 SIMD

add_library(axplayer SHARED
        ${AXPLAYER_SRC}
        ${SOUNDTOUCH_SRC}
        )

# ===== 预编译 libAXFCore.so =====
if(NOT DEFINED AXFCORE_BASE)
    set(AXFCORE_BASE "${CMAKE_SOURCE_DIR}/../android/build/ffmpeg")
endif()
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

# ===== 链接 & 宏 =====
target_compile_definitions(axplayer PRIVATE
        #SOUNDTOUCH_DISABLE_SIMD
        #SOUNDTOUCH_FLOAT_SAMPLES=1   # 如需强制使用 float 采样，可启用
        )

target_link_libraries(axplayer
        axfcore
        log
        android
        EGL
        GLESv3
        jnigraphics
        )

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections")