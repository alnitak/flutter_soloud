#ifndef AMPLITUDE_MODULATOR_FILTER_H
#define AMPLITUDE_MODULATOR_FILTER_H

#include "../soloud/include/soloud.h"

class AmplitudeModulator;

// Bipolar amplitude modulator / ring modulator with a sine carrier.
//
// At 100% wet:
//
//   output(t) = input(t) * sin(phase(t))
//   phaseIncrement = 2 * pi * frequency / sampleRate
//
// This is suppressed-carrier (ring) modulation, not tremolo: the carrier has
// no DC bias, so a sine input at f1 modulated by a carrier at f2 yields
// components at f2 - f1 and f2 + f1 only.
//
// The carrier is driven by a continuous double-precision phase accumulator
// (not an integer samples-per-period counter like RobotizeFilter), so any
// frequency below Nyquist is represented exactly, e.g. 16 kHz at 44.1 kHz.
// The phase persists across callbacks and frequency changes, and one carrier
// value is computed per sample frame and applied to every channel so
// multichannel input stays phase coherent.
class AmplitudeModulatorInstance : public SoLoud::FilterInstance {
  AmplitudeModulator *mParent;

  // Carrier phase in radians, kept in [0, 2*pi).
  double mPhase;
  // Last valid per-sample phase increment, reused if the frequency parameter
  // ever becomes non-finite (e.g. through an unvalidated fade target).
  double mPhaseIncrement;

public:
  virtual void filter(float *aBuffer, unsigned int aSamples,
                      unsigned int aBufferSize, unsigned int aChannels,
                      float aSamplerate, SoLoud::time aTime);
  AmplitudeModulatorInstance(AmplitudeModulator *aParent);
  void setFilterParameter(unsigned int aAttributeId, float aValue);
};

class AmplitudeModulator : public SoLoud::Filter {

public:
  enum FILTERATTRIBUTE { WET = 0, FREQUENCY = 1 };

  // Highest carrier frequency actually processed, as a fraction of the active
  // sample rate. Requests above this (the public range goes up to 20 kHz,
  // which is above Nyquist for low device rates) are clamped so the carrier
  // itself never aliases.
  static constexpr double MAX_NYQUIST_RATIO = 0.49;

  float mWet;       // wet/dry mix, 1.0 = fully modulated, 0.0 = bypass
  float mFrequency; // carrier frequency in Hz

  virtual int getParamCount();
  virtual const char *getParamName(unsigned int aParamIndex);
  virtual unsigned int getParamType(unsigned int aParamIndex);
  virtual float getParamMax(unsigned int aParamIndex);
  virtual float getParamMin(unsigned int aParamIndex);
  SoLoud::result setParam(unsigned int aParamIndex, float aValue);
  virtual SoLoud::FilterInstance *createInstance();
  AmplitudeModulator();
};

#endif // AMPLITUDE_MODULATOR_FILTER_H
