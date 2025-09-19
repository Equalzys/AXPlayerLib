cmake_minimum_required(VERSION 3.22)
project(AXPlayerCore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ===================== 目录结构 =====================
set(AX_MEDIA_CORE_DIR ${CMAKE_CURRENT_LIST_DIR})
set(AX_PLAYER_DIR     ${AX_MEDIA_CORE_DIR}/player)
set(AX_SOUNDTOUCH_DIR ${AX_MEDIA_CORE_DIR}/soundtouch)
set(AX_LIBYUV_DIR     ${AX_MEDIA_CORE_DIR}/libyuv)
set(AX_JNI_DIR        ${AX_MEDIA_CORE_DIR}/player/jni)
set(AX_OBOE_DIR       ${AX_MEDIA_CORE_DIR}/oboe)   # ★ Oboe 仅放在 MediaCore/oboe

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

# ===================== Oboe 开关（可选） =====================
option(AX_WITH_OBOE "Enable Oboe audio backend if MediaCore/oboe exists" ON)
set(OBOE_TARGET "")   # 将在后面探测并设置为 oboe 或 oboe::oboe

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
)
# target_compile_definitions(AXPlayer PRIVATE ST_NO_EXCEPTION)

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

# ===================== 可选导入 Oboe（仅当存在 MediaCore/oboe 且开关为 ON） =====================
if (AX_WITH_OBOE)
    if (EXISTS "${AX_OBOE_DIR}/CMakeLists.txt")
        # 使用独立的构建目录，避免污染主二进制输出
        add_subdirectory("${AX_OBOE_DIR}" "${CMAKE_BINARY_DIR}/oboe_build" EXCLUDE_FROM_ALL)
        # Oboe 1.10.0 通常导出 target 'oboe'，也可能同时提供别名 'oboe::oboe'
        if (TARGET oboe)
            set(OBOE_TARGET oboe)
        elseif (TARGET oboe::oboe)
            set(OBOE_TARGET oboe::oboe)
        endif ()
        if (OBOE_TARGET)
            target_link_libraries(AXPlayer ${OBOE_TARGET})
            target_compile_definitions(AXPlayer PRIVATE AX_WITH_OBOE=1)
            message(STATUS "Oboe found and linked: target=${OBOE_TARGET}")
        else ()
            message(WARNING "Oboe subdirectory added but target not found; continue without Oboe link.")
        endif ()
    else ()
        message(STATUS "Oboe not present at ${AX_OBOE_DIR}; skip Oboe integration.")
    endif ()
else ()
    message(STATUS "AX_WITH_OBOE=OFF; skip Oboe integration.")
endif ()

# ===================== 系统库链接 =====================
# 建议显式 find_library，避免某些 NDK 版本名字大小写差异
find_package(Threads REQUIRED)

find_library(log-lib      log)
find_library(android-lib  android)
find_library(egl-lib      EGL)
find_library(glesv3-lib   GLESv3)
find_library(jnigfx-lib   jnigraphics)
find_library(mediandk-lib mediandk)
find_library(dl-lib       dl)

# 仅在 armeabi-v7a 下兜底链接 libatomic；arm64-v8a / 其他 ABI 不需要
set(AXPLAYER_EXTRA_LIBS "")
if (ANDROID_ABI STREQUAL "armeabi-v7a")
    find_library(atomic-lib atomic)
    if (atomic-lib)
        list(APPEND AXPLAYER_EXTRA_LIBS ${atomic-lib})
    endif ()
endif ()

target_link_libraries(AXPlayer
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        ${jnigfx-lib}
        ${mediandk-lib}
        ${dl-lib}
        ${AXPLAYER_EXTRA_LIBS}
        Threads::Threads
)

# 更紧凑的 SO：丢弃未用符号 & 隐藏静态库内部符号
set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections -Wl,--exclude-libs,ALL")