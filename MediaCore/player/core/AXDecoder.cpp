#include "AXDecoder.h"
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

    // 记录时间基（pkt 与输出帧用）
    ctx_->pkt_timebase = timeBase;
    tb_ = timeBase;

    // 线程并行（FFmpeg内部自动优化）
    ctx_->thread_count = 0;
    ctx_->thread_type  = 0;
#ifdef FF_THREAD_FRAME
    if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
        ctx_->thread_type |= FF_THREAD_FRAME;
#endif
#ifdef FF_THREAD_SLICE
    if (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)
        ctx_->thread_type |= FF_THREAD_SLICE;
#endif

    // TODO: 硬解可在此切到 MediaCodec（另行实现）

    if ((ret = avcodec_open2(ctx_, codec, nullptr)) < 0) {
        AX_LOGE("avcodec_open2 fail: %d", ret);
        return false;
    }
    return true;
}

void AXDecoder::start() {
    abort_.store(false);
    // 若已在跑，直接返回（防呆；通常上层会新建实例）
    if (th_.joinable()) return;
    th_ = std::thread(&AXDecoder::loop_, this);
}

void AXDecoder::stop() {
    abort_.store(true);
    if (pktQ_) pktQ_->abort();   // 唤醒 pop() 退出
    if (frmQ_) frmQ_->abort();   // 唤醒 push() 返回 false
    if (th_.joinable()) th_.join();
}

void AXDecoder::flush() {
    if (ctx_) avcodec_flush_buffers(ctx_);
    if (frmQ_) frmQ_->flush();
    if (pktQ_) pktQ_->flush();
}

bool AXDecoder::safePushFrame_(AVFrame* frm) {
    if (!frmQ_) {
        av_frame_free(&frm);
        return false;
    }
    if (!frmQ_->push(frm)) {
        av_frame_free(&frm);
        return false;
    }
    return true;
}

void AXDecoder::loop_() {
    AVFrame* frame = av_frame_alloc();
    if (!frame) return;

    while (!abort_.load()) {
        if (!pktQ_ || !frmQ_) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }

        AVPacket* pkt = nullptr;
        if (!pktQ_->pop(pkt)) break; // 队列被 abort

        if (!pkt) continue;

        // 空包：表示 demux EOF，送 NULL packet 触发冲刷
        if (pkt->data == nullptr && pkt->size == 0) {
            av_packet_free(&pkt);

            // drain 剩余帧
            int ret = 0;
            ret = avcodec_send_packet(ctx_, nullptr);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                AX_LOGW("send_packet(NULL) ret=%d", ret);
            }

            while (!abort_.load()) {
                ret = avcodec_receive_frame(ctx_, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                if (ret < 0) { AX_LOGW("receive_frame ret=%d on drain", ret); break; }

                // 交给渲染/上层
                AVFrame* out = av_frame_clone(frame);
                if (!out) { AX_LOGE("av_frame_clone OOM"); break; }
                if (!safePushFrame_(out)) { // 队列 abort
                    break;
                }
                av_frame_unref(frame);
            }
            break; // EOF 后退出解码线程
        }

        // 常规包
        int ret = avcodec_send_packet(ctx_, pkt);
        av_packet_free(&pkt);

        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            AX_LOGW("send_packet ret=%d", ret);
        }

        // 尽量把可取的帧都取出来（避免缓存积压）
        while (!abort_.load()) {
            ret = avcodec_receive_frame(ctx_, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                AX_LOGW("receive_frame ret=%d", ret);
                break;
            }

            AVFrame* out = av_frame_clone(frame);
            if (!out) { AX_LOGE("av_frame_clone OOM"); break; }
            if (!safePushFrame_(out)) {
                // 队列已 abort，直接退出线程
                abort_.store(true);
                break;
            }
            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
}