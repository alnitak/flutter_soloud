#include "parametric_eq_filter.h"
#include "soloud.h"
#include <algorithm>
#include <math.h>
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

  // Initialize FFT setup and allocate buffers
  initFFTBuffers();

  // Initialize band parameters (count, gains, centers, boundaries)
  initBandParameters();
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
  // Copy band count from parent
  mBands = mParent->mBands;

  // Re-initialize parameter arrays to match band count
  mNumParams = 3 + mBands;
  initParams(mNumParams);
  mParam[0] = 1.0f; // Reset wet param

  // Copy band gains into parameter slots (params[3..3+bands-1])
  for (int i = 0; i < mBands; i++) {
    mParam[3 + i] = mParent->mGain[i];
  }

  // Precompute band centers and boundaries (boundaries are midpoints)
  mBandCenter.resize(mBands);
  mBandBoundary.resize(mBands + 1);
  for (int i = 0; i < mBands; i++) {
    mBandCenter[i] = mParent->mFreq[i];
  }

  // boundaries: first = 0, last = +inf (will be clamped to Nyquist when used)
  mBandBoundary[0] = 0.0f;
  for (int i = 0; i < mBands - 1; i++) {
    mBandBoundary[i + 1] = 0.5f * (mBandCenter[i] + mBandCenter[i + 1]);
  }
  mBandBoundary[mBands] = 1e9f; // effectively infinity for the upper bound
}

void ParametricEqInstance::initFFTBuffers() {
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

  // Free and reallocate channel buffers
  for (int i = 0; i < mParent->mChannels; i++) {
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
  for (int i = 0; i < mParent->mChannels; i++) {
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

  // 0 wet
  // 1 SFTF_WINDOW_SIZE
  // 2 nBands
  // 3..nBands per-band gains

  mParamFader[aAttributeId].mActive = 0;

  switch (aAttributeId) {
  case 1: // SFTF_WINDOW_SIZE
    if (mParent->mSTFT_WINDOW_SIZE == (int)aValue)
      return;
    mParent->mSTFT_WINDOW_SIZE = (int)aValue;
    mParent->mSTFT_WINDOW_HALF = mParent->mSTFT_WINDOW_SIZE >> 1;
    mParent->mSTFT_WINDOW_TWICE = mParent->mSTFT_WINDOW_SIZE << 1;
    mParent->mFFT_SCALE = 1.0f / (float)mParent->mSTFT_WINDOW_SIZE;
    // Reinitialize FFT with new window size
    initFFTBuffers();
    break;
  case 2: // nBands
    if (mParent->mBands == (unsigned int)aValue)
      return;
    // Update parent's band configuration first
    mParent->setFreqs((unsigned int)aValue);
    // Re-initialize band parameters from parent
    initBandParameters();
    break;

  default: // wet and band gains
    mParam[aAttributeId] = aValue;
    break;
  }
}

void ParametricEqInstance::filterChannel(float *aBuffer, unsigned int aSamples,
                                         float aSamplerate, SoLoud::time aTime,
                                         unsigned int aChannel,
                                         unsigned int aChannels) {
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
  comp2MagPhase(aFFTBuffer, aSamples);
  // Triangular interpolation across user-configured bands.
  // mBandCenter holds band centers, mBandBoundary has midpoints between centers
  // (size mBands+1)
  float nyquist = aSamplerate * 0.5f;
  unsigned int halfSamples = aSamples / 2;

  for (unsigned int i = 0; i < aSamples; i++) {
    // For a real signal FFT: bins 0..N/2 are positive frequencies,
    // bins N/2+1..N-1 are negative frequencies (conjugates).
    // Map negative frequency bins to their positive counterpart.
    unsigned int freqBin = (i <= halfSamples) ? i : (aSamples - i);
    float current_freq = (float)freqBin * aSamplerate / (float)aSamples;

    float gain = 0.0f;
    float weight_sum = 0.0f;

    for (int b = 0; b < mBands; b++) {
      float center = mBandCenter[b];
      float low = mBandBoundary[b];
      float high = mBandBoundary[b + 1];
      // clamp upper boundary to nyquist
      if (high > nyquist)
        high = nyquist;

      // Asymmetric triangular window: use separate half-widths for left/right
      float leftHalfwidth = center - low;
      float rightHalfwidth = high - center;
      float weight = 0.0f;

      if (current_freq <= center && leftHalfwidth > 0.0f) {
        // Frequency is on the left side of the triangle
        float d = center - current_freq;
        float w = 1.0f - (d / leftHalfwidth);
        if (w >= 0.0f)                  // Include boundary (w=0)
          weight = std::max(w, 0.001f); // Ensure minimum weight at boundary
      } else if (current_freq > center && rightHalfwidth > 0.0f) {
        // Frequency is on the right side of the triangle
        float d = current_freq - center;
        float w = 1.0f - (d / rightHalfwidth);
        if (w >= 0.0f)                  // Include boundary (w=0)
          weight = std::max(w, 0.001f); // Ensure minimum weight at boundary
      } else if (leftHalfwidth <= 0.0f && rightHalfwidth <= 0.0f) {
        // degenerate: treat only exact center
        weight = (fabsf(current_freq - center) < 1e-6f) ? 1.0f : 0.0f;
      }

      float bandGain = mParam[3 + b]; // param index 3..3+mBands-1 -> band gains
      gain += bandGain * weight;
      weight_sum += weight;
    }

    if (weight_sum > 0.0f) {
      gain /= weight_sum; // normalize so overlapping triangles sum to 1
    } else {
      gain = 1.0f; // pass-through: unity gain if no band matches
    }

    aFFTBuffer[i * 2] *= gain;
  }

  magPhase2Comp(aFFTBuffer, aSamples);
}

SoLoud::result ParametricEq::setParam(unsigned int aParamIndex, float aValue) {
  // Not used. Parameters are read in the ParametricEqInstance and set in the
  // FilterInstance
  return SoLoud::SO_NO_ERROR;
}

int ParametricEq::getParamCount() {
  return 3 + mBands; // wet + SFTF_WINDOW_SIZE + nBands + per-band gains
}

const char *ParametricEq::getParamName(unsigned int aParamIndex) {
  if (aParamIndex == 0)
    return "Wet";
  if (aParamIndex == 1)
    return "Window Size";
  if (aParamIndex == 2)
    return "Bands Count";
  static thread_local std::string s;
  unsigned int band = aParamIndex + 3; // 1-based
  s = std::string("Band ") + std::to_string(band);
  return s.c_str();
}

unsigned int ParametricEq::getParamType(unsigned int aParamIndex) {
  if (aParamIndex == 1)
    return INT_PARAM;
  return FLOAT_PARAM;
}

float ParametricEq::getParamMax(unsigned int aParamIndex) {
  if (aParamIndex == 0)
    return 1; // wet
  if (aParamIndex == 1)
    return 4096; // window size
  if (aParamIndex == 2)
    return 64; // band count
  return 4.0f;
}

float ParametricEq::getParamMin(unsigned int aParamIndex) {
  if (aParamIndex == 0)
    return 0; // wet
  if (aParamIndex == 1)
    return 32; // window size
  if (aParamIndex == 2)
    return 1; // band count
  return 0.0f;
}

void ParametricEq::setFreqs(unsigned int nBands) {
  mBands = std::max(1U, nBands);

  // resize vectors
  mGain.assign(mBands, 1.0f);
  mFreq.resize(mBands);

  // default frequency distribution: geometric spacing between 30Hz and 16000Hz
  float f0 = 60.0f;
  float f1 = 10000.0f;
  if (mBands == 1) {
    mFreq[0] = 1000.0f;
  } else {
    for (int i = 0; i < mBands; i++) {
      float t = (float)i / (float)(mBands - 1);
      mFreq[i] = f0 * powf(f1 / f0, t);
    }
  }
}

ParametricEq::ParametricEq(SoLoud::Soloud *aSoloud, int bands) {
  mSoloud = aSoloud;
  mChannels = aSoloud->mChannels;

  mSTFT_WINDOW_SIZE = 1024;
  mSTFT_WINDOW_HALF = mSTFT_WINDOW_SIZE >> 1;
  mSTFT_WINDOW_TWICE = mSTFT_WINDOW_SIZE << 1;
  mFFT_SCALE = 1.0f / (float)mSTFT_WINDOW_SIZE;

  setFreqs(bands);
}

SoLoud::FilterInstance *ParametricEq::createInstance() {
  return new ParametricEqInstance(this);
}