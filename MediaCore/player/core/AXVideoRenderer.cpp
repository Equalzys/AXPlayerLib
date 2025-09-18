#include "AXVideoRenderer.h"
#include <android/log.h>


// =================== 着色器源码 ===================
static const char* kVS = R"(#version 310 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTex;
out vec2 vTex;
void main(){
    vTex = aTex;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// YUV420P/I420 => RGB
static const char* kFS = R"(#version 310 es
precision mediump float;
in vec2 vTex;
out vec4 fragColor;

uniform sampler2D uTexY;
uniform sampler2D uTexU;
uniform sampler2D uTexV;

void main(){
    float y = texture(uTexY, vTex).r;
    float u = texture(uTexU, vTex).r - 0.5;
    float v = texture(uTexV, vTex).r - 0.5;

    // BT.601
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;

    fragColor = vec4(r, g, b, 1.0);
}
)";

// =================== 小工具 ===================
static GLuint compileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = GL_FALSE;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetShaderInfoLog(sh, len, nullptr, &log[0]);
        AX_LOGE("shader compile fail: %s", log.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = GL_FALSE;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        glGetProgramInfoLog(p, len, nullptr, &log[0]);
        AX_LOGE("program link fail: %s", log.c_str());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

AXVideoRenderer::AXVideoRenderer() {}
AXVideoRenderer::~AXVideoRenderer() { release(); }

// =================== EGL/GLES 生命周期 ===================
bool AXVideoRenderer::init(ANativeWindow* win, int w, int h, int sarNum, int sarDen) {
    std::lock_guard<std::mutex> lk(wMtx_);
    // 释放旧窗口的 Surface（保留 Context/Display）
    if (surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
    if (win_) {
        ANativeWindow_release(win_);
        win_ = nullptr;
    }

    if (!win) {
        AX_LOGW("init with null window");
        return false;
    }
    ANativeWindow_acquire(win);
    win_ = win;

    videoW_ = (w > 0) ? w : 0;
    videoH_ = (h > 0) ? h : 0;
    sarNum_ = (sarNum > 0) ? sarNum : 1;
    sarDen_ = (sarDen > 0) ? sarDen : 1;

    if (!ensureEGL_()) {
        AX_LOGE("ensureEGL_ failed");
        return false;
    }
    if (!ensureGLObjects_()) {
        AX_LOGE("ensureGLObjects_ failed");
        return false;
    }
    AX_LOGI("init ok: w=%d h=%d sar=%d/%d", videoW_, videoH_, sarNum_, sarDen_);
    return true;
}

void AXVideoRenderer::release() {
    std::lock_guard<std::mutex> lk(wMtx_);

    if (pending_) {
        av_frame_free(&pending_);
    }

    destroyGLObjects_(); // 先销毁 GL 资源（program/vao/vbo/texture）

    destroyEGL_();       // 再销毁 EGL surface/context/display

    if (win_) {
        ANativeWindow_release(win_);
        win_ = nullptr;
    }
    lastWinW_ = lastWinH_ = 0;
}

bool AXVideoRenderer::ensureEGL_() {
    // Display
    if (display_ == EGL_NO_DISPLAY) {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY) {
            AX_LOGE("eglGetDisplay failed");
            return false;
        }
        if (!eglInitialize(display_, nullptr, nullptr)) {
            AX_LOGE("eglInitialize failed");
            return false;
        }
    }

    // Config
    if (!config_) {
        const EGLint cfg[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
                EGL_RED_SIZE,        8,
                EGL_GREEN_SIZE,      8,
                EGL_BLUE_SIZE,       8,
                EGL_ALPHA_SIZE,      8, // 允许窗口有 alpha
                EGL_NONE
        };
        EGLint num = 0;
        if (!eglChooseConfig(display_, cfg, &config_, 1, &num) || num <= 0) {
            AX_LOGE("eglChooseConfig failed");
            return false;
        }
    }

    // Context
    if (context_ == EGL_NO_CONTEXT) {
        const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, ctxAttr);
        if (context_ == EGL_NO_CONTEXT) {
            AX_LOGE("eglCreateContext failed");
            return false;
        }
    }

    // Surface（可重复创建）
    if (surface_ == EGL_NO_SURFACE) {
        surface_ = eglCreateWindowSurface(display_, config_, win_, nullptr);
        if (surface_ == EGL_NO_SURFACE) {
            AX_LOGE("eglCreateWindowSurface failed");
            return false;
        }
    }

    if (!eglMakeCurrent(display_, surface_, surface_, context_)) {
        AX_LOGE("eglMakeCurrent failed");
        return false;
    }
    return true;
}

void AXVideoRenderer::destroyEGL_() {
    if (display_ == EGL_NO_DISPLAY) return;

    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (surface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_, surface_);
        surface_ = EGL_NO_SURFACE;
    }
    if (context_ != EGL_NO_CONTEXT) {
        eglDestroyContext(display_, context_);
        context_ = EGL_NO_CONTEXT;
    }
    config_ = nullptr;

    eglTerminate(display_);
    display_ = EGL_NO_DISPLAY;
}

// =================== GL 资源 ===================
bool AXVideoRenderer::ensureGLObjects_() {
    if (prog_ != 0) return true;

    GLuint vs = compileShader(GL_VERTEX_SHADER,   kVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    prog_ = linkProgram(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!prog_) return false;

    aPosLoc_ = 0; // layout(location=0)
    aTexLoc_ = 1; // layout(location=1)

    glUseProgram(prog_);
    uTexY_ = glGetUniformLocation(prog_, "uTexY");
    uTexU_ = glGetUniformLocation(prog_, "uTexU");
    uTexV_ = glGetUniformLocation(prog_, "uTexV");
    glUniform1i(uTexY_, 0);
    glUniform1i(uTexU_, 1);
    glUniform1i(uTexV_, 2);

    // 顶点数据：两个三角形的全屏矩形
    const GLfloat verts[] = {
            // pos     // uv(翻转 v)
            -1.f,-1.f, 0.f, 1.f,
            1.f,-1.f, 1.f, 1.f,
            -1.f, 1.f, 0.f, 0.f,
            1.f, 1.f, 1.f, 0.f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(aPosLoc_);
    glVertexAttribPointer(aPosLoc_, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);
    glEnableVertexAttribArray(aTexLoc_);
    glVertexAttribPointer(aTexLoc_, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2));
    glBindVertexArray(0);

    // 3 个 LUMA/CHROMA 纹理（GL_R8）
    glGenTextures(1, &texY_);
    glGenTextures(1, &texU_);
    glGenTextures(1, &texV_);

    auto setupTex = [](GLuint tex){
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    };
    setupTex(texY_);
    setupTex(texU_);
    setupTex(texV_);

    glUseProgram(0);
    return true;
}

void AXVideoRenderer::destroyGLObjects_() {
    if (texV_) { glDeleteTextures(1, &texV_); texV_ = 0; }
    if (texU_) { glDeleteTextures(1, &texU_); texU_ = 0; }
    if (texY_) { glDeleteTextures(1, &texY_); texY_ = 0; }

    if (vbo_)  { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_)  { glDeleteVertexArrays(1, &vao_); vao_ = 0; }

    if (prog_) { glDeleteProgram(prog_); prog_ = 0; }
}

// =================== 渲染节流与绘制 ===================
void AXVideoRenderer::drawLoopOnce(int64_t masterPtsUs) {
    std::lock_guard<std::mutex> lk(wMtx_);
    if (!win_ || !ensureEGL_() || !ensureGLObjects_()) return;

    // 取/保持一个待渲染帧
    if (!pending_) {
        if (!fQ_) return;
        if (!fQ_->pop(pending_) || !pending_) return;
    }

    // 只处理 YUV420P/I420
    if (pending_->format != AV_PIX_FMT_YUV420P) {
        // TODO: sws/libyuv 转换；当前直接“尽量显示”，避免卡在队列
        drawFrame_(pending_);
        av_frame_free(&pending_);
        eglSwapBuffers(display_, surface_);
        return;
    }

    const int64_t ptsUs = framePtsUs_(pending_, tb_);
    if (ptsUs >= 0) {
        const int64_t diff = ptsUs - masterPtsUs;
        if (diff > +20000) {
            // 提前太多：等下次
            return;
        }
        if (diff < -120000) {
            // 落后太多：丢帧追时钟
            av_frame_free(&pending_);
            return;
        }
    }
    // 未知 PTS 或在窗口内：渲染
    drawFrame_(pending_);
    av_frame_free(&pending_);

//    AX_LOGI("render frame; swap, masterUs=%lld", (long long)masterPtsUs);
    eglSwapBuffers(display_, surface_);
}

void AXVideoRenderer::drawFrame_(AVFrame* frm) {
    if (!frm) return;

    // 更新纹理（假设 YUV420P）
    const int w = frm->width;
    const int h = frm->height;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Y
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texY_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, frm->data[0]);

    // U
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texU_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w/2, h/2, 0, GL_RED, GL_UNSIGNED_BYTE, frm->data[1]);

    // V
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texV_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w/2, h/2, 0, GL_RED, GL_UNSIGNED_BYTE, frm->data[2]);

    // 计算 viewport（保持比例 + letterbox）
    EGLint winW = 0, winH = 0;
    eglQuerySurface(display_, surface_, EGL_WIDTH,  &winW);
    eglQuerySurface(display_, surface_, EGL_HEIGHT, &winH);
    int vx=0, vy=0, vw=winW, vh=winH;
    if (winW != lastWinW_ || winH != lastWinH_) {
        computeViewport_(winW, winH, vx, vy, vw, vh);
        lastWinW_ = winW; lastWinH_ = winH;
    } else {
        computeViewport_(winW, winH, vx, vy, vw, vh);
    }
    glViewport(vx, vy, vw, vh);

    // 清屏 & 绘制
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(prog_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
}

void AXVideoRenderer::computeViewport_(int winW, int winH, int& vx, int& vy, int& vw, int& vh) {
    if (winW <= 0 || winH <= 0 || videoW_ <= 0 || videoH_ <= 0) {
        vx = vy = 0; vw = winW; vh = winH; return;
    }
    // 物理像素宽高比（考虑 SAR）
    const double dar = (double)videoW_ * (double)sarNum_ / ((double)videoH_ * (double)sarDen_);
    const double wnd = (double)winW / (double)winH;

    if (wnd > dar) {
        // 窗口更宽，匹配高度
        vh = winH;
        vw = (int)(vh * dar + 0.5);
        vx = (winW - vw) / 2;
        vy = 0;
    } else {
        // 窗口更窄，匹配宽度
        vw = winW;
        vh = (int)(vw / dar + 0.5);
        vx = 0;
        vy = (winH - vh) / 2;
    }
}