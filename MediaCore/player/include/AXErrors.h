//
// Created by admin on 2025/9/18.
//

#ifndef AXPLAYERLIB_AXERRORS_H
#define AXPLAYERLIB_AXERRORS_H
#pragma once
#include <string>
extern "C" {
#include <libavutil/error.h>
}

// what：错误大类；extra：细节（通常放 FFmpeg 的负错误码或 codec_id）
enum AXErrWhat {
    AXERR_UNKNOWN      = -1,
    AXERR_SOURCE_OPEN  = 1,   // 打开输入失败
    AXERR_STREAM_INFO  = 2,   // 读取流信息失败
    AXERR_NO_STREAM    = 3,   // 找不到音/视频流
    AXERR_DECODER_OPEN = 4,   // 解码器打开失败
    AXERR_DEMUX        = 5,   // 读包失败/中断
    AXERR_DECODE       = 6,   // 解码异常
    AXERR_RENDER       = 7,   // 渲染初始化失败
    AXERR_INTERNAL     = 99,  // 内部状态机/线程错误
};

inline std::string fferr2str(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return std::string(buf);
}
#endif //AXPLAYERLIB_AXERRORS_H
