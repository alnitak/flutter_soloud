namespace Waveform {
    void readSamples(
        const char *filePath,
        const unsigned char *buffer,
        unsigned long dataSize,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        bool average,
        float *pSamples);
}
