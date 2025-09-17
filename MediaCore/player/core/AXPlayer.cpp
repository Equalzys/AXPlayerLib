#include "../include/AXPlayer.h"
#include <android/log.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <android/native_window.h>
#include <chrono>
#include <thread>

#define AXLOGI(...) __android_log_print(ANDROID_LOG_INFO,  "AXPlayer", __VA_ARGS__)
#define AXLOGE(...) __android_log_print(ANDROID_LOG_ERROR, "AXPlayer", __VA_ARGS__)
#define AXLOGW(...) __android_log_print(ANDROID_LOG_WARN,  "AXPlayer", __VA_ARGS__)

static inline int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

AXPlayer::AXPlayer(std::shared_ptr<AXPlayerCallback> cb) : cb_(std::move(cb)) {
    AXLOGI("AXPlayer ctor");
}

AXPlayer::~AXPlayer() {
    AXLOGI("AXPlayer dtor");
    abort_ = true;
    playing_ = false;

    if (ioThread_.joinable()) ioThread_.join();
    if (playThread_.joinable()) playThread_.join();

    resetCodec_l(true);
}

void AXPlayer::setDataSource(const std::string& urlOrPath, const std::map<std::string, std::string>& headers) {
    source_ = urlOrPath;
    headers_ = headers;
    AXLOGI("setDataSource: %s", source_.c_str());
}

void AXPlayer::prepareAsync() {
    if (state_ != State::IDLE && state_ != State::PAUSED) {
        AXLOGW("prepareAsync in state != IDLE/PAUSED");
    }
    changeState(State::PREPARING);
    abort_ = false;
    preparedNotified_ = false;

    if (ioThread_.joinable()) ioThread_.join();
    ioThread_ = std::thread(&AXPlayer::ioThreadLoop, this);
}

void AXPlayer::start() {
    if (state_ == State::PREPARED || state_ == State::PAUSED) {
        playing_ = true;
        changeState(State::PLAYING);
        if (!playThread_.joinable()) {
            playThread_ = std::thread(&AXPlayer::playThreadLoop, this);
        } else {
            cv_.notify_all();
        }
        AXLOGI("start play");
    } else {
        AXLOGW("start in invalid state");
    }
}

void AXPlayer::pause() {
    playing_ = false;
    changeState(State::PAUSED);
    AXLOGI("pause");
}

void AXPlayer::seekTo(int64_t /*msec*/) {
    // 第1步不实现，可在 FFmpeg 接入后做准确 seek
    AXLOGW("seekTo not implemented in step1");
}

bool AXPlayer::isPlaying() { return playing_.load(); }

void AXPlayer::setSpeed(float speed) { speed_ = speed; }

int64_t AXPlayer::getCurrentPositionMs() { return positionMs_.load(); }

int64_t AXPlayer::getDurationMs() { return durationMs_; }

void AXPlayer::setVolume(float l, float r) { volL_ = l; volR_ = r; }

int AXPlayer::getVideoWidth() { return videoW_; }
int AXPlayer::getVideoHeight() { return videoH_; }
int AXPlayer::getVideoSarNum() { return sarNum_; }
int AXPlayer::getVideoSarDen() { return sarDen_; }
int AXPlayer::getAudioSessionId() { return audioSessionId_; }

void AXPlayer::setWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> lg(wmtx_);
    window_ = window; // 不持有引用，JNI 层管理
    AXLOGI("setWindow: %p", window_);
}

void AXPlayer::changeState(State s) { state_ = s; }

void AXPlayer::postError(int what, int extra, const std::string& msg) {
    changeState(State::ERROR);
    AXLOGE("error: what=%d extra=%d msg=%s", what, extra, msg.c_str());
    if (cb_) cb_->onError(what, extra, msg);
}

void AXPlayer::resetCodec_l(bool releaseExtractor) {
    if (vcodec_) {
        AMediaCodec_stop(vcodec_);
        AMediaCodec_delete(vcodec_);
        vcodec_ = nullptr;
    }
    if (releaseExtractor && extractor_) {
        AMediaExtractor_delete(extractor_);
        extractor_ = nullptr;
    }
    videoTrackIndex_ = -1;
}

bool AXPlayer::setupExtractor_l() {
    resetCodec_l(true);

    extractor_ = AMediaExtractor_new();
    if (!extractor_) { postError(-100, -1, "AMediaExtractor_new failed"); return false; }

    media_status_t r;
    // 小步就地先做 URL/path 直开；Headers/ContentResolver 后续接入
    r = AMediaExtractor_setDataSource(extractor_, source_.c_str());
    if (r != AMEDIA_OK) {
        postError(-101, r, "setDataSource failed");
        return false;
    }

    const size_t trackCnt = AMediaExtractor_getTrackCount(extractor_);
    for (size_t i = 0; i < trackCnt; ++i) {
        AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor_, i);
        const char* mime = nullptr;
        if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && mime) {
            if (strncmp(mime, "video/", 6) == 0) {
                videoTrackIndex_ = static_cast<int>(i);
                int32_t w=0,h=0;
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
                AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
                videoW_ = w; videoH_ = h;
                AXLOGI("select video track %d %dx%d mime=%s", videoTrackIndex_, w, h, mime);
                AMediaFormat_delete(fmt);
                break;
            }
        }
        AMediaFormat_delete(fmt);
    }

    if (videoTrackIndex_ < 0) {
        postError(-102, -1, "no video track");
        return false;
    }
    AMediaExtractor_selectTrack(extractor_, (size_t)videoTrackIndex_);
    return true;
}

bool AXPlayer::setupVideoCodec_l() {
    if (!window_) { postError(-103, -1, "window null"); return false; }

    AMediaFormat* vfmt = AMediaExtractor_getTrackFormat(extractor_, (size_t)videoTrackIndex_);
    const char* mime = nullptr;
    if (!AMediaFormat_getString(vfmt, AMEDIAFORMAT_KEY_MIME, &mime) || !mime) {
        AMediaFormat_delete(vfmt);
        postError(-104, -1, "video mime null");
        return false;
    }

    vcodec_ = AMediaCodec_createDecoderByType(mime);
    if (!vcodec_) {
        AMediaFormat_delete(vfmt);
        postError(-105, -1, "createDecoderByType failed");
        return false;
    }

    media_status_t r = AMediaCodec_configure(vcodec_, vfmt, window_, nullptr, 0);
    AMediaFormat_delete(vfmt);
    if (r != AMEDIA_OK) {
        postError(-106, r, "AMediaCodec_configure failed");
        return false;
    }

    r = AMediaCodec_start(vcodec_);
    if (r != AMEDIA_OK) {
        postError(-107, r, "AMediaCodec_start failed");
        return false;
    }

    AXLOGI("video codec started");
    return true;
}

void AXPlayer::ioThreadLoop() {
    AXLOGI("ioThreadLoop begin");
    if (!setupExtractor_l()) return;
    if (!setupVideoCodec_l()) return;

    // 通知上层已准备好
    changeState(State::PREPARED);
    if (!preparedNotified_.exchange(true) && cb_) {
        cb_->onPrepared();
    }
    AXLOGI("ioThreadLoop prepared");
}

void AXPlayer::playThreadLoop() {
    AXLOGI("playThreadLoop begin");
    if (!vcodec_) { AXLOGW("no codec in playThread"); return; }

    // 简单的喂入/拉取循环：视频-only，不做复杂同步
    bool inputEOS = false;
    bool outputEOS = false;
    int64_t startMs = nowMs();

    while (!abort_) {
        if (!playing_) {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(50));
            continue;
        }

        // input
        if (!inputEOS) {
            ssize_t idx = AMediaCodec_dequeueInputBuffer(vcodec_, 10 * 1000);
            if (idx >= 0) {
                size_t bufSize = 0;
                uint8_t* buf = AMediaCodec_getInputBuffer(vcodec_, (size_t)idx, &bufSize);
                if (buf) {
                    AMediaCodecBufferInfo info{};
                    int sampleSize = AMediaExtractor_readSampleData(extractor_, buf, bufSize);
                    int64_t pts = AMediaExtractor_getSampleTime(extractor_);
                    if (sampleSize < 0) {
                        AMediaCodec_queueInputBuffer(vcodec_, (size_t)idx, 0, 0, 0,
                                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputEOS = true;
                    } else {
                        AMediaCodec_queueInputBuffer(vcodec_, (size_t)idx, 0,
                                                     (size_t)sampleSize, (uint64_t)pts, 0);
                        AMediaExtractor_advance(extractor_);
                    }
                }
            }
        }

        // output
        if (!outputEOS) {
            AMediaCodecBufferInfo info{};
            ssize_t oidx = AMediaCodec_dequeueOutputBuffer(vcodec_, &info, 10 * 1000);
            if (oidx >= 0) {
                bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
                // 简单基于 pts 的节拍：尽量避免跑太快（非严格同步）
                if (info.presentationTimeUs > 0) {
                    int64_t elapsed = nowMs() - startMs;
                    int64_t target = info.presentationTimeUs / 1000;
                    if (target > elapsed + 3) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(target - elapsed));
                    }
                    positionMs_ = target;
                }
                AMediaCodec_releaseOutputBuffer(vcodec_, (size_t)oidx, true /*render*/);

                if (eos) {
                    outputEOS = true;
                    playing_ = false;
                    changeState(State::COMPLETED);
                    if (cb_) cb_->onCompletion();
                    AXLOGI("playback complete");
                    break;
                }
            } else if (oidx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* newFmt = AMediaCodec_getOutputFormat(vcodec_);
                int32_t w=0,h=0;
                AMediaFormat_getInt32(newFmt, AMEDIAFORMAT_KEY_WIDTH, &w);
                AMediaFormat_getInt32(newFmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
                videoW_ = w; videoH_ = h;
                AXLOGI("output format changed: %dx%d", w, h);
                if (cb_) cb_->onVideoSizeChanged(w, h, sarNum_, sarDen_);
                AMediaFormat_delete(newFmt);
            }
        }
    }

    AXLOGI("playThreadLoop end");
}