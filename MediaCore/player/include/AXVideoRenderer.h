//
// Created by admin on 2025/9/18.
//

#ifndef AXPLAYERLIB_AXVIDEORENDERER_H
#define AXPLAYERLIB_AXVIDEORENDERER_H


#pragma once
#include "AXQueues.h"
#include <android/native_window.h>
#include <mutex>

class AXVideoRenderer {
public:
    AXVideoRenderer();
    ~AXVideoRenderer();

    bool init(ANativeWindow* win, int w, int h, int sarNum, int sarDen);
    void setFrameQueue(FrameQueue* fq) { fQ_ = fq; }
    void drawLoopOnce(int64_t masterPtsUs); // 由 AXPlayer 驱动；根据时钟选择丢/渲
    void release();

private:
    bool ensureEGL_();
    void destroyEGL_();
    void drawFrame_(AVFrame* frm);

    FrameQueue* fQ_{nullptr};
    ANativeWindow* win_{nullptr};
    std::mutex wMtx_;

    // EGL/GLES 资源
    void* display_{nullptr};
    void* surface_{nullptr};
    void* context_{nullptr};
    int videoW_{0}, videoH_{0}, sarNum_{1}, sarDen_{1};
};


#endif //AXPLAYERLIB_AXVIDEORENDERER_H
