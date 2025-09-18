//AXPlayerLib/MediaCore/player/include/AXDecoder.h

#ifndef AXPLAYERLIB_AXDECODER_H
#define AXPLAYERLIB_AXDECODER_H


#pragma once
#include "AXQueues.h"
#include <thread>

#define AX_LOG_TAG "AXDecoder"
#include "AXLog.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

class AXDecoder {
public:
    AXDecoder();
    ~AXDecoder();

    bool open(AVCodecParameters* par, AVRational timeBase, bool isVideo);
    void setPacketQueue(PacketQueue* q) { pktQ_ = q; }
    void setFrameQueue(FrameQueue* q) { frmQ_ = q; }
    void start();
    void stop();
    void flush();

    AVRational timeBase() const { return tb_; }
    AVCodecContext* ctx() const { return ctx_; }
    bool isVideo() const { return isVideo_; }

private:
    void loop_();
    bool safePushFrame_(AVFrame* frm);

    AVCodecContext* ctx_{nullptr};
    AVRational tb_{1,1000};
    PacketQueue* pktQ_{nullptr};
    FrameQueue*  frmQ_{nullptr};
    std::thread th_;
    std::atomic<bool> abort_{false};
    bool isVideo_{false};

};


#endif //AXPLAYERLIB_AXDECODER_H
