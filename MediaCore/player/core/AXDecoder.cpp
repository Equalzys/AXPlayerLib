//
// Created by admin on 2025/9/18.
//

#include "AXDecoder.h"
#include <android/log.h>
#include <thread>
#include <chrono>

AXDecoder::AXDecoder() {}
AXDecoder::~AXDecoder() {
    stop();
    if (ctx_) {
        avcodec_free_context(&ctx_);
    }
}

bool AXDecoder::open(AVCodecParameters* par, AVRational timeBase, bool isVideo) {
    isVideo_ = isVideo;
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) { AX_LOGE("no decoder for codec_id=%d", par->codec_id); return false; }
    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) return false;
    int ret = avcodec_parameters_to_context(ctx_, par);
    if (ret < 0) return false;
    ctx_->pkt_timebase = timeBase;
    tb_ = timeBase;
    // 低时延/稳定性参数
    // 线程并行：让 FFmpeg 自动挑选线程数（0 = auto）
    ctx_->thread_count = 0;
    // 选择线程类型（帧/切片），根据解码器 capabilities 兼容各种版本
    ctx_->thread_type = 0;
    if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
        ctx_->thread_type |= FF_THREAD_FRAME;
    }
    if (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) {
        ctx_->thread_type |= FF_THREAD_SLICE;
    }
    // 若都不支持则保持 0，让 FFmpeg 走单线程
    // TODO: 硬解路径在此选择 MediaCodec（另行实现）
    if ((ret = avcodec_open2(ctx_, codec, nullptr)) < 0) {
        AX_LOGE("avcodec_open2 fail: %d", ret);
        return false;
    }
    return true;
}

void AXDecoder::start() {
    abort_.store(false);
    th_ = std::thread(&AXDecoder::loop_, this);
}

void AXDecoder::stop() {
    abort_.store(true);
    if (th_.joinable()) th_.join();
}

void AXDecoder::flush() {
    if (ctx_) avcodec_flush_buffers(ctx_);
    if (frmQ_) frmQ_->flush();
    if (pktQ_) pktQ_->flush();
}

void AXDecoder::loop_() {
    while (!abort_.load()) {
        if (!pktQ_ || !frmQ_) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        AVPacket* pkt = nullptr;
        if (!pktQ_->pop(pkt)) break; // aborted
        if (!pkt) continue;
        if (pkt->data == nullptr && pkt->size == 0) {
            // eos: 送空包冲刷
            avcodec_send_packet(ctx_, nullptr);
            AVFrame* frm = av_frame_alloc();
            while (avcodec_receive_frame(ctx_, frm) == 0) {
                frmQ_->push(frm);
                frm = av_frame_alloc();
            }
            av_frame_free(&frm);
            av_packet_free(&pkt);
            break;
        }

        int ret = avcodec_send_packet(ctx_, pkt);
        av_packet_free(&pkt);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            AX_LOGW("send_packet ret=%d", ret);
        }
        while (ret >= 0 && !abort_.load()) {
            AVFrame* frm = av_frame_alloc();
            ret = avcodec_receive_frame(ctx_, frm);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) { av_frame_free(&frm); break; }
            if (ret < 0) { AX_LOGW("receive_frame=%d", ret); av_frame_free(&frm); break; }
            // 成功：push
            if (!frmQ_->push(frm)) { av_frame_free(&frm); return; }
        }
    }
}
