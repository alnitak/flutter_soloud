#include "analyzer.h"
#include "soloud/include/soloud_fft.h"

#include <math.h>
#include <cstring>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Analyzer::Analyzer(int windowSize, float sampleRate)
    : mWindowSize(windowSize),
      sampleRate(sampleRate),
      alpha(0.16f),
      a0(0.5f * (1 - alpha)),
      a1(0.5f),
      a2(0.5f * alpha),
      fftSmoothing(0.8)
{
    for (float &i : FFTData)
        i = 0.0f;
    for (float &i : temp)
        i = 0.0f;
}

Analyzer::~Analyzer() = default;

/// Blackman windowing
/// used by ShaderToy
void Analyzer::blackmanWindow(float *samples, const float *waveData) const {
    memset(samples + 512, 0, 512 * sizeof(float));
    for (int i = 0; i < 256; i++) {
        float multiplier = a0 - a1 * cosf(2 * M_PI * i / mWindowSize) + a2 * cosf(4 * M_PI * i / mWindowSize);
        samples[i*2] = waveData[i] * multiplier;
        samples[i*2+1] = 0;
    }
}

/// Hann windowing
void Analyzer::hanningWindow(float *samples, const float *waveData) const
{
    memset(samples + 512, 0, 512 * sizeof(float));
    for (int i = 0; i < 256; i++)
    {
        samples[i * 2] = waveData[i] * 0.5f * (1.0f - cosf(2.0f * M_PI * (float)(i) / (float)(mWindowSize - 1)));
        samples[i * 2 + 1] = 0.0f;
    }
}

/// Hamming windowing
void Analyzer::hammingWindow(float *samples, const float *waveData) const
{
    memset(samples + 512, 0, 512 * sizeof(float));
    for (int i = 0; i < 256; i++)
    {
        samples[i * 2] = waveData[i] * (0.54f - 0.46f * cosf(2.0f * M_PI * i / (mWindowSize - 1)));
        samples[i * 2 + 1] = 0.0f;
    }
}

/// Gaussian windowing
void Analyzer::gaussWindow(float *samples, const float *waveData) const
{
    const float sigma = 0.4f;  // Standard deviation (adjustable, typical values between 0.3 and 0.5)
    const float N = mWindowSize - 1;
    
    memset(samples + 512, 0, 512 * sizeof(float));
    for (int i = 0; i < 256; i++)
    {
        float n = i - N/2;  // Center the Gaussian
        float gaussian = expf(-0.5f * powf((n / (sigma * N/2)), 2));
        samples[i * 2] = waveData[i] * gaussian;
        samples[i * 2 + 1] = 0.0f;
    }
}

int Analyzer::freqToBin(float frequency) const {
    // Simpler linear mapping
    return static_cast<int>((frequency * 256.0f) / maxFreq);
}

int Analyzer::mapFrequencyToFFTDataIndex(float freq) const {
    // Map frequency to 0-255 range
    return static_cast<int>(255.0f * (freq - minFreq) / (maxFreq - minFreq));
}

float Analyzer::mapFFTDataIndexToFrequency(int index) const {
    // Map 0-255 range back to frequency
    return minFreq + (index * (maxFreq - minFreq) / 255.0f);
}

float Analyzer::getBinFrequency(int binIndex) const {
    // Consider Nyquist frequency
    return binIndex * (sampleRate * 0.5f / 256.0f);
}

float* Analyzer::calcFFT(float* waveData, float minFrequency, float maxFrequency)
{
    if (waveData == nullptr)
        return nullptr;

    // https://en.wikipedia.org/wiki/Window_function
    blackmanWindow(temp, waveData);
    // hanningWindow(temp, waveData);
    // hammingWindow(temp, waveData);
    // gaussWindow(temp, waveData);

    SoLoud::FFT::fft1024(temp);

    float real = temp[255 * 2];
    float imag = temp[255 * 2 + 1];
    float mag = sqrtf(real*real+imag*imag);
    // Apply frequency-dependent scaling
    float freqScaling = sqrtf(255.f + 1.f);  // Adjust scaling based on frequency bin
    mag *= freqScaling / 2.0f;  // Normalize the scaling
    // The "+ 1.0" is to make sure I don't get negative values,
    float t = 2.f * log10f(mag+1.0f);
    FFTData[255] = t;

    for (int i = 254; i >= 0; i--)
    {
        float real = temp[i * 2];
        float imag = temp[i * 2 + 1];
        float mag = sqrtf(real*real+imag*imag);

        // Apply frequency-dependent scaling
        float freqScaling = sqrtf((float)(i + 1));  // Adjust scaling based on frequency bin
        mag *= freqScaling / 2.0f;  // Normalize the scaling

        // The "+ 1.0" is to make sure I don't get negative values,
        float t = 2.f * log10f(mag+1.0f) - FFTData[255];

        if (t > 1.0f) t = 1.0f;
        else if (t < 0.001f) t = 0.0f;
        if (t >= FFTData[i])
            FFTData[i] = t;
        else {
            // smooth when decreasing the new value with the previous
            FFTData[i] = fftSmoothing * FFTData[i] + (1.0f-fftSmoothing) * t;
        }
    }
    FFTData[255] = 0.0f;

    return FFTData;
}

void Analyzer::setWindowsSize(int fftWindowSize)
{
    mWindowSize = fftWindowSize;
}

void Analyzer::setSmoothing(float smooth)
{
    if (smooth < 0.0f || smooth > 1.0f)
        return;
    fftSmoothing = smooth;
}