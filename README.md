# AXPlayerLib

## 以下为Linux环境编译流程
## 安装编译工具、NDK、SDK
### 安装必要工具

sudo apt install unzip gperf gettext autopoint cmake autoconf automake libtool meson patchelf binutils

### 安装JDK
sudo add-apt-repository ppa:openjdk-r/ppa
sudo apt update
sudo apt install openjdk-17-jdk

### 安装SDK
wget https://mirrors.cloud.tencent.com/AndroidSDK/commandlinetools-linux-13114758_latest.zip

unzip commandlinetools-linux-13114758_latest.zip

cd cmdline-tools

mkdir latest
shopt -s extglob
mv !(latest) latest/

vim ~/.bashrc

输入"i"
bashrc环境变量中添加以下内容：
export ANDROID_HOME=~/SDK
export PATH=$PATH:$ANDROID_HOME/cmdline-tools/latest/bin
export PATH=$PATH:$ANDROID_HOME/platform-tools

esc退出输入模式，保存退出":wq"
source ~/.bashrc

sdkmanager "platforms;android-35" "platform-tools" "build-tools;35.0.1"


### 安装NDK
wget https://googledownloads.cn/android/repository/android-ndk-r28c-linux.zip

unzip android-ndk-r28c-linux.zip

vim ~/.bashrc

输入"i"
bashrc环境变量中添加以下内容：
export ANDROID_NDK=~/NDK/android-ndk-r28c
export ANDROID_NDK_HOME=$ANDROID_NDK
export NDK_HOME=$ANDROID_NDK

esc退出输入模式，保存退出":wq"
source ~/.bashrc

### 源码

源码clone到本地后

cd AXPlayerLib

touch local.properties

vim local.properties

输入以下内容为自己环境SDK和NDK路径：
sdk.dir=/root/SDK
ndk.dir=/root/NDK/android-ndk-r28c
ndk_toolchains=/root/NDK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64


### 编译
./init-android.sh

cd android

1. 执行：
./build_all_static_libs.sh all
最后执行：
./build_ffmpeg.sh all

2. 或一个一个编译：
./build_zlib.sh all
./build_libunibreak.sh all
./build_freetype2.sh all
./build_iconv.sh all
./build_libsoxr.sh all
./build_harfbuzz.sh all
./build_fribidi.sh all
./build_libass.sh all
./build_SoundTouch.sh all
./build_openssl.sh all
./build_x264.sh all
./build_libmp3lame.sh all
./build_dav1d.sh all
最后执行：
./build_ffmpeg.sh all



# 播放器整体方案设计

## 流程设计
1.	解封装 (Demux)
•	基于 FFmpeg，支持本地文件、HTTP(S)、HLS、直播流。
•	分离音视频/字幕轨道，支持多轨选择与切换。
2.	解码 (Decode)
•	视频：
•	优先使用 MediaCodec 硬解，零拷贝输出到 Surface (OES)。
•	不支持或异常时回退 FFmpeg 软解，输出 YUV420 等。
•	音频：
•	使用 FFmpeg 解码（AAC/MP3/Opus/FLAC…）。
•	经 libswresample/libsoxr 转换为统一格式。
3.	渲染/播放 (Render/Play)
•	视频：基于 OpenGL ES 3.1 渲染；支持滤镜链、HDR→SDR 色调映射、截图。
•	音频：基于 OpenSL ES（API23+ 通用），可选 AAudio/Oboe（API26+ 低延迟）。
4.	同步 (Sync)
•	默认 音频时钟为主；视频帧对齐到音频时钟，早到 sleep，晚到丢帧/追帧。
•	无音轨场景：以 外部单调时钟为主，视频按 PTS 调度。
•	支持倍速播放（视频按调度，音频经 SoundTouch 变速不变调）。

⸻

## 功能支持
•	音乐/视频播放（本地/网络/直播）
•	多音轨/多字幕轨道选择
•	倍速播放（0.25–4.0x）
•	硬/软解切换
•	HDR 视频播放与色彩管理
•	截图、缓冲进度回调、播放状态回调
•	视频列表预加载、多实例播放
•	字幕渲染（文本/图片字幕，基于 libass）

⸻

## 第三方依赖
•	FFmpeg 8.0：解封装、解码、重采样
•	MediaCodec (NDK)：硬件视频解码
•	OpenGL ES 3.1：视频渲染、滤镜处理
•	OpenSL ES / AAudio / Oboe：音频播放
•	libyuv：YUV 转换/缩放/旋转
•	SoundTouch：变速不变调
•	libass + freetype2 + harfbuzz + fribidi + libunibreak：字幕渲染
•	openssl / zlib / libiconv：网络安全与辅助
•	dav1d：高性能 AV1 软解（可选）
•	x264 / libmp3lame：编码扩展（录制/转码用，可选）

⸻

## 设计原则
•	硬解优先，软解兜底
•	音频时钟驱动同步，无音轨则用外部时钟
•	零拷贝/最少拷贝，降低带宽与功耗
•	模块化设计：解封装、解码、渲染、同步、字幕可独立演进
•	跨 Android 6–15，支持 arm64-v8a 与 armeabi-v7a


#### 其他可能用到的命令：
删除当前目录下的所有子目录及其内容
删除前可以先看看要删除的目录
find . -mindepth 1 -maxdepth 1 -type d -exec ls -d {} \;
删除
find . -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
 











