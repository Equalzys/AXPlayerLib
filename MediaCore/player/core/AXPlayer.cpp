#include "AXPlayer.h"
#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstdlib>

#include "AXDemuxer.h"
#include "AXDecoder.h"
#include "AXVideoRenderer.h"
#include "AXAudioRenderer.h"
#include "AXClock.h"
#include "AXQueues.h"
#include "AXErrors.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
}


// nowMs 工具
static inline int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ===== JavaVM 线程附着工具 =====
JavaVM* AXPlayer::sVm = nullptr;
void AXPlayer::SetJavaVM(JavaVM* vm) {
    AXPlayer::sVm = vm;
    AXAudioRenderer::setJavaVM(vm);
}
JavaVM* AXPlayer::GetJavaVM() { return AXPlayer::sVm; }

struct JniThreadScope {
    bool attached = false;
    JNIEnv* env = nullptr;
    JniThreadScope() {
        JavaVM* vm = AXPlayer::GetJavaVM();
        if (vm && vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
            if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            }
        }
    }
    ~JniThreadScope() {
        JavaVM* vm = AXPlayer::GetJavaVM();
        if (attached && vm) vm->DetachCurrentThread();
    }
};

AXPlayer::AXPlayer(std::shared_ptr<AXPlayerCallback> cb) : cb_(std::move(cb)) {
    AX_LOGI("AXPlayer ctor");
}

AXPlayer::~AXPlayer() {
    AX_LOGI("AXPlayer dtor: begin");
    playing_.store(false);
    abort_.store(true);

    // 先停播放/IO 线程（防止它们再驱动渲染器）
    if (ioThread_.joinable())   ioThread_.join();
    if (playThread_.joinable()) playThread_.join();

    // ★ 关键：停 demux/decoder，并让队列退出
    stopPipelines_();

    // 再释放渲染器（此时不会再被调用）
    if (aRen_) aRen_->release();
    if (vRen_) vRen_->release();
    aRen_.reset();
    vRen_.reset();

    // 最后释放窗口
    {
        std::lock_guard<std::mutex> lk(wmtx_);
        if (window_) {
            ANativeWindow_release(window_);
            window_ = nullptr;
        }
    }
    AX_LOGI("AXPlayer dtor: done");
}
void AXPlayer::stopPipelines_() {
    // 停 demuxer 线程
    if (demux_) demux_->stop();

    // 通知队列退出并唤醒
    if (aPktQ_) aPktQ_->abort();
    if (vPktQ_) vPktQ_->abort();
    if (aFrmQ_) aFrmQ_->abort();
    if (vFrmQ_) vFrmQ_->abort();

    // 停解码线程
    if (aDec_) aDec_->stop();
    if (vDec_) vDec_->stop();

    // 清空/释放（此时内部互斥量仍然存活且线程已停）
    aDec_.reset();
    vDec_.reset();
    demux_.reset(); // AXDemuxer 析构里也会安全 close_input()

    aPktQ_.reset();
    vPktQ_.reset();
    aFrmQ_.reset();
    vFrmQ_.reset();
}

void AXPlayer::setDataSource(const std::string& urlOrPath, const std::map<std::string, std::string>& headers) {
    source_  = urlOrPath;
    headers_ = headers;
    AX_LOGI("setDataSource: %s", source_.c_str());
}

void AXPlayer::prepareAsync() {
    if (state_ != State::IDLE && state_ != State::STOPPED) {
        AX_LOGW("prepareAsync called in wrong state");
        return;
    }
    changeState(State::PREPARING);
    abort_.store(false);
    prepared_.store(false);
    ioThread_ = std::thread(&AXPlayer::ioThreadLoop, this);
}

void AXPlayer::start() {
    if (state_ == State::COMPLETED) {
        AX_LOGI("restart from COMPLETED: seek to 0 and restart demux/dec");
        // 1) flush
        if (aDec_) aDec_->flush();
        if (vDec_) vDec_->flush();
        if (aPktQ_) aPktQ_->flush();
        if (vPktQ_) vPktQ_->flush();
        if (aFrmQ_) aFrmQ_->flush();
        if (vFrmQ_) vFrmQ_->flush();

        // 2) 选择一个可用流的 time_base
        int targetStream = (vDec_ && vStreamIdx_ >= 0) ? vStreamIdx_ :
                           (aDec_ && aStreamIdx_ >= 0) ? aStreamIdx_ : -1;
        if (targetStream >= 0) {
            demux_->seek(targetStream, 0);
        }
        // 3) 重新启动 demux/dec 线程（如果它们会在 EOF 退出）
        demux_->start(aPktQ_.get(), vPktQ_.get());
        if (aDec_) aDec_->start();
        if (vDec_) vDec_->start();

        // 4) 时钟归零
        if (clock_) { clock_->reset(0); clock_->setSpeed(speed_); }
        prepared_.store(true);
        changeState(State::PREPARED);
        return;
    }
    if (state_ == State::PREPARED || state_ == State::PAUSED || state_ == State::COMPLETED) {
        playing_.store(true);
        if (clock_) {
            clock_->setSpeed(speed_);
            clock_->pause(false);
        }
        if (aRen_)  aRen_->pause(false);
        if (!playThread_.joinable())
            playThread_ = std::thread(&AXPlayer::playThreadLoop, this);
        changeState(State::PLAYING);
        AX_LOGI("start");
    } else {
        AX_LOGW("start ignored: state=%d", (int)state_.load());
    }
}

void AXPlayer::pause() {
    AX_LOGI("pause");
    playing_.store(false);
    if (clock_) clock_->pause(true);
    if (aRen_)  aRen_->pause(true);
    changeState(State::PAUSED);
}

void AXPlayer::seekTo(int64_t msec) {
    AX_LOGI("seekTo: %lld ms", (long long)msec);
    if (!demux_) return;

    int targetStream = -1;
    AVRational tb{1,1000};
    if (vDec_ && vStreamIdx_ >= 0) {
        targetStream = vStreamIdx_;
        tb = vDec_->timeBase();
    } else if (aDec_ && aStreamIdx_ >= 0) {
        targetStream = aStreamIdx_;
        tb = aDec_->timeBase();
    }
    if (targetStream < 0) return;

    int64_t pts = av_rescale_q(msec, AVRational{1,1000}, tb);

    if (aDec_) aDec_->flush();
    if (vDec_) vDec_->flush();
    if (aPktQ_) aPktQ_->flush();
    if (vPktQ_) vPktQ_->flush();
    if (aFrmQ_) aFrmQ_->flush();
    if (vFrmQ_) vFrmQ_->flush();

    demux_->seek(targetStream, pts);

    if (clock_) {
        float sp = speed_;
        bool wasPaused = !playing_.load();
        clock_->reset(msec * 1000);
        clock_->setSpeed(sp);
        clock_->pause(wasPaused);
    }
    if (aRen_) {
        aRen_->release();
        aRen_->init();
        if (playing_.load()) aRen_->start();
    }
    positionMs_.store(msec);
}

bool AXPlayer::isPlaying() { return playing_.load(); }
void AXPlayer::setSpeed(float speed) {
    speed_ = speed;
    if (clock_) clock_->setSpeed(speed);
    if (aRen_)       aRen_->setSpeed(speed);
}
int64_t AXPlayer::getCurrentPositionMs() { return positionMs_.load(); }
int64_t AXPlayer::getDurationMs() { return durationMs_; }
void AXPlayer::setVolume(float l, float r) {
    volL_ = l; volR_ = r;
    if (aRen_) aRen_->setVolume(l, r);
}
int AXPlayer::getVideoWidth() { return videoW_; }
int AXPlayer::getVideoHeight() { return videoH_; }
int AXPlayer::getVideoSarNum() { return sarNum_; }
int AXPlayer::getVideoSarDen() { return sarDen_; }
int AXPlayer::getAudioSessionId() { return audioSessionId_; }

void AXPlayer::setWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lk(wmtx_);
    if (window) ANativeWindow_acquire(window);
    if (window_ && window_ != window) {
        ANativeWindow_release(window_);
    }
    window_ = window;
    if (vRen_) vRen_->init(window_, videoW_, videoH_, sarNum_, sarDen_);
}

void AXPlayer::changeState(State s) { state_.store(s); }

void AXPlayer::notifyError(int what, int extra, const std::string& msg) {
    changeState(State::ERROR);
    playing_.store(false);
    AX_LOGE("notifyError what=%d extra=%d msg=%s", what, extra, msg.c_str());
    if (cb_) cb_->onError(what, extra, msg);
}

void AXPlayer::ioThreadLoop() {
    JniThreadScope jscope;
    AX_LOGI("ioThread start");

    demux_.reset(new AXDemuxer());
    aPktQ_.reset(new PacketQueue(256));
    vPktQ_.reset(new PacketQueue(256));
    aFrmQ_.reset(new FrameQueue(64));
    vFrmQ_.reset(new FrameQueue(32));
    // 记录容量（BoundedQueue 无 capacity()）
    aPktCap_ = 256;
    vPktCap_ = 256;
    aFrmCap_ = 64;
    vFrmCap_ = 32;

    clock_.reset(new AXClock());
    clock_->setSpeed(speed_);

    DemuxResult info;
    if (!demux_->open(source_, headers_, info)) {
        notifyError(AXERR_SOURCE_OPEN, -1, "open source failed");
        stopPipelines_();
        return;
    }
    if (info.audioStream < 0 && info.videoStream < 0) {
        notifyError(AXERR_NO_STREAM, -1, "no audio/video stream");
        stopPipelines_();
        return;
    }

    durationMs_ = info.durationUs / 1000;
    videoW_ = info.width;
    videoH_ = info.height;
    sarNum_ = info.sarNum;
    sarDen_ = info.sarDen;

    aStreamIdx_ = info.audioStream;
    vStreamIdx_ = info.videoStream;

    bool audioOk = false, videoOk = false;
    if (info.audioStream >= 0) {
        aDec_.reset(new AXDecoder());
        auto st = demux_->fmt()->streams[info.audioStream];
        if (!aDec_->open(st->codecpar, info.aTimeBase, false)) {
            notifyError(AXERR_DECODER_OPEN, st->codecpar->codec_id, "open audio decoder failed");
            aDec_.reset();
        } else {
            aDec_->setPacketQueue(aPktQ_.get());
            aDec_->setFrameQueue(aFrmQ_.get());
            audioOk = true;
        }
    }
    if (info.videoStream >= 0) {
        vDec_.reset(new AXDecoder());
        auto st = demux_->fmt()->streams[info.videoStream];
        if (!vDec_->open(st->codecpar, info.vTimeBase, true)) {
            notifyError(AXERR_DECODER_OPEN, st->codecpar->codec_id, "open video decoder failed");
            vDec_.reset();
        } else {
            vDec_->setPacketQueue(vPktQ_.get());
            vDec_->setFrameQueue(vFrmQ_.get());
            videoOk = true;
        }
    }
    if (!audioOk && !videoOk) {
        notifyError(AXERR_DECODER_OPEN, -1, "no decoder available");
        stopPipelines_();
        return;
    }

    vRen_.reset(new AXVideoRenderer());
    if (window_ && !vRen_->init(window_, videoW_, videoH_, sarNum_, sarDen_)) {
//        notifyError(AXERR_RENDER, -1, "video renderer init failed");
        AX_LOGE("video renderer init failed");
        // 不中断：允许纯音频播放
    }
    aRen_.reset(new AXAudioRenderer());
    if (aDec_) {
        aRen_->setFrameQueue(aFrmQ_.get());
        aRen_->setTimeBase(aDec_->timeBase());
        aRen_->setSpeed(speed_);
        aRen_->setVolume(volL_, volR_);
        if (!aRen_->init()) {
            AX_LOGW("audio renderer init failed");
        }
    }

    // 视频时间基传给渲染器（即便当前无窗口也可先设置）
    if (vDec_ && vRen_) {
        vRen_->setTimeBase(vDec_->timeBase());
    }

    if (cb_) {
        cb_->onVideoSizeChanged(videoW_, videoH_, sarNum_, sarDen_);
        cb_->onPrepared();
    }
    changeState(State::PREPARED);

    demux_->start(aPktQ_.get(), vPktQ_.get());
    if (aDec_) aDec_->start();
    if (vDec_) vDec_->start();

    prepared_.store(true);
    cvReady_.notify_all();

    AX_LOGI("ioThread prepared");
}

void AXPlayer::playThreadLoop() {
    JniThreadScope jscope;
    AX_LOGI("playThread start");

    // === 等待 prepared：支持被 abort_ 打断 ===
    {
        std::unique_lock<std::mutex> lk(prepMtx_);
        cvReady_.wait(lk, [this]{ return abort_.load() || prepared_.load(); });
    }
    if (abort_.load()) {
        AX_LOGI("playThread exit early (aborted before prepared)");
        return;
    }

    // === 渲染器一次性绑定帧队列/时间基（避免循环内重复设置） ===
    if (vRen_) {
        vRen_->setFrameQueue(vFrmQ_.get());             // ★ 必须给视频渲染器队列
        if (vDec_) vRen_->setTimeBase(vDec_->timeBase());
    }
    if (aRen_) {
        aRen_->setFrameQueue(aFrmQ_.get());
        if (aDec_) aRen_->setTimeBase(aDec_->timeBase());
    }

    // 确保 clock_ 存在
    if (!clock_) {
        clock_.reset(new AXClock());
        clock_->setSpeed(speed_);
    }

    bool    completedNotified = false;
    int64_t lastBufCbMs       = 0;  // 上次缓冲回调时间（ms）

    while (!abort_.load()) {
        if (!playing_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // ==== 主时钟选择：优先用“已到达DAC”的音频时钟 ====
        int64_t masterUs = 0;
        // 用 auto* 避免命名空间拼写问题
        auto* aRen = aRen_.get();
        auto* vRen = vRen_.get();

        if (aRen) {
            const int64_t audioPlayedUs = aRen->lastRenderedPtsUs();   // -1 表示还没基准
            if (audioPlayedUs >= 0) {
                const int64_t curUs = clock_->ptsUs();
                // 对齐阈值 5ms，避免抖动
                if (std::llabs(audioPlayedUs - curUs) > 5000) {
                    const float sp       = speed_;
                    const bool  wasPause = !playing_.load();
                    clock_->reset(audioPlayedUs);
                    clock_->setSpeed(sp);
                    clock_->pause(wasPause);
                }
                masterUs = clock_->ptsUs();
            } else {
                // 音频未就绪：先用外部时钟
                masterUs = clock_->ptsUs();
            }
        } else {
            // 无音频：外部时钟
            masterUs = clock_->ptsUs();
        }

        positionMs_.store(masterUs / 1000);

        // ==== 渲染 ====
        if (vRen) vRen->drawLoopOnce(masterUs);
        if (aRen) aRen->renderOnce(masterUs);

        // ==== 缓冲进度：每 500ms 回调一次 ====
        const int64_t now = nowMs();
        if (cb_ && (now - lastBufCbMs >= 500)) {
            int cap = 0, sz = 0;
            if (aPktQ_) { cap += aPktCap_; sz += (int)aPktQ_->size(); }
            if (vPktQ_) { cap += vPktCap_; sz += (int)vPktQ_->size(); }
            if (cap > 0) {
                int percent = (int)((100LL * sz) / cap);
                if (percent < 0)   percent = 0;
                if (percent > 100) percent = 100;
                cb_->onBuffering(percent);
            }
            lastBufCbMs = now;
        }

        // ==== 完成判定：EOF 且两侧帧队列为空 ====
        const bool framesEmpty =
                (!vFrmQ_ || vFrmQ_->size() == 0) &&
                (!aFrmQ_ || aFrmQ_->size() == 0);

        if (!completedNotified && framesEmpty && demux_ && demux_->isEof()) {
            completedNotified = true;
            playing_.store(false);
            changeState(State::COMPLETED);
            if (cb_) cb_->onCompletion();
            AX_LOGI("onCompletion notified");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    AX_LOGI("playThread exit");
}