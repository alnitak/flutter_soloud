#include "custom_source.h"

#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace SoLoud {

struct CustomAudioSourceSnapshot : public SourceStateSnapshot {
  bool mHasEnded = false;
  bool mNotifiedCreated = false;
};

CustomAudioSourceInstance::CustomAudioSourceInstance(CustomAudioSource *aParent)
    : mParent(aParent) {
  mFlags |= AudioSourceInstance::REALTIME_DIRECT;
  if (mParent != nullptr) {
    mBaseSamplerate = mParent->mBaseSamplerate;
    mSamplerate = mBaseSamplerate;
    mChannels = mParent->mChannels;
  }
}

CustomAudioSourceInstance::~CustomAudioSourceInstance() {
  if (mParent != nullptr && mNotifiedCreated) {
    SoLoudSourceInstanceLifecycleCallback_t lc =
        mParent->getLifecycleCallback();
    if (lc != nullptr) {
      lc(0, mPlayIndex, mParent->getUserData());
    }
  }
}

SourceStateSnapshot *CustomAudioSourceInstance::captureSourceState() {
  auto *snap = new CustomAudioSourceSnapshot();
  snap->mHasEnded = mHasEnded;
  snap->mNotifiedCreated = mNotifiedCreated;
  return snap;
}

void CustomAudioSourceInstance::restoreSourceState(
    SourceStateSnapshot *aState) {
  const auto *snap = static_cast<const CustomAudioSourceSnapshot *>(aState);
  if (snap == nullptr) {
    return;
  }
  mHasEnded = snap->mHasEnded;
  mNotifiedCreated = snap->mNotifiedCreated;
}

unsigned int CustomAudioSourceInstance::getAudio(float *aBuffer,
                                                 unsigned int aSamplesToRead,
                                                 unsigned int aBufferSize) {
  if (aBuffer == nullptr || mChannels == 0 || aSamplesToRead == 0) {
    return 0;
  }

  if (mParent == nullptr) {
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      std::memset(aBuffer + ch * aBufferSize, 0,
                  sizeof(float) * aSamplesToRead);
    }
    mHasEnded = true;
    return 0;
  }

  void *userData = mParent->getUserData();

  if (!mNotifiedCreated) {
    mNotifiedCreated = true;
    SoLoudSourceInstanceLifecycleCallback_t lc =
        mParent->getLifecycleCallback();
    if (lc != nullptr) {
      lc(1, mPlayIndex, userData);
    }
  }

  uint32_t outHasEnded = mHasEnded ? 1u : 0u;
  SoLoudSourceDspCallback_t cb = mParent->getCallback();
  if (cb != nullptr) {
    const unsigned int written =
        cb(aBuffer, aSamplesToRead, aBufferSize, mChannels, mSamplerate,
           mStreamTime, mStreamPosition, &outHasEnded, userData);
    if (outHasEnded != 0) {
      mHasEnded = true;
    }
    return written;
  }

  const std::string &jsFn = mParent->getWebJsCallbackName();
#ifdef __EMSCRIPTEN__
  if (!jsFn.empty()) {
    const int written = EM_ASM_INT(
        {
          var fn = globalThis[UTF8ToString($0)];
          if (typeof fn === "function") {
            return fn($1, $2, $3, $4, $5, $6, $7, $8, $9) | 0;
          }
          return 0;
        },
        jsFn.c_str(), aBuffer, aSamplesToRead, aBufferSize, mChannels,
        static_cast<double>(mSamplerate), mStreamTime, mStreamPosition,
        &outHasEnded, userData);
    if (outHasEnded != 0) {
      mHasEnded = true;
    }
    return static_cast<unsigned int>(written >= 0 ? written : 0);
  }
#endif

  // Leave this JS callback here for testing
  if (jsFn == "soloudCustomSynthCallback") {
    const double sr =
        mSamplerate > 0.0f ? static_cast<double>(mSamplerate) : 48000.0;
    const double invSr = 1.0 / sr;
    constexpr double kTwoPi = 2.0 * M_PI;
    // Procedural stereo warm A-minor9 pad with slow LFO modulation
    for (unsigned int i = 0; i < aSamplesToRead; ++i) {
      const double t = mStreamPosition + static_cast<double>(i) * invSr;
      const double lfo = 1.0 + 0.003 * std::sin(kTwoPi * 0.4 * t);
      const float sL =
          static_cast<float>(0.18 * std::sin(kTwoPi * 110.0 * t) +
                             0.14 * std::sin(kTwoPi * 164.81 * lfo * t) +
                             0.12 * std::sin(kTwoPi * 220.0 * t) +
                             0.10 * std::sin(kTwoPi * 329.63 * t));
      const float sR =
          static_cast<float>(0.18 * std::sin(kTwoPi * 110.2 * t) +
                             0.14 * std::sin(kTwoPi * 196.0 * t) +
                             0.12 * std::sin(kTwoPi * 246.94 * lfo * t) +
                             0.10 * std::sin(kTwoPi * 329.9 * t));
      aBuffer[i] = sL;
      for (unsigned int ch = 1; ch < mChannels; ++ch) {
        aBuffer[ch * aBufferSize + i] = (ch & 1u) ? sR : sL;
      }
    }
    return aSamplesToRead;
  }

  for (unsigned int ch = 0; ch < mChannels; ++ch) {
    std::memset(aBuffer + ch * aBufferSize, 0, sizeof(float) * aSamplesToRead);
  }
  return 0;
}

bool CustomAudioSourceInstance::hasEnded() { return mHasEnded; }

result CustomAudioSourceInstance::rewind() {
  mStreamPosition = 0.0;
  mHasEnded = false;
  return SO_NO_ERROR;
}

// /////////////////////////////////////////////////////////////////////////////
// CustomAudioSource
// /////////////////////////////////////////////////////////////////////////////

CustomAudioSource::CustomAudioSource(
    unsigned int sampleRate, unsigned int channels,
    SoLoudSourceDspCallback_t callback, void *userData,
    SoLoudSourceInstanceLifecycleCallback_t lifecycleCallback,
    bool singleInstance, const char *webJsCallbackName) {
  mBaseSamplerate = static_cast<float>(sampleRate > 0 ? sampleRate : 48000);
  mChannels = channels > 0 ? channels : 2;
  mCallback.store(callback, std::memory_order_release);
  mUserData.store(userData, std::memory_order_release);
  mLifecycleCallback.store(lifecycleCallback, std::memory_order_release);
  if (singleInstance) {
    mFlags |= AudioSource::SINGLE_INSTANCE;
  }
  if (webJsCallbackName != nullptr) {
    mWebJsCallbackName = webJsCallbackName;
  }
}

CustomAudioSource::~CustomAudioSource() { stop(); }

void CustomAudioSource::setCallback(
    SoLoudSourceDspCallback_t callback, void *userData,
    SoLoudSourceInstanceLifecycleCallback_t lifecycleCallback) {
  mUserData.store(userData, std::memory_order_release);
  mLifecycleCallback.store(lifecycleCallback, std::memory_order_release);
  mCallback.store(callback, std::memory_order_release);
}

void CustomAudioSource::setWebJsCallbackName(const char *jsCallbackName) {
  if (jsCallbackName != nullptr) {
    mWebJsCallbackName = jsCallbackName;
  } else {
    mWebJsCallbackName.clear();
  }
}

AudioSourceInstance *CustomAudioSource::createInstance() {
  return new CustomAudioSourceInstance(this);
}

} // namespace SoLoud
