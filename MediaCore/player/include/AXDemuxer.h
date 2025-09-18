//
// Created by admin on 2025/9/18.
//

#ifndef AXPLAYERLIB_AXDEMUXER_H
#define AXPLAYERLIB_AXDEMUXER_H


#pragma once
#include <string>
#include <map>
#include <thread>
#include <atomic>
#include "AXQueues.h"

extern "C" {
#include <libavformat/avformat.h>
}

struct DemuxResult {
    int audioStream{-1};
    int videoStream{-1};
    int64_t durationUs{0};
    AVRational aTimeBase{1,1000};
    AVRational vTimeBase{1,1000};
    int width{0}, height{0};
    int sarNum{1}, sarDen{1};
};

class AXDemuxer {
public:
    AXDemuxer();
    ~AXDemuxer();

    bool open(const std::string& url, const std::map<std::string, std::string>& headers, DemuxResult& out);
    void start(PacketQueue* aQ, PacketQueue* vQ);
    void stop();
    bool seek(int streamIndex, int64_t pts); // pts in stream timebase
    AVFormatContext* fmt() const { return fmt_; }
    AVRational tb(int idx) const { return fmt_ ? fmt_->streams[idx]->time_base : AVRational{1,1000}; }

private:
    void loop_();

    AVFormatContext* fmt_{nullptr};
    std::thread th_;
    std::atomic<bool> abort_{false};
    PacketQueue* aQ_{nullptr};
    PacketQueue* vQ_{nullptr};
    int aIdx_{-1}, vIdx_{-1};
};


#endif //AXPLAYERLIB_AXDEMUXER_H
