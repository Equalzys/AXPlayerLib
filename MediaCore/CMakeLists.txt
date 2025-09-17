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
get_filename_component(PROJ_ROOT ${AX_MEDIA_CORE_DIR} DIRECTORY)     # AXPlayerLib/
if (NOT DEFINED AXFCORE_BASE)
    set(AXFCORE_BASE "${PROJ_ROOT}/android/build/ffmpeg")
endif ()

# ★ 将相对路径转为“以工程根”为基准的绝对路径（关键）
if (NOT IS_ABSOLUTE "${AXFCORE_BASE}")
    get_filename_component(AXFCORE_BASE "${AXFCORE_BASE}" ABSOLUTE BASE_DIR "${PROJ_ROOT}")
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

add_library(AXPlayer SHARED
        ${AXPLAYER_SRC}
        ${AXPLAYER_JNI_SRC}
        ${SOUNDTOUCH_SRC}
)

target_compile_features(AXPlayer PRIVATE cxx_std_11)

target_include_directories(AXPlayer
        PUBLIC
        ${AX_PLAYER_DIR}/include
        ${AX_LIBYUV_DIR}/include
        ${AX_SOUNDTOUCH_DIR}/include
        ${AX_SOUNDTOUCH_DIR}/source
        # AXFCore 的头文件目录仅在存在时追加（见下方）
)

# ===================== 编译选项 =====================
target_compile_options(AXPlayer PRIVATE
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        -fno-exceptions
        -fno-rtti
)
#target_compile_definitions(AXPlayer PRIVATE ST_NO_EXCEPTION)

option(WITH_SOUNDTOUCH_SIMD "Enable SoundTouch SIMD (NEON/SSE) if available" OFF)
if (NOT WITH_SOUNDTOUCH_SIMD)
    target_compile_definitions(AXPlayer PRIVATE SOUNDTOUCH_DISABLE_SIMD)
endif ()

# ===================== 导入 AXFCore（存在才链接） =====================
# FIX: 自动探测 libAXFCore.so：存在→导入并链接；不存在→跳过，先跑起来
set(AXFCORE_SO "${AXFCORE_BASE}/${ANDROID_ABI}/libAXFCore.so")
if (EXISTS "${AXFCORE_SO}")
    add_library(axfcore SHARED IMPORTED)
    set_target_properties(axfcore PROPERTIES IMPORTED_LOCATION "${AXFCORE_SO}")
    target_include_directories(AXPlayer PUBLIC "${AXFCORE_BASE}/${ANDROID_ABI}/include")
    target_link_libraries(AXPlayer axfcore)
    message(STATUS "Linking with AXFCore: ${AXFCORE_SO}")
else ()
    message(WARNING "AXFCore not found at ${AXFCORE_SO}; build continues WITHOUT it.")
endif ()



# ===================== 系统库链接 =====================
# 建议显式 find_library，避免某些 NDK 版本名字大小写差异
find_library(log-lib      log)
find_library(android-lib  android)
find_library(egl-lib      EGL)
find_library(glesv3-lib   GLESv3)
find_library(jnigfx-lib   jnigraphics)
find_library(mediandk-lib mediandk)

set_source_files_properties(${SOUNDTOUCH_SRC}
        PROPERTIES COMPILE_OPTIONS "-fexceptions"
)

target_link_libraries(AXPlayer
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        ${jnigfx-lib}
        ${mediandk-lib}
)

set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--exclude-libs,ALL")