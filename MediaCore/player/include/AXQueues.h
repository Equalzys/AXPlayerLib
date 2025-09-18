//
// Created by admin on 2025/9/18.
//

#ifndef AXPLAYERLIB_AXQUEUES_H
#define AXPLAYERLIB_AXQUEUES_H

#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <type_traits>
#include <android/log.h>

extern "C" {
#include <libavcodec/avcodec.h>   // av_packet_free 等
#include <libavutil/frame.h>      // av_frame_free
}

#define AX_LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  "AXPlayer", __VA_ARGS__)
#define AX_LOGW(...)  __android_log_print(ANDROID_LOG_WARN,  "AXPlayer", __VA_ARGS__)
#define AX_LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, "AXPlayer", __VA_ARGS__)

// -------- C++11：类型专用释放器（替代 if constexpr）--------
template<typename T>
struct AvItemReleaser {
    static inline void free(T&) {
        // 默认不做任何事（针对非 AVPacket*/AVFrame* 类型）
    }
};

template<>
struct AvItemReleaser<AVPacket*> {
    static inline void free(AVPacket*& p) {
        if (p) av_packet_free(&p);
    }
};

template<>
struct AvItemReleaser<AVFrame*> {
    static inline void free(AVFrame*& f) {
        if (f) av_frame_free(&f);
    }
};

// -------- 有界线程安全队列 --------
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t cap) : cap_(cap) {}
    ~BoundedQueue() { clear(); }

    void abort() {
        aborted_.store(true);
        cv_.notify_all();
    }

    void flush() {
        std::lock_guard<std::mutex> lk(m_);
        while (!q_.empty()) {
            T item = q_.front();
            q_.pop();
            AvItemReleaser<T>::free(item);
        }
        cv_.notify_all();
    }

    bool push(T item) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return aborted_.load() || q_.size() < cap_; });
        if (aborted_.load()) return false;
        q_.push(item);
        lk.unlock();
        cv_.notify_all();
        return true;
    }

    bool pop(T& out) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return aborted_.load() || !q_.empty(); });
        if (aborted_.load()) return false;
        out = q_.front();
        q_.pop();
        lk.unlock();
        cv_.notify_all();
        return true;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(m_);
        while (!q_.empty()) {
            T item = q_.front();
            q_.pop();
            AvItemReleaser<T>::free(item);
        }
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

private:
    size_t cap_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
    std::atomic<bool> aborted_{false};
};

using PacketQueue = BoundedQueue<AVPacket*>;
using FrameQueue  = BoundedQueue<AVFrame*>;

#endif //AXPLAYERLIB_AXQUEUES_H