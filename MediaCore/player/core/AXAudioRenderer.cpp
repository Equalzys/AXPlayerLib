//AXPlayerLib/MediaCore/player/core/AXAudioRenderer.cpp
#include "AXAudioRenderer.h"

#include <algorithm>
#include <limits>
#include <thread>

#if defined(AX_WITH_OBOE)

#include <oboe/Oboe.h>

#endif

using namespace std::chrono;

JavaVM *AXAudioRenderer::sVm = nullptr;

// ======================= 工具 =======================
static inline int64_t nowUs() {
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static inline int bytesPerFrameOf(AVSampleFormat fmt, int channels) {
    switch (fmt) {
        case AV_SAMPLE_FMT_FLT:
            return sizeof(float) * channels;
        case AV_SAMPLE_FMT_S16:
            return sizeof(int16_t) * channels;
        default:
            return 0;
    }
}

static inline AVSampleFormat pickOutFormat(bool preferFloat) {
    return preferFloat ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_S16;
}

static inline AVChannelLayout layoutForChannels(int ch) {
    AVChannelLayout l{};
    switch (ch) {
        case 8:
            l = AV_CHANNEL_LAYOUT_7POINT1;
            break;
        case 6:
            l = AV_CHANNEL_LAYOUT_5POINT1;
            break;
        default:
            l = AV_CHANNEL_LAYOUT_STEREO;
            break;
    }
    // 保险起见可再 check 一下
    if (!av_channel_layout_check(&l)) {
        av_channel_layout_uninit(&l);
        av_channel_layout_default(&l, 2);
    }
    return l;
}

// ======================= PcmFifo =======================
AXAudioRenderer::PcmFifo::PcmFifo(size_t maxFrames) : capFrames_(maxFrames) {}

void AXAudioRenderer::PcmFifo::clear() {
    std::lock_guard<std::mutex> lk(m_);
    q_.clear();
    framesSum_ = 0;
}

void AXAudioRenderer::PcmFifo::push(const PcmChunk &c) {
    std::lock_guard<std::mutex> lk(m_);
    // 简单防溢策略：超出上限就丢队尾（最新数据更重要）
    if (framesSum_ + c.frames > (int64_t) capFrames_) {
        int64_t drop = framesSum_ + c.frames - (int64_t) capFrames_;
        while (drop > 0 && !q_.empty()) {
            auto &back = q_.back();
            drop -= back.frames;
            framesSum_ -= back.frames;
            q_.pop_back();
        }
    }
    q_.push_back(c);
    framesSum_ += c.frames;
}

int32_t AXAudioRenderer::PcmFifo::popInterleaved(void *dst, int32_t frames, int32_t bpf,
                                                 int64_t &outPtsUs) {
    std::lock_guard<std::mutex> lk(m_);
    int32_t need = frames;
    uint8_t *wr = static_cast<uint8_t *>(dst);
    outPtsUs = -1;
    while (need > 0 && !q_.empty()) {
        auto &f = q_.front();
        if (outPtsUs < 0 && f.ptsUs >= 0) outPtsUs = f.ptsUs;
        int32_t take = std::min(need, f.frames);
        int32_t bytes = take * bpf;
        std::memcpy(wr, f.bytes.data(), bytes);
        wr += bytes;
        need -= take;
        framesSum_ -= take;

        if (take < f.frames) {
            // 剩余部分回写（简单起见，直接擦除已拷部分）
            f.bytes.erase(f.bytes.begin(), f.bytes.begin() + bytes);
            f.frames -= take;
            f.ptsUs = (f.ptsUs >= 0) ? (f.ptsUs + (int64_t) (1'000'000LL * take / std::max(1,
                                                                                           bpf /
                                                                                           (int) sizeof(float))))
                                     : -1;
        } else {
            q_.pop_front();
        }
    }
    // 不足则补零
    int32_t filled = frames - need;
    if (need > 0) {
        std::memset(wr, 0, need * bpf);
    }
    return filled;
}

int64_t AXAudioRenderer::PcmFifo::framesAvailable() const {
    std::lock_guard<std::mutex> lk(m_);
    return framesSum_;
}

int64_t AXAudioRenderer::PcmFifo::durationUs(int sampleRate) const {
    std::lock_guard<std::mutex> lk(m_);
    if (sampleRate <= 0) return 0;
    return (int64_t) (framesSum_ * 1'000'000LL / sampleRate);
}

// ======================= OboeSink =======================
class AXAudioRenderer::OboeSink
#if defined(AX_WITH_OBOE)
        : public oboe::AudioStreamCallback
#endif
{
public:
    explicit OboeSink(AXAudioRenderer *owner) : owner_(owner) {}

    bool open() {
#if !defined(AX_WITH_OBOE)
        AX_LOGE("Oboe backend not enabled at build time");
    return false;
#else
        using namespace oboe;

        AudioStreamBuilder b;
        b.setDirection(Direction::Output);
        b.setPerformanceMode(PerformanceMode::LowLatency);
        b.setSharingMode(SharingMode::Exclusive);
        b.setUsage(Usage::Media);
        b.setContentType(ContentType::Music);

        // 优先 F32，失败再回退
        bool preferFloat = true;
        b.setFormat(AudioFormat::Float);
        b.setCallback(this);

        // 让系统选最优采样率/通道（若你想强制 8ch，可先 setChannelCount(8) 再失败降级）
        // 这里采用“先申请 8→6→2”的策略
        static const int kTryCh[] = {2, 6, 8};
        AudioStream *tmp = nullptr;
        Result r = Result::OK;
        for (int ch: kTryCh) {
            b.setChannelCount(ch);
            r = b.openStream(&tmp);
            if (r == Result::OK) {
                stream_.reset(tmp);
                break;
            }
        }
        if (!stream_) {
            // 回退 2ch + I16
            preferFloat = false;
            b.setFormat(AudioFormat::I16);
            b.setChannelCount(2);
            r = b.openStream(&tmp);
            if (r == Result::OK) stream_.reset(tmp);
        }
        if (!stream_) {
            AX_LOGE("Oboe openStream failed: %s", convertToText(r));
            return false;
        }

        // 拿到最终参数
        actualRate_ = stream_->getSampleRate();
        actualChannels_ = stream_->getChannelCount();
        actualIsFloat_ = (stream_->getFormat() == AudioFormat::Float);
        framesPerBurst_ = stream_->getFramesPerBurst();

        AX_LOGI("Oboe opened: rate=%d, ch=%d, fmt=%s, burst=%d, perf=%d, share=%d",
                actualRate_, actualChannels_, actualIsFloat_ ? "F32" : "S16",
                framesPerBurst_, (int) stream_->getPerformanceMode(),
                (int) stream_->getSharingMode());

        // 启动
        r = stream_->requestStart();
        if (r != Result::OK) {
            AX_LOGE("Oboe requestStart failed: %s", convertToText(r));
            stream_.reset();
            return false;
        }
        started_.store(true, std::memory_order_release);
        return true;
#endif
    }

    bool startStream() {
#if defined(AX_WITH_OBOE)
        if (stream_) return stream_->requestStart() == oboe::Result::OK;
#endif
        return false;
    }

    bool stopStream() {
#if defined(AX_WITH_OBOE)
        if (stream_) {
            (void) stream_->requestStop();
            return true;
        }
#endif
        return false;
    }

    void resetBase() { baseSet_.store(false, std::memory_order_release); }

    void close() {
#if defined(AX_WITH_OBOE)
        if (stream_) {
            (void) stream_->requestStop();
            stream_.reset();
        }
#endif
        started_.store(false, std::memory_order_release);
        baseSet_.store(false, std::memory_order_release);
    }

    bool started() const { return started_.load(std::memory_order_acquire); }

    int sampleRate() const { return actualRate_; }

    int channels() const { return actualChannels_; }

    bool isFloat() const { return actualIsFloat_; }

    int framesPerBurst() const { return framesPerBurst_; }

    void setVolume(float l, float r) {
//#if defined(AX_WITH_OBOE)
//            if (stream_) {
//                // Oboe 没有左右声道独立音量，使用 master gain
//                float g = std::max(l, r);
//                (void) stream_->setVolume(g);
//            }
//#endif
//            (void)l; (void)r;
    }

    // 取播放头时间戳（CLOCK_MONOTONIC）。返回 true 则 outPtsUs 有效。
    bool getClockUs(int64_t &outPtsUs) {
#if !defined(AX_WITH_OBOE)
        return false;
#else
        if (!stream_) return false;

        // 设备时间戳：framePosition & timeNanos
        int64_t framePos = 0;
        int64_t timeNs = 0;
        auto r = stream_->getTimestamp(CLOCK_MONOTONIC, &framePos, &timeNs);
        if (r != oboe::Result::OK) return false;

        // 建立首帧播放时的媒体 PTS 基准（外部通过 owner_->basePtsUs_ 传入）
        int64_t basePts = owner_->basePtsUs_.load(std::memory_order_acquire);
        if (basePts < 0) return false;

        if (!baseSet_.load(std::memory_order_acquire)) {
            baseFramePos_ = framePos;
            baseTimeNs_ = timeNs;
            baseSet_.store(true, std::memory_order_release);
        }

        // 注意：framePos 是“已播放到 DAC 的帧数”
        const int rate = std::max(1, actualRate_);
        int64_t framesDiff = (framePos - baseFramePos_);
        outPtsUs = basePts + framesDiff * 1'000'000LL / rate;
        return true;
#endif
    }

#if defined(AX_WITH_OBOE)

    // ========== 回调：从 FIFO 取数据 ==========
    oboe::DataCallbackResult
    onAudioReady(oboe::AudioStream *, void *audioData, int32_t numFrames) override {
        if (!owner_) return oboe::DataCallbackResult::Stop;

        const int bpf = bytesPerFrameOf(owner_->outFormat_, owner_->outChannels_);
        // ★ 暂停：写静音，不动 FIFO，不刷新任何时钟
        if (owner_->paused_.load(std::memory_order_acquire)) {
            std::memset(audioData, 0, numFrames * bpf);
            return oboe::DataCallbackResult::Continue;
        }
        int64_t ptsUs = -1;
        int32_t filled = owner_->fifo_.popInterleaved(audioData, numFrames, bpf, ptsUs);

        if (filled < numFrames) {
            // 欠载，补零
            std::memset((uint8_t *) audioData + filled * bpf, 0, (numFrames - filled) * bpf);
            owner_->underrunCnt_.fetch_add(1, std::memory_order_relaxed);
        }
        // 更新基准：当我们第一次把真实数据送到 DAC 时，用该块的 PTS 作为 basePts
        if (ptsUs >= 0 && owner_->basePtsUs_.load(std::memory_order_acquire) < 0) {
            owner_->basePtsUs_.store(ptsUs, std::memory_order_release);
        }
        // 更新时间戳（用于上层查询）
        int64_t clk;
        if (getClockUs(clk)) {
            owner_->lastPtsUs_.store(clk, std::memory_order_release);
            owner_->active_.store(true, std::memory_order_release);
        }
        return oboe::DataCallbackResult::Continue;
    }

    void onErrorAfterClose(oboe::AudioStream *, oboe::Result r) override {
        AX_LOGE("Oboe onErrorAfterClose: %s", oboe::convertToText(r));
        // 让上层感知非活跃，必要时触发重建
        owner_->active_.store(false, std::memory_order_release);
        owner_->basePtsUs_.store(-1, std::memory_order_release);
    }

#endif

private:
    AXAudioRenderer *owner_{nullptr};
#if defined(AX_WITH_OBOE)
    std::unique_ptr<oboe::AudioStream> stream_;
#endif
    std::atomic<bool> started_{false};

    // 播放头基准
    std::atomic<bool> baseSet_{false};
    int64_t baseFramePos_{0};
    int64_t baseTimeNs_{0};

    // 实参
    int actualRate_{48000};
    int actualChannels_{2};
    bool actualIsFloat_{true};
    int framesPerBurst_{192};
};

// ======================= AXAudioRenderer =======================
AXAudioRenderer::AXAudioRenderer()
        : fifo_(48000 * 4) // FIFO 上限 ~4 秒（48k 单声道），足够抗抖
{
    av_channel_layout_uninit(&inChLayout_);
    av_channel_layout_uninit(&outChLayout_);
}

AXAudioRenderer::~AXAudioRenderer() {
    release();
}

bool AXAudioRenderer::init() {
#if !defined(AX_WITH_OBOE)
    AX_LOGE("AXAudioRenderer built WITHOUT Oboe. Please enable AX_WITH_OBOE and add MediaCore/oboe.");
return false;
#else
    if (sink_) {
        AX_LOGW("AXAudioRenderer::init called twice");
        return true;
    }
    sink_ = std::make_unique<OboeSink>(this);
    if (!sink_->open()) {
        AX_LOGE("OboeSink open failed");
        sink_.reset();
        return false;
    }
    outRate_ = sink_->sampleRate();
    outChannels_ = sink_->channels();
    outFormat_ = pickOutFormat(sink_->isFloat());
    outChLayout_ = layoutForChannels(outChannels_);
    AX_LOGI("Audio out params: rate=%d ch=%d fmt=%s",
            outRate_, outChannels_, outFormat_ == AV_SAMPLE_FMT_FLT ? "F32" : "S16");

    resetClock_();
    return true;
#endif
}

bool AXAudioRenderer::start() {
    if (!sink_) {
        AX_LOGE("start() called before init()");
        return false;
    }
    // Oboe 已在 open 时 requestStart，这里只置位
    active_.store(true, std::memory_order_release);
    return true;
}

void AXAudioRenderer::pause(bool on) {
    paused_.store(on, std::memory_order_release);
#if defined(AX_WITH_OBOE)
    if (sink_) {
        if (on) {
            (void) sink_->stopStream();
            sink_->resetBase();     // 让下次 getTimestamp 重建 frame 基准
        } else {
            sink_->resetBase();
            (void) sink_->startStream();
        }
    }
#endif
    if (on) {
        fifo_.clear();   // 不把暂停前的音频残留带到恢复后
        resetClock_();   // basePtsUs_ = -1, lastPtsUs_ = -1
    }
}

void AXAudioRenderer::stop() {
    if (sink_) {
        sink_->close();
    }
    active_.store(false, std::memory_order_release);
}

void AXAudioRenderer::release() {
    stop();
    fifo_.clear();

    if (swr_) {
        swr_free(&swr_);
        swr_ = nullptr;
    }
    av_channel_layout_uninit(&inChLayout_);
    av_channel_layout_uninit(&outChLayout_);
    sink_.reset();
}

void AXAudioRenderer::setSpeed(float spd) {
    if (spd <= 0.f) spd = 1.f;
    speed_ = std::min(4.f, std::max(0.25f, spd));
    // 若启用 SoundTouch，这里会更新 tempo；我们在 convertAndQueue_ 里按需处理
}

void AXAudioRenderer::setVolume(float left, float right) {
    volL_ = std::clamp(left, 0.f, 1.f);
    volR_ = std::clamp(right, 0.f, 1.f);
//        if (sink_) sink_->setVolume(volL_, volR_);
}

int64_t AXAudioRenderer::lastRenderedPtsUs() const {
    return lastPtsUs_.load(std::memory_order_acquire);
}

void AXAudioRenderer::resetClock_() {
    lastPtsUs_.store(-1, std::memory_order_release);
    basePtsUs_.store(-1, std::memory_order_release);
}

bool AXAudioRenderer::ensureSwrForFrame_(const AVFrame *frm) {
    if (!frm) return false;

    // 输入参数发生变化就重建
    if (inFmt_ != (AVSampleFormat) frm->format
        || inRate_ != frm->sample_rate
        || av_channel_layout_compare(&inChLayout_, &frm->ch_layout) != 0) {

        if (swr_) {
            swr_free(&swr_);
            swr_ = nullptr;
        }
        av_channel_layout_uninit(&inChLayout_);
        av_channel_layout_copy(&inChLayout_, &frm->ch_layout);
        inFmt_ = (AVSampleFormat) frm->format;
        inRate_ = frm->sample_rate;

        // 目标布局/格式已在 init() 时确定：outChLayout_/outRate_/outFormat_
        int ret = swr_alloc_set_opts2(
                &swr_,                // ctx
                &outChLayout_,        // 输出布局
                outFormat_,           // 输出格式
                outRate_,             // 输出采样率
                &inChLayout_,         // 输入布局
                inFmt_,               // 输入格式
                inRate_,              // 输入采样率
                0, nullptr);
        if (ret < 0 || !swr_) {
            AX_LOGE("swr_alloc_set_opts2 failed: %d", ret);
            return false;
        }
        if ((ret = swr_init(swr_)) < 0) {
            AX_LOGE("swr_init failed: %d", ret);
            swr_free(&swr_);
            return false;
        }
        AX_LOGI("Swr (in: %dHz %dch fmt=%d) -> (out: %dHz %dch fmt=%d)",
                inRate_, inChLayout_.nb_channels, (int) inFmt_,
                outRate_, outChLayout_.nb_channels, (int) outFormat_);
    }
    return true;
}

static void interleavePlanarToInterleaved(const uint8_t **srcPlanes, int nbSamples, int ch,
                                          AVSampleFormat fmt,
                                          std::vector<uint8_t> &out) {
    // swr_convert 已可直接输出交织；该函数作为兜底/调试用途
    const int bytesPerSample = (fmt == AV_SAMPLE_FMT_FLT || fmt == AV_SAMPLE_FMT_FLTP)
                               ? sizeof(float) : sizeof(int16_t);
    out.resize((size_t) nbSamples * ch * bytesPerSample);
    if (fmt == AV_SAMPLE_FMT_FLT || fmt == AV_SAMPLE_FMT_FLTP) {
        auto *dst = reinterpret_cast<float *>(out.data());
        for (int n = 0; n < nbSamples; ++n) {
            for (int c = 0; c < ch; ++c) {
                dst[n * ch + c] = reinterpret_cast<const float *>(srcPlanes[c])[n];
            }
        }
    } else {
        auto *dst = reinterpret_cast<int16_t *>(out.data());
        for (int n = 0; n < nbSamples; ++n) {
            for (int c = 0; c < ch; ++c) {
                dst[n * ch + c] = reinterpret_cast<const int16_t *>(srcPlanes[c])[n];
            }
        }
    }
}

// 帧 → 重采样 → (可选时伸) → FIFO
bool AXAudioRenderer::convertAndQueue_(const AVFrame *frm) {
    if (!ensureSwrForFrame_(frm)) return false;

    const int outCh = outChLayout_.nb_channels;
    const int outBps = (outFormat_ == AV_SAMPLE_FMT_FLT) ? sizeof(float) : sizeof(int16_t);
    const int outBpf = outBps * outCh;

    // 估算输出样本数（加 64 保险）
    const int maxOut = swr_get_out_samples(swr_, frm->nb_samples) + 64;

    std::vector<uint8_t> tmp;
    tmp.resize((size_t) maxOut * outBpf);

    const uint8_t **inData = (const uint8_t **) frm->extended_data;
    uint8_t *outData[1] = {tmp.data()};
    int outSamples = swr_convert(swr_, outData, maxOut, inData, frm->nb_samples);
    if (outSamples < 0) {
        AX_LOGE("swr_convert failed: %d", outSamples);
        return false;
    }

    // 倍速：可选 SoundTouch（这里给出留口，默认直通）
    // 如果需要接入：将 tmp(F32) 送入 ST，取出 tempo= speed_ 的输出，再覆盖 tmp/outSamples
    // 为简洁，本版先直通，后续你需要我再补 SoundTouch 集成时，我会按 F32 路径对齐。

    // 音量（软件侧增益，避免设备不支持左右独立）
    if (outFormat_ == AV_SAMPLE_FMT_FLT && (volL_ < 0.999f || volR_ < 0.999f)) {
        float *p = reinterpret_cast<float *>(tmp.data());
        for (int n = 0; n < outSamples; ++n) {
            for (int c = 0; c < outCh; ++c) {
                const float g = (c == 0 ? volL_ : (c == 1 ? volR_ : std::max(volL_, volR_)));
                p[n * outCh + c] *= g;
            }
        }
    } else if (outFormat_ == AV_SAMPLE_FMT_S16 && (volL_ < 0.999f || volR_ < 0.999f)) {
        int16_t *p = reinterpret_cast<int16_t *>(tmp.data());
        for (int n = 0; n < outSamples; ++n) {
            for (int c = 0; c < outCh; ++c) {
                const float g = (c == 0 ? volL_ : (c == 1 ? volR_ : std::max(volL_, volR_)));
                int v = (int) std::lrint((float) p[n * outCh + c] * g);
                p[n * outCh + c] = (int16_t) std::clamp(v,
                                                        (int) std::numeric_limits<int16_t>::min(),
                                                        (int) std::numeric_limits<int16_t>::max());
            }
        }
    }

    // 入 FIFO（一次一个 chunk，记录首样本 PTS）
    PcmChunk c;
    c.frames = outSamples;
    c.ptsUs = (frm->pts == AV_NOPTS_VALUE)
              ? -1
              : av_rescale_q(frm->pts, tb_, AVRational{1, 1000000});
    c.bytes.resize((size_t) c.frames * outBpf);
    std::memcpy(c.bytes.data(), tmp.data(), c.bytes.size());

    fifo_.push(c);
    return true;
}

bool AXAudioRenderer::renderOnce(int64_t /*masterClockUs*/) {
    if (!sink_ || !sink_->started()) return false;
    if (!frmQ_) return false;

    // 目标 FIFO 水位（AAudio 60~120ms；OpenSL 100~200ms）
    const int64_t lowUs = 80'000;   // 80ms
    const int64_t highUs = 160'000;  // 160ms
    int64_t curUs = fifo_.durationUs(outRate_);

    bool wrote = false;
    while (curUs < lowUs) {
        if (frmQ_->isAborted()) break;

        AVFrame *frm = nullptr;
        if (!frmQ_->tryPop(frm, std::chrono::milliseconds(5))) { // 最多等5ms，避免卡死
            break;
        }
        if (!frm) {
            // 哨兵：流结束
            AX_LOGI("Audio frame nullptr (eof sentinel)");
            break;
        }
        wrote |= convertAndQueue_(frm);

        // 释放传入帧（由 AXDecoder clone 的帧）
        av_frame_free(&frm);

        curUs = fifo_.durationUs(outRate_);
        if (curUs >= highUs) break;
    }

    // 更新时钟
    int64_t clk;
    if (sink_->getClockUs(clk)) {
        lastPtsUs_.store(clk, std::memory_order_release);
        active_.store(true, std::memory_order_release);
    } else {
        active_.store(false, std::memory_order_release);
    }
    return wrote;
}
