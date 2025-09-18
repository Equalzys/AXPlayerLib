// 文件路径：AXPlayerLib/MediaCore/player/include/AXPlayer.h
#ifndef AXPLAYERLIB_AXPLAYER_H
#define AXPLAYERLIB_AXPLAYER_H
#pragma once

#include <string>
#include <map>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <jni.h>

// 前置声明，避免强依赖 NDK 头扫描
struct ANativeWindow;
struct AMediaExtractor;
struct AMediaCodec;
struct AMediaFormat;

class AXPlayerCallback {
public:
    virtual ~AXPlayerCallback() = default;
    virtual void onPrepared() = 0;
    virtual void onCompletion() = 0;
    virtual void onBuffering(int percent) = 0;
    virtual void onVideoSizeChanged(int w, int h, int sarNum, int sarDen) = 0;
    virtual void onError(int what, int extra, const std::string& msg) = 0;
};

class AXPlayer {
public:
    explicit AXPlayer(std::shared_ptr<AXPlayerCallback> cb);
    ~AXPlayer();

    // 数据源
    void setDataSource(const std::string& urlOrPath, const std::map<std::string,std::string>& headers);

    // 生命周期
    void prepareAsync();
    void start();
    void pause();
    void seekTo(int64_t msec); // 第1步先不实现
    bool isPlaying();

    // 属性/控制
    void setSpeed(float speed); // 第1步先不实现
    int64_t getCurrentPositionMs(); // 第1步先不实现（返回解码进度估计）
    int64_t getDurationMs();        // 第1步先不实现
    void setVolume(float left, float right); // 第1步先不实现
    int getVideoWidth();
    int getVideoHeight();
    int getVideoSarNum();
    int getVideoSarDen();
    int getAudioSessionId(); // 第1步先不实现（返回0）
    void setWindow(ANativeWindow* window); // 不持有引用，JNI 管理生命周期
    // ★ 新增：在 JNI_OnLoad 里把 JavaVM 传进来
    static void SetJavaVM(JavaVM* vm);
    static JavaVM* GetJavaVM();
private:
    enum class State { IDLE, PREPARING, PREPARED, PLAYING, PAUSED, COMPLETED, ERROR };

    // 线程
    void ioThreadLoop();   // 负责提取器与编解码初始化
    void playThreadLoop(); // 负责喂入/拉取解码数据（视频-only）

    // 内部
    void changeState(State s);
    void postError(int what, int extra, const std::string& msg);
    void resetCodec_l(bool releaseExtractor);
    bool setupExtractor_l();     // 解析媒体、选择视频轨
    bool setupVideoCodec_l();    // 配置视频硬解，绑定 window
    void drainDecoderLoop();     // 解码循环（在 playThread）

private:
    // 回调
    std::shared_ptr<AXPlayerCallback> cb_;

    // 状态
    std::atomic<State> state_{State::IDLE};
    std::atomic<bool> abort_{false};
    std::atomic<bool> playing_{false};

    // 源
    std::string source_;
    std::map<std::string,std::string> headers_;

    // 窗口
    ANativeWindow* window_ = nullptr; // 外部生命周期
    std::mutex wmtx_;

    // 线程
    std::thread ioThread_;
    std::thread playThread_;

    // 同步
    std::mutex mtx_;
    std::condition_variable cv_;

    // 媒体信息
    int videoW_ = 0, videoH_ = 0;
    int sarNum_ = 1, sarDen_ = 1;
    int64_t durationMs_ = 0;
    std::atomic<int64_t> positionMs_{0};
    float speed_ = 1.0f;
    float volL_ = 1.0f, volR_ = 1.0f;
    int audioSessionId_ = 0;

    // NDK 媒体组件
    AMediaExtractor* extractor_ = nullptr;
    AMediaCodec* vcodec_ = nullptr;
    int videoTrackIndex_ = -1;

    // 控制
    std::atomic<bool> preparedNotified_{false};

    static JavaVM* sVm;
};

#endif //AXPLAYERLIB_AXPLAYER_H