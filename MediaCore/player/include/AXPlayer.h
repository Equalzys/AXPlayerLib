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
#include <android/native_window.h>
#include <jni.h>
#include "AXQueues.h"

#define AX_LOG_TAG "AXPlayer"
#include "AXLog.h"

extern "C" {
#include <libavutil/rational.h>
}

class AXDemuxer;
class AXDecoder;
class AXVideoRenderer;
class AXAudioRenderer;
class AXClock;

// 上层回调接口（与 AXMediaPlayer.java 对应）
class AXPlayerCallback {
public:
    virtual ~AXPlayerCallback() = default;
    virtual void onPrepared() = 0;
    virtual void onCompletion() = 0;
    virtual void onBuffering(int percent) = 0;
    virtual void onVideoSizeChanged(int w, int h, int sarNum, int sarDen) = 0;
    virtual void onError(int what, int extra, const std::string &msg) = 0;
};

class AXPlayer {
public:
    explicit AXPlayer(std::shared_ptr<AXPlayerCallback> cb);
    ~AXPlayer();

    // 数据源 & 控制
    void setDataSource(const std::string &urlOrPath, const std::map<std::string, std::string> &headers);
    void prepareAsync();
    void start();
    void pause();
    void seekTo(int64_t msec);
    bool isPlaying();
    void setSpeed(float speed);

    // 查询
    int64_t getCurrentPositionMs();
    int64_t getDurationMs();
    void setVolume(float left, float right);
    int getVideoWidth();
    int getVideoHeight();
    int getVideoSarNum();
    int getVideoSarDen();
    int getAudioSessionId();
    void setWindow(ANativeWindow *window);

    // JavaVM 设置（JNI_OnLoad 中调用）
    static void SetJavaVM(JavaVM *vm);
    static JavaVM *GetJavaVM();

private:
    enum class State { IDLE, STOPPED, PREPARING, PREPARED, PLAYING, PAUSED, COMPLETED, ERROR };

    void ioThreadLoop();   // 打开输入、启动 demuxer、创建 decoders
    void playThreadLoop(); // 渲染驱动与时钟同步
    void changeState(State s);
    void notifyError(int what, int extra, const std::string &msg);
    void stopPipelines_();//有序关闭 demux/decoder/队列

private:
    std::shared_ptr<AXPlayerCallback> cb_;
    std::atomic<State> state_{State::IDLE};

    std::string source_;
    std::map<std::string, std::string> headers_;

    // 线程 & 控制
    std::thread ioThread_;
    std::thread playThread_;
    std::atomic<bool> abort_{false};
    std::atomic<bool> playing_{false};

    // 准备完成同步
    std::atomic<bool> prepared_{false};
    std::mutex prepMtx_;
    std::condition_variable cvReady_;

    std::mutex wmtx_; // 保护 window

    // 媒体信息
    int videoW_{0}, videoH_{0};
    int sarNum_{1}, sarDen_{1};
    int64_t durationMs_{0};
    std::atomic<int64_t> positionMs_{0};
    float speed_{1.0f};
    float volL_{1.0f}, volR_{1.0f};
    int audioSessionId_{0};

    int aStreamIdx_{-1};
    int vStreamIdx_{-1};

    // 组件
    std::unique_ptr<AXDemuxer> demux_;
    std::unique_ptr<AXDecoder> aDec_;
    std::unique_ptr<AXDecoder> vDec_;
    std::unique_ptr<AXVideoRenderer> vRen_;
    std::unique_ptr<AXAudioRenderer> aRen_;
    std::unique_ptr<AXClock> clock_;

    // 队列
    std::unique_ptr<PacketQueue> aPktQ_;
    std::unique_ptr<PacketQueue> vPktQ_;
    std::unique_ptr<FrameQueue>  aFrmQ_;
    std::unique_ptr<FrameQueue>  vFrmQ_;

    // 队列容量缓存（BoundedQueue 无 capacity()，用我们构造时的参数保存）
    int aPktCap_{0};
    int vPktCap_{0};
    int aFrmCap_{0};
    int vFrmCap_{0};

    // 渲染
    ANativeWindow *window_{nullptr};

    // 全局 JavaVM
    static JavaVM *sVm;
};

#endif //AXPLAYERLIB_AXPLAYER_H