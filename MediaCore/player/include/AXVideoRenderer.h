#ifndef AXPLAYERLIB_AXVIDEORENDERER_H
#define AXPLAYERLIB_AXVIDEORENDERER_H

#pragma once
#include "AXQueues.h"
#include <android/native_window.h>
#include <mutex>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#define AX_LOG_TAG "AXVideoRenderer"
#include "AXLog.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/frame.h>
}

class AXVideoRenderer {
public:
    AXVideoRenderer();
    ~AXVideoRenderer();

    // 以当前 Surface 初始化渲染（可重复调用以切换窗口）
    bool init(ANativeWindow* win, int w, int h, int sarNum, int sarDen);

    // 帧时间基（来自视频解码器的 time_base，必须设置）
    void setTimeBase(AVRational tb) { tb_ = tb; }

    void setFrameQueue(FrameQueue* fq) { fQ_ = fq; }

    // 由 AXPlayer 周期调用。根据主时钟选择渲染/丢弃
    void drawLoopOnce(int64_t masterPtsUs);

    // 释放所有 GLES/EGL 资源与窗口引用
    void release();

private:
    bool ensureEGL_();
    void destroyEGL_();
    bool ensureGLObjects_();
    void destroyGLObjects_();

    void drawFrame_(AVFrame* frm);
    void computeViewport_(int winW, int winH, int& vx, int& vy, int& vw, int& vh);

    // 工具：把帧 pts(以 tb_) 转为 us；返回 <0 表示未知
    static inline int64_t framePtsUs_(AVFrame* f, AVRational tb) {
        if (!f || f->pts == AV_NOPTS_VALUE) return -1;
        return av_rescale_q(f->pts, tb, AVRational{1,1000000});
    }

private:
    FrameQueue*     fQ_{nullptr};
    ANativeWindow*  win_{nullptr};
    std::mutex      wMtx_;

    // EGL/GLES 资源
    EGLDisplay display_{EGL_NO_DISPLAY};
    EGLSurface surface_{EGL_NO_SURFACE};
    EGLContext context_{EGL_NO_CONTEXT};
    EGLConfig  config_{nullptr};

    int videoW_{0}, videoH_{0}, sarNum_{1}, sarDen_{1};
    AVRational tb_{1,1000}; // 帧时间基，默认毫秒

    // GL program & textures
    GLuint prog_{0};
    GLuint texY_{0}, texU_{0}, texV_{0};
    GLuint vao_{0}, vbo_{0};

    // attribute/uniform 位置
    GLint aPosLoc_{-1}, aTexLoc_{-1};
    GLint uTexY_{-1}, uTexU_{-1}, uTexV_{-1};

    // 记录上一次绘制的窗口尺寸，便于 viewport 计算
    int lastWinW_{0}, lastWinH_{0};

    // 待渲染帧（节流：只保留一帧）
    AVFrame* pending_{nullptr};
};

#endif //AXPLAYERLIB_AXVIDEORENDERER_H