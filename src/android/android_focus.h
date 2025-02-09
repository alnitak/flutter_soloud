#ifndef ANDROID_FOCUS_H
#define ANDROID_FOCUS_H

#include <jni.h>
#include <functional>

enum class AudioFocusState {
    GAIN,
    GAIN_TRANSIENT,
    GAIN_TRANSIENT_EXCLUSIVE,
    GAIN_TRANSIENT_MAY_DUCK,
    LOSS,
    LOSS_TRANSIENT,
    LOSS_TRANSIENT_CAN_DUCK,
    NONE
};

class AndroidAudioFocusManager {
public:
    AndroidAudioFocusManager(JNIEnv* env);
    ~AndroidAudioFocusManager();

    void setAudioFocusChangeCallback(std::function<void(AudioFocusState)> callback);

private:
    JNIEnv* env_;
    jobject audioManager_;
    jobject audioFocusChangeListener_;
    jobject audioFocusListener_;
};

#ifdef __cplusplus
extern "C" {
#endif

extern jclass globalPluginClass;

#ifdef __cplusplus
}
#endif

#endif // ANDROID_FOCUS_H
