#include "parametric_eq_filter.h"
#include "../simd/soloud_simd.h"
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
  mUseRealFFT = (winSize >= 32);
  mHannWindow.resize(winSize);
  const float scale = mFFTScale;
  for (int i = 0; i < winSize; i++) {
    mHannWindow[i] =
        0.5f * (1.0f - cosf((2.0f * (float)M_PI * i) / (float)winSize)) * scale;
  }
  const int halfWin = mWinHalf;
  mLogBinIndex.resize(static_cast<size_t>(halfWin) + 1u);
  mLogBinIndex[0] = 0.0f;
  for (int i = 1; i <= halfWin; i++) {
    mLogBinIndex[static_cast<size_t>(i)] = logf(static_cast<float>(i));
  }
  mBinGainTable.assign(
      mUseRealFFT ? (static_cast<size_t>(halfWin) + 1u)
                  : static_cast<size_t>(winSize),
      1.0f);
  mGainTableDirty = true;

  // Initialize FFT setup: real-to-complex for N >= 32 (half the FFT cost and
  // zero interleave/de-interleave), complex fallback only if N == 16.
  mFFTSetup =
      pffft_new_setup(mWinSize, mUseRealFFT ? PFFFT_REAL : PFFFT_COMPLEX);

  // Allocate aligned buffers for FFT
  const size_t fftFloats =
      static_cast<size_t>(mUseRealFFT ? mWinSize : mWinTwice);
  mFFTBuffer = (float *)pffft_aligned_malloc(fftFloats * sizeof(float));
  mFFTWork = (float *)pffft_aligned_malloc(fftFloats * sizeof(float));
  mTemp = (float *)pffft_aligned_malloc(fftFloats * sizeof(float));

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

  const unsigned int halfSamples = aSamples / 2;
  const size_t tableSize =
      mUseRealFFT ? (static_cast<size_t>(halfSamples) + 1u)
                  : static_cast<size_t>(aSamples);
  if (mBinGainTable.size() != tableSize) {
    mBinGainTable.assign(tableSize, 1.0f);
  }

  float wet = mParam[0];
  if (wet <= 0.0f) {
    mHasActiveBands = false;
    return;
  }

  struct ActiveBand {
    float gainMinusOne;
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

    activeBands[numActiveBands].gainMinusOne = gain - 1.0f;
    activeBands[numActiveBands].log_fc = logf(freq);
    activeBands[numActiveBands].inv_two_sigma_sq = inv_two_sigma_sq;
    numActiveBands++;
  }

  if (numActiveBands == 0) {
    mHasActiveBands = false;
    return;
  }

  mHasActiveBands = true;
  const float bin_hz = aSamplerate / (float)aSamples;
  const float log_bin_hz = logf(bin_hz);
  const bool haveLogTable = (mLogBinIndex.size() > halfSamples);
  constexpr float kNegInvLn2 = -1.4426950408889634f;
  const float dry = 1.0f - wet;

  mBinGainTable[0] = 1.0f;
  for (unsigned int i = 1; i <= halfSamples; i++) {
    const float log_f = haveLogTable ? (mLogBinIndex[i] + log_bin_hz)
                                     : logf((float)i * bin_hz);
    float total_gain = 1.0f;

    for (int b = 0; b < numActiveBands; b++) {
      float d = log_f - activeBands[b].log_fc;
      float exponent = d * d * activeBands[b].inv_two_sigma_sq;
      if (exponent > 9.0f) {
        continue;
      }
      float weight = soloud_simd::fast_exp2(exponent * kNegInvLn2);
      float band_factor = 1.0f + activeBands[b].gainMinusOne * weight;
      if (band_factor < 0.0f)
        band_factor = 0.0f;
      total_gain *= band_factor;
    }

    float final_scale = total_gain * wet + dry;
    mBinGainTable[i] = final_scale;
    if (!mUseRealFFT && i < halfSamples) {
      mBinGainTable[aSamples - i] = final_scale;
    }
  }
}

void ParametricEqInstance::filterChannel(float *aBuffer, unsigned int aSamples,
                                         float aSamplerate, SoLoud::time aTime,
                                         unsigned int aChannel,
                                         unsigned int aChannels) {
  if (mParent == nullptr || aChannel >= MAX_CHANNELS || mWinTwice <= 0 ||
      mWinTwice > 131072 || mFFTSetup == nullptr || mFFTBuffer == nullptr ||
      mTemp == nullptr) {
    return;
  }

  soloud_simd::ScopedFlushDenormals flushDenormals;

  // Advance parameter fades/oscillations once per audio callback
  if (aChannel == 0) {
    updateParams(aTime);
    if (mParamChanged != 0) {
      mGainTableDirty = true;
    }
  }

  if (mInputBuffer[aChannel] == nullptr || mMixBuffer[aChannel] == nullptr) {
    delete[] mInputBuffer[aChannel];
    delete[] mMixBuffer[aChannel];
    mInputBuffer[aChannel] = new float[mWinTwice]();
    mMixBuffer[aChannel] = new float[mWinTwice]();
  }

  const int winSize = mWinSize;
  const int winHalf = mWinHalf;
  const unsigned int uWinSize = static_cast<unsigned int>(winSize);
  const unsigned int uWinTwice = static_cast<unsigned int>(mWinTwice);
  const unsigned int winTwiceMask = uWinTwice - 1u;
  const float *hannWindow = mHannWindow.data();
  float *inRing = mInputBuffer[aChannel];
  float *mixRing = mMixBuffer[aChannel];

  unsigned int ofs = 0;
  unsigned int inputofs = mInputOffset[aChannel];
  unsigned int mixofs = mMixOffset[aChannel];
  unsigned int readofs = mReadOffset[aChannel];

  auto overlapAddSpan = [](float *dst, const float *src, const float *win,
                           unsigned int count, float extraScale) {
    unsigned int k = 0;
#if defined(SOLOUD_SIMD_ENABLED)
    const soloud_simd::f32x4 vScale = soloud_simd::f32x4::splat(extraScale);
    for (; k + 4 <= count; k += 4) {
      const soloud_simd::f32x4 s = soloud_simd::f32x4::loadu(src + k);
      const soloud_simd::f32x4 w = soloud_simd::f32x4::loadu(win + k) * vScale;
      const soloud_simd::f32x4 cur = soloud_simd::f32x4::loadu(dst + k);
      soloud_simd::fma(s, w, cur).storeu(dst + k);
    }
#endif
    for (; k < count; ++k) {
      dst[k] += src[k] * (win[k] * extraScale);
    }
  };

  while (ofs < aSamples) {
    unsigned int samples =
        static_cast<unsigned int>(winHalf) -
        (inputofs & static_cast<unsigned int>(winHalf - 1));
    if (ofs + samples > aSamples)
      samples = aSamples - ofs;

    // Write input span and clear corresponding mix span (at most 1 wrap)
    const unsigned int writeStart =
        (inputofs + static_cast<unsigned int>(winHalf)) & winTwiceMask;
    const unsigned int w1 = std::min(samples, uWinTwice - writeStart);
    const unsigned int w2 = samples - w1;
    std::memcpy(inRing + writeStart, aBuffer + ofs, w1 * sizeof(float));
    std::memset(mixRing + writeStart, 0, w1 * sizeof(float));
    if (w2 > 0) {
      std::memcpy(inRing, aBuffer + ofs + w1, w2 * sizeof(float));
      std::memset(mixRing, 0, w2 * sizeof(float));
    }
    inputofs += samples;

    if ((inputofs & static_cast<unsigned int>(winHalf - 1)) == 0) {
      const unsigned int baseIdx =
          (inputofs + uWinTwice - static_cast<unsigned int>(winHalf)) &
          winTwiceMask;

      if (mUseRealFFT) {
        // Refresh gain table up front so flat/dry EQ can bypass Forward+Inverse
        // FFT while preserving exact STFT overlap-add latency and phase.
        const size_t expectedTable =
            static_cast<size_t>(uWinSize / 2u) + 1u;
        if (mParam[0] > 0.0f &&
            (mGainTableDirty || mCachedSampleRate != aSamplerate ||
             mBinGainTable.size() != expectedTable)) {
          updateBinGainTable(uWinSize, aSamplerate);
        }

        const unsigned int r1 = std::min(uWinSize, uWinTwice - baseIdx);
        const unsigned int r2 = uWinSize - r1;
        std::memcpy(mFFTBuffer, inRing + baseIdx, r1 * sizeof(float));
        if (r2 > 0) {
          std::memcpy(mFFTBuffer + r1, inRing, r2 * sizeof(float));
        }

        float extraScale = 1.0f;
        if (mParam[0] > 0.0f && mHasActiveBands) {
          pffft_transform_ordered(mFFTSetup, mFFTBuffer, mTemp, mFFTWork,
                                  PFFFT_FORWARD);
          fftFilterChannel(mTemp, uWinSize, aSamplerate, aTime, aChannel,
                           aChannels);
          pffft_transform_ordered(mFFTSetup, mTemp, mFFTBuffer, mFFTWork,
                                  PFFFT_BACKWARD);
        } else {
          // Unprocessed PFFFT_BACKWARD(PFFFT_FORWARD(x)) == winSize * x
          extraScale = static_cast<float>(winSize);
        }

        const unsigned int mixStart = mixofs & winTwiceMask;
        const unsigned int m1 = std::min(uWinSize, uWinTwice - mixStart);
        const unsigned int m2 = uWinSize - m1;
        overlapAddSpan(mixRing + mixStart, mFFTBuffer, hannWindow, m1,
                       extraScale);
        if (m2 > 0) {
          overlapAddSpan(mixRing, mFFTBuffer + m1, hannWindow + m1, m2,
                         extraScale);
        }
        mixofs += uWinSize;
      } else {
        // Fallback path for N == 16 (PFFFT_COMPLEX)
        for (int i = 0; i < winSize; i++) {
          mFFTBuffer[i * 2] =
              inRing[(baseIdx + static_cast<unsigned int>(i)) & winTwiceMask];
          mFFTBuffer[i * 2 + 1] = 0.0f;
        }
        pffft_transform_ordered(mFFTSetup, mFFTBuffer, mTemp, mFFTWork,
                                PFFFT_FORWARD);
        fftFilterChannel(mTemp, uWinSize, aSamplerate, aTime, aChannel,
                         aChannels);
        pffft_transform_ordered(mFFTSetup, mTemp, mFFTBuffer, mFFTWork,
                                PFFFT_BACKWARD);
        for (int i = 0; i < winSize; i++) {
          mixRing[mixofs & winTwiceMask] += mFFTBuffer[i * 2] * hannWindow[i];
          mixofs++;
        }
      }
      mixofs -= static_cast<unsigned int>(winHalf);
    }

    // Read output span from mix ring (at most 1 wrap)
    const unsigned int readStart = readofs & winTwiceMask;
    const unsigned int out1 = std::min(samples, uWinTwice - readStart);
    const unsigned int out2 = samples - out1;
    std::memcpy(aBuffer + ofs, mixRing + readStart, out1 * sizeof(float));
    if (out2 > 0) {
      std::memcpy(aBuffer + ofs + out1, mixRing, out2 * sizeof(float));
    }
    readofs += samples;
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

  const unsigned int halfSamples = aSamples / 2;
  const size_t expectedTable =
      mUseRealFFT ? (static_cast<size_t>(halfSamples) + 1u)
                  : static_cast<size_t>(aSamples);
  if (mGainTableDirty || mCachedSampleRate != aSamplerate ||
      mBinGainTable.size() != expectedTable) {
    updateBinGainTable(aSamples, aSamplerate);
  }

  if (!mHasActiveBands) {
    return;
  }

  const float *scale = mBinGainTable.data();
  if (mUseRealFFT) {
    // PFFFT_REAL ordered format:
    //   aFFTBuffer[0] = DC (bin 0), aFFTBuffer[1] = Nyquist (bin N/2),
    //   aFFTBuffer[2*k], aFFTBuffer[2*k+1] = Re/Im of bin k (1 <= k < N/2).
    const float dc = aFFTBuffer[0] * scale[0];
    const float nyquist = aFFTBuffer[1] * scale[halfSamples];
    unsigned int k = 0;
#if defined(SOLOUD_SSE_INTRINSICS)
    for (; k + 4 <= halfSamples; k += 4) {
      const __m128 s = _mm_loadu_ps(scale + k);
      const __m128 sLo = _mm_unpacklo_ps(s, s);
      const __m128 sHi = _mm_unpackhi_ps(s, s);
      const __m128 c0 = _mm_loadu_ps(aFFTBuffer + k * 2);
      const __m128 c1 = _mm_loadu_ps(aFFTBuffer + k * 2 + 4);
      _mm_storeu_ps(aFFTBuffer + k * 2, _mm_mul_ps(c0, sLo));
      _mm_storeu_ps(aFFTBuffer + k * 2 + 4, _mm_mul_ps(c1, sHi));
    }
#elif defined(SOLOUD_NEON_INTRINSICS)
    for (; k + 4 <= halfSamples; k += 4) {
      const float32x4_t s = vld1q_f32(scale + k);
      const float32x4x2_t sDup = vzipq_f32(s, s);
      const float32x4_t c0 = vld1q_f32(aFFTBuffer + k * 2);
      const float32x4_t c1 = vld1q_f32(aFFTBuffer + k * 2 + 4);
      vst1q_f32(aFFTBuffer + k * 2, vmulq_f32(c0, sDup.val[0]));
      vst1q_f32(aFFTBuffer + k * 2 + 4, vmulq_f32(c1, sDup.val[1]));
    }
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
    for (; k + 4 <= halfSamples; k += 4) {
      const v128_t s = wasm_v128_load(scale + k);
      const v128_t sLo = wasm_i32x4_shuffle(s, s, 0, 0, 1, 1);
      const v128_t sHi = wasm_i32x4_shuffle(s, s, 2, 2, 3, 3);
      const v128_t c0 = wasm_v128_load(aFFTBuffer + k * 2);
      const v128_t c1 = wasm_v128_load(aFFTBuffer + k * 2 + 4);
      wasm_v128_store(aFFTBuffer + k * 2, wasm_f32x4_mul(c0, sLo));
      wasm_v128_store(aFFTBuffer + k * 2 + 4, wasm_f32x4_mul(c1, sHi));
    }
#endif
    if (k == 0) {
      k = 1;
    }
    for (; k < halfSamples; ++k) {
      const float s = scale[k];
      aFFTBuffer[k * 2] *= s;
      aFFTBuffer[k * 2 + 1] *= s;
    }
    aFFTBuffer[0] = dc;
    aFFTBuffer[1] = nyquist;
    return;
  }

  for (unsigned int i = 1; i < aSamples; i++) {
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