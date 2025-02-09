#include "android_focus.h"
#include "common.h"
#include <android/log.h>

#define LOG_TAG "AndroidAudioFocusManager"

std::function<void(AudioFocusState)> focusChangeCallback_;
extern JavaVM *javaVM;
extern jclass globalPluginClass;

AndroidAudioFocusManager::AndroidAudioFocusManager(JNIEnv *mainEnv)
    : env_(nullptr), audioFocusListener_(nullptr)
{

    JNIEnv *env;
    // Attach current thread if needed
    int getEnvResult = javaVM->GetEnv((void **)&env, JNI_VERSION_1_6);
    if (getEnvResult == JNI_EDETACHED)
    {
        if (javaVM->AttachCurrentThread(&env, nullptr) != JNI_OK)
        {
            platform_log("Failed to attach thread in AudioFocusManager");
            return;
        }
    }
    else if (getEnvResult != JNI_OK)
    {
        platform_log("Failed to get JNIEnv in AudioFocusManager");
        return;
    }

    env_ = env;

    // Use the global class reference instead of FindClass
    if (!globalPluginClass)
    {
        platform_log("Plugin class reference is null");
        return;
    }

    // Get default constructor
    jmethodID constructor = env_->GetMethodID(globalPluginClass, "<init>", "()V");
    if (!constructor)
    {
        platform_log("Failed to get plugin constructor");
        return;
    }

    // Create plugin instance for audio focus listening
    jobject localListener = env_->NewObject(globalPluginClass, constructor);
    if (!localListener)
    {
        platform_log("Failed to create plugin instance");
        return;
    }

    audioFocusListener_ = env_->NewGlobalRef(localListener);
    env_->DeleteLocalRef(localListener);
}

AndroidAudioFocusManager::~AndroidAudioFocusManager()
{
    if (env_ && audioFocusListener_)
    {
        env_->DeleteGlobalRef(audioFocusListener_);
    }
    // Detach thread if we attached it
    javaVM->DetachCurrentThread();
}

void AndroidAudioFocusManager::setAudioFocusChangeCallback(std::function<void(AudioFocusState)> callback)
{
    focusChangeCallback_ = callback;
}

extern "C"
{
    JNIEXPORT void JNICALL
    Java_flutter_soloud_flutter_1soloud_FlutterSoloudPlugin_nativeOnAudioFocusChange(
        JNIEnv *env, jobject thiz, jint focusChange)
    {
        AudioFocusState state;

        platform_log("Native audio focus changed: %d", focusChange);

        // The cases match Android's audio focus change values with our corresponding enum states
        switch (focusChange)
        {
        case 1: // AudioManager.AUDIOFOCUS_GAIN
            state = AudioFocusState::GAIN;
            break;
        case 2: // AudioManager.AUDIOFOCUS_GAIN_TRANSIENT
            state = AudioFocusState::GAIN_TRANSIENT;
            break;
        case 3: // AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE
            state = AudioFocusState::GAIN_TRANSIENT_EXCLUSIVE;
            break;
        case 4: // AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK
            state = AudioFocusState::GAIN_TRANSIENT_MAY_DUCK;
            break;
        case -1: // AudioManager.AUDIOFOCUS_LOSS
            state = AudioFocusState::LOSS;
            break;
        case -2: // AudioManager.AUDIOFOCUS_LOSS_TRANSIENT
            state = AudioFocusState::LOSS_TRANSIENT;
            break;
        case -3: // AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK
            state = AudioFocusState::LOSS_TRANSIENT_CAN_DUCK;
            break;
        default:
            state = AudioFocusState::NONE;
        }

        if (focusChangeCallback_)
        {
            platform_log("Calling focus change callback");
            focusChangeCallback_(state);
        }
        else
        {
            platform_log("No focus change callback registered");
        }
    }
}
