#ifndef LIMITER_H
#define LIMITER_H

#include "../soloud/include/soloud.h"
#include <vector>

class Limiter;

// True look-ahead brick-wall peak limiter.
//
// Replaces the original feed-forward soft-knee compressor that smoothed the
// gain envelope AFTER computing the required reduction (which let transients
// leak through the "ceiling"). The new implementation:
//
//   * delays the audio by ATTACK_TIME (now interpreted as a look-ahead
//     window in milliseconds) so the gain has time to ramp DOWN to the
//     required value BEFORE the offending sample is emitted;
//   * uses a stereo-linked peak detector (max across channels) so the
//     stereo image stays stable on transients;
//   * walks the gain envelope backward across the look-ahead window with a
//     linear attack ramp, guaranteeing that the gain at output position
//     equals the required reduction;
//   * applies a one-pole release smoother (attack is instantaneous via the
//     envelope; only release is smoothed);
//   * hard-clips the post-mix output at the ceiling as a safety net so the
//     output is mathematically guaranteed to stay <= ceiling even when the
//     wet/dry mix introduces dry-path overshoot.
//
// Public parameter IDs and ranges are unchanged so the Dart wrapper and
// existing presets continue to work. The semantic of ATTACK_TIME shifts
// from "envelope smoothing time constant" to "look-ahead window length";
// in practice the typical value (1 ms) means the same thing to the user.
class LimiterInstance : public SoLoud::FilterInstance {
  Limiter *mParent;

  // Ring buffer holding delayed audio: size = mRingSize * mChannels
  std::vector<float> mDelayBuffer;
  // Ring buffer holding the per-sample required gain envelope: size = mRingSize
  std::vector<float> mGainEnv;

  int mRingSize;       // look-ahead in samples
  int mChannels;       // cached channel count
  int mWritePos;       // ring index for the newest sample
  float mSmoothedGain; // gain after release smoothing
  float mReleaseCoef;  // one-pole release coefficient (recomputed per block)

  void resizeBuffers(int lookaheadSamples, int channels);

public:
  virtual void filter(float *aBuffer, unsigned int aSamples,
                      unsigned int aBufferSize, unsigned int aChannels,
                      float aSamplerate, SoLoud::time aTime);
  LimiterInstance(Limiter *aParent);
  void setFilterParameter(unsigned int aAttributeId, float aValue);
};

class Limiter : public SoLoud::Filter {

public:
  enum FILTERATTRIBUTE {
    WET = 0,
    THRESHOLD = 1,
    OUTPUT_CEILING = 2,
    KNEE_WIDTH = 3,
    RELEASE_TIME = 4,
    ATTACK_TIME = 5
  };
  unsigned int mSamplerate;
  float mWet;           // wet/dry mix, 1.0 = fully limited, 0.0 = bypass
  float mThreshold;     // dB at which gain reduction starts (knee centred here)
  float mOutputCeiling; // dB hard maximum output (guaranteed)
  float mKneeWidth;     // dB width of the soft-knee transition into limiting
  float mReleaseTime;   // ms one-pole release time constant
  float mAttackTime;    // ms look-ahead window length (was: envelope attack)

  virtual int getParamCount();
  virtual const char *getParamName(unsigned int aParamIndex);
  virtual unsigned int getParamType(unsigned int aParamIndex);
  virtual float getParamMax(unsigned int aParamIndex);
  virtual float getParamMin(unsigned int aParamIndex);
  SoLoud::result setParam(unsigned int aParamIndex, float aValue);
  virtual SoLoud::FilterInstance *createInstance();
  Limiter(unsigned int aSamplerate);
};

#endif // LIMITER_H
