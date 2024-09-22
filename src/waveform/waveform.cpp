#include "../soloud/src/backend/miniaudio/miniaudio.h"
#include "waveform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace Waveform
{
    void readSamples(
        ma_decoder *decoder,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        float *pSamples)
    {
        ma_uint32 sampleRate;
        ma_uint32 channels;
        ma_format format;

        // Initialize the device
        ma_result result = ma_data_source_get_data_format(decoder, &format, &channels, &sampleRate, NULL, 0);
        if (result != MA_SUCCESS) {
            printf("Failed to retrieve decoder data format.");
            ma_decoder_uninit(decoder);
            return;
        }

        // Calculate start and end frames based on startTime and endTime
        ma_uint64 startFrame = (ma_uint64)(startTime * sampleRate);
        ma_uint64 endFrame;
        if (endTime == -1)
        {
            ma_decoder_get_length_in_pcm_frames(decoder, &endFrame);
        }
        else
        {
            endFrame = (ma_uint64)(endTime * sampleRate);
        }
        ma_uint64 totalFrames = endFrame - startFrame;
        ma_uint64 stepFrames = totalFrames / numSamplesNeeded;

        // Move decoder to start frame
        result = ma_decoder_seek_to_pcm_frame(decoder, startFrame);
        if (result != MA_SUCCESS)
        {
            printf("Failed to seek to start time.\n");
            ma_decoder_uninit(decoder);
            return;
        }

        // Allocate temporary memory for all frames between startTime and endTime
        float *tempBuffer = (float *)malloc(stepFrames * channels * sizeof(float));
        // Read all PCM data between startFrame and endFrame
        int id = 0;
        for (int i = 0; i < totalFrames; i += stepFrames, id++)
        {
            ma_uint64 framesRead;
            result = ma_decoder_read_pcm_frames(decoder, tempBuffer, stepFrames, &framesRead);
            if (result != MA_SUCCESS && result != MA_AT_END)
            {
                printf("Failed to read PCM frames.\n");
                ma_decoder_uninit(decoder);
                return;
            }

            if (framesRead == 0)
                break;
            // Inside tempBuffer there are now all the samples within a stepFrame values
            double average = 0;
            for (int j = 0; j < framesRead; j++)
            {
                average += tempBuffer[j] * tempBuffer[j];
            }
            pSamples[id] = sqrtf(average / framesRead) * channels;
            // printf("%f ", pSamples[id]);
        }
        printf("\n id: %d\n", id);
        free(tempBuffer);

    }

    void readSamplesFromMem(
        const unsigned char *buffer,
        unsigned long dataSize,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        float *pSamples)
    {
        // Init the decoder
        ma_decoder decoder;
        ma_result result = ma_decoder_init_memory(buffer, dataSize, NULL, &decoder);
        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize decoder.\n");
            memset(pSamples, 0, numSamplesNeeded * sizeof(float));
            return;
        }
        readSamples(&decoder, startTime, endTime, numSamplesNeeded, pSamples);
        ma_decoder_uninit(&decoder);
    }

    void readSamplesFromFile(
        const char *filePath,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        float *pSamples)
    {
        // Init the decoder
        ma_decoder decoder;
        ma_result result = ma_decoder_init_file(filePath, NULL, &decoder);
        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize decoder.\n");
            memset(pSamples, 0, numSamplesNeeded * sizeof(float));
            return;
        }
        readSamples(&decoder, startTime, endTime, numSamplesNeeded, pSamples);
        ma_decoder_uninit(&decoder);
    }

};