// AXLog.h
#ifndef AXPLAYERLIB_AXLOG_H
#define AXPLAYERLIB_AXLOG_H

#include <android/log.h>

// 模块名可以统一用 "AXPlayer"，也可以在不同文件自己传 TAG
#ifndef AX_LOG_TAG
#define AX_LOG_TAG "AXPlayer"
#endif

#define AX_LOGI(...)  __android_log_print(ANDROID_LOG_INFO,  AX_LOG_TAG, __VA_ARGS__)
#define AX_LOGW(...)  __android_log_print(ANDROID_LOG_WARN,  AX_LOG_TAG, __VA_ARGS__)
#define AX_LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, AX_LOG_TAG, __VA_ARGS__)

#endif // AXPLAYERLIB_AXLOG_H