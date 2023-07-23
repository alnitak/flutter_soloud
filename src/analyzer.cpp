#include "analyzer.h"
#include "soloud/include/soloud_fft.h"

#include <math.h>

Analyzer::Analyzer(int windowSize) 
    : mWindowSize(windowSize),
    alpha(0.16f),
    a0(0.5f * (1 - alpha)),
    a1(0.5f),
    a2(0.5f * alpha),
    fftSmoothing(0.8)
{
    for (float & i : FFTData) i = 0.0f;
    for (float & i : temp) i = 0.0f;
}

Analyzer::~Analyzer() = default;

/// Blackman windowing
/// used by ShaderToy
void Analyzer::blackmanWindow(float *samples, const float *waveData) const {
    for (int i = 0; i < 256; i++) {
        float multiplier = a0 - a1 * cosf(2 * M_PI * i / mWindowSize) + a2 * cosf(4 * M_PI * i / mWindowSize);
        samples[i*2] = waveData[i] * multiplier;
        samples[i*2+1] = 0;
        samples[i+512] = 0;
        samples[i+768] = 0;
    }
}

/// Hann windowing
void Analyzer::hanningWindow(float* samples, const float *waveData) const
{
    for (int i = 0; i < 256; i++) {
        samples[i*2] = waveData[i] * 0.5f*(1.0f-cosf(2.0f*M_PI*(float)(i)/(float)(mWindowSize-1)));
        samples[i*2+1] = 0.0f;
        samples[i+512] = 0.0f;
        samples[i+768] = 0.0f;
    }
}

/// Hamming windowing
void Analyzer::hammingWindow(float* samples, const float *waveData) const
{
    for (int i = 0; i < 256; i++) {
        samples[i*2] = waveData[i] * (0.54f - 0.46f * cosf(2.0f*M_PI*i/(mWindowSize-1)));
        samples[i*2+1] = 0.0f;
        samples[i+512] = 0.0f;
        samples[i+768] = 0.0f;
    }
}

float * Analyzer::calcFFT(float *waveData)
{
    blackmanWindow(temp, waveData);
    // hanningWindow(temp, waveData);
    // hammingWindow(temp, waveData);

    SoLoud::FFT::fft1024(temp);

    for (int i = 0; i < 256; i++)
    {
        float real = temp[i * 2];
        float imag = temp[i * 2 + 1];
        float mag = sqrtf(real*real+imag*imag);
        // The "+ 1.0" is to make sure I don't get negative values,
        // Multiplying the log10 by the usual 20, by 2 seems to have a better visualization
        float t = 2.0f * log10f(mag+0.995f);
        // float t = 30.0f * (exp(mag)-1.0f);  // the + 1.0 is to make sure I don't get negative values, this is the value you should use in your audio reactive system
        // t *= 8.0f*log10(i+1.4f);

        if (t > 1.0f) t = 1.0f;
        else if (t < 0.00001) t = 0.0f;
        if (t >= FFTData[i])
            FFTData[i] = t;
        else {
            // smooth when decreasing the new value with the previous
            // outputFftData[i] = outputFftData[i] - (outputFftData[i] - t) / 4.0f;
            FFTData[i] = fftSmoothing * FFTData[i] + (1.0f-fftSmoothing) * t;
        }
    }

    return FFTData;
}

void Analyzer::setWindowsSize(int fftWindowSize)
{
    mWindowSize = fftWindowSize;
}

void Analyzer::setSmoothing(float smooth)
{
    if (smooth < 0.0f || smooth > 1.0f) return;
    fftSmoothing = smooth;
}