#ifndef PITCH_SHIFT_FILTER_H
#define PITCH_SHIFT_FILTER_H

#include <cstring>
#include <vector>

#include "../soloud/include/soloud.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

class PitchShift;

class PitchShiftInstance : public SoLoud::FilterInstance {
  signalsmith::stretch::SignalsmithStretch<float> mStretch;
  PitchShift *mParent;
  unsigned int mChannelCount = 0;
  float mSampleRate = 0;

public:
  virtual void filter(float *aBuffer, unsigned int aSamples,
                      unsigned int aBufferSize, unsigned int aChannels,
                      float aSamplerate, SoLoud::time aTime);
  PitchShiftInstance(PitchShift *aParent);
  void setFilterParameter(unsigned int aAttributeId, float aValue);
};

class PitchShift : public SoLoud::Filter {

public:
  enum FILTERATTRIBUTE { WET = 0, SHIFT = 1, SEMITONES = 2 };
  float mWet;
  float mShift;
  float mSemitones;
  virtual int getParamCount();
  virtual const char *getParamName(unsigned int aParamIndex);
  virtual unsigned int getParamType(unsigned int aParamIndex);
  virtual float getParamMax(unsigned int aParamIndex);
  virtual float getParamMin(unsigned int aParamIndex);
  SoLoud::result setParam(unsigned int aParamIndex, float aValue);
  virtual SoLoud::FilterInstance *createInstance();
  PitchShift();
};

#endif
