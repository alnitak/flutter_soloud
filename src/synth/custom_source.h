#ifndef CUSTOM_SOURCE_H
#define CUSTOM_SOURCE_H

#include "../enums.h"
#include "../soloud/include/soloud.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace SoLoud {

class CustomAudioSource;

class CustomAudioSourceInstance : public AudioSourceInstance {
public:
  explicit CustomAudioSourceInstance(CustomAudioSource *aParent);
  ~CustomAudioSourceInstance() override;

  unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                        unsigned int aBufferSize) override;
  bool hasEnded() override;
  result rewind() override;

  SourceStateSnapshot *captureSourceState() override;
  void restoreSourceState(SourceStateSnapshot *aState) override;

private:
  CustomAudioSource *mParent = nullptr;
  bool mHasEnded = false;
  bool mNotifiedCreated = false;
};

class CustomAudioSource : public AudioSource {
public:
  CustomAudioSource(unsigned int sampleRate, unsigned int channels,
                    SoLoudSourceDspCallback_t callback, void *userData,
                    SoLoudSourceInstanceLifecycleCallback_t lifecycleCallback,
                    bool singleInstance, const char *webJsCallbackName = nullptr);
  ~CustomAudioSource() override;

  void setCallback(SoLoudSourceDspCallback_t callback, void *userData = nullptr,
                   SoLoudSourceInstanceLifecycleCallback_t lifecycleCallback =
                       nullptr);
  void setWebJsCallbackName(const char *jsCallbackName);

  SoLoudSourceDspCallback_t getCallback() const noexcept {
    return mCallback.load(std::memory_order_acquire);
  }
  SoLoudSourceInstanceLifecycleCallback_t getLifecycleCallback() const noexcept {
    return mLifecycleCallback.load(std::memory_order_acquire);
  }
  void *getUserData() const noexcept {
    return mUserData.load(std::memory_order_acquire);
  }
  const std::string &getWebJsCallbackName() const noexcept {
    return mWebJsCallbackName;
  }

  AudioSourceInstance *createInstance() override;

private:
  std::atomic<SoLoudSourceDspCallback_t> mCallback{nullptr};
  std::atomic<SoLoudSourceInstanceLifecycleCallback_t> mLifecycleCallback{
      nullptr};
  std::atomic<void *> mUserData{nullptr};
  std::string mWebJsCallbackName;
};

} // namespace SoLoud

#endif // CUSTOM_SOURCE_H
