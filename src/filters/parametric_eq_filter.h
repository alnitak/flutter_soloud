#ifndef PARAMETRIC_EQ_FILTER_H
#define PARAMETRIC_EQ_FILTER_H

#include "../pffft/pffft.h"
#include "../soloud/include/soloud.h"
#include <string>
#include <vector>

class ParametricEq;

class ParametricEqInstance : public SoLoud::FilterInstance {
  ParametricEq *mParent;
  float *mInputBuffer[MAX_CHANNELS];
  float *mMixBuffer[MAX_CHANNELS];
  float *mTemp;
  float *mFFTBuffer; // Aligned buffer for PFFFT
  float *mFFTWork;   // Work buffer for PFFFT
  PFFFT_Setup *mFFTSetup;
  unsigned int mInputOffset[MAX_CHANNELS];
  unsigned int mMixOffset[MAX_CHANNELS];
  unsigned int mReadOffset[MAX_CHANNELS];

  int mBands;

  // Helper functions for FFT processing
  void comp2MagPhase(float *aFFTBuffer, unsigned int aSamples);
  void magPhase2Comp(float *aFFTBuffer, unsigned int aSamples);

  // Initialize band parameters (gains, frequencies, Q factors)
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
  static const unsigned int MAX_BANDS = 64;
  static const unsigned int BAND_GAIN_OFFSET = 3;
  static const unsigned int BAND_FREQ_OFFSET = 3 + MAX_BANDS;   // 67
  static const unsigned int BAND_Q_OFFSET = 3 + MAX_BANDS * 2;  // 131
  static const unsigned int NUM_PARAMS = 3 + MAX_BANDS * 3;     // 195

  unsigned int mBands;      // number of active EQ bands (user configurable)
  float mWet;
  std::vector<float> mGain; // per-band gain (size MAX_BANDS)
  std::vector<float> mFreq; // per-band center frequency (size MAX_BANDS)
  std::vector<float> mQ;    // per-band Q factor (size MAX_BANDS)
  int mSTFT_WINDOW_SIZE;    // FFT window size (power of 2, 32-4096)
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