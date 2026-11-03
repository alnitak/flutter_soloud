#include "parametric_eq_filter.h"
#include "soloud.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string.h>
#include <string>

#if defined(SOLOUD_SSE_INTRINSICS)
#include <emmintrin.h>
#elif defined(SOLOUD_NEON_INTRINSICS)
#include <arm_neon.h>
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
#include <wasm_simd128.h>
#endif

// MSVC fix: Undefine min/max macros that may be defined by Windows.h
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

void ParametricEqInstance::setWindowSize(int aWindowSize) {
  mWinSize = aWindowSize;
  mWinHalf = aWindowSize >> 1;
  mWinTwice = aWindowSize << 1;
  mFFTScale = aWindowSize > 0 ? 1.0f / (float)aWindowSize : 0.0f;
}

ParametricEqInstance::ParametricEqInstance(ParametricEq *aParent) {
  mParent = aParent;

  // Initialize pointers to null before first allocation
  mFFTSetup = nullptr;
  mFFTBuffer = nullptr;
  mFFTWork = nullptr;
  mTemp = nullptr;
  for (int i = 0; i < MAX_CHANNELS; i++) {
    mInputBuffer[i] = nullptr;
    mMixBuffer[i] = nullptr;
  }

  initParams(ParametricEq::NUM_PARAMS);

  // Each instance owns its window size (see setFilterParameter).
  setWindowSize(mParent->mSTFT_WINDOW_SIZE);

  // Initialize FFT setup and allocate buffers
  initFFTBuffers();

  // Initialize band parameters (gains, frequencies, Q factors)
  initBandParameters();

  mParam[0] = mParent->mWet;              // wet param (index 0)
  mParam[1] = mWinSize; // window size param (index 1)
  mParam[2] = mParent->mBands;            // band count param (index 2)
}

void ParametricEqInstance::comp2MagPhase(float *aFFTBuffer,
                                         unsigned int aSamples) {
  for (unsigned int i = 0; i < aSamples; i++) {
    float re = aFFTBuffer[i * 2];
    float im = aFFTBuffer[i * 2 + 1];
    aFFTBuffer[i * 2] = sqrtf(re * re + im * im);
    aFFTBuffer[i * 2 + 1] = atan2f(im, re);
  }
}

void ParametricEqInstance::magPhase2Comp(float *aFFTBuffer,
                                         unsigned int aSamples) {
  for (unsigned int i = 0; i < aSamples; i++) {
    float mag = aFFTBuffer[i * 2];
    float phase = aFFTBuffer[i * 2 + 1];
    aFFTBuffer[i * 2] = mag * cosf(phase);
    aFFTBuffer[i * 2 + 1] = mag * sinf(phase);
  }
}

void ParametricEqInstance::initBandParameters() {
  mBands = mParent->mBands;

  // Copy band gains, frequencies, and Q factors into parameter slots
  for (unsigned int i = 0; i < ParametricEq::MAX_BANDS; i++) {
    mParam[ParametricEq::BAND_GAIN_OFFSET + i] = mParent->mGain[i];
    mParam[ParametricEq::BAND_FREQ_OFFSET + i] = mParent->mFreq[i];
    mParam[ParametricEq::BAND_Q_OFFSET + i] = mParent->mQ[i];
  }
}

void ParametricEqInstance::initFFTBuffers() {
  // Safety check: ensure window size is valid
  // PFFFT requires power-of-2 sizes, minimum 16
  if (mWinSize < 16 || mWinSize > 65536) {
    return; // Invalid window size, don't reallocate
  }

  // Free existing FFT resources if any
  if (mFFTSetup != nullptr) {
    pffft_destroy_setup(mFFTSetup);
    mFFTSetup = nullptr;
  }
  if (mFFTBuffer != nullptr) {
    pffft_aligned_free(mFFTBuffer);
    mFFTBuffer = nullptr;
  }
  if (mFFTWork != nullptr) {
    pffft_aligned_free(mFFTWork);
    mFFTWork = nullptr;
  }
  if (mTemp != nullptr) {
    pffft_aligned_free(mTemp);
    mTemp = nullptr;
  }

  // Free and reallocate channel buffers for all possible channels (up to
  // MAX_CHANNELS) outside the real-time audio path
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (mInputBuffer[i] != nullptr) {
      delete[] mInputBuffer[i];
      mInputBuffer[i] = nullptr;
    }
    if (mMixBuffer[i] != nullptr) {
      delete[] mMixBuffer[i];
      mMixBuffer[i] = nullptr;
    }
    mInputBuffer[i] = new float[mWinTwice]();
    mMixBuffer[i] = new float[mWinTwice]();
    // Reset offsets
    mInputOffset[i] = mWinSize;
    mMixOffset[i] = mWinHalf;
    mReadOffset[i] = 0;
  }

  // Precompute scaled Hann window table once per window-size change
  const int winSize = mWinSize;
  mHannWindow.resize(winSize);
  const float scale = mFFTScale;
  for (int i = 0; i < winSize; i++) {
    mHannWindow[i] =
        0.5f * (1.0f - cosf((2.0f * (float)M_PI * i) / (float)winSize)) * scale;
  }
  mBinGainTable.assign(winSize, 1.0f);
  mGainTableDirty = true;

  // Initialize FFT setup for complex transforms
  mFFTSetup = pffft_new_setup(mWinSize, PFFFT_COMPLEX);

  // Allocate aligned buffers for FFT
  mFFTBuffer = (float *)pffft_aligned_malloc(mWinTwice *
                                             sizeof(float));
  mFFTWork = (float *)pffft_aligned_malloc(mWinTwice *
                                           sizeof(float));
  mTemp = (float *)pffft_aligned_malloc(mWinTwice *
                                        sizeof(float));

  mParam[1] = mWinSize; // Update window size param (index 1)
}

ParametricEqInstance::~ParametricEqInstance() {
  // Free PFFFT resources
  if (mFFTSetup != nullptr) {
    pffft_destroy_setup(mFFTSetup);
    mFFTSetup = nullptr;
  }

  // Free aligned buffers
  if (mFFTBuffer != nullptr) {
    pffft_aligned_free(mFFTBuffer);
    mFFTBuffer = nullptr;
  }
  if (mFFTWork != nullptr) {
    pffft_aligned_free(mFFTWork);
    mFFTWork = nullptr;
  }
  if (mTemp != nullptr) {
    pffft_aligned_free(mTemp);
    mTemp = nullptr;
  }

  // Free channel buffers
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (mInputBuffer[i] != nullptr) {
      delete[] mInputBuffer[i];
      mInputBuffer[i] = nullptr;
    }
    if (mMixBuffer[i] != nullptr) {
      delete[] mMixBuffer[i];
      mMixBuffer[i] = nullptr;
    }
  }
}

void ParametricEqInstance::setFilterParameter(unsigned int aAttributeId,
                                              float aValue) {
  if (aAttributeId >= mNumParams)
    return;

  mParamFader[aAttributeId].mActive = 0;

  switch (aAttributeId) {
  case 0: // wet
    if (mParam[0] == aValue)
      return;
    mParam[0] = aValue;
    mParent->mWet = aValue;
    break;

  case 1: { // STFT_WINDOW_SIZE
    const int requested = (int)aValue;
    if (mWinSize == requested)
      return;
    // PFFFT needs a power of two in [16, 65536]. Reject anything else
    // before touching the window state, so it always matches the buffers.
    if (requested < 16 || requested > 65536 ||
        (requested & (requested - 1)) != 0)
      return;
    // Only this instance's buffers are rebuilt, so only this instance takes
    // the new size. Other instances of the same filter (other voices of the
    // sound) keep their own, consistent size; the parent's value is just the
    // default for instances created from now on.
    setWindowSize(requested);
    mParam[1] = mWinSize;
    mParent->mSTFT_WINDOW_SIZE = mWinSize;
    mParent->mSTFT_WINDOW_HALF = mWinHalf;
    mParent->mSTFT_WINDOW_TWICE = mWinTwice;
    mParent->mFFT_SCALE = mFFTScale;
    initFFTBuffers();
    break;
  }

  case 2: // nBands
    if (mParent->mBands == (unsigned int)aValue)
      return;
    mBands = mParent->mBands = (int)aValue;
    mParam[2] = mBands;
    // Update parent's frequencies and Q defaults for the new band count
    mParent->setFreqs((unsigned int)aValue);
    // Refresh band parameters in mParam
    initBandParameters();
    break;

  default:
    if (aAttributeId >= ParametricEq::BAND_GAIN_OFFSET &&
        aAttributeId < ParametricEq::BAND_FREQ_OFFSET) {
      // Band Gain
      unsigned int idx = aAttributeId - ParametricEq::BAND_GAIN_OFFSET;
      mParam[aAttributeId] = aValue;
      if (idx < ParametricEq::MAX_BANDS)
        mParent->mGain[idx] = aValue;
    } else if (aAttributeId >= ParametricEq::BAND_FREQ_OFFSET &&
               aAttributeId < ParametricEq::BAND_Q_OFFSET) {
      // Band Frequency
      unsigned int idx = aAttributeId - ParametricEq::BAND_FREQ_OFFSET;
      mParam[aAttributeId] = aValue;
      if (idx < ParametricEq::MAX_BANDS)
        mParent->mFreq[idx] = aValue;
    } else if (aAttributeId >= ParametricEq::BAND_Q_OFFSET &&
               aAttributeId < ParametricEq::NUM_PARAMS) {
      // Band Q
      unsigned int idx = aAttributeId - ParametricEq::BAND_Q_OFFSET;
      mParam[aAttributeId] = aValue;
      if (idx < ParametricEq::MAX_BANDS)
        mParent->mQ[idx] = aValue;
    }
    break;
  }

  mParamChanged |= 1ULL << (aAttributeId < 63 ? aAttributeId : 63);
  mGainTableDirty = true;
}

void ParametricEqInstance::updateBinGainTable(unsigned int aSamples,
                                              float aSamplerate) {
  mCachedSampleRate = aSamplerate;
  mGainTableDirty = false;
  mParamChanged = 0;

  if (mBinGainTable.size() != aSamples) {
    mBinGainTable.assign(aSamples, 1.0f);
  }

  float wet = mParam[0];
  if (wet <= 0.0f) {
    mHasActiveBands = false;
    return;
  }

  struct ActiveBand {
    float gain;
    float log_fc;
    float inv_two_sigma_sq;
  };
  ActiveBand activeBands[ParametricEq::MAX_BANDS];
  int numActiveBands = 0;

  float nyquist = aSamplerate * 0.5f;

  for (int b = 0; b < mBands; b++) {
    float gain = mParam[ParametricEq::BAND_GAIN_OFFSET + b];
    if (std::fabs(gain - 1.0f) < 0.001f) {
      continue;
    }

    float freq = mParam[ParametricEq::BAND_FREQ_OFFSET + b];
    if (freq < 10.0f)
      freq = 10.0f;
    if (freq > nyquist)
      freq = nyquist;

    float q = mParam[ParametricEq::BAND_Q_OFFSET + b];
    if (q < 0.1f)
      q = 0.1f;
    if (q > 20.0f)
      q = 20.0f;

    float inv_two_sigma_sq = 4.16277f * q * q;

    activeBands[numActiveBands].gain = gain;
    activeBands[numActiveBands].log_fc = logf(freq);
    activeBands[numActiveBands].inv_two_sigma_sq = inv_two_sigma_sq;
    numActiveBands++;
  }

  if (numActiveBands == 0) {
    mHasActiveBands = false;
    return;
  }

  mHasActiveBands = true;
  float bin_hz = aSamplerate / (float)aSamples;
  unsigned int halfSamples = aSamples / 2;

  mBinGainTable[0] = 1.0f;
  for (unsigned int i = 1; i <= halfSamples; i++) {
    float current_freq = (float)i * bin_hz;
    float log_f = logf(current_freq);
    float total_gain = 1.0f;

    for (int b = 0; b < numActiveBands; b++) {
      float d = log_f - activeBands[b].log_fc;
      float exponent = d * d * activeBands[b].inv_two_sigma_sq;
      if (exponent > 9.0f) {
        continue;
      }
      float weight = expf(-exponent);
      float band_factor = 1.0f + (activeBands[b].gain - 1.0f) * weight;
      if (band_factor < 0.0f)
        band_factor = 0.0f;
      total_gain *= band_factor;
    }

    float final_scale = total_gain * wet + (1.0f - wet);
    mBinGainTable[i] = final_scale;
    if (i < halfSamples) {
      mBinGainTable[aSamples - i] = final_scale;
    }
  }
}

void ParametricEqInstance::filterChannel(float *aBuffer, unsigned int aSamples,
                                         float aSamplerate, SoLoud::time aTime,
                                         unsigned int aChannel,
                                         unsigned int aChannels) {
  if (mParent == nullptr) {
    return;
  }

  if (aChannel >= MAX_CHANNELS) {
    return;
  }

  if (mWinTwice <= 0 ||
      mWinTwice > 131072) {
    return;
  }

  // Advance parameter fades/oscillations once per audio callback
  if (aChannel == 0) {
    updateParams(aTime);
    if (mParamChanged != 0) {
      mGainTableDirty = true;
    }
  }

  if (mInputBuffer[aChannel] == nullptr) {
    mInputBuffer[aChannel] = new float[mWinTwice]();
    mMixBuffer[aChannel] = new float[mWinTwice]();
  }

  const int winSize = mWinSize;
  const int winHalf = mWinHalf;
  const int winTwiceMask = mWinTwice - 1;
  const float *hannWindow = mHannWindow.data();

  unsigned int ofs = 0;
  unsigned int chofs = 0;
  unsigned int inputofs = mInputOffset[aChannel];
  unsigned int mixofs = mMixOffset[aChannel];
  unsigned int readofs = mReadOffset[aChannel];

  while (ofs < aSamples) {
    int samples = winHalf - (inputofs & (winHalf - 1));
    if (ofs + samples > aSamples)
      samples = aSamples - ofs;
    for (int i = 0; i < samples; i++) {
      const unsigned int idx = chofs + ((inputofs + winHalf) & winTwiceMask);
      mInputBuffer[aChannel][idx] = aBuffer[ofs + i];
      mMixBuffer[aChannel][idx] = 0;
      inputofs++;
    }

    if ((inputofs & (winHalf - 1)) == 0) {
      // Copy input to FFT buffer (interleaved real/imag format)
      const unsigned int baseIdx =
          inputofs + mWinTwice - winHalf;
      const unsigned int winTwice =
          static_cast<unsigned int>(mWinTwice);
      int i = 0;
#if defined(SOLOUD_SSE_INTRINSICS)
      const __m128 zero = _mm_setzero_ps();
      for (; i + 4 <= winSize;) {
        const unsigned int ringIdx = (baseIdx + static_cast<unsigned int>(i)) &
                                     static_cast<unsigned int>(winTwiceMask);
        if (ringIdx + 4 > winTwice) {
          mFFTBuffer[i * 2] = mInputBuffer[aChannel][chofs + ringIdx];
          mFFTBuffer[i * 2 + 1] = 0.0f;
          ++i;
          continue;
        }
        const __m128 r = _mm_loadu_ps(&mInputBuffer[aChannel][chofs + ringIdx]);
        _mm_storeu_ps(&mFFTBuffer[i * 2], _mm_unpacklo_ps(r, zero));
        _mm_storeu_ps(&mFFTBuffer[i * 2 + 4], _mm_unpackhi_ps(r, zero));
        i += 4;
      }
#elif defined(SOLOUD_NEON_INTRINSICS)
      const float32x4_t zero = vdupq_n_f32(0.0f);
      for (; i + 4 <= winSize;) {
        const unsigned int ringIdx = (baseIdx + static_cast<unsigned int>(i)) &
                                     static_cast<unsigned int>(winTwiceMask);
        if (ringIdx + 4 > winTwice) {
          mFFTBuffer[i * 2] = mInputBuffer[aChannel][chofs + ringIdx];
          mFFTBuffer[i * 2 + 1] = 0.0f;
          ++i;
          continue;
        }
        float32x4x2_t ri;
        ri.val[0] = vld1q_f32(&mInputBuffer[aChannel][chofs + ringIdx]);
        ri.val[1] = zero;
        vst2q_f32(&mFFTBuffer[i * 2], ri);
        i += 4;
      }
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
      const v128_t zero = wasm_f32x4_splat(0.0f);
      for (; i + 4 <= winSize;) {
        const unsigned int ringIdx = (baseIdx + static_cast<unsigned int>(i)) &
                                     static_cast<unsigned int>(winTwiceMask);
        if (ringIdx + 4 > winTwice) {
          mFFTBuffer[i * 2] = mInputBuffer[aChannel][chofs + ringIdx];
          mFFTBuffer[i * 2 + 1] = 0.0f;
          ++i;
          continue;
        }
        const v128_t r =
            wasm_v128_load(&mInputBuffer[aChannel][chofs + ringIdx]);
        wasm_v128_store(&mFFTBuffer[i * 2],
                        wasm_i32x4_shuffle(r, zero, 0, 4, 1, 5));
        wasm_v128_store(&mFFTBuffer[i * 2 + 4],
                        wasm_i32x4_shuffle(r, zero, 2, 6, 3, 7));
        i += 4;
      }
#endif
      for (; i < winSize; i++) {
        float sample =
            mInputBuffer[aChannel][chofs + ((baseIdx + i) & winTwiceMask)];
        mFFTBuffer[i * 2] = sample;   // Real part
        mFFTBuffer[i * 2 + 1] = 0.0f; // Imaginary part
      }

      // Forward FFT (ordered output: interleaved complex numbers)
      pffft_transform_ordered(mFFTSetup, mFFTBuffer, mTemp, mFFTWork,
                              PFFFT_FORWARD);

      // Apply EQ on the transformed complex bins (process all N complex bins)
      fftFilterChannel(mTemp, winSize, aSamplerate, aTime, aChannel, aChannels);

      // Inverse FFT (ordered output)
      pffft_transform_ordered(mFFTSetup, mTemp, mFFTBuffer, mFFTWork,
                              PFFFT_BACKWARD);

      // Apply precomputed scaled Hann window for overlap-add
      i = 0;
#if defined(SOLOUD_SSE_INTRINSICS)
      for (; i + 4 <= winSize;) {
        const unsigned int ringIdx =
            mixofs & static_cast<unsigned int>(winTwiceMask);
        if (ringIdx + 4 > winTwice) {
          mMixBuffer[aChannel][chofs + ringIdx] +=
              mFFTBuffer[i * 2] * hannWindow[i];
          ++i;
          ++mixofs;
          continue;
        }
        const __m128 c0 = _mm_loadu_ps(&mFFTBuffer[i * 2]);
        const __m128 c1 = _mm_loadu_ps(&mFFTBuffer[i * 2 + 4]);
        const __m128 re = _mm_shuffle_ps(c0, c1, _MM_SHUFFLE(2, 0, 2, 0));
        const __m128 w = _mm_loadu_ps(&hannWindow[i]);
        float *dst = &mMixBuffer[aChannel][chofs + ringIdx];
        const __m128 cur = _mm_loadu_ps(dst);
        _mm_storeu_ps(dst, _mm_add_ps(cur, _mm_mul_ps(re, w)));
        i += 4;
        mixofs += 4;
      }
#elif defined(SOLOUD_NEON_INTRINSICS)
      for (; i + 4 <= winSize;) {
        const unsigned int ringIdx =
            mixofs & static_cast<unsigned int>(winTwiceMask);
        if (ringIdx + 4 > winTwice) {
          mMixBuffer[aChannel][chofs + ringIdx] +=
              mFFTBuffer[i * 2] * hannWindow[i];
          ++i;
          ++mixofs;
          continue;
        }
        const float32x4x2_t ri = vld2q_f32(&mFFTBuffer[i * 2]);
        const float32x4_t w = vld1q_f32(&hannWindow[i]);
        float *dst = &mMixBuffer[aChannel][chofs + ringIdx];
        const float32x4_t cur = vld1q_f32(dst);
        vst1q_f32(dst, vmlaq_f32(cur, ri.val[0], w));
        i += 4;
        mixofs += 4;
      }
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
      for (; i + 4 <= winSize;) {
        const unsigned int ringIdx =
            mixofs & static_cast<unsigned int>(winTwiceMask);
        if (ringIdx + 4 > winTwice) {
          mMixBuffer[aChannel][chofs + ringIdx] +=
              mFFTBuffer[i * 2] * hannWindow[i];
          ++i;
          ++mixofs;
          continue;
        }
        const v128_t c0 = wasm_v128_load(&mFFTBuffer[i * 2]);
        const v128_t c1 = wasm_v128_load(&mFFTBuffer[i * 2 + 4]);
        const v128_t re = wasm_i32x4_shuffle(c0, c1, 0, 2, 4, 6);
        const v128_t w = wasm_v128_load(&hannWindow[i]);
        float *dst = &mMixBuffer[aChannel][chofs + ringIdx];
        const v128_t cur = wasm_v128_load(dst);
        wasm_v128_store(dst, wasm_f32x4_add(cur, wasm_f32x4_mul(re, w)));
        i += 4;
        mixofs += 4;
      }
#endif
      for (; i < winSize; i++) {
        float sample = mFFTBuffer[i * 2] * hannWindow[i];
        mMixBuffer[aChannel][chofs + (mixofs & winTwiceMask)] += sample;
        mixofs++;
      }
      mixofs -= winHalf;
    }

    for (int i = 0; i < samples; i++) {
      aBuffer[ofs + i] = mMixBuffer[aChannel][chofs + (readofs & winTwiceMask)];
      readofs++;
    }

    ofs += samples;
  }
  mInputOffset[aChannel] = inputofs;
  mReadOffset[aChannel] = readofs;
  mMixOffset[aChannel] = mixofs;
}

void ParametricEqInstance::fftFilterChannel(float *aFFTBuffer,
                                            unsigned int aSamples,
                                            float aSamplerate,
                                            SoLoud::time /*aTime*/,
                                            unsigned int /*aChannel*/,
                                            unsigned int /*aChannels*/) {
  if (mParam[0] <= 0.0f) {
    return;
  }

  if (mGainTableDirty || mCachedSampleRate != aSamplerate ||
      mBinGainTable.size() != aSamples) {
    updateBinGainTable(aSamples, aSamplerate);
  }

  if (!mHasActiveBands) {
    return;
  }

  const float *scale = mBinGainTable.data();
  unsigned int i = 0;
#if defined(SOLOUD_SSE_INTRINSICS)
  for (; i + 4 <= aSamples; i += 4) {
    const __m128 s = _mm_loadu_ps(scale + i);
    const __m128 sLo = _mm_unpacklo_ps(s, s);
    const __m128 sHi = _mm_unpackhi_ps(s, s);
    const __m128 c0 = _mm_loadu_ps(aFFTBuffer + i * 2);
    const __m128 c1 = _mm_loadu_ps(aFFTBuffer + i * 2 + 4);
    _mm_storeu_ps(aFFTBuffer + i * 2, _mm_mul_ps(c0, sLo));
    _mm_storeu_ps(aFFTBuffer + i * 2 + 4, _mm_mul_ps(c1, sHi));
  }
#elif defined(SOLOUD_NEON_INTRINSICS)
  for (; i + 4 <= aSamples; i += 4) {
    const float32x4_t s = vld1q_f32(scale + i);
    const float32x4x2_t sDup = vzipq_f32(s, s);
    const float32x4_t c0 = vld1q_f32(aFFTBuffer + i * 2);
    const float32x4_t c1 = vld1q_f32(aFFTBuffer + i * 2 + 4);
    vst1q_f32(aFFTBuffer + i * 2, vmulq_f32(c0, sDup.val[0]));
    vst1q_f32(aFFTBuffer + i * 2 + 4, vmulq_f32(c1, sDup.val[1]));
  }
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
  for (; i + 4 <= aSamples; i += 4) {
    const v128_t s = wasm_v128_load(scale + i);
    const v128_t sLo = wasm_i32x4_shuffle(s, s, 0, 0, 1, 1);
    const v128_t sHi = wasm_i32x4_shuffle(s, s, 2, 2, 3, 3);
    const v128_t c0 = wasm_v128_load(aFFTBuffer + i * 2);
    const v128_t c1 = wasm_v128_load(aFFTBuffer + i * 2 + 4);
    wasm_v128_store(aFFTBuffer + i * 2, wasm_f32x4_mul(c0, sLo));
    wasm_v128_store(aFFTBuffer + i * 2 + 4, wasm_f32x4_mul(c1, sHi));
  }
#endif
  if (i == 0) {
    i = 1;
  }
  for (; i < aSamples; i++) {
    const float s = scale[i];
    aFFTBuffer[i * 2] *= s;
    aFFTBuffer[i * 2 + 1] *= s;
  }
}

SoLoud::result ParametricEq::setParam(unsigned int aParamIndex, float aValue) {
  return SoLoud::SO_NO_ERROR;
}

int ParametricEq::getParamCount() {
  return NUM_PARAMS; // 195
}

const char *ParametricEq::getParamName(unsigned int aParamIndex) {
  if (aParamIndex == 0)
    return "Wet";
  if (aParamIndex == 1)
    return "Window Size";
  if (aParamIndex == 2)
    return "Bands Count";

  static thread_local std::string s;
  if (aParamIndex >= BAND_GAIN_OFFSET && aParamIndex < BAND_FREQ_OFFSET) {
    s = "Band " + std::to_string(aParamIndex - BAND_GAIN_OFFSET) + " Gain";
    return s.c_str();
  }
  if (aParamIndex >= BAND_FREQ_OFFSET && aParamIndex < BAND_Q_OFFSET) {
    s = "Band " + std::to_string(aParamIndex - BAND_FREQ_OFFSET) + " Frequency";
    return s.c_str();
  }
  if (aParamIndex >= BAND_Q_OFFSET && aParamIndex < NUM_PARAMS) {
    s = "Band " + std::to_string(aParamIndex - BAND_Q_OFFSET) + " Q";
    return s.c_str();
  }
  return "Unknown";
}

unsigned int ParametricEq::getParamType(unsigned int aParamIndex) {
  if (aParamIndex == 1 || aParamIndex == 2)
    return INT_PARAM;
  return FLOAT_PARAM;
}

float ParametricEq::getParamMax(unsigned int aParamIndex) {
  if (aParamIndex == 0)
    return 1.0f; // wet
  if (aParamIndex == 1)
    return 4096.0f; // window size
  if (aParamIndex == 2)
    return 64.0f; // band count

  if (aParamIndex >= BAND_GAIN_OFFSET && aParamIndex < BAND_FREQ_OFFSET)
    return 4.0f; // gain
  if (aParamIndex >= BAND_FREQ_OFFSET && aParamIndex < BAND_Q_OFFSET)
    return 24000.0f; // frequency
  if (aParamIndex >= BAND_Q_OFFSET && aParamIndex < NUM_PARAMS)
    return 20.0f; // Q

  return 1.0f;
}

float ParametricEq::getParamMin(unsigned int aParamIndex) {
  if (aParamIndex == 0)
    return 0.0f; // wet
  if (aParamIndex == 1)
    return 32.0f; // window size
  if (aParamIndex == 2)
    return 1.0f; // band count

  if (aParamIndex >= BAND_GAIN_OFFSET && aParamIndex < BAND_FREQ_OFFSET)
    return 0.0f; // gain
  if (aParamIndex >= BAND_FREQ_OFFSET && aParamIndex < BAND_Q_OFFSET)
    return 10.0f; // frequency
  if (aParamIndex >= BAND_Q_OFFSET && aParamIndex < NUM_PARAMS)
    return 0.1f; // Q

  return 0.0f;
}

void ParametricEq::setFreqs(unsigned int nBands) {
  mBands = std::max(1U, std::min(nBands, MAX_BANDS));

  mGain.assign(MAX_BANDS, 1.0f);
  mFreq.resize(MAX_BANDS);
  mQ.resize(MAX_BANDS);

  // Default frequency distribution: geometric spacing between 30Hz and 12000Hz
  float f0 = 30.0f;
  float f1 = 12000.0f;

  float defaultQ = 1.0f;
  if (mBands > 1) {
    float octaves = log2f(f1 / f0) / (float)(mBands - 1);
    defaultQ = std::max(0.1f, std::min(20.0f, 1.0f / (octaves * 0.693147f)));
  }

  if (mBands == 1) {
    mFreq[0] = 1000.0f;
    mQ[0] = 1.0f;
  } else {
    for (unsigned int i = 0; i < mBands; i++) {
      float t = (float)i / (float)(mBands - 1);
      mFreq[i] = f0 * powf(f1 / f0, t);
      mQ[i] = defaultQ;
    }
  }

  // Safe defaults for remaining slots
  for (unsigned int i = mBands; i < MAX_BANDS; i++) {
    mFreq[i] = 1000.0f;
    mQ[i] = 1.0f;
  }
}

ParametricEq::ParametricEq(SoLoud::Soloud *aSoloud, int bands) {
  mSoloud = aSoloud;
  mChannels = aSoloud->mChannels;

  mWet = 1.0f;

  mSTFT_WINDOW_SIZE = 1024;
  mSTFT_WINDOW_HALF = mSTFT_WINDOW_SIZE >> 1;
  mSTFT_WINDOW_TWICE = mSTFT_WINDOW_SIZE << 1;
  mFFT_SCALE = 1.0f / (float)mSTFT_WINDOW_SIZE;

  setFreqs(bands);
}

SoLoud::FilterInstance *ParametricEq::createInstance() {
  return new ParametricEqInstance(this);
}