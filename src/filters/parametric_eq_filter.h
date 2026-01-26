#ifndef PARAMETRIC_EQ_FILTER_H
#define PARAMETRIC_EQ_FILTER_H

#include "../pffft/pffft.h"
#include "../soloud/include/soloud.h"
#include <string>
#include <vector>

class ParametricEq;

class ParametricEqInstance : public SoLoud::FilterInstance {
  ParametricEq *mParent;
  // float* mBuffer;          // Temporary buffer for FFT processing
  // float* mWindow;          // Window function coefficients
  // unsigned int mBufferSize;
  // unsigned int mBufferPos;
  float *mInputBuffer[MAX_CHANNELS];
  float *mMixBuffer[MAX_CHANNELS];
  float *mTemp;
  float *mFFTBuffer; // Aligned buffer for PFFFT
  float *mFFTWork;   // Work buffer for PFFFT
  PFFFT_Setup *mFFTSetup;
  unsigned int mInputOffset[MAX_CHANNELS];
  unsigned int mMixOffset[MAX_CHANNELS];
  unsigned int mReadOffset[MAX_CHANNELS];

  // Band information precomputed for fast lookup
  int mBands;
  std::vector<float> mBandCenter;
  std::vector<float> mBandBoundary; // size = mBands+1, boundaries between bands

  // Helper functions for FFT processing
  void comp2MagPhase(float *aFFTBuffer, unsigned int aSamples);
  void magPhase2Comp(float *aFFTBuffer, unsigned int aSamples);

  // Initialize band parameters (centers, boundaries, gains)
  void initBandParameters();

  // Initialize FFT setup and allocate buffers
  void initFFTBuffers();

public:
  ParametricEqInstance(ParametricEq *aParent);
  virtual ~ParametricEqInstance();
  virtual void filterChannel(float *aBuffer, unsigned int aSamples,
                             float aSamplerate, SoLoud::time aTime,
                             unsigned int aChannel, unsigned int aChannels);
  virtual void fftFilterChannel(float *aFFTBuffer, unsigned int aSamples,
                                float aSamplerate, SoLoud::time aTime,
                                unsigned int aChannel, unsigned int aChannels);
  virtual void setFilterParameter(unsigned int aAttributeId, float aValue);
};

class ParametricEq : public SoLoud::Filter {
public:
  // Wet param is index 0 in the filter param list; band gains start at index 1
  unsigned int mBands;      // number of EQ bands (user configurable)
  std::vector<float> mGain; // per-band gain
  std::vector<float> mFreq; // per-band center frequency
  int mSTFT_WINDOW_SIZE; // Increased for better frequency resolution. Must be a
                         // power of 2.
  int mSTFT_WINDOW_HALF;
  int mSTFT_WINDOW_TWICE;
  float mFFT_SCALE;

  ParametricEq(SoLoud::Soloud *aSoloud, int bands = 3);
  virtual int getParamCount();
  virtual const char *getParamName(unsigned int aParamIndex);
  virtual unsigned int getParamType(unsigned int aParamIndex);
  virtual float getParamMax(unsigned int aParamIndex);
  virtual float getParamMin(unsigned int aParamIndex);
  SoLoud::result setParam(unsigned int aParamIndex, float aValue);
  void setFreqs(unsigned int nBands);
  virtual SoLoud::FilterInstance *createInstance();

  /// main SoLoud engine, the one used by player.cpp
  SoLoud::Soloud *mSoloud;

  int mChannels;
};

#endif