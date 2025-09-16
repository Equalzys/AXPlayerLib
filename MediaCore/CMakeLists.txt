cmake_minimum_required(VERSION 3.22)
project(AXPlayerCore)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===================== 目录结构 =====================
set(AX_MEDIA_CORE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(AX_PLAYER_DIR ${AX_MEDIA_CORE_DIR}/player)
set(AX_SOUNDTOUCH_DIR ${AX_MEDIA_CORE_DIR}/soundtouch)
set(AX_LIBYUV_DIR ${AX_MEDIA_CORE_DIR}/libyuv)
set(AX_JNI_DIR ${AX_MEDIA_CORE_DIR}/player/jni)

# ===================== 外部参数 =====================
if (NOT DEFINED AXFCORE_BASE)
    set(AXFCORE_BASE "${CMAKE_SOURCE_DIR}/../android/build/ffmpeg")
endif ()

message(STATUS "ANDROID_ABI=${ANDROID_ABI}")
message(STATUS "AXFCORE_BASE=${AXFCORE_BASE}")

# ===================== 源文件收集 =====================
file(GLOB AXPLAYER_SRC
        ${AX_PLAYER_DIR}/core/*.cpp
        ${AX_PLAYER_DIR}/core/gl/*.cpp
        )
file(GLOB AXPLAYER_JNI_SRC
        ${AX_JNI_DIR}/*.cpp
        )
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

# ✅ 显式声明此目标使用 C++11（让 IDE/索引器不再误判）
target_compile_features(axplayer PRIVATE cxx_std_11)

#  用 target_include_directories 绑定到 axplayer（PUBLIC）
target_include_directories(axplayer
        PUBLIC
        ${AX_PLAYER_DIR}/include            # 这里包含 AXPlayer.h
        ${AX_LIBYUV_DIR}/include
        ${AX_SOUNDTOUCH_DIR}/include
        ${AX_SOUNDTOUCH_DIR}/source
        ${AXFCORE_BASE}/${ANDROID_ABI}/include
        )

# ===================== 编译选项 =====================
target_compile_options(axplayer PRIVATE
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        -fno-exceptions
        -fno-rtti
        )

option(WITH_SOUNDTOUCH_SIMD "Enable SoundTouch SIMD (NEON/SSE) if available" OFF)
if (NOT WITH_SOUNDTOUCH_SIMD)
    target_compile_definitions(axplayer PRIVATE SOUNDTOUCH_DISABLE_SIMD)
endif ()

# ===================== 导入 AXFCore =====================
add_library(axfcore SHARED IMPORTED)
if (${ANDROID_ABI} STREQUAL "arm64-v8a")
    set_target_properties(axfcore PROPERTIES IMPORTED_LOCATION
            "${AXFCORE_BASE}/arm64-v8a/libAXFCore.so")
elseif (${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set_target_properties(axfcore PROPERTIES IMPORTED_LOCATION
            "${AXFCORE_BASE}/armeabi-v7a/libAXFCore.so")
else ()
    message(FATAL_ERROR "Unsupported ABI: ${ANDROID_ABI}")
endif ()

# ===================== 系统库链接 =====================
# 如你当前能正常链接，可继续直接写库名；若 IDE 仍飘红，可改为 find_library 显式查找
target_link_libraries(axplayer
        axfcore
        log
        android
        EGL
        GLESv3
        jnigraphics
        )

set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--exclude-libs,ALL")