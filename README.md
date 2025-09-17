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


#### 其他可能用到的命令：
删除当前目录下的所有子目录及其内容
删除前可以先看看要删除的目录
find . -mindepth 1 -maxdepth 1 -type d -exec ls -d {} \;
删除
find . -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
 











