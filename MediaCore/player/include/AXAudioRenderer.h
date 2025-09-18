//
// Created by admin on 2025/9/18.
//

#ifndef AXPLAYERLIB_AXAUDIORENDERER_H
#define AXPLAYERLIB_AXAUDIORENDERER_H


#pragma once
#include "AXQueues.h"

class AXAudioRenderer {
public:
    AXAudioRenderer() = default;
    ~AXAudioRenderer() = default;

    bool init(int sampleRate, int channels, AVSampleFormat fmt) {
        // TODO: 初始化 OpenSL ES 或 AAudio（API>=26）; 先占位返回 true
        (void)sampleRate; (void)channels; (void)fmt;
        return true;
    }

    void setFrameQueue(FrameQueue* fq) { fQ_ = fq; }

    void renderOnce() {
        // TODO: 从 fQ_ 取帧，重采样到目标格式，写入音频输出队列/回调缓冲
        if (!fQ_) return;
        AVFrame* frm = nullptr;
        if (!fQ_->pop(frm)) return;
        // consume...
        av_frame_free(&frm);
    }

    void release() {
        // TODO: 关闭音频设备
    }

private:
    FrameQueue* fQ_{nullptr};
};


#endif //AXPLAYERLIB_AXAUDIORENDERER_H
