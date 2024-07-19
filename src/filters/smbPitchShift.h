#pragma once

// http://blogs.zynaptiq.com/bernsee/pitch-shifting-using-the-ft/

class CSmbPitchShift
{
    enum { MAX_FRAME_LENGTH = 8192 };

public:
    void reset()
    {
        gRover = 0;
        gInit = false;
    }
    void smbPitchShift(float pitchShift, long numSampsToProcess, long fftFrameSize, long osamp, float sampleRate, float *indata, float *outdata);

private:
    float gInFIFO[MAX_FRAME_LENGTH];
    float gOutFIFO[MAX_FRAME_LENGTH];
    float gFFTworksp[2 * MAX_FRAME_LENGTH];
    float gLastPhase[MAX_FRAME_LENGTH / 2 + 1];
    float gSumPhase[MAX_FRAME_LENGTH / 2 + 1];
    float gOutputAccum[2 * MAX_FRAME_LENGTH];
    float gAnaFreq[MAX_FRAME_LENGTH];
    float gAnaMagn[MAX_FRAME_LENGTH];
    float gSynFreq[MAX_FRAME_LENGTH];
    float gSynMagn[MAX_FRAME_LENGTH];

    float gErrors[MAX_FRAME_LENGTH];

    long gRover = 0;
    bool gInit = false;
};
