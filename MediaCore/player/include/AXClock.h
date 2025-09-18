//
// Created by admin on 2025/9/18.
//

#ifndef AXPLAYERLIB_AXCLOCK_H
#define AXPLAYERLIB_AXCLOCK_H


#pragma once
#include <atomic>
#include <chrono>
#include <mutex>

class AXClock {
public:
    AXClock() { reset(0); }

    void setSpeed(float spd) {
        std::lock_guard<std::mutex> lk(m_);
        if (spd <= 0.f) spd = 1.f;
        // 把当前播放位置“冻结”为基准，再切速
        int64_t nowUs = nowMicro();
        basePtsUs_ = ptsUsAt(nowUs);
        speed_ = spd;
        startMonoUs_ = nowUs;
    }

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
            // 进入暂停：累计暂停时长
            pauseStartUs_ = nowUs;
            paused_ = true;
        } else {
            // 退出暂停：修正 base
            pauseAccumUs_ += (nowUs - pauseStartUs_);
            paused_ = false;
        }
    }

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
