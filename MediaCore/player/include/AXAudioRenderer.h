// AXPlayerLib/MediaCore/player/include/AXAudioRenderer.h
#ifndef AXPLAYERLIB_AXAUDIORENDERER_H
#define AXPLAYERLIB_AXAUDIORENDERER_H

#pragma once

#include <atomic>
#include <cstdint>
#include <jni.h>
#include <android/native_window.h>

#include "AXQueues.h"  // PacketQueue/FrameQueue、BoundedQueue

#define AX_LOG_TAG "AXAudioRenderer"

#include "AXLog.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/channel_layout.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}


/**
 * 音频渲染器（Oboe 后端，AAudio 优先）。
 * - 从 FrameQueue 取 AVFrame
 * - libswresample 统一到设备支持的 PCM（优先 F32、否则 S16；采样率用设备 nativeRate）
 * - 可选 SoundTouch 做倍速时伸不变调
 * - Oboe 数据回调从 FIFO 取样本送声卡
 * - 以音频播放头为“主时钟”（若音频活跃）
 */
class AXAudioRenderer {
public:
    AXAudioRenderer();

    ~AXAudioRenderer();

    // ------- 生命周期 -------
    // 必须在设置队列/时基之后调用
    bool init();

    // 启停/暂停
    bool start();

    void pause(bool on);

    void stop();     // 停止并释放底层输出
    void release();  // 等价 stop + 释放一切缓存

    // ------- 输入与配置 -------
    void setFrameQueue(FrameQueue *q) { frmQ_ = q; }

    void setTimeBase(AVRational tb) { tb_ = tb; }

    void setSpeed(float spd);   // 0.25~4.0（SoundTouch）
    void setVolume(float left, float right); // 0.0~1.0

    // ------- 拉流/喂料（由上层 play 线程周期调用） -------
    // 目标：将 FIFO 水位保持在 60~120ms（AAudio）或 100~200ms（OpenSL）
    // 返回：本次是否实际写入了数据（用于缓冲状态估算）
    bool renderOnce(int64_t /*masterClockUs*/);

    // ------- 时钟/状态 -------
    // 若音频活跃，返回播放头对应的媒体 PTS（微秒）；否则返回 <0
    int64_t lastRenderedPtsUs() const;

    bool isActive() const { return active_.load(std::memory_order_acquire); }

    // 当前输出的设备参数（打开流后可查询）
    int outputSampleRate() const { return outRate_; }

    int outputChannels() const { return outChannels_; }

    bool outputFloat() const { return outFormat_ == AV_SAMPLE_FMT_FLT; }

    // JavaVM 注入（在 JNI_OnLoad 里赋值）
    static void setJavaVM(JavaVM *vm) { sVm = vm; }

private:
    // ============ 内部类型 ============
    struct PcmChunk {
        // 交织 PCM 数据（F32 或 S16）
        std::vector<uint8_t> bytes;
        int32_t frames = 0;  // 帧数（每帧 = channels 样本）
        int64_t ptsUs = -1; // 此块首样本对应的媒体 PTS（用于建立基准）
    };

    // 单生产者/单消费者安全队列（供 Oboe 回调线程消费）
    class PcmFifo {
    public:
        explicit PcmFifo(size_t maxFrames = 48000 * 2); // 默认~2秒上限（按48k、单声道计）
        void clear();

        // 写入全部拷贝；当 FIFO 满时丢尾部并告警（防止阻塞）
        void push(const PcmChunk &c);

        // 读出指定帧数到目标缓冲，不足则填 0；返回实际填充帧数与对齐的首帧 PTS
        int32_t popInterleaved(void *dst, int32_t frames, int32_t bytesPerFrame, int64_t &outPtsUs);

        // 当前累计帧数
        int64_t framesAvailable() const;

        // 估算 FIFO 对应时长（微秒）
        int64_t durationUs(int sampleRate) const;

    private:
        mutable std::mutex m_;
        std::deque<PcmChunk> q_;
        size_t capFrames_;
        int64_t framesSum_ = 0;
    };

    // Oboe 后端（数据回调从 FIFO 拉取）
    class OboeSink;

    // ============ 内部方法 ============
    bool openSink_();

    void closeSink_();

    // 准备/复用 swresample：源->目标（outFormat_/outRate_/outChannels_/layout）
    bool ensureSwrForFrame_(const AVFrame *frm);

    // 源 AVFrame → 目标 PCM（交织），并写入 FIFO（必要时走 SoundTouch）
    bool convertAndQueue_(const AVFrame *frm);

    // 统计/状态维护
    void resetClock_();

private:
    // 输入
    FrameQueue *frmQ_{nullptr};
    AVRational tb_{1, 1000};
    std::atomic<bool> paused_{false};
    // 输出协商结果（打开设备后确定）
    int outRate_{48000};
    int outChannels_{2};
    AVSampleFormat outFormat_{AV_SAMPLE_FMT_FLT}; // F32 优先，否则 S16
    AVChannelLayout outChLayout_{}; // 与 outChannels 对应的布局（7.1/5.1/2.0）

    // swresample
    SwrContext *swr_{nullptr};
    AVSampleFormat inFmt_{AV_SAMPLE_FMT_NONE};
    AVChannelLayout inChLayout_{};
    int inRate_{0};

    // 倍速
    float speed_{1.0f};
    bool stInited_{false};
    // 为减少依赖震荡，这里不直接包含 soundtouch 头；在 cpp 里做可选集成

    // FIFO & Sink
    PcmFifo fifo_;
    std::unique_ptr<OboeSink> sink_;

    // 音频主时钟（来自 sink 的播放头）
    std::atomic<bool> active_{false};
    std::atomic<int64_t> lastPtsUs_{-1};

    // 基准：第一帧播放的媒体 PTS 与设备 framePos 对齐
    std::atomic<int64_t> basePtsUs_{-1};

    // 音量
    float volL_{1.0f}, volR_{1.0f};

    // 日志辅助
    std::atomic<int> underrunCnt_{0};
    std::atomic<int> overflowCnt_{0};

    // 全局 JavaVM
    static JavaVM *sVm;
};


#endif //AXPLAYERLIB_AXAUDIORENDERER_H