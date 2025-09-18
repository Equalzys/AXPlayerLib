#include "AXPlayer.h"                 // 项目内相对包含（不要再用 ../include/AXPlayer.h）
#include <android/log.h>
#include <android/native_window.h>
#include <jni.h>
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "AXDemuxer.h"
#include "AXDecoder.h"
#include "AXVideoRenderer.h"
#include "AXAudioRenderer.h"
#include "AXClock.h"
#include "AXQueues.h"
#include "AXErrors.h"

extern "C" {
#include <libavformat/avformat.h>     // AVFormatContext / av_rescale_q 依赖
#include <libavutil/avutil.h>
}

// 统一日志宏（兼容历史写法）
#define AXLOGI(...) __android_log_print(ANDROID_LOG_INFO,  "AXPlayer", __VA_ARGS__)
#define AXLOGW(...) __android_log_print(ANDROID_LOG_WARN,  "AXPlayer", __VA_ARGS__)
#define AXLOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AXPlayer", __VA_ARGS__)

static inline int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ===== JavaVM 线程附着工具 =====

// 静态 JavaVM 保存（定义）
JavaVM* AXPlayer::sVm = nullptr;

// 静态设置/获取
void AXPlayer::SetJavaVM(JavaVM* vm) { AXPlayer::sVm = vm; }
JavaVM* AXPlayer::GetJavaVM()        { return AXPlayer::sVm; }

// 线程作用域：进入线程时 attach，退出时 detach
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

// ===== AXPlayer 实现 =====

AXPlayer::AXPlayer(std::shared_ptr<AXPlayerCallback> cb) : cb_(std::move(cb)) {
    AXLOGI("AXPlayer ctor");
}

AXPlayer::~AXPlayer() {
    AXLOGI("AXPlayer dtor: begin");
    abort_.store(true);
    if (ioThread_.joinable())   ioThread_.join();
    if (playThread_.joinable()) playThread_.join();

    // 安全释放窗口（如果 AXPlayer 持有）
    {
        std::lock_guard<std::mutex> lk(wmtx_);
        if (window_) {
            // 如果在 setWindow() 里通过 ANativeWindow_acquire 接管了，需要这里 release
            // ANativeWindow_release(window_);
            window_ = nullptr;
        }
    }
    AXLOGI("AXPlayer dtor: done");
}

void AXPlayer::setDataSource(const std::string& urlOrPath, const std::map<std::string, std::string>& headers) {
    source_  = urlOrPath;
    headers_ = headers;
    AXLOGI("setDataSource: %s", source_.c_str());
}

void AXPlayer::prepareAsync() {
    if (state_ != State::IDLE && state_ != State::STOPPED) {
        AXLOGW("prepareAsync called in wrong state");
        return;
    }
    changeState(State::PREPARING);
    abort_.store(false);
    ioThread_ = std::thread(&AXPlayer::ioThreadLoop, this);
}

void AXPlayer::start() {
    if (state_ == State::PREPARED || state_ == State::PAUSED || state_ == State::COMPLETED) {
        playing_.store(true);
        if (!playThread_.joinable())
            playThread_ = std::thread(&AXPlayer::playThreadLoop, this);
        if (clock_) clock_->pause(false);
        changeState(State::PLAYING);
        AXLOGI("start");
    } else {
        AXLOGW("start ignored: state=%d", (int)state_.load());
    }
}

void AXPlayer::pause() {
    AXLOGI("pause");
    playing_.store(false);
    if (clock_) clock_->pause(true);
    changeState(State::PAUSED);
}

void AXPlayer::seekTo(int64_t msec) {
    AXLOGI("seekTo: %lld ms", (long long)msec);
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

    // ms -> stream timebase
    int64_t pts = av_rescale_q(msec, AVRational{1,1000}, tb);

    // flush 队列与解码器
    if (aDec_) aDec_->flush();
    if (vDec_) vDec_->flush();
    if (aPktQ_) aPktQ_->flush();
    if (vPktQ_) vPktQ_->flush();
    if (aFrmQ_) aFrmQ_->flush();
    if (vFrmQ_) vFrmQ_->flush();

    // demux seek
    demux_->seek(targetStream, pts);

    // 时钟重置
    if (clock_) clock_->reset(msec * 1000);
}

bool AXPlayer::isPlaying() { return playing_.load(); }
void AXPlayer::setSpeed(float speed) { speed_ = speed; if (clock_) clock_->setSpeed(speed); }
int64_t AXPlayer::getCurrentPositionMs() { return positionMs_.load(); }
int64_t AXPlayer::getDurationMs() { return durationMs_; }
void AXPlayer::setVolume(float l, float r) { volL_ = l; volR_ = r; /* TODO: 传给音频渲染 */ }
int AXPlayer::getVideoWidth() { return videoW_; }
int AXPlayer::getVideoHeight() { return videoH_; }
int AXPlayer::getVideoSarNum() { return sarNum_; }
int AXPlayer::getVideoSarDen() { return sarDen_; }
int AXPlayer::getAudioSessionId() { return audioSessionId_; }

void AXPlayer::setWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lk(wmtx_);
    // 如果 JNI 侧释放了 fromSurface 的引用，这里可采用 acquire 接管所有权
    // if (window) ANativeWindow_acquire(window);
    window_ = window;
    if (vRen_) vRen_->init(window_, videoW_, videoH_, sarNum_, sarDen_);
}

void AXPlayer::changeState(State s) { state_.store(s); }

void AXPlayer::notifyError(int what, int extra, const std::string& msg) {
    changeState(State::ERROR);
    playing_.store(false);
    AXLOGE("notifyError what=%d extra=%d msg=%s", what, extra, msg.c_str());
    if (cb_) cb_->onError(what, extra, msg);
}

void AXPlayer::ioThreadLoop() {
    JniThreadScope jscope;
    AXLOGI("ioThread start");

    demux_.reset(new AXDemuxer());
    aPktQ_.reset(new PacketQueue(256));
    vPktQ_.reset(new PacketQueue(256));
    aFrmQ_.reset(new FrameQueue(64));
    vFrmQ_.reset(new FrameQueue(32));
    clock_.reset(new AXClock());

    DemuxResult info;
    if (!demux_->open(source_, headers_, info)) {
        notifyError(AXERR_SOURCE_OPEN, -1, "open source failed");
        return;
    }
    if (info.audioStream < 0 && info.videoStream < 0) {
        notifyError(AXERR_NO_STREAM, -1, "no audio/video stream");
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
        return;
    }

    vRen_.reset(new AXVideoRenderer());
    aRen_.reset(new AXAudioRenderer());
    if (window_ && !vRen_->init(window_, videoW_, videoH_, sarNum_, sarDen_)) {
        notifyError(AXERR_RENDER, -1, "video renderer init failed");
        // 不中断：允许纯音频播放
    }
    if (aDec_ && aDec_->ctx()) {
        if (!aRen_->init(aDec_->ctx()->sample_rate, aDec_->ctx()->ch_layout.nb_channels, aDec_->ctx()->sample_fmt)) {
            AXLOGW("audio renderer init failed");
        }
    }

    // 上层回调：prepared + 视频尺寸
    if (cb_) {
        cb_->onVideoSizeChanged(videoW_, videoH_, sarNum_, sarDen_);
        cb_->onPrepared();
    }
    changeState(State::PREPARED);

    // 启动解复用与解码线程
    demux_->start(aPktQ_.get(), vPktQ_.get());
    if (aDec_) aDec_->start();
    if (vDec_) vDec_->start();

    AXLOGI("ioThread prepared");
}

void AXPlayer::playThreadLoop() {
    JniThreadScope jscope;
    AXLOGI("playThread start");
    while (!abort_.load()) {
        if (!playing_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        // 主时钟：优先音频
        int64_t masterUs = clock_ ? clock_->ptsUs() : 0;
        positionMs_.store(masterUs / 1000);

        // 视频渲染（被动拉）
        if (vRen_) vRen_->setFrameQueue(vFrmQ_.get());
        if (vRen_) vRen_->drawLoopOnce(masterUs);

        // 音频渲染（占位，后续接 OpenSL ES/AAudio 回调）
        if (aRen_) aRen_->setFrameQueue(aFrmQ_.get());
        if (aRen_) aRen_->renderOnce();

        // TODO: 根据队列水位/网络状态回调 onBuffering
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    AXLOGI("playThread exit");
}