#ifndef ANALYZER_H
#define ANALYZER_H

#ifndef COMMON_H
#include "common.h"
#endif

class Analyzer {
public:
    Analyzer(int windowSize);
    ~Analyzer();

    float * calcFFT(float *waveData);
    void setWindowsSize(int fftWindowSize);
    void setSmoothing(float smooth);

private:
    /// @brief elaborate FFT data with the Blackman windowing algorithm
    void blackmanWindow(float *samples, const float *waveData) const;

    /// @brief elaborate FFT data with the hanning windowing algorithm
    void hanningWindow(float* samples, const float *waveData) const;

    /// @brief elaborate FFT data with the hamm windowing algorithm
    void hammingWindow(float* samples, const float *waveData) const;

    /// array used by filling it with audio samples and calculate FFT
    float temp[1024];

    /// contains latest calulated FFT
    float FFTData[256];

    /// window size used by windowing algorithms.
    /// The size is optained when the player has been initialized
    /// and is given by the backend buffer size 
    /// over its number of channels (maybe@#`#@`#!!)
    int mWindowSize;

    /// parameters for the Blackman windowing algorithm
    float alpha;
    float a0;
    float a1;
    float a2;
    float fftSmoothing;
};

#endif // ANALYZER_H
