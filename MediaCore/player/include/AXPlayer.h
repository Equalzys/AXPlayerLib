// 文件路径：AXPlayerLib/MediaCore/player/include/AXPlayer.h
#ifndef AXPLAYERLIB_AXPLAYER_H
#define AXPLAYERLIB_AXPLAYER_H

#pragma once
#include <string>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <memory>

// 用前置声明替代 <android/native_window.h>，避免 IDE 对 NDK 头的索引依赖
struct ANativeWindow;

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

    void setDataSource(const std::string& urlOrPath,
                       const std::map<std::string,std::string>& headers);
    void prepareAsync();
    void start();
    void pause();
    void seekTo(int64_t msec);
    bool isPlaying();

    void setSpeed(float speed);
    int64_t getCurrentPositionMs();
    int64_t getDurationMs();
    void setVolume(float left, float right);

    int getVideoWidth();
    int getVideoHeight();
    int getVideoSarNum();
    int getVideoSarDen();
    int getAudioSessionId();

    void setWindow(ANativeWindow* window); // 不持有引用，JNI 层管理

private:
    enum class State {
        IDLE,
        STOPPED,
        PREPARING,
        PREPARED,
        PLAYING,
        PAUSED,
        COMPLETED,
        ERROR
    };

    void ioThreadLoop();    // 负责读帧/解复用
    void playThreadLoop();  // 负责解码/渲染/音频写入
    void changeState(State s);

    // ---- 内部数据 ----
    std::shared_ptr<AXPlayerCallback> cb_;
    std::atomic<State> state_{State::IDLE};

    std::string source_;
    std::map<std::string,std::string> headers_;

    ANativeWindow* window_ = nullptr; // 外部管理生命周期
    std::mutex wmtx_;

    std::thread ioThread_;
    std::thread playThread_;
    std::atomic<bool> abort_{false};

    std::mutex ctrlMtx_;
    std::condition_variable ctrlCv_;

    // 占位的媒体信息（后续对齐 FFmpeg）
    int videoW_ = 0, videoH_ = 0;
    int sarNum_ = 1, sarDen_ = 1;
    int64_t durationMs_ = 0;
    std::atomic<int64_t> positionMs_{0};
    std::atomic<bool> playing_{false};

    float speed_ = 1.0f;
    float volL_ = 1.0f, volR_ = 1.0f;
    int audioSessionId_ = 0; // AudioTrack 创建后返回
};

#endif //AXPLAYERLIB_AXPLAYER_H