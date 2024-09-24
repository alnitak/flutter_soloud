#ifndef WAVEFORM_H
#define WAVEFORM_H

#include "../enums.h"

namespace Waveform {
    ReadSamplesErrors readSamples(
        const char *filePath,
        const unsigned char *buffer,
        unsigned long dataSize,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        bool average,
        float *pSamples);
}

#endif // WAVEFORM_H