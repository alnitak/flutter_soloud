#include "amplitude_modulator_filter.h"
#include "../simd/soloud_simd.h"

#include <cmath>

namespace {
constexpr double TWO_PI = 6.283185307179586476925286766559;
constexpr double INV_TWO_PI = 0.1591549430918953357688837633725;
constexpr double PI = 3.1415926535897932384626433832795;

inline float evaluateCarrier(int waveform, double phase, double delta) {
  switch (waveform) {
  case AmplitudeModulator::WAVE_TRIANGLE: {
    // phase in [0, 2*pi), normalized to [0, 1)
    // Starts at 0, ramps to +1 at phase=pi/2 (p=0.25), 0 at phase=pi (p=0.5), -1 at phase=3*pi/2 (p=0.75)
    double p = phase * INV_TWO_PI;
    return (float)(p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0));
  }
  case AmplitudeModulator::WAVE_SQUARE: {
    // Smooth transition between +1 and -1 around phase = 0/2*pi and phase = pi.
    // Transition width w is the larger of 3 samples (3 * delta) or ~0.5 ms in radians.
    // Capped at 0.1 * pi to preserve square shape even at high frequencies.
    double w = delta * 3.0;
    if (w < 0.0005 * TWO_PI * 40.0) // small lower bound for low freqs
      w = 0.0005 * TWO_PI * 40.0;
    if (w > 0.1 * PI)
      w = 0.1 * PI;

    // Transition 1: around phase = pi (from +1 to -1)
    if (phase > PI - w && phase < PI + w) {
      double t = (phase - (PI - w)) / (2.0 * w); // 0 to 1
      return (float)std::cos(t * PI);             // +1 smoothly to -1
    }
    // Transition 2: around phase = 0 / 2*pi (from -1 to +1)
    if (phase < w) {
      double t = (phase + w) / (2.0 * w);        // 0.5 to 1
      return (float)-std::cos(t * PI);            // 0 smoothly to +1
    }
    if (phase > TWO_PI - w) {
      double t = (phase - (TWO_PI - w)) / (2.0 * w); // 0 to 0.5
      return (float)-std::cos(t * PI);            // -1 smoothly to 0
    }
    return phase < PI ? 1.0f : -1.0f;
  }
  case AmplitudeModulator::WAVE_SAW: {
    // Saw down: starts at +1 (phase = 0), ramps linearly down to -1 (phase -> 2*pi)
    // Smooth the wrap-around edge around phase = 0/2*pi from -1 back to +1
    double w = delta * 3.0;
    if (w > 0.05 * PI)
      w = 0.05 * PI;
    if (phase > TWO_PI - w) {
      double t = (phase - (TWO_PI - w)) / (2.0 * w); // 0 to 0.5
      return (float)-std::cos(t * PI);
    }
    if (phase < w) {
      double t = (phase + w) / (2.0 * w); // 0.5 to 1
      return (float)-std::cos(t * PI);
    }
    double normPhase = phase * INV_TWO_PI;
    return (float)(1.0 - 2.0 * normPhase);
  }
  case AmplitudeModulator::WAVE_SINE:
  default:
    return (float)std::sin(phase);
  }
}
} // namespace

AmplitudeModulatorInstance::AmplitudeModulatorInstance(
    AmplitudeModulator *aParent) {
  mParent = aParent;
  initParams(3);
  mParam[AmplitudeModulator::WET] = aParent->mWet;
  mParam[AmplitudeModulator::FREQUENCY] = aParent->mFrequency;
  mParam[AmplitudeModulator::WAVEFORM] = (float)aParent->mWaveform;

  mPhase = 0.0;
  // The sample rate is only known in filter(), which computes the increment
  // before processing the first frame.
  mPhaseIncrement = 0.0;
  mGainScratch.resize(SAMPLE_GRANULARITY, 1.0f);
}

void AmplitudeModulatorInstance::filter(float *aBuffer, unsigned int aSamples,
                                        unsigned int aBufferSize,
                                        unsigned int aChannels,
                                        float aSamplerate, SoLoud::time aTime) {
  // Pick up direct parameter changes as well as fadeFilterParameter() and
  // oscillateFilterParameter() faders.
  updateParams(aTime);

  if (!soloud_simd::isFiniteBits(aSamplerate) || !(aSamplerate > 0.0f))
    return;

  // Faders are not validated by setFilterParameter(), so sanitize here.
  float wet = mParam[AmplitudeModulator::WET];
  if (!soloud_simd::isFiniteBits(wet))
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
  const float requestedF = mParam[AmplitudeModulator::FREQUENCY];
  if (soloud_simd::isFiniteBits(requestedF)) {
    double freq = (double)requestedF;
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
  const int waveform = (int)mParam[AmplitudeModulator::WAVEFORM];
  if (aChannels == 0) {
    for (unsigned int i = 0; i < aSamples; ++i) {
      phase += increment;
      if (phase >= TWO_PI)
        phase -= TWO_PI;
    }
  } else {
    // The gain scratch is sized once in the constructor; blocks larger than
    // it (global or bus filters on a large quantum) are processed in chunks
    // rather than growing it on the audio thread.
    float *gains = mGainScratch.data();
    const unsigned int chunkMax =
        static_cast<unsigned int>(mGainScratch.size());
    for (unsigned int base = 0; base < aSamples; base += chunkMax) {
      const unsigned int count =
          aSamples - base < chunkMax ? aSamples - base : chunkMax;
      for (unsigned int i = 0; i < count; ++i) {
        const float carrier = evaluateCarrier(waveform, phase, increment);
        gains[i] = dryGain + wet * carrier;
        phase += increment;
        if (phase >= TWO_PI)
          phase -= TWO_PI;
      }
      for (unsigned int ch = 0; ch < aChannels; ++ch) {
        float *chBuf = aBuffer + (size_t)ch * stride + base;
        unsigned int i = 0;
#if defined(SOLOUD_SIMD_ENABLED)
        for (; i + 4 <= count; i += 4) {
          (soloud_simd::f32x4::loadu(chBuf + i) *
           soloud_simd::f32x4::loadu(gains + i))
              .storeu(chBuf + i);
        }
#endif
        for (; i < count; ++i) {
          chBuf[i] *= gains[i];
        }
      }
    }
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
  mParamChanged |= 1ULL << (aAttributeId < 63 ? aAttributeId : 63);
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
  case WAVEFORM:
    mWaveform = (int)aValue;
    break;
  default:
    return SoLoud::INVALID_PARAMETER;
  }
  return SoLoud::SO_NO_ERROR;
}

int AmplitudeModulator::getParamCount() { return 3; }

const char *AmplitudeModulator::getParamName(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return "Wet";
  case FREQUENCY:
    return "Frequency";
  case WAVEFORM:
    return "Waveform";
  }
  return "Wet";
}

unsigned int AmplitudeModulator::getParamType(unsigned int aParamIndex) {
  if (aParamIndex == WAVEFORM)
    return INT_PARAM;
  return FLOAT_PARAM;
}

float AmplitudeModulator::getParamMax(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return 1.0f;
  case FREQUENCY:
    return 20000.0f;
  case WAVEFORM:
    return 3.0f;
  }
  return 1;
}

float AmplitudeModulator::getParamMin(unsigned int aParamIndex) {
  switch (aParamIndex) {
  case WET:
    return 0.0f;
  case FREQUENCY:
    return 0.1f;
  case WAVEFORM:
    return 0.0f;
  }
  return 0;
}

AmplitudeModulator::AmplitudeModulator() {
  mWet = 1.0f;
  mFrequency = 440.0f;
  mWaveform = WAVE_SINE;
}

SoLoud::FilterInstance *AmplitudeModulator::createInstance() {
  return new AmplitudeModulatorInstance(this);
}
