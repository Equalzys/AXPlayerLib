//文件路径：AXPlayerLib/MediaCore/player/core/AXPlayer.cpp
#include "AXPlayer.h"
#include <chrono>

AXPlayer::AXPlayer(std::shared_ptr<AXPlayerCallback> cb) : cb_(std::move(cb)) {
    // TODO: 初始化全局 FFmpeg、日志等（只需一次，可做静态计数）
}

AXPlayer::~AXPlayer() {
    abort_ = true;
    {
        std::unique_lock<std::mutex> lk(ctrlMtx_);
        ctrlCv_.notify_all();
    }
    if (ioThread_.joinable()) ioThread_.join();
    if (playThread_.joinable()) playThread_.join();

    // TODO: 关闭解码器、释放帧队列、关闭 demux、关闭音频输出/视频上下文
}

void AXPlayer::setDataSource(const std::string& urlOrPath,
                             const std::map<std::string, std::string>& headers) {
    source_ = urlOrPath;
    headers_ = headers;
    changeState(State::STOPPED);
}

void AXPlayer::prepareAsync() {
    if (state_ == State::PREPARING || source_.empty()) return;
    changeState(State::PREPARING);

    // TODO: 打开输入、读取流信息、选择最佳流、打开解码器、读取时长/分辨率/SAR
    // 占位: 假定 1920x1080、SAR 1:1、时长 2 分钟
    videoW_ = 1920; videoH_ = 1080;
    sarNum_ = 1;    sarDen_ = 1;
    durationMs_ = 2 * 60 * 1000;

    // 启动读写线程（后续真正填充）
    abort_ = false;
    ioThread_ = std::thread(&AXPlayer::ioThreadLoop, this);
    playThread_ = std::thread(&AXPlayer::playThreadLoop, this);

    changeState(State::PREPARED);
    if (cb_) cb_->onPrepared();
    if (cb_) cb_->onVideoSizeChanged(videoW_, videoH_, sarNum_, sarDen_);
}

void AXPlayer::start() {
    if (state_ == State::PREPARED || state_ == State::PAUSED || state_ == State::COMPLETED) {
        playing_ = true;
        changeState(State::PLAYING);
        std::unique_lock<std::mutex> lk(ctrlMtx_);
        ctrlCv_.notify_all();
    }
}

void AXPlayer::pause() {
    if (state_ == State::PLAYING) {
        playing_ = false;
        changeState(State::PAUSED);
    }
}

void AXPlayer::seekTo(int64_t msec) {
    // TODO: FFmpeg seek，刷新解码队列
    positionMs_ = msec;
}

bool AXPlayer::isPlaying() {
    return playing_.load();
}

void AXPlayer::setSpeed(float speed) {
    // TODO: AudioTrack + SoundTouch/soxr 时间拉伸，视频时钟同步
    if (speed <= 0.01f) speed = 0.01f;
    if (speed > 4.0f)   speed = 4.0f;
    speed_ = speed;
}

int64_t AXPlayer::getCurrentPositionMs() {
    return positionMs_.load();
}

int64_t AXPlayer::getDurationMs() {
    return durationMs_;
}

void AXPlayer::setVolume(float left, float right) {
    // TODO: 音频混音或 AudioTrack 音量（注意 API 23 兼容）
    volL_ = left;
    volR_ = right;
}

int AXPlayer::getVideoWidth()  { return videoW_; }
int AXPlayer::getVideoHeight() { return videoH_; }
int AXPlayer::getVideoSarNum() { return sarNum_; }
int AXPlayer::getVideoSarDen() { return sarDen_; }
int AXPlayer::getAudioSessionId() { return audioSessionId_; }

void AXPlayer::setWindow(ANativeWindow* window) {
    std::lock_guard<std::mutex> _l(wmtx_);
    window_ = window;
    // TODO: 依据 window 创建/重建 EGL 上下文、OpenGLES 渲染管线、外部纹理等
}

void AXPlayer::ioThreadLoop() {
    // TODO: 读包、送入队列、更新缓冲百分比
    while (!abort_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // 模拟缓冲
        if (cb_) cb_->onBuffering(100);
    }
}

void AXPlayer::playThreadLoop() {
    auto last = std::chrono::steady_clock::now();
    while (!abort_) {
        // 播放控制：未播放则等待
        if (!playing_) {
            std::unique_lock<std::mutex> lk(ctrlMtx_);
            ctrlCv_.wait_for(lk, std::chrono::milliseconds(50),
                             [&]{ return abort_ || playing_.load(); });
            continue;
        }

        // TODO: 解码音视频帧、同步；音频写入 AudioTrack、视频提交 OpenGL
        auto now = std::chrono::steady_clock::now();
        double dtMs = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;

        // 简单推进时钟
        int64_t inc = static_cast<int64_t>(dtMs * speed_);
        positionMs_ += inc;

        if (durationMs_ > 0 && positionMs_.load() >= durationMs_) {
            playing_ = false;
            changeState(State::COMPLETED);
            if (cb_) cb_->onCompletion();
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void AXPlayer::changeState(State s) {
    state_.store(s, std::memory_order_release);
}