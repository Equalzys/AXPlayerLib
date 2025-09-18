//AXPlayerLib/MediaCore/player/include/AXClock.h

#ifndef AXPLAYERLIB_AXCLOCK_H
#define AXPLAYERLIB_AXCLOCK_H

#pragma once
#include <atomic>
#include <chrono>
#include <mutex>

#define AX_LOG_TAG "AXClock"
#include "AXLog.h"

class AXClock {
public:
    AXClock() { reset(0); }

    // 设置倍速：以“当前播放位置”为锚点重新起算，避免跳变
    void setSpeed(float spd) {
        std::lock_guard<std::mutex> lk(m_);
        if (spd <= 0.f) spd = 1.f;
        int64_t nowUs = nowMicro();
        basePtsUs_ = ptsUsAt(nowUs);
        speed_ = spd;
        startMonoUs_ = nowUs;
    }

    // 复位到指定 PTS（微秒）。注意：会把 speed 复为 1.0 且清除暂停累计。
    // 如果希望“保持倍速”，上层在 reset 后应立刻 setSpeed(之前的 speed)。
    void reset(int64_t startPtsUs) {
        std::lock_guard<std::mutex> lk(m_);
        speed_ = 1.0f;
        basePtsUs_ = startPtsUs;
        startMonoUs_ = nowMicro();
        paused_ = false;
        pauseAccumUs_ = 0;
    }

    void pause(bool p) {
        std::lock_guard<std::mutex> lk(m_);
        if (paused_ == p) return;
        int64_t nowUs = nowMicro();
        if (p) {
            pauseStartUs_ = nowUs;
            paused_ = true;
        } else {
            pauseAccumUs_ += (nowUs - pauseStartUs_);
            paused_ = false;
        }
    }

    // 读取当前时钟的 PTS（微秒）
    int64_t ptsUs() const {
        std::lock_guard<std::mutex> lk(m_);
        return ptsUsAt(nowMicro());
    }

private:
    static int64_t nowMicro() {
        using namespace std::chrono;
        return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    }

    int64_t ptsUsAt(int64_t monoUs) const {
        if (paused_) return basePtsUs_;
        int64_t elapsedUs = monoUs - startMonoUs_ - pauseAccumUs_;
        if (elapsedUs < 0) elapsedUs = 0;
        return basePtsUs_ + static_cast<int64_t>(elapsedUs * speed_);
    }

    mutable std::mutex m_;
    float   speed_{1.0f};
    bool    paused_{false};
    int64_t basePtsUs_{0};
    int64_t startMonoUs_{0};
    int64_t pauseAccumUs_{0};
    int64_t pauseStartUs_{0};
};

#endif //AXPLAYERLIB_AXCLOCK_H