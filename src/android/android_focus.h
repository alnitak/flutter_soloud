#ifndef ANDROID_FOCUS_H
#define ANDROID_FOCUS_H

#include <jni.h>
#include <functional>

enum class AudioFocusState {
    GAIN,
    LOSS,
    LOSS_TRANSIENT,
    LOSS_TRANSIENT_CAN_DUCK,
    NONE
};

class AndroidAudioFocusManager {
public:
    explicit AndroidAudioFocusManager(JavaVM* vm);
    ~AndroidAudioFocusManager();

    void setAudioFocusChangeCallback(std::function<void(AudioFocusState)> callback);

private:
    JavaVM* javaVM_;
    jobject audioFocusListener_;
    JNIEnv* getEnv();
};

#endif // ANDROID_FOCUS_H
