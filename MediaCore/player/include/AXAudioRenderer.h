#ifndef AXPLAYERLIB_AXAUDIORENDERER_H
#define AXPLAYERLIB_AXAUDIORENDERER_H

#pragma once
#include "AXQueues.h"
#include <atomic>

#define AX_LOG_TAG "AXAudioRenderer"
#include "AXLog.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
}

class AXAudioRenderer {
public:
    AXAudioRenderer() = default;
    ~AXAudioRenderer() = default;

    bool init(int sampleRate, int channels, AVSampleFormat fmt) {
        (void)sampleRate; (void)channels; (void)fmt;
        active_.store(false);
        lastPtsUs_.store(-1);   // 关键：默认无效
        return true;
    }

    void setFrameQueue(FrameQueue* fq) { fQ_ = fq; }
    void setTimeBase(AVRational tb)    { tb_ = tb; }

    // 由 AXPlayer 驱动；占位实现只消费一帧并记录 PTS，不真正出声
    void renderOnce(int64_t /*masterUs*/) {
        if (!fQ_) return;
        AVFrame* frm = nullptr;
        if (!fQ_->pop(frm) || !frm) return;

        // 记录"已交给设备"的 pts（占位=立刻认为已播放）
        int64_t ptsUs = -1;
        if (frm->pts != AV_NOPTS_VALUE) {
            ptsUs = av_rescale_q(frm->pts, tb_, AVRational{1,1000000});
        }
        lastPtsUs_.store(ptsUs);
        active_.store(true);

        av_frame_free(&frm);
    }

    // 未真正输出前，必须返回 -1
    int64_t lastRenderedPtsUs() const { return lastPtsUs_.load(); }
    bool    isActive()          const { return active_.load(); }

    void release() {
        active_.store(false);
        lastPtsUs_.store(-1);
    }

private:
    FrameQueue* fQ_{nullptr};
    AVRational tb_{1,1000}; // 注意：有的头没定义宏，这里保持用 AVRational 更安全
    std::atomic<int64_t> lastPtsUs_{-1};
    std::atomic<bool>    active_{false};
};

#endif //AXPLAYERLIB_AXAUDIORENDERER_H