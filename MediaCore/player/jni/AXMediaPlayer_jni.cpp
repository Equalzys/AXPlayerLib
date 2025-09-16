#include <jni.h>
#include <android/native_window_jni.h>
#include <android/log.h>
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include "AXPlayer.h" // C++内核头

#define LOG_TAG "AXMediaPlayerJNI"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
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

static JavaVM* g_vm = nullptr;

struct JRefs {
    jclass cls_AXMediaPlayer = nullptr;
    jmethodID m_postOnPrepared = nullptr;
    jmethodID m_postOnCompletion = nullptr;
    jmethodID m_postOnBufferingUpdate = nullptr;
    jmethodID m_postOnVideoSizeChanged = nullptr;
    jmethodID m_postOnError = nullptr;
};

static JRefs g_jrefs;

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

// ---------------- Native <-> Java 桥：包装回调 ----------------
class JavaCallbackBridge : public AXPlayerCallback {
public:
    JavaCallbackBridge(JNIEnv* env, jobject jWeakSelf)
            : m_weakGlobal(env->NewGlobalRef(jWeakSelf)) {}

    ~JavaCallbackBridge() override {
        bool need;
        JNIEnv* env = GetEnvAttach(&need);
        if (env && m_weakGlobal) {
            env->DeleteGlobalRef(m_weakGlobal);
            m_weakGlobal = nullptr;
        }
        DetachIfNeeded(need);
    }

    void onPrepared() override {
        callVoid("postOnPrepared");
    }
    void onCompletion() override {
        callVoid("postOnCompletion");
    }
    void onBuffering(int percent) override {
        callInt("postOnBufferingUpdate", percent);
    }
    void onVideoSizeChanged(int w, int h, int sarNum, int sarDen) override {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal) { DetachIfNeeded(need); return; }
        env->CallVoidMethod(m_weakGlobal, g_jrefs.m_postOnVideoSizeChanged, (jint)w,(jint)h,(jint)sarNum,(jint)sarDen);
        DetachIfNeeded(need);
    }
    void onError(int what, int extra, const std::string& msg) override {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal) { DetachIfNeeded(need); return; }
        jstring jMsg = env->NewStringUTF(msg.c_str());
        env->CallVoidMethod(m_weakGlobal, g_jrefs.m_postOnError, what, extra, jMsg);
        env->DeleteLocalRef(jMsg);
        DetachIfNeeded(need);
    }

private:
    jobject m_weakGlobal = nullptr;

    void callVoid(const char* name) {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal) { DetachIfNeeded(need); return; }
        jmethodID mid = nullptr;
        if (strcmp(name, "postOnPrepared") == 0) mid = g_jrefs.m_postOnPrepared;
        else if (strcmp(name, "postOnCompletion") == 0) mid = g_jrefs.m_postOnCompletion;
        if (mid) env->CallVoidMethod(m_weakGlobal, mid);
        DetachIfNeeded(need);
    }
    void callInt(const char* name, int arg) {
        bool need; JNIEnv* env = GetEnvAttach(&need);
        if (!env || !m_weakGlobal) { DetachIfNeeded(need); return; }
        jmethodID mid = nullptr;
        if (strcmp(name, "postOnBufferingUpdate") == 0) mid = g_jrefs.m_postOnBufferingUpdate;
        if (mid) env->CallVoidMethod(m_weakGlobal, mid, (jint)arg);
        DetachIfNeeded(need);
    }
};

// 句柄表
struct NativeHolder {
    std::unique_ptr<AXPlayer> player;
    std::shared_ptr<JavaCallbackBridge> jcb;
    ANativeWindow* window = nullptr;
    std::mutex mtx;
};

static jlong nativeCreate(JNIEnv* env, jclass, jobject jWeakSelf) {
    auto holder = new NativeHolder();
    holder->jcb = std::make_shared<JavaCallbackBridge>(env, jWeakSelf);
    holder->player = std::make_unique<AXPlayer>(holder->jcb);
    return reinterpret_cast<jlong>(holder);
}

static void nativeSetDataSourceUri(JNIEnv* env, jclass, jlong ctx, jobject /*context*/, jstring juri, jobject jheaders) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    const char* uri = env->GetStringUTFChars(juri, nullptr);

    std::map<std::string,std::string> headers;
    if (jheaders) {
        // Map<String,String> -> std::map
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
            headers[k] = v ? v : "";
            env->ReleaseStringUTFChars(kObj, k);
            env->ReleaseStringUTFChars(vObj, v);
            env->DeleteLocalRef(entry);
            env->DeleteLocalRef(kObj);
            env->DeleteLocalRef(vObj);
        }
        env->DeleteLocalRef(setObj);
        env->DeleteLocalRef(mapCls);
        env->DeleteLocalRef(setCls);
        env->DeleteLocalRef(itObj);
        env->DeleteLocalRef(itCls);
        env->DeleteLocalRef(entryCls);
    }

    h->player->setDataSource(uri, headers);
    env->ReleaseStringUTFChars(juri, uri);
}

static void nativeSetDataSourcePath(JNIEnv* env, jclass, jlong ctx, jstring jpath) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    const char* path = env->GetStringUTFChars(jpath, nullptr);
    h->player->setDataSource(path, {});
    env->ReleaseStringUTFChars(jpath, path);
}

static void nativePrepareAsync(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->prepareAsync();
}

static void nativeStart(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->start();
}

static void nativePause(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->pause();
}

static void nativeSeekTo(JNIEnv*, jclass, jlong ctx, jlong msec) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->seekTo((int64_t)msec);
}

static jboolean nativeIsPlaying(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return JNI_FALSE;
    return h->player->isPlaying() ? JNI_TRUE : JNI_FALSE;
}

static void nativeSetSpeed(JNIEnv*, jclass, jlong ctx, jfloat speed) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->setSpeed((float)speed);
}

static jlong nativeGetCurrentPosition(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jlong)h->player->getCurrentPositionMs();
}

static jlong nativeGetDuration(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jlong)h->player->getDurationMs();
}

static void nativeSetVolume(JNIEnv*, jclass, jlong ctx, jfloat left, jfloat right) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return;
    h->player->setVolume(left, right);
}

static jint nativeGetVideoWidth(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jint)h->player->getVideoWidth();
}

static jint nativeGetVideoHeight(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jint)h->player->getVideoHeight();
}

static jint nativeGetVideoSarNum(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 1;
    return (jint)h->player->getVideoSarNum();
}

static jint nativeGetVideoSarDen(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 1;
    return (jint)h->player->getVideoSarDen();
}

static jint nativeGetAudioSessionId(JNIEnv*, jclass, jlong ctx) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
    if (!h) return 0;
    return (jint)h->player->getAudioSessionId();
}

static void nativeSetSurface(JNIEnv* env, jclass, jlong ctx, jobject surface) {
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
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
    auto* h = reinterpret_cast<NativeHolder*>(ctx);
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

// ---------------- JNI 注册 ----------------
static JNINativeMethod g_methods[] = {
        {"nativeCreate", "(Ljava/lang/ref/WeakReference;)J", (void*)nativeCreate},
        {"nativeSetDataSourceUri", "(JLandroid/content/Context;Ljava/lang/String;Ljava/util/Map;)V", (void*)nativeSetDataSourceUri},
        {"nativeSetDataSourcePath", "(JLjava/lang/String;)V", (void*)nativeSetDataSourcePath},
        {"nativePrepareAsync", "(J)V", (void*)nativePrepareAsync},
        {"nativeStart", "(J)V", (void*)nativeStart},
        {"nativePause", "(J)V", (void*)nativePause},
        {"nativeSeekTo", "(JJ)V", (void*)nativeSeekTo},
        {"nativeIsPlaying", "(J)Z", (void*)nativeIsPlaying},
        {"nativeSetSpeed", "(JF)V", (void*)nativeSetSpeed},
        {"nativeGetCurrentPosition", "(J)J", (void*)nativeGetCurrentPosition},
        {"nativeGetDuration", "(J)J", (void*)nativeGetDuration},
        {"nativeSetVolume", "(JFF)V", (void*)nativeSetVolume},
        {"nativeGetVideoWidth", "(J)I", (void*)nativeGetVideoWidth},
        {"nativeGetVideoHeight", "(J)I", (void*)nativeGetVideoHeight},
        {"nativeGetVideoSarNum", "(J)I", (void*)nativeGetVideoSarNum},
        {"nativeGetVideoSarDen", "(J)I", (void*)nativeGetVideoSarDen},
        {"nativeGetAudioSessionId", "(J)I", (void*)nativeGetAudioSessionId},
        {"nativeSetSurface", "(JLandroid/view/Surface;)V", (void*)nativeSetSurface},
        {"nativeRelease", "(J)V", (void*)nativeRelease},
};

jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;

    jclass cls = FindGlobalClass(env, JPATH_AXMEDIAPLAYER);
    if (!cls) return JNI_ERR;
    g_jrefs.cls_AXMediaPlayer = cls;

    g_jrefs.m_postOnPrepared = env->GetMethodID(cls, JMETHOD_postOnPrepared, JSIG_postOnPrepared);
    g_jrefs.m_postOnCompletion = env->GetMethodID(cls, JMETHOD_postOnCompletion, JSIG_postOnCompletion);
    g_jrefs.m_postOnBufferingUpdate = env->GetMethodID(cls, JMETHOD_postOnBufferingUpdate, JSIG_postOnBufferingUpdate);
    g_jrefs.m_postOnVideoSizeChanged = env->GetMethodID(cls, JMETHOD_postOnVideoSizeChanged, JSIG_postOnVideoSizeChanged);
    g_jrefs.m_postOnError = env->GetMethodID(cls, JMETHOD_postOnError, JSIG_postOnError);

    if (env->RegisterNatives(cls, g_methods, sizeof(g_methods)/sizeof(g_methods[0])) != 0) {
        ALOGE("RegisterNatives failed");
        return JNI_ERR;
    }
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
}