//
// Created by admin on 2025/9/18.
//

#include "AXVideoRenderer.h"
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

AXVideoRenderer::AXVideoRenderer() {}
AXVideoRenderer::~AXVideoRenderer() { release(); }

bool AXVideoRenderer::init(ANativeWindow* win, int w, int h, int sarNum, int sarDen) {
    std::lock_guard<std::mutex> lk(wMtx_);
    win_ = win; videoW_ = w; videoH_ = h; sarNum_ = sarNum; sarDen_ = sarDen;
    return ensureEGL_();
}

void AXVideoRenderer::release() {
    std::lock_guard<std::mutex> lk(wMtx_);
    destroyEGL_();
    win_ = nullptr;
}

bool AXVideoRenderer::ensureEGL_() {
    if (!win_) return false;
    // TODO: 创建 EGLDisplay/EGLContext/EGLSurface 并设置到窗口；初始化 shader/program/纹理
    // 这里留最小骨架，避免篇幅过大
    return true;
}

void AXVideoRenderer::destroyEGL_() {
    // TODO: 销毁 GLES/EGL 资源（程序/纹理/FBO、EGLSurface/Context/Display）
}

void AXVideoRenderer::drawLoopOnce(int64_t masterPtsUs) {
    if (!fQ_) return;
    AVFrame* frm = nullptr;
    if (!fQ_->pop(frm)) return;
    if (!frm) return;
    // TODO: 根据 masterPtsUs 与帧的 pts（转换为 us）决定是否丢帧/渲染
    drawFrame_(frm);
    av_frame_free(&frm);
}

void AXVideoRenderer::drawFrame_(AVFrame* frm) {
    // TODO: yuv->texture 上传 & 绘制
    // 注意：当解码输出为非 YUV420P 时，可用 libyuv/sws_scale 先转
    // glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(...); eglSwapBuffers(...)
}
