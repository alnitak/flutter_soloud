#include "jni_helper.h"

JavaVM* JNIHelper::javaVM = nullptr;
jobject JNIHelper::globalContext = nullptr;

void JNIHelper::init(JavaVM* vm) {
    javaVM = vm;
}

JNIEnv* JNIHelper::getEnv() {
    if (!javaVM) {
        __android_log_print(ANDROID_LOG_ERROR, "JNIHelper", "JavaVM is null");
        return nullptr;
    }

    JNIEnv* env = nullptr;
    jint result = javaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
    
    if (result == JNI_EDETACHED) {
        if (javaVM->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "JNIHelper", "Failed to attach thread");
            return nullptr;
        }
    }
    
    return env;
}

void JNIHelper::setContext(jobject context) {
    JNIEnv* env = getEnv();
    if (env && context) {
        if (globalContext) {
            env->DeleteGlobalRef(globalContext);
        }
        globalContext = env->NewGlobalRef(context);
    }
}

jobject JNIHelper::getContext() {
    return globalContext;
}

jobject JNIHelper::getContextFromFlutterEngine(JNIEnv* env, jobject flutter_engine) {
    // Get FlutterEngine class
    jclass flutter_engine_class = env->GetObjectClass(flutter_engine);
    
    // Get getContext method ID
    jmethodID get_context_method = env->GetMethodID(flutter_engine_class, "getContext", 
        "()Landroid/content/Context;");
    
    // Call getContext()
    jobject context = env->CallObjectMethod(flutter_engine, get_context_method);
    
    // Clean up local reference
    env->DeleteLocalRef(flutter_engine_class);
    
    return context;
}
