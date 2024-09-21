namespace Waveform {
    void readSamplesFromFile(
        const char *filePath,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        float* pSamples);

    void readSamplesFromMem(
        const unsigned char *buffer,
        unsigned long dataSize,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        float* pSamples);
}
