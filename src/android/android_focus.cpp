#include "android_focus.h"
#include "common.h"
#include <android/log.h>

#define LOG_TAG "AndroidAudioFocusManager"

std::function<void(AudioFocusState)> focusChangeCallback_;

JNIEnv* AndroidAudioFocusManager::getEnv() {
    JNIEnv* env;
    int getEnvStat = javaVM_->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvStat == JNI_EDETACHED) {
        if (javaVM_->AttachCurrentThread(&env, nullptr) != 0) {
            platform_log("Failed to attach thread");
            return nullptr;
        }
    }
    return env;
}

AndroidAudioFocusManager::AndroidAudioFocusManager(JavaVM* vm)
    : javaVM_(vm), audioFocusListener_(nullptr) {
    JNIEnv* env = getEnv();
    if (!env) return;

    // Get FlutterSoloudPlugin class - Fixed class path format
    jclass pluginClass = env->FindClass("flutter/soloud/flutter_soloud/FlutterSoloudPlugin");
    if (!pluginClass) {
        // Try alternative path format if first attempt fails
        pluginClass = env->FindClass("flutter.soloud.flutter_soloud.FlutterSoloudPlugin");
        if (!pluginClass) {
            platform_log("Failed to find FlutterSoloudPlugin class");
            return;
        }
    }

    // Get default constructor
    jmethodID constructor = env->GetMethodID(pluginClass, "<init>", "()V");
    if (!constructor) {
        platform_log("Failed to get plugin constructor");
        return;
    }

    // Create plugin instance for audio focus listening
    jobject localListener = env->NewObject(pluginClass, constructor);
    if (!localListener) {
        platform_log("Failed to create plugin instance");
        return;
    }

    audioFocusListener_ = env->NewGlobalRef(localListener);
    env->DeleteLocalRef(localListener);
}

AndroidAudioFocusManager::~AndroidAudioFocusManager() {
    JNIEnv* env = getEnv();
    if (env && audioFocusListener_) {
        env->DeleteGlobalRef(audioFocusListener_);
    }
}

void AndroidAudioFocusManager::setAudioFocusChangeCallback(std::function<void(AudioFocusState)> callback) {
    focusChangeCallback_ = callback;
}

extern "C" {
    JNIEXPORT void JNICALL
    // Updated JNI function name to match package structure
    Java_flutter_soloud_flutter_1soloud_FlutterSoloudPlugin_nativeOnAudioFocusChange(
            JNIEnv* env, jobject thiz, jint focusChange) {
        AudioFocusState state;
        switch (focusChange) {
            case 1:  // AUDIOFOCUS_GAIN
                state = AudioFocusState::GAIN;
                break;
            case 2:  // AUDIOFOCUS_LOSS
                state = AudioFocusState::LOSS;
                break;
            case 3:  // AUDIOFOCUS_LOSS_TRANSIENT
                state = AudioFocusState::LOSS_TRANSIENT;
                break;
            case 4:  // AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK
                state = AudioFocusState::LOSS_TRANSIENT_CAN_DUCK;
                break;
            default:
                state = AudioFocusState::NONE;
        }
        
        if (focusChangeCallback_) {
            focusChangeCallback_(state);
        }
    }
}
