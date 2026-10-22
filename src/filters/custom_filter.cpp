#include "custom_filter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace SoLoud {

struct CustomFilterSnapshot : public FilterStateSnapshot {
  unsigned int mNumParams = 0;
  uint64_t mParamChanged = 0;
  std::array<float, CustomFilter::kMaxParams> mParams{};
  std::array<Fader, CustomFilter::kMaxParams> mFaders{};
  std::array<float, MAX_CHANNELS> mSvfLow{};
  std::array<float, MAX_CHANNELS> mSvfBand{};
};

CustomFilterInstance::CustomFilterInstance(CustomFilter *aParent)
    : mParent(aParent) {
  const unsigned int numParams =
      mParent != nullptr
          ? static_cast<unsigned int>(mParent->getParamCount())
          : CustomFilter::kMaxParams;
  initParams(static_cast<int>(numParams));
  for (unsigned int i = 0; i < numParams; ++i) {
    mParam[i] = mParent != nullptr ? mParent->getParamDefault(i)
                                   : (i == 0 ? 1.0f : 0.0f);
  }
}

FilterStateSnapshot *CustomFilterInstance::captureState() {
  auto *snap = new CustomFilterSnapshot();
  snap->mNumParams =
      std::min<unsigned int>(mNumParams, CustomFilter::kMaxParams);
  snap->mParamChanged = mParamChanged;
  for (unsigned int i = 0; i < snap->mNumParams; ++i) {
    snap->mParams[i] = mParam[i];
    snap->mFaders[i] = mParamFader[i];
  }
  snap->mSvfLow = mSvfLow;
  snap->mSvfBand = mSvfBand;
  return snap;
}

void CustomFilterInstance::restoreState(FilterStateSnapshot *aState) {
  const auto *snap = static_cast<const CustomFilterSnapshot *>(aState);
  if (snap == nullptr) {
    return;
  }
  const unsigned int count = std::min<unsigned int>(snap->mNumParams, mNumParams);
  mParamChanged = snap->mParamChanged;
  for (unsigned int i = 0; i < count; ++i) {
    mParam[i] = snap->mParams[i];
    mParamFader[i] = snap->mFaders[i];
  }
  mSvfLow = snap->mSvfLow;
  mSvfBand = snap->mSvfBand;
}

void CustomFilterInstance::invokeCallback(float *aBuffer, unsigned int aSamples,
                                          unsigned int aBufferSize,
                                          unsigned int aChannels,
                                          float aSamplerate, time aTime) {
  if (mParent == nullptr) {
    return;
  }
  SoLoudFilterDspCallback_t cb = mParent->getCallback();
  void *userData = mParent->getUserData();
  if (cb != nullptr) {
    cb(aBuffer, aSamples, aBufferSize, aChannels, aSamplerate, aTime, mParam,
       mParamChanged, mNumParams, userData);
    return;
  }

  const std::string &jsFn = mParent->getWebJsCallbackName();
#ifdef __EMSCRIPTEN__
  if (!jsFn.empty()) {
    const uint32_t maskLo = static_cast<uint32_t>(mParamChanged & 0xFFFFFFFFULL);
    const uint32_t maskHi = static_cast<uint32_t>(mParamChanged >> 32);
    EM_ASM(
        {
          var fn = globalThis[UTF8ToString($0)];
          if (typeof fn === "function") {
            fn($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11);
          }
        },
        jsFn.c_str(), aBuffer, aSamples, aBufferSize, aChannels,
        static_cast<double>(aSamplerate), aTime, mParam, maskLo, maskHi,
        mNumParams, userData);
    return;
  }
#endif

  // Built-in native DSP reference implementations for cross-platform workbench
  // and named DSP presets when no native function pointer was supplied.
  if (jsFn == "soloudOverdriveFilterCallback") {
    const float drive =
        (mNumParams > 1 && mParam[1] >= 1.0f) ? mParam[1] : 2.5f;
    const float asym = (mNumParams > 2) ? mParam[2] : 0.0f;
    const float dcOffset = std::tanh(asym);
    const float norm = 1.0f / std::max(0.35f, std::tanh(drive));
    for (unsigned int ch = 0; ch < aChannels; ++ch) {
      float *dst = aBuffer + static_cast<size_t>(ch) * aBufferSize;
      for (unsigned int i = 0; i < aSamples; ++i) {
        const float x = dst[i];
        dst[i] = std::clamp((std::tanh(x * drive + asym) - dcOffset) * norm,
                            -1.0f, 1.0f);
      }
    }
  } else if (jsFn == "soloudSvfFilterCallback") {
    const float sr = aSamplerate > 0.0f ? aSamplerate : 48000.0f;
    const float cutoff = std::clamp(
        (mNumParams > 1 && mParam[1] > 20.0f) ? mParam[1] : 1200.0f, 40.0f,
        sr * 0.45f);
    const float res = std::clamp(
        (mNumParams > 2 && mParam[2] > 0.05f) ? mParam[2] : 0.4f, 0.05f, 0.95f);
    const float f = std::min(
        0.95f,
        2.0f * std::sin(static_cast<float>(M_PI) * cutoff / (sr * 2.0f)));
    const float q = 1.0f - res * 0.85f;
    const unsigned int maxCh = std::min<unsigned int>(aChannels, MAX_CHANNELS);
    for (unsigned int ch = 0; ch < maxCh; ++ch) {
      float *dst = aBuffer + static_cast<size_t>(ch) * aBufferSize;
      float low = mSvfLow[ch];
      float band = mSvfBand[ch];
      for (unsigned int i = 0; i < aSamples; ++i) {
        const float in = dst[i];
        // 2x oversampled Chamberlin SVF for stability up to Nyquist/2
        for (int step = 0; step < 2; ++step) {
          low += f * band;
          const float high = in - low - q * band;
          band += f * high;
        }
        low = std::clamp(low, -2.0f, 2.0f);
        band = std::clamp(band, -2.0f, 2.0f);
        dst[i] = low;
      }
      mSvfLow[ch] = low;
      mSvfBand[ch] = band;
    }
  }
}

void CustomFilterInstance::filter(float *aBuffer, unsigned int aSamples,
                                  unsigned int aBufferSize,
                                  unsigned int aChannels, float aSamplerate,
                                  time aTime) {
  if (aBuffer == nullptr || aSamples == 0 || aChannels == 0 ||
      mParent == nullptr) {
    return;
  }

  updateParams(aTime);
  const float wet = mParam[CustomFilter::WET];

  // Branch 1: Hard bypass when wet <= 0.0f.
  if (wet <= 0.0f) {
    mParamChanged = 0;
    return;
  }

  // Branch 2: 100% wet (wet >= 1.0f) — process in-place with zero copies.
  if (wet >= 1.0f) {
    invokeCallback(aBuffer, aSamples, aBufferSize, aChannels, aSamplerate,
                   aTime);
    mParamChanged = 0;
    return;
  }

  // Branch 3: Partial blend (0.0f < wet < 1.0f) — copy dry signal into
  // pre-allocated scratch, invoke callback in-place, and blend.
  const size_t totalSamples = static_cast<size_t>(aSamples) * aChannels;
  if (mDryScratch.size() < totalSamples) {
    mDryScratch.resize(totalSamples);
  }

  for (unsigned int ch = 0; ch < aChannels; ++ch) {
    std::memcpy(mDryScratch.data() + static_cast<size_t>(ch) * aSamples,
                aBuffer + static_cast<size_t>(ch) * aBufferSize,
                sizeof(float) * aSamples);
  }

  invokeCallback(aBuffer, aSamples, aBufferSize, aChannels, aSamplerate, aTime);

  for (unsigned int ch = 0; ch < aChannels; ++ch) {
    const float *dry = mDryScratch.data() + static_cast<size_t>(ch) * aSamples;
    float *out = aBuffer + static_cast<size_t>(ch) * aBufferSize;
    for (unsigned int i = 0; i < aSamples; ++i) {
      out[i] = dry[i] + (out[i] - dry[i]) * wet;
    }
  }

  mParamChanged = 0;
}

// /////////////////////////////////////////////////////////////////////////////
// CustomFilter
// /////////////////////////////////////////////////////////////////////////////

CustomFilter::CustomFilter() {
  mDefaultValues.fill(0.0f);
  mMinValues.fill(-1000000.0f);
  mMaxValues.fill(1000000.0f);

  mDefaultValues[WET] = 1.0f;
  mMinValues[WET] = 0.0f;
  mMaxValues[WET] = 1.0f;
  mParamNames[WET] = "Wet";

  for (unsigned int i = 1; i < kMaxParams; ++i) {
    mParamNames[i] = "Param" + std::to_string(i);
  }
}

void CustomFilter::setCallback(SoLoudFilterDspCallback_t callback,
                               void *userData, unsigned int paramCount) {
  mParamCount =
      std::clamp<unsigned int>(paramCount, 1u, CustomFilter::kMaxParams);
  mUserData.store(userData, std::memory_order_release);
  mCallback.store(callback, std::memory_order_release);
}

void CustomFilter::setWebJsCallbackName(const char *jsCallbackName) {
  if (jsCallbackName != nullptr) {
    mWebJsCallbackName = jsCallbackName;
  } else {
    mWebJsCallbackName.clear();
  }
}

void CustomFilter::setParamDefinition(unsigned int paramIndex, float defaultVal,
                                      float minVal, float maxVal,
                                      const char *name) {
  if (paramIndex >= kMaxParams) {
    return;
  }
  if (paramIndex == WET) {
    mDefaultValues[WET] = std::clamp(defaultVal, 0.0f, 1.0f);
    mMinValues[WET] = 0.0f;
    mMaxValues[WET] = 1.0f;
  } else {
    mDefaultValues[paramIndex] = defaultVal;
    mMinValues[paramIndex] = minVal;
    mMaxValues[paramIndex] = maxVal;
  }
  if (name != nullptr && name[0] != '\0') {
    mParamNames[paramIndex] = name;
  }
}

int CustomFilter::getParamCount() { return static_cast<int>(mParamCount); }

const char *CustomFilter::getParamName(unsigned int aParamIndex) {
  if (aParamIndex < kMaxParams) {
    return mParamNames[aParamIndex].c_str();
  }
  return "Param";
}

unsigned int CustomFilter::getParamType(unsigned int /*aParamIndex*/) {
  return Filter::FLOAT_PARAM;
}

float CustomFilter::getParamMax(unsigned int aParamIndex) {
  if (aParamIndex < kMaxParams) {
    return mMaxValues[aParamIndex];
  }
  return 1.0f;
}

float CustomFilter::getParamMin(unsigned int aParamIndex) {
  if (aParamIndex < kMaxParams) {
    return mMinValues[aParamIndex];
  }
  return 0.0f;
}

float CustomFilter::getParamDefault(unsigned int aParamIndex) const noexcept {
  if (aParamIndex < kMaxParams) {
    return mDefaultValues[aParamIndex];
  }
  return 0.0f;
}

FilterInstance *CustomFilter::createInstance() {
  return new CustomFilterInstance(this);
}

} // namespace SoLoud
