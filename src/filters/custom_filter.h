#ifndef CUSTOM_FILTER_H
#define CUSTOM_FILTER_H

#include "../enums.h"
#include "../soloud/include/soloud.h"
#include "../soloud/include/soloud_filter.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace SoLoud {

class CustomFilter;

class CustomFilterInstance : public FilterInstance {
public:
  explicit CustomFilterInstance(CustomFilter *aParent);
  ~CustomFilterInstance() override = default;

  void filter(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize,
              unsigned int aChannels, float aSamplerate, time aTime) override;

  FilterStateSnapshot *captureState() override;
  void restoreState(FilterStateSnapshot *aState) override;

private:
  void invokeCallback(float *aBuffer, unsigned int aSamples,
                      unsigned int aBufferSize, unsigned int aChannels,
                      float aSamplerate, time aTime);

  CustomFilter *mParent;
  std::vector<float> mDryScratch;
};

class CustomFilter : public Filter {
public:
  static constexpr unsigned int kMaxParams = 16;

  enum FILTERATTRIBUTE {
    WET = 0,
    PARAM_1 = 1,
    PARAM_2 = 2,
    PARAM_3 = 3,
    PARAM_4 = 4,
    PARAM_5 = 5,
    PARAM_6 = 6,
    PARAM_7 = 7,
    PARAM_8 = 8,
    PARAM_9 = 9,
    PARAM_10 = 10,
    PARAM_11 = 11,
    PARAM_12 = 12,
    PARAM_13 = 13,
    PARAM_14 = 14,
    PARAM_15 = 15
  };

  CustomFilter();
  ~CustomFilter() override = default;

  void setCallback(SoLoudFilterDspCallback_t callback, void *userData = nullptr,
                   unsigned int paramCount = kMaxParams);
  void setWebJsCallbackName(const char *jsCallbackName);
  void setParamDefinition(unsigned int paramIndex, float defaultVal,
                          float minVal, float maxVal,
                          const char *name = nullptr);

  SoLoudFilterDspCallback_t getCallback() const noexcept {
    return mCallback.load(std::memory_order_acquire);
  }
  void *getUserData() const noexcept {
    return mUserData.load(std::memory_order_acquire);
  }
  const std::string &getWebJsCallbackName() const noexcept {
    return mWebJsCallbackName;
  }

  int getParamCount() override;
  const char *getParamName(unsigned int aParamIndex) override;
  unsigned int getParamType(unsigned int aParamIndex) override;
  float getParamMax(unsigned int aParamIndex) override;
  float getParamMin(unsigned int aParamIndex) override;
  float getParamDefault(unsigned int aParamIndex) const noexcept;

  FilterInstance *createInstance() override;

private:
  std::atomic<SoLoudFilterDspCallback_t> mCallback{nullptr};
  std::atomic<void *> mUserData{nullptr};
  std::string mWebJsCallbackName;
  unsigned int mParamCount = kMaxParams;
  std::array<float, kMaxParams> mDefaultValues;
  std::array<float, kMaxParams> mMinValues;
  std::array<float, kMaxParams> mMaxValues;
  std::array<std::string, kMaxParams> mParamNames;
};

} // namespace SoLoud

#endif // CUSTOM_FILTER_H
