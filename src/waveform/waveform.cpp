#include "../soloud/src/backend/miniaudio/miniaudio.h"
#include "waveform.h"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace Waveform
{
    ReadSamplesErrors readSamplesFromDecoder(
        ma_decoder *decoder,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        bool average,
        float *pSamples)
    {
        ma_uint32 sampleRate = decoder->outputSampleRate;
        ma_uint32 channels = decoder->outputChannels;

        // Calculate start and end frames based on startTime and endTime
        ma_uint64 startFrame = (ma_uint64)(startTime * sampleRate);
        ma_uint64 endFrame;
        if (endTime == -1)
            ma_decoder_get_length_in_pcm_frames(decoder, &endFrame);
        else
            endFrame = (ma_uint64)(endTime * sampleRate);
            
        ma_uint64 totalFrames = endFrame - startFrame;
        ma_uint64 stepFrames = totalFrames / numSamplesNeeded;

        // Move decoder to start frame
        ma_result result = ma_decoder_seek_to_pcm_frame(decoder, startFrame);
        if (result != MA_SUCCESS)
        {
            printf("Failed to seek to start time.\n");
            ma_decoder_uninit(decoder);
            return failedToSeekPcm;
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
                if (result != MA_AT_END)
                    return failedToReadPcmFrames;
            }

            if (framesRead == 0)
                break;
            // Inside [tempBuffer] there are now all the samples between the values ​​of a stepFrame
            if (average)
            {
                double sum = 0.0;
                for (int j = 0; j < framesRead * channels; j++)
                {
                    // Square the sample value
                    sum += tempBuffer[j] * tempBuffer[j];
                }
                // Calculate RMS (Root Mean Square)
                pSamples[id] = sqrtf(sum / (framesRead * channels));
            }
            else
                pSamples[id] = tempBuffer[0];
        }
        free(tempBuffer);
        return readSamplesNoError;
    }

    ReadSamplesErrors readSamples(
        const char *filePath,
        const unsigned char *buffer,
        unsigned long dataSize,
        float startTime,
        float endTime,
        unsigned long numSamplesNeeded,
        bool average,
        float *pSamples)
    {
        // Clear memory
        memset(pSamples, 0, numSamplesNeeded * sizeof(float));

        ma_decoder decoder;
        ma_result result;
        // Init the decoder with file or memory
        if (filePath != NULL)
            result = ma_decoder_init_file(filePath, NULL, &decoder);
        else
            result = ma_decoder_init_memory(buffer, dataSize, NULL, &decoder);

        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize decoder.\n");
            return noBackend;
        }

        ma_uint32 sampleRate;
        ma_uint32 channels;
        ma_format format;
        // Get audio [sampleRate] and  [channels]
        result = ma_data_source_get_data_format(&decoder, &format, &channels, &sampleRate, NULL, 0);
        if (result != MA_SUCCESS)
        {
            printf("Failed to retrieve decoder data format.");
            ma_decoder_uninit(&decoder);
            return failedToGetDataFormat;
        }
        // Re-init decoder forcing ma_format_f32
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, channels, sampleRate);
        if (filePath != NULL)
            result = ma_decoder_init_file(filePath, &config, &decoder);
        else
            result = ma_decoder_init_memory(buffer, dataSize, &config, &decoder);

        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize decoder forcing f32.\n");
            return noBackend;
        }

        ReadSamplesErrors ret = readSamplesFromDecoder(&decoder, startTime, endTime, numSamplesNeeded, average, pSamples);
        ma_decoder_uninit(&decoder);
        return ret;
    }
};