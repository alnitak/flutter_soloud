#include "parametric_eq_filter.h"
#include "soloud.h"
#include <algorithm>
#include <cmath>
#include <string.h>
#include <string>

// MSVC fix: Undefine min/max macros that may be defined by Windows.h
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

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

  // Initialize FFT setup and allocate buffers
  initFFTBuffers();

  // Initialize band parameters (gains, frequencies, Q factors)
  initBandParameters();

  mParam[0] = mParent->mWet;              // wet param (index 0)
  mParam[1] = mParent->mSTFT_WINDOW_SIZE; // window size param (index 1)
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
  if (mParent->mSTFT_WINDOW_SIZE < 16 || mParent->mSTFT_WINDOW_SIZE > 65536) {
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
  // MAX_CHANNELS)
  for (int i = 0; i < MAX_CHANNELS; i++) {
    if (mInputBuffer[i] != nullptr) {
      delete[] mInputBuffer[i];
      mInputBuffer[i] = nullptr;
    }
    if (mMixBuffer[i] != nullptr) {
      delete[] mMixBuffer[i];
      mMixBuffer[i] = nullptr;
    }
    // Reset offsets
    mInputOffset[i] = mParent->mSTFT_WINDOW_SIZE;
    mMixOffset[i] = mParent->mSTFT_WINDOW_HALF;
    mReadOffset[i] = 0;
  }

  // Initialize FFT setup for complex transforms
  mFFTSetup = pffft_new_setup(mParent->mSTFT_WINDOW_SIZE, PFFFT_COMPLEX);

  // Allocate aligned buffers for FFT
  mFFTBuffer = (float *)pffft_aligned_malloc(mParent->mSTFT_WINDOW_TWICE *
                                             sizeof(float));
  mFFTWork = (float *)pffft_aligned_malloc(mParent->mSTFT_WINDOW_TWICE *
                                           sizeof(float));
  mTemp = (float *)pffft_aligned_malloc(mParent->mSTFT_WINDOW_TWICE *
                                        sizeof(float));

  mParam[1] = mParent->mSTFT_WINDOW_SIZE; // Update window size param (index 1)
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

  case 1: // STFT_WINDOW_SIZE
    if (mParent->mSTFT_WINDOW_SIZE == (int)aValue)
      return;
    mParam[1] = (int)aValue;
    mParent->mSTFT_WINDOW_SIZE = (int)aValue;
    mParent->mSTFT_WINDOW_HALF = mParent->mSTFT_WINDOW_SIZE >> 1;
    mParent->mSTFT_WINDOW_TWICE = mParent->mSTFT_WINDOW_SIZE << 1;
    mParent->mFFT_SCALE = 1.0f / (float)mParent->mSTFT_WINDOW_SIZE;
    initFFTBuffers();
    break;

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

  if (mParent->mSTFT_WINDOW_TWICE <= 0 ||
      mParent->mSTFT_WINDOW_TWICE > 131072) {
    return;
  }

  // Advance parameter fades/oscillations once per audio callback
  if (aChannel == 0) {
    updateParams(aTime);
  }

  // Lazy initialization of buffers for this channel
  if (mInputBuffer[aChannel] == nullptr) {
    mInputBuffer[aChannel] =
        new float[mParent->mSTFT_WINDOW_TWICE](); // () initializes to zero
    mMixBuffer[aChannel] = new float[mParent->mSTFT_WINDOW_TWICE]();
  }

  unsigned int ofs = 0;
  unsigned int chofs = 0;
  unsigned int inputofs = mInputOffset[aChannel];
  unsigned int mixofs = mMixOffset[aChannel];
  unsigned int readofs = mReadOffset[aChannel];

  while (ofs < aSamples) {
    int samples = mParent->mSTFT_WINDOW_HALF -
                  (inputofs & (mParent->mSTFT_WINDOW_HALF - 1));
    if (ofs + samples > aSamples)
      samples = aSamples - ofs;
    for (int i = 0; i < samples; i++) {
      mInputBuffer[aChannel][chofs + ((inputofs + mParent->mSTFT_WINDOW_HALF) &
                                      (mParent->mSTFT_WINDOW_TWICE - 1))] =
          aBuffer[ofs + i];
      mMixBuffer[aChannel][chofs + ((inputofs + mParent->mSTFT_WINDOW_HALF) &
                                    (mParent->mSTFT_WINDOW_TWICE - 1))] = 0;
      inputofs++;
    }

    if ((inputofs & (mParent->mSTFT_WINDOW_HALF - 1)) == 0) {
      // Copy input to FFT buffer (interleaved real/imag format)
      for (int i = 0; i < mParent->mSTFT_WINDOW_SIZE; i++) {
        float sample =
            mInputBuffer[aChannel]
                        [chofs + ((inputofs + mParent->mSTFT_WINDOW_TWICE -
                                   mParent->mSTFT_WINDOW_HALF + i) &
                                  (mParent->mSTFT_WINDOW_TWICE - 1))];
        mFFTBuffer[i * 2] = sample;   // Real part
        mFFTBuffer[i * 2 + 1] = 0.0f; // Imaginary part
      }

      // Forward FFT (ordered output: interleaved complex numbers)
      pffft_transform_ordered(mFFTSetup, mFFTBuffer, mTemp, mFFTWork,
                              PFFFT_FORWARD);

      // Apply EQ on the transformed complex bins (process all N complex bins)
      fftFilterChannel(mTemp, mParent->mSTFT_WINDOW_SIZE, aSamplerate, aTime,
                       aChannel, aChannels);

      // Inverse FFT (ordered output)
      pffft_transform_ordered(mFFTSetup, mTemp, mFFTBuffer, mFFTWork,
                              PFFFT_BACKWARD);

      // Apply scaling and Hann window for overlap-add
      for (int i = 0; i < mParent->mSTFT_WINDOW_SIZE; i++) {
        float window =
            0.5f *
            (1.0f - cosf((2.0f * M_PI * i) / mParent->mSTFT_WINDOW_SIZE));
        float sample = mFFTBuffer[i * 2] * mParent->mFFT_SCALE *
                       window; // Only use real part
        mMixBuffer[aChannel]
                  [chofs + (mixofs & (mParent->mSTFT_WINDOW_TWICE - 1))] +=
            sample;
        mixofs++;
      }
      mixofs -= mParent->mSTFT_WINDOW_HALF;
    }

    for (int i = 0; i < samples; i++) {
      aBuffer[ofs + i] =
          mMixBuffer[aChannel]
                    [chofs + (readofs & (mParent->mSTFT_WINDOW_TWICE - 1))];
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
  float wet = mParam[0];
  if (wet <= 0.0f) {
    return;
  }

  // Pre-filter and collect active bands (skipping flat bands where gain == 1.0)
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

    // Bell curve (Gaussian) on logarithmic frequency scale:
    // weight(f) = exp( - 0.5 * (ln(f / f_c) / sigma)^2 )
    // where sigma = ln(2) / (2 * Q)
    // 2 * sigma^2 = (ln(2))^2 / (2 * Q^2)
    // inv_two_sigma_sq = 2 * Q^2 / (ln(2))^2 ~= 4.16277f * Q^2
    float inv_two_sigma_sq = 4.16277f * q * q;

    activeBands[numActiveBands].gain = gain;
    activeBands[numActiveBands].log_fc = logf(freq);
    activeBands[numActiveBands].inv_two_sigma_sq = inv_two_sigma_sq;
    numActiveBands++;
  }

  // If no bands are modified, pass through untouched
  if (numActiveBands == 0) {
    return;
  }

  float bin_hz = aSamplerate / (float)aSamples;
  unsigned int halfSamples = aSamples / 2;

  // Process positive and negative FFT bins (bin 0 is DC, untouched)
  for (unsigned int i = 1; i < aSamples; i++) {
    unsigned int freqBin = (i <= halfSamples) ? i : (aSamples - i);
    float current_freq = (float)freqBin * bin_hz;
    float log_f = logf(current_freq);

    float total_gain = 1.0f;

    for (int b = 0; b < numActiveBands; b++) {
      float d = log_f - activeBands[b].log_fc;
      float d2 = d * d;
      float exponent = d2 * activeBands[b].inv_two_sigma_sq;

      // Truncate bell curve when weight is negligible (< ~0.0001, beyond 3 standard deviations)
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
    aFFTBuffer[i * 2] *= final_scale;
    aFFTBuffer[i * 2 + 1] *= final_scale;
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