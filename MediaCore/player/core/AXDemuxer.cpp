#include "AXDemuxer.h"
#include <android/log.h>
#include "AXErrors.h"


AXDemuxer::AXDemuxer() {}
AXDemuxer::~AXDemuxer() {
    stop();
    if (fmt_) {
        avformat_close_input(&fmt_);
    }
}

static AVDictionary* buildDict(const std::map<std::string,std::string>& headers) {
    AVDictionary* dict = nullptr;
    if (!headers.empty()) {
        std::string h;
        for (auto& kv : headers) {
            h += kv.first + ": " + kv.second + "\r\n";
        }
        av_dict_set(&dict, "headers", h.c_str(), 0);
    }
    // 可选：网络优化参数
    av_dict_set(&dict, "reconnect", "1", 0);
    av_dict_set(&dict, "user_agent", "AXPlayer/1.0", 0);
    // 合理的网络超时（微秒）
    av_dict_set(&dict, "timeout", "8000000", 0); // 8s
    return dict;
}

bool AXDemuxer::open(const std::string& url, const std::map<std::string,std::string>& headers, DemuxResult& out) {
    eof_.store(false);

    AVDictionary* dict = buildDict(headers);
    int ret = avformat_open_input(&fmt_, url.c_str(), nullptr, &dict);
    av_dict_free(&dict);
    if (ret < 0) {
        AX_LOGE("open input fail: %d", ret);
        return false;
    }

    // 让 FFmpeg 的阻塞 IO 能响应外部中断
    fmt_->interrupt_callback.callback = [](void* opaque)->int {
        AXDemuxer* self = static_cast<AXDemuxer*>(opaque);
        return (self && self->abort_.load()) ? 1 : 0;
    };
    fmt_->interrupt_callback.opaque = this;

    if ((ret = avformat_find_stream_info(fmt_, nullptr)) < 0) {
        AX_LOGE("find_stream_info fail: %d", ret);
        return false;
    }

    aIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    vIdx_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (aIdx_ < 0 && vIdx_ < 0) {
        AX_LOGE("no a/v stream");
        return false;
    }

    out.audioStream = aIdx_;
    out.videoStream = vIdx_;
    out.durationUs  = (fmt_->duration > 0) ? fmt_->duration * 1000000LL / AV_TIME_BASE : 0;

    if (vIdx_ >= 0) {
        AVStream* st = fmt_->streams[vIdx_];
        out.vTimeBase = st->time_base;
        out.width  = st->codecpar->width;
        out.height = st->codecpar->height;
        if (st->sample_aspect_ratio.num > 0 && st->sample_aspect_ratio.den > 0) {
            out.sarNum = st->sample_aspect_ratio.num;
            out.sarDen = st->sample_aspect_ratio.den;
        }
    }
    if (aIdx_ >= 0) {
        AVStream* st = fmt_->streams[aIdx_];
        out.aTimeBase = st->time_base;
    }
    return true;
}

void AXDemuxer::start(PacketQueue* aQ, PacketQueue* vQ) {
    aQ_ = aQ;
    vQ_ = vQ;
    abort_.store(false);
    eof_.store(false);
    th_ = std::thread(&AXDemuxer::loop_, this);
}

void AXDemuxer::stop() {
    // 先置位中断标志
    abort_.store(true);
    // ★ 同时让队列退出阻塞（防止 push 卡住）
    if (aQ_) aQ_->abort();
    if (vQ_) vQ_->abort();
    // 再等待线程结束
    if (th_.joinable()) th_.join();
}

bool AXDemuxer::seek(int streamIndex, int64_t pts) {
    if (!fmt_) return false;
    eof_.store(false);

    // 将局部 PTS 转为全局时基
    int64_t seekTarget = av_rescale_q(pts, fmt_->streams[streamIndex]->time_base, AV_TIME_BASE_Q);
    int flags = AVSEEK_FLAG_BACKWARD;
    int ret = av_seek_frame(fmt_, -1, seekTarget, flags);
    if (ret < 0) {
        AX_LOGW("seek fail: %d", ret);
        return false;
    }
    // 清除解复用内部缓冲
    avformat_flush(fmt_);
    return true;
}

void AXDemuxer::loop_() {
    while (!abort_.load()) {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt) {
            AX_LOGE("av_packet_alloc fail");
            break;
        }

        int ret = av_read_frame(fmt_, pkt);

        if (ret == AVERROR_EOF) {
            eof_.store(true);
            // 尝试发送 EOF 空包；若队列已 abort，push 会返回 false，我们直接 free
            if (aIdx_ >= 0 && aQ_) {
                AVPacket* ap = av_packet_alloc();
                if (ap) {
                    ap->stream_index = aIdx_;
                    ap->data = nullptr; ap->size = 0;
                    if (!aQ_->push(ap)) av_packet_free(&ap);
                }
            }
            if (vIdx_ >= 0 && vQ_) {
                AVPacket* vp = av_packet_alloc();
                if (vp) {
                    vp->stream_index = vIdx_;
                    vp->data = nullptr; vp->size = 0;
                    if (!vQ_->push(vp)) av_packet_free(&vp);
                }
            }
            av_packet_free(&pkt);
            break;
        }

        if (ret < 0) {
            // 其他错误：如果是中断或临时错误，略过；避免忙等稍微让步
            AX_LOGW("read_frame error: %d", ret);
            av_packet_free(&pkt);
            if (abort_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        bool pushed = false;
        if (pkt->stream_index == aIdx_ && aQ_) {
            pushed = aQ_->push(pkt);
        } else if (pkt->stream_index == vIdx_ && vQ_) {
            pushed = vQ_->push(pkt);
        }

        if (!pushed) {
            // 队列已被 abort 或者其它原因导致 push 失败
            av_packet_free(&pkt);
            if (abort_.load()) break;
            // 如果只是容量满而未 abort，这里不要马上退出；轻微让步
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}