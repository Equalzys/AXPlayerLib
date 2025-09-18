//
// Created by admin on 2025/9/18.
//

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
    return dict;
}

bool AXDemuxer::open(const std::string& url, const std::map<std::string,std::string>& headers, DemuxResult& out) {
    AVDictionary* dict = buildDict(headers);
    int ret = avformat_open_input(&fmt_, url.c_str(), nullptr, &dict);
    av_dict_free(&dict);
    if (ret < 0) {
        AX_LOGE("open input fail: %d", ret);
        return false;
    }
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
        auto st = fmt_->streams[vIdx_];
        out.vTimeBase = st->time_base;
        out.width  = st->codecpar->width;
        out.height = st->codecpar->height;
        if (st->sample_aspect_ratio.num > 0 && st->sample_aspect_ratio.den > 0) {
            out.sarNum = st->sample_aspect_ratio.num;
            out.sarDen = st->sample_aspect_ratio.den;
        }
    }
    if (aIdx_ >= 0) {
        auto st = fmt_->streams[aIdx_];
        out.aTimeBase = st->time_base;
    }
    return true;
}

void AXDemuxer::start(PacketQueue* aQ, PacketQueue* vQ) {
    aQ_ = aQ; vQ_ = vQ;
    abort_.store(false);
    th_ = std::thread(&AXDemuxer::loop_, this);
}

void AXDemuxer::stop() {
    abort_.store(true);
    if (th_.joinable()) th_.join();
}

bool AXDemuxer::seek(int streamIndex, int64_t pts) {
    if (!fmt_) return false;
    // 转成全局时间基
    int64_t seekTarget = av_rescale_q(pts, fmt_->streams[streamIndex]->time_base, AV_TIME_BASE_Q);
    int ret = av_seek_frame(fmt_, -1, seekTarget, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        AX_LOGW("seek fail: %d", ret);
        return false;
    }
    // flush demux queues 在上层处理（调用方清空 PacketQueue/Decoder）
    return true;
}

void AXDemuxer::loop_() {
    while (!abort_.load()) {
        AVPacket* pkt = av_packet_alloc();
        int ret = av_read_frame(fmt_, pkt);
        if (ret == AVERROR_EOF) {
            // 发送空包通知结束
            if (aIdx_ >= 0) {
                AVPacket* ap = av_packet_alloc(); ap->stream_index = aIdx_; ap->data = nullptr; ap->size = 0;
                aQ_->push(ap);
            }
            if (vIdx_ >= 0) {
                AVPacket* vp = av_packet_alloc(); vp->stream_index = vIdx_; vp->data = nullptr; vp->size = 0;
                vQ_->push(vp);
            }
            av_packet_free(&pkt);
            break;
        }
        if (ret < 0) {
            AX_LOGW("read_frame error: %d", ret);
            av_packet_free(&pkt);
            continue;
        }
        if (pkt->stream_index == aIdx_ && aQ_) {
            if (!aQ_->push(pkt)) { av_packet_free(&pkt); break; }
        } else if (pkt->stream_index == vIdx_ && vQ_) {
            if (!vQ_->push(pkt)) { av_packet_free(&pkt); break; }
        } else {
            av_packet_free(&pkt);
        }
    }
}
