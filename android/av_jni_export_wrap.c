#include <jni.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// FFmpeg 提供的函数原型（静态链接进 AXFCore，符号可 hidden）
int av_jni_set_java_vm(void *vm, void *log_ctx);

// 对外导出的小门面：默认可见
__attribute__((visibility("default")))
int axf_av_jni_set_java_vm(JavaVM *vm) {
    return av_jni_set_java_vm((void*)vm, NULL);
}

#ifdef __cplusplus
}
#endif