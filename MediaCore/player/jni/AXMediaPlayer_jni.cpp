// AXMediaPlayer_jni.cpp
// 动态注册 JNI；桥接 Java 层 AXMediaPlayer 与 C++ 内核 AXPlayer（C++11 兼容版）

#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <string>
#include <map>
#include <memory>
#include <mutex>
extern "C" int axf_av_jni_set_java_vm(JavaVM* vm);


#include "../include/AXPlayer.h" // C++内核头（相对路径按你的仓库结构）

// ================= 日志 =================
#define LOG_TAG "AXMediaPlayerJNI"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ================= Java 包名/类路径集中管理 =================
#define JPATH_AXMEDIAPLAYER "com/axplayer/devlib/AXMediaPlayer"

// Java 层回调方法名
#define JMETHOD_postOnPrepared           "postOnPrepared"
#define JMETHOD_postOnCompletion         "postOnCompletion"
#define JMETHOD_postOnBufferingUpdate    "postOnBufferingUpdate"
#define JMETHOD_postOnVideoSizeChanged   "postOnVideoSizeChanged"
#define JMETHOD_postOnError              "postOnError"

// Java 层回调方法签名
#define JSIG_postOnPrepared              "()V"
#define JSIG_postOnCompletion            "()V"
#define JSIG_postOnBufferingUpdate       "(I)V"
#define JSIG_postOnVideoSizeChanged      "(IIII)V"
#define JSIG_postOnError                 "(IILjava/lang/String;)V"

// Java 层 native 方法签名（需与 AXMediaPlayer.java 完全一致）
#define JSIG_nativeCreate                "(Ljava/lang/ref/WeakReference;)J"
#define JSIG_nativeSetDataSourceUri      "(JLandroid/content/Context;Ljava/lang/String;Ljava/util/Map;)V"
#define JSIG_nativeSetDataSourcePath     "(JLjava/lang/String;)V"
#define JSIG_nativePrepareAsync          "(J)V"
#define JSIG_nativeStart                 "(J)V"
#define JSIG_nativePause                 "(J)V"
#define JSIG_nativeSeekTo                "(JJ)V"
#define JSIG_nativeIsPlaying             "(J)Z"
#define JSIG_nativeSetSpeed              "(JF)V"
#define JSIG_nativeGetCurrentPosition    "(J)J"
#define JSIG_nativeGetDuration           "(J)J"
#define JSIG_nativeSetVolume             "(JFF)V"
#define JSIG_nativeGetVideoWidth         "(J)I"
#define JSIG_nativeGetVideoHeight        "(J)I"
#define JSIG_nativeGetVideoSarNum        "(J)I"
#define JSIG_nativeGetVideoSarDen        "(J)I"
#define JSIG_nativeGetAudioSessionId     "(J)I"
#define JSIG_nativeSetSurface            "(JLandroid/view/Surface;)V"
#define JSIG_nativeRelease               "(J)V"

// ================= VM/引用缓存 =================
static JavaVM* g_vm = nullptr;

struct JRefs {
    jclass cls_AXMediaPlayer;

    // AXMediaPlayer.postOn* 回调
    jmethodID m_postOnPrepared;
    jmethodID m_postOnCompletion;
    jmethodID m_postOnBufferingUpdate;
    jmethodID m_postOnVideoSizeChanged;
    jmethodID m_postOnError;

    // WeakReference#get()
    jmethodID m_weakGet;

    JRefs()
            : cls_AXMediaPlayer(nullptr),
              m_postOnPrepared(nullptr),
              m_postOnCompletion(nullptr),
              m_postOnBufferingUpdate(nullptr),
              m_postOnVideoSizeChanged(nullptr),
              m_postOnError(nullptr),
              m_weakGet(nullptr) {}
};

static JRefs g_jrefs;

// ================ 工具函数 ================
static jclass FindGlobalClass(JNIEnv* env, const char* name) {
    jclass local = env->FindClass(name);
    if (!local) return nullptr;
    jclass global = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    return global;
}

static JNIEnv* GetEnvAttach(bool* didAttach) {
    *didAttach = false;
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            *didAttach = true;
        }
    }
    return env;
}

static void DetachIfNeeded(bool didAttach) {
    if (didAttach) g_vm->DetachCurrentThread();
}

static void ClearIfException(JNIEnv* env, const char* where) {
    if (env && env->ExceptionCheck()) {
        env->ExceptionDescribe(); // 调试期可打开；线上可改为仅 Clear
        env->ExceptionClear();
        ALOGE("JNI exception cleared @ %s", where ? where : "unknown");
    }
}

// ================ Java 回调桥接 ================
class JavaCallbackBridge : public AXPlayerCallback {
public:
    JavaCallbackBridge(JNIEnv* env, jobject jWeakSelf)
            : m_weakGlobal(env->NewGlobalRef(jWeakSelf)) {}

    ~JavaCallbackBridge() override {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (env && m_weakGlobal) {
            env->DeleteGlobalRef(m_weakGlobal);
            m_weakGlobal = nullptr;
        }
        DetachIfNeeded(need);
    }

    void onPrepared() override               { callVoid(g_jrefs.m_postOnPrepared, "onPrepared"); }
    void onCompletion() override             { callVoid(g_jrefs.m_postOnCompletion, "onCompletion"); }
    void onBuffering(int percent) override   { callInt(g_jrefs.m_postOnBufferingUpdate, percent, "onBuffering"); }

    void onVideoSizeChanged(int w, int h, int sarNum, int sarDen) override {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal || !g_jrefs.m_weakGet || !g_jrefs.m_postOnVideoSizeChanged) { DetachIfNeeded(need); return; }
        jobject strong = env->CallObjectMethod(m_weakGlobal, g_jrefs.m_weakGet);
        ClearIfException(env, "weak.get()");
        if (strong) {
            env->CallVoidMethod(strong, g_jrefs.m_postOnVideoSizeChanged, (jint)w,(jint)h,(jint)sarNum,(jint)sarDen);
            ClearIfException(env, "postOnVideoSizeChanged");
            env->DeleteLocalRef(strong);
        }
        DetachIfNeeded(need);
    }

    void onError(int what, int extra, const std::string& msg) override {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal || !g_jrefs.m_weakGet || !g_jrefs.m_postOnError) { DetachIfNeeded(need); return; }
        jstring jMsg = env->NewStringUTF(msg.c_str());
        jobject strong = env->CallObjectMethod(m_weakGlobal, g_jrefs.m_weakGet);
        ClearIfException(env, "weak.get()");
        if (strong) {
            env->CallVoidMethod(strong, g_jrefs.m_postOnError, (jint)what, (jint)extra, jMsg);
            ClearIfException(env, "postOnError");
            env->DeleteLocalRef(strong);
        }
        if (jMsg) env->DeleteLocalRef(jMsg);
        DetachIfNeeded(need);
    }

private:
    jobject m_weakGlobal;

    void callVoid(jmethodID mid, const char* where) {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal || !g_jrefs.m_weakGet || !mid) { DetachIfNeeded(need); return; }
        jobject strong = env->CallObjectMethod(m_weakGlobal, g_jrefs.m_weakGet);
        ClearIfException(env, "weak.get()");
        if (strong) {
            env->CallVoidMethod(strong, mid);
            ClearIfException(env, where ? where : "callVoid");
            env->DeleteLocalRef(strong);
        }
        DetachIfNeeded(need);
    }

    void callInt(jmethodID mid, int arg, const char* where) {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal || !g_jrefs.m_weakGet || !mid) { DetachIfNeeded(need); return; }
        jobject strong = env->CallObjectMethod(m_weakGlobal, g_jrefs.m_weakGet);
        ClearIfException(env, "weak.get()");
        if (strong) {
            env->CallVoidMethod(strong, mid, (jint)arg);
            ClearIfException(env, where ? where : "callInt");
            env->DeleteLocalRef(strong);
        }
        DetachIfNeeded(need);
    }
};

// ================ Native 句柄聚合 ================
struct NativeHolder {
    std::unique_ptr<AXPlayer> player;
    std::shared_ptr<JavaCallbackBridge> jcb;
    ANativeWindow* window;
    std::mutex mtx; // 保护 window

    NativeHolder() : player(), jcb(), window(nullptr) {}
};

// ================ Native 实现 ================
static jlong nativeCreate(JNIEnv* env, jclass, jobject jWeakSelf) {
    ALOGI("nativeCreate");
    NativeHolder* holder = new NativeHolder();
    holder->jcb = std::make_shared<JavaCallbackBridge>(env, jWeakSelf);
    holder->player.reset(new AXPlayer(holder->jcb)); // C++11 兼容写法
    return reinterpret_cast<jlong>(holder);
}

static void nativeSetDataSourceUri(JNIEnv* env, jclass, jlong ctx,
                                   jobject /*context*/, jstring juri, jobject jheaders) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;

    const char* uri = env->GetStringUTFChars(juri, nullptr);
    std::map<std::string,std::string> headers;

    if (jheaders) {
        // Map<String,String> -> std::map<string,string>
        jclass mapCls = env->GetObjectClass(jheaders);
        jmethodID entrySet = env->GetMethodID(mapCls, "entrySet", "()Ljava/util/Set;");
        jobject setObj = env->CallObjectMethod(jheaders, entrySet);

        jclass setCls = env->GetObjectClass(setObj);
        jmethodID iterator = env->GetMethodID(setCls, "iterator", "()Ljava/util/Iterator;");
        jobject itObj = env->CallObjectMethod(setObj, iterator);

        jclass itCls = env->GetObjectClass(itObj);
        jmethodID hasNext = env->GetMethodID(itCls, "hasNext", "()Z");
        jmethodID next = env->GetMethodID(itCls, "next", "()Ljava/lang/Object;");

        jclass entryCls = env->FindClass("java/util/Map$Entry");
        jmethodID getKey = env->GetMethodID(entryCls, "getKey", "()Ljava/lang/Object;");
        jmethodID getVal = env->GetMethodID(entryCls, "getValue", "()Ljava/lang/Object;");

        while (env->CallBooleanMethod(itObj, hasNext)) {
            jobject entry = env->CallObjectMethod(itObj, next);
            jstring kObj = (jstring)env->CallObjectMethod(entry, getKey);
            jstring vObj = (jstring)env->CallObjectMethod(entry, getVal);
            const char* k = env->GetStringUTFChars(kObj, nullptr);
            const char* v = env->GetStringUTFChars(vObj, nullptr);
            headers[k ? k : ""] = v ? v : "";
            env->ReleaseStringUTFChars(kObj, k);
            env->ReleaseStringUTFChars(vObj, v);
            env->DeleteLocalRef(entry);
            env->DeleteLocalRef(kObj);
            env->DeleteLocalRef(vObj);
        }

        // 清理局部引用
        env->DeleteLocalRef(entryCls);
        env->DeleteLocalRef(itObj);
        env->DeleteLocalRef(itCls);
        env->DeleteLocalRef(setObj);
        env->DeleteLocalRef(setCls);
        env->DeleteLocalRef(mapCls);
        ClearIfException(env, "nativeSetDataSourceUri: map parse");
    }

    h->player->setDataSource(uri ? uri : "", headers);
    env->ReleaseStringUTFChars(juri, uri);
}

static void nativeSetDataSourcePath(JNIEnv* env, jclass, jlong ctx, jstring jpath) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    h->player->setDataSource(path ? path : "", std::map<std::string,std::string>());
    env->ReleaseStringUTFChars(jpath, path);
}

static void nativePrepareAsync(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->prepareAsync();
}

static void nativeStart(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->start();
}

static void nativePause(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->pause();
}

static void nativeSeekTo(JNIEnv*, jclass, jlong ctx, jlong msec) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->seekTo((int64_t)msec);
}

static jboolean nativeIsPlaying(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return JNI_FALSE;
    return h->player->isPlaying() ? JNI_TRUE : JNI_FALSE;
}

static void nativeSetSpeed(JNIEnv*, jclass, jlong ctx, jfloat speed) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->setSpeed((float)speed);
}

static jlong nativeGetCurrentPosition(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jlong)h->player->getCurrentPositionMs();
}

static jlong nativeGetDuration(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jlong)h->player->getDurationMs();
}

static void nativeSetVolume(JNIEnv*, jclass, jlong ctx, jfloat left, jfloat right) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->setVolume(left, right);
}

static jint nativeGetVideoWidth(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jint)h->player->getVideoWidth();
}

static jint nativeGetVideoHeight(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jint)h->player->getVideoHeight();
}

static jint nativeGetVideoSarNum(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 1;
    return (jint)h->player->getVideoSarNum();
}

static jint nativeGetVideoSarDen(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 1;
    return (jint)h->player->getVideoSarDen();
}

static jint nativeGetAudioSessionId(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jint)h->player->getAudioSessionId();
}

static void nativeSetSurface(JNIEnv* env, jclass, jlong ctx, jobject surface) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;

    std::lock_guard<std::mutex> _l(h->mtx);
    if (h->window) {
        ANativeWindow_release(h->window);
        h->window = nullptr;
    }
    if (surface) {
        h->window = ANativeWindow_fromSurface(env, surface);
    }
    h->player->setWindow(h->window);
}

static void nativeRelease(JNIEnv*, jclass, jlong ctx) {
    NativeHolder* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;

    {
        std::lock_guard<std::mutex> _l(h->mtx);
        if (h->window) {
            ANativeWindow_release(h->window);
            h->window = nullptr;
        }
    }
    h->player.reset();
    delete h;
}

// ================ 动态注册 ================
static JNINativeMethod g_methods[] = {
        {"nativeCreate",             JSIG_nativeCreate,             (void*)nativeCreate},
        {"nativeSetDataSourceUri",   JSIG_nativeSetDataSourceUri,   (void*)nativeSetDataSourceUri},
        {"nativeSetDataSourcePath",  JSIG_nativeSetDataSourcePath,  (void*)nativeSetDataSourcePath},
        {"nativePrepareAsync",       JSIG_nativePrepareAsync,       (void*)nativePrepareAsync},
        {"nativeStart",              JSIG_nativeStart,              (void*)nativeStart},
        {"nativePause",              JSIG_nativePause,              (void*)nativePause},
        {"nativeSeekTo",             JSIG_nativeSeekTo,             (void*)nativeSeekTo},
        {"nativeIsPlaying",          JSIG_nativeIsPlaying,          (void*)nativeIsPlaying},
        {"nativeSetSpeed",           JSIG_nativeSetSpeed,           (void*)nativeSetSpeed},
        {"nativeGetCurrentPosition", JSIG_nativeGetCurrentPosition, (void*)nativeGetCurrentPosition},
        {"nativeGetDuration",        JSIG_nativeGetDuration,        (void*)nativeGetDuration},
        {"nativeSetVolume",          JSIG_nativeSetVolume,          (void*)nativeSetVolume},
        {"nativeGetVideoWidth",      JSIG_nativeGetVideoWidth,      (void*)nativeGetVideoWidth},
        {"nativeGetVideoHeight",     JSIG_nativeGetVideoHeight,     (void*)nativeGetVideoHeight},
        {"nativeGetVideoSarNum",     JSIG_nativeGetVideoSarNum,     (void*)nativeGetVideoSarNum},
        {"nativeGetVideoSarDen",     JSIG_nativeGetVideoSarDen,     (void*)nativeGetVideoSarDen},
        {"nativeGetAudioSessionId",  JSIG_nativeGetAudioSessionId,  (void*)nativeGetAudioSessionId},
        {"nativeSetSurface",         JSIG_nativeSetSurface,         (void*)nativeSetSurface},
        {"nativeRelease",            JSIG_nativeRelease,            (void*)nativeRelease},
};

jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        ALOGE("GetEnv failed");
        return JNI_ERR;
    }

    jclass cls = FindGlobalClass(env, JPATH_AXMEDIAPLAYER);
    if (!cls) {
        ALOGE("FindClass %s failed", JPATH_AXMEDIAPLAYER);
        return JNI_ERR;
    }
    g_jrefs.cls_AXMediaPlayer = cls;

    // 回调方法 ID
    g_jrefs.m_postOnPrepared         = env->GetMethodID(cls, JMETHOD_postOnPrepared,         JSIG_postOnPrepared);
    g_jrefs.m_postOnCompletion       = env->GetMethodID(cls, JMETHOD_postOnCompletion,       JSIG_postOnCompletion);
    g_jrefs.m_postOnBufferingUpdate  = env->GetMethodID(cls, JMETHOD_postOnBufferingUpdate,  JSIG_postOnBufferingUpdate);
    g_jrefs.m_postOnVideoSizeChanged = env->GetMethodID(cls, JMETHOD_postOnVideoSizeChanged, JSIG_postOnVideoSizeChanged);
    g_jrefs.m_postOnError            = env->GetMethodID(cls, JMETHOD_postOnError,            JSIG_postOnError);

    // WeakReference#get()
    jclass weakClsLocal = env->FindClass("java/lang/ref/WeakReference");
    if (!weakClsLocal) {
        ALOGE("FindClass WeakReference failed");
        return JNI_ERR;
    }
    g_jrefs.m_weakGet = env->GetMethodID(weakClsLocal, "get", "()Ljava/lang/Object;");
    env->DeleteLocalRef(weakClsLocal);

    if (!g_jrefs.m_postOnPrepared || !g_jrefs.m_postOnCompletion ||
        !g_jrefs.m_postOnBufferingUpdate || !g_jrefs.m_postOnVideoSizeChanged ||
        !g_jrefs.m_postOnError || !g_jrefs.m_weakGet) {
        ALOGE("GetMethodID some failed");
        return JNI_ERR;
    }

    // 注册 native
    if (env->RegisterNatives(cls, g_methods, sizeof(g_methods)/sizeof(g_methods[0])) != 0) {
        ALOGE("RegisterNatives failed");
        return JNI_ERR;
    }
//    av_jni_set_java_vm(vm, nullptr);
//    TrySetFFmpegJavaVM(vm);
    axf_av_jni_set_java_vm(vm);
    AXPlayer::SetJavaVM(vm);
    ALOGI("JNI_OnLoad OK");
    return JNI_VERSION_1_6;
}

void JNI_OnUnload(JavaVM* vm, void*) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return;

    if (g_jrefs.cls_AXMediaPlayer) {
        env->UnregisterNatives(g_jrefs.cls_AXMediaPlayer);
        env->DeleteGlobalRef(g_jrefs.cls_AXMediaPlayer);
        g_jrefs.cls_AXMediaPlayer = nullptr;
    }
    ALOGI("JNI_OnUnload");
}