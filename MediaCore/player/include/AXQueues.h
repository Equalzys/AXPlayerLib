// AXPlayerLib/MediaCore/player/include/AXQueues.h
#ifndef AXPLAYERLIB_AXQUEUES_H
#define AXPLAYERLIB_AXQUEUES_H

#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <type_traits>
#include <chrono>
#include <android/log.h>

#define AX_LOG_TAG "AXQueues"
#include "AXLog.h"

extern "C" {
#include <libavcodec/avcodec.h>   // av_packet_free
#include <libavutil/frame.h>      // av_frame_free
}

// ---------- 类型专用释放器（C++11 兼容） ----------
template<typename T>
struct AvItemReleaser { static inline void free(T&) {} };

template<> struct AvItemReleaser<AVPacket*> {
    static inline void free(AVPacket*& p){ if (p) av_packet_free(&p); }
};
template<> struct AvItemReleaser<AVFrame*> {
    static inline void free(AVFrame*& f){ if (f) av_frame_free(&f); }
};

// ---------- 有界线程安全队列 ----------
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t cap) : cap_(cap) {}
    // ★ 安全析构：先 abort() 唤醒所有 wait，再 clear() 释放元素
    ~BoundedQueue() {
        abort();
        clear();
    }

    // 让所有阻塞的 push()/pop() 立即返回 false
    void abort() {
        aborted_.store(true);
        cv_.notify_all();
    }

    bool isAborted() const { return aborted_.load(); }

    // 清空并释放内部对象（不会改变 aborted_ 状态）
    void clear() {
        std::lock_guard<std::mutex> lk(m_);
        while (!q_.empty()) {
            T item = q_.front();
            q_.pop();
            AvItemReleaser<T>::free(item);
        }
        // 不再 notify：避免外部把 clear 当事件，真正的退出事件用 abort 通知
    }

    // 丢弃元素并唤醒 wait（用于 seek 等快速清队）
    void flush() {
        std::lock_guard<std::mutex> lk(m_);
        while (!q_.empty()) {
            T item = q_.front();
            q_.pop();
            AvItemReleaser<T>::free(item);
        }
        cv_.notify_all();
    }

    // 阻塞入队；若 aborted 返回 false
    bool push(T item) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&]{ return aborted_.load() || q_.size() < cap_; });
        if (aborted_.load()) return false;
        q_.push(item);
        lk.unlock();
        cv_.notify_all();
        return true;
    }

    // 阻塞出队；若 aborted 返回 false
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

    // 带超时的出队（常用于渲染/解码线程的柔性退出）
    template<typename Rep, typename Period>
    bool tryPop(T& out, const std::chrono::duration<Rep,Period>& timeout) {
        std::unique_lock<std::mutex> lk(m_);
        if (!cv_.wait_for(lk, timeout, [&]{ return aborted_.load() || !q_.empty(); }))
            return false; // 超时
        if (aborted_.load()) return false;
        out = q_.front();
        q_.pop();
        lk.unlock();
        cv_.notify_all();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lk(m_);
        return q_.empty();
    }

    size_t capacity() const { return cap_; }

private:
    const size_t cap_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::queue<T> q_;
    std::atomic<bool> aborted_{false};
};

using PacketQueue = BoundedQueue<AVPacket*>;
using FrameQueue  = BoundedQueue<AVFrame*>;

#endif //AXPLAYERLIB_AXQUEUES_H