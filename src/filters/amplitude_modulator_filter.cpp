#include "amplitude_modulator_filter.h"

#include <cmath>

namespace {
constexpr double TWO_PI = 6.283185307179586476925286766559;
} // namespace

AmplitudeModulatorInstance::AmplitudeModulatorInstance(
    AmplitudeModulator *aParent) {
  mParent = aParent;
  initParams(2);
  mParam[AmplitudeModulator::WET] = aParent->mWet;
  mParam[AmplitudeModulator::FREQUENCY] = aParent->mFrequency;

  mPhase = 0.0;
  // The sample rate is only known in filter(), which computes the increment
  // before processing the first frame.
  mPhaseIncrement = 0.0;
}

void AmplitudeModulatorInstance::filter(float *aBuffer, unsigned int aSamples,
                                        unsigned int aBufferSize,
                                        unsigned int aChannels,
                                        float aSamplerate, SoLoud::time aTime) {
  // Pick up direct parameter changes as well as fadeFilterParameter() and
  // oscillateFilterParameter() faders.
  updateParams(aTime);

  if (!(aSamplerate > 0.0f) || !std::isfinite(aSamplerate))
    return;

  // Faders are not validated by setFilterParameter(), so sanitize here.
  float wet = mParam[AmplitudeModulator::WET];
  if (!std::isfinite(wet))
    wet = 0.0f;
  else if (wet < 0.0f)
    wet = 0.0f;
  else if (wet > 1.0f)
    wet = 1.0f;

  // Effective carrier frequency: the requested one, constrained below Nyquist
  // for the active sample rate. The requested value in mParam is left as is,
  // so querying the parameter still returns what the user set. Only the
  // increment changes here; the accumulated phase is preserved so frequency
  // changes are click-free.
  const double sampleRate = (double)aSamplerate;
  const double requested = (double)mParam[AmplitudeModulator::FREQUENCY];
  if (std::isfinite(requested)) {
    double freq = requested;
    const double minFreq =
        (double)mParent->getParamMin(AmplitudeModulator::FREQUENCY);
    const double maxFreq = sampleRate * AmplitudeModulator::MAX_NYQUIST_RATIO;
    if (freq < minFreq)
      freq = minFreq;
    if (freq > maxFreq)
      freq = maxFreq;
    mPhaseIncrement = TWO_PI * freq / sampleRate;
  }

  // The increment is always below pi (the carrier is below Nyquist), so a
  // single subtraction keeps the phase in [0, 2*pi).
  const double increment = mPhaseIncrement;
  double phase = mPhase;

  // SoLoud passes audio in PLANAR layout: channel ch occupies
  // [ch*aBufferSize, ch*aBufferSize + aSamples).
  const size_t stride = (size_t)aBufferSize;

  if (wet <= 0.0f) {
    // Fully dry: leave the audio untouched but keep the oscillator running,
    // so the phase trajectory does not depend on the wet setting.
    for (unsigned int i = 0; i < aSamples; ++i) {
      phase += increment;
      if (phase >= TWO_PI)
        phase -= TWO_PI;
    }
    mPhase = phase;
    return;
  }

  // dry * (1 - wet) + dry * carrier * wet == dry * ((1 - wet) + wet * carrier)
  const float dryGain = 1.0f - wet;
  for (unsigned int i = 0; i < aSamples; ++i) {
    // One carrier value per frame, shared by every channel.
    const float gain = dryGain + wet * (float)std::sin(phase);
    for (unsigned int ch = 0; ch < aChannels; ++ch) {
      aBuffer[i + (size_t)ch * stride] *= gain;
    }
    phase += increment;
    if (phase >= TWO_PI)
      phase -= TWO_PI;
  }
  mPhase = phase;
}

void AmplitudeModulatorInstance::setFilterParameter(unsigned int aAttributeId,
                                                    float aValue) {
  if (aAttributeId >= mNumParams)
    return;

  // Written so that NaN is rejected too.
  if (!(aValue >= mParent->getParamMin(aAttributeId) &&
        aValue <= mParent->getParamMax(aAttributeId)))
    return;

  mParamFader[aAttributeId].mActive = 0;
  mParam[aAttributeId] = aValue;
  mParamChanged |= 1 << aAttributeId;
}

SoLoud::result AmplitudeModulator::setParam(unsigned int aParamIndex,
                                            float aValue) {
  if (!(aValue >= getParamMin(aParamIndex) &&
        aValue <= getParamMax(aParamIndex)))
    return SoLoud::INVALID_PARAMETER;

  switch (aParamIndex) {
  case WET:
    mWet = aValue;
    break;
  case FREQUENCY:
    mFrequency = aValue;
    break;
  default:
    return SoLoud::INVALID_PARAMETER;
  }
  return SoLoud::SO_NO_ERROR;
}

int AmplitudeModulator::getParamCount() { return 2; }

const char *AmplitudeModulator::getParamName(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return "Wet";
  case FREQUENCY:
    return "Frequency";
  }
  return "Wet";
}

unsigned int AmplitudeModulator::getParamType(unsigned int /*aParamIndex*/) {
  return FLOAT_PARAM;
}

float AmplitudeModulator::getParamMax(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return 1.0f;
  case FREQUENCY:
    return 20000.0f;
  }
  return 1;
}

float AmplitudeModulator::getParamMin(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return 0.0f;
  case FREQUENCY:
    return 0.1f;
  }
  return 0;
}

AmplitudeModulator::AmplitudeModulator() {
  mWet = 1.0f;
  mFrequency = 440.0f;
}

SoLoud::FilterInstance *AmplitudeModulator::createInstance() {
  return new AmplitudeModulatorInstance(this);
}
