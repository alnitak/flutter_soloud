#include "../soloud/src/backend/miniaudio/miniaudio.h"
#if !defined(NO_XIPH_LIBS)
    #include "miniaudio_libvorbis.h"
#endif
#include "soloud_common.h"
#include "waveform.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

// Helper function to convert UTF-8 to wide string on Windows
#ifdef _WIN32
static wchar_t* utf8ToWide(const char* utf8Str)
{
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
    if (wideLen == 0)
        return NULL;
    wchar_t* wideStr = new wchar_t[wideLen];
    if (MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, wideLen) == 0)
    {
        delete[] wideStr;
        return NULL;
    }
    return wideStr;
}
#endif

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
        ma_uint64 totalFramesInFile;
        ma_result lengthResult = ma_decoder_get_length_in_pcm_frames(decoder, &totalFramesInFile);
        
        if (endTime == -1)
            endFrame = totalFramesInFile;
        else
            endFrame = (ma_uint64)(endTime * sampleRate);
        
        // Clamp endFrame to file length
        if (endFrame > totalFramesInFile && lengthResult == MA_SUCCESS)
            endFrame = totalFramesInFile;
            
        // Ensure startFrame doesn't exceed endFrame
        if (startFrame >= endFrame)
        {
            printf("readSamplesFromDecoder: startFrame >= endFrame (%llu >= %llu).\n", 
                   (unsigned long long)startFrame, (unsigned long long)endFrame);
            return readSamplesNoError;  // Return zeros (already memset)
        }
            
        ma_uint64 totalFrames = endFrame - startFrame;
        
        // Ensure numSamplesNeeded is not larger than totalFrames to avoid stepFrames being 0
        if (numSamplesNeeded > totalFrames)
        {
            numSamplesNeeded = (unsigned long)totalFrames;
            if (numSamplesNeeded == 0)
            {
                printf("readSamplesFromDecoder: numSamplesNeeded adjusted to 0.\n");
                return readSamplesNoError;
            }
        }
        
        ma_uint64 stepFrames = totalFrames / numSamplesNeeded;
        if (stepFrames == 0)
        {
            stepFrames = 1;  // Ensure at least 1 frame per sample
        }

        // Move decoder to start frame
        ma_result result = ma_decoder_seek_to_pcm_frame(decoder, startFrame);
        if (result != MA_SUCCESS)
        {
            printf("Failed to seek to start time.\n");
            return failedToSeekPcm;
        }

        // Allocate temporary memory for all frames between startTime and endTime
        size_t tempBufferSize = stepFrames * channels * sizeof(float);
        float *tempBuffer = (float *)malloc(tempBufferSize);
        if (tempBuffer == NULL)
        {
            printf("Failed to allocate tempBuffer (%zu bytes).\n", tempBufferSize);
            return failedToReadPcmFrames;
        }
        // Read all PCM data between startFrame and endFrame
        unsigned long id = 0;
        for (ma_uint64 i = 0; i < totalFrames && id < numSamplesNeeded; i += stepFrames, id++)
        {
            ma_uint64 framesRead;
            result = ma_decoder_read_pcm_frames(decoder, tempBuffer, stepFrames, &framesRead);
            if (result != MA_SUCCESS && result != MA_AT_END)
            {
                printf("Failed to read PCM frames.\n");
                free(tempBuffer);
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
        // Validate parameters
        if (pSamples == NULL)
        {
            printf("readSamples: pSamples is NULL.\n");
            return failedToReadPcmFrames;
        }
        if (filePath == NULL && (buffer == NULL || dataSize == 0))
        {
            printf("readSamples: buffer is NULL or dataSize is 0 when filePath is NULL.\n");
            return failedToReadPcmFrames;
        }
        if (numSamplesNeeded == 0)
        {
            printf("readSamples: numSamplesNeeded is 0.\n");
            return readSamplesNoError;  // Nothing to do
        }

        // Clear memory
        memset(pSamples, 0, numSamplesNeeded * sizeof(float));

        ma_decoder decoder;
        ma_decoder_config decoderConfig = ma_decoder_config_init_default();
        ma_result result;
        bool isOgg = false;

#if !defined(NO_XIPH_LIBS)
        // Create a static backend vtable to ensure it persists
        static ma_decoding_backend_vtable* pCustomBackendVTables[] = {
            ma_decoding_backend_libvorbis
        };
#endif

        // Check if the file is an OGG file by reading the header
        if (filePath != NULL)
        {
            FILE *file = NULL;
#ifdef _WIN32
            // On Windows, convert UTF-8 to wide string and use _wfopen
            wchar_t* wideName = utf8ToWide(filePath);
            if (wideName)
            {
                file = _wfopen(wideName, L"rb");
                delete[] wideName;
            }
#else
            file = fopen(filePath, "rb");
#endif
            if (file)
            {
                unsigned char header[4];
                size_t bytesRead = fread(header, 1, 4, file);
                if (bytesRead == 4 && header[0] == 'O' && header[1] == 'g' && header[2] == 'g' && header[3] == 'S')
                    isOgg = true;
                fclose(file);
            }
        }
        // Check if buffer is an OGG file
        else if (dataSize >= 4 && buffer[0] == 'O' && buffer[1] == 'g' && buffer[2] == 'g' && buffer[3] == 'S')
            isOgg = true;

#if defined(NO_XIPH_LIBS)
        if (isOgg)
        {
            soloud_platform_log("No OGG support. If you want OGG support, please undefine NO_XIPH_LIBS\n");
            return ReadSamplesErrors::noBackend;
        }
#endif

#if !defined(NO_XIPH_LIBS)
        if (isOgg)
        {
            decoderConfig.pCustomBackendUserData = NULL;
            decoderConfig.ppCustomBackendVTables = pCustomBackendVTables;
            decoderConfig.customBackendCount = sizeof(pCustomBackendVTables) / sizeof(pCustomBackendVTables[0]);
        }
#endif
        
        // Init the decoder with file or memory
        if (filePath != NULL)
        {
#ifdef _WIN32
            // On Windows, use wide string version for UTF-8 support
            wchar_t* wideName = utf8ToWide(filePath);
            if (!wideName)
            {
                printf("Failed to convert filename to wide string.\n");
                return noBackend;
            }
            result = ma_decoder_init_file_w(wideName, isOgg ? &decoderConfig : NULL, &decoder);
            delete[] wideName;
#else
            result = ma_decoder_init_file(filePath, isOgg ? &decoderConfig : NULL, &decoder);
#endif
        }
        else
        {
            result = ma_decoder_init_memory(buffer, dataSize, isOgg ? &decoderConfig : NULL, &decoder);
        }

        if (result != MA_SUCCESS)
        {
            printf("Failed to initialize decoder.\n");
            return noBackend;
        }

        ma_uint32 sampleRate;
        ma_uint32 channels;
        ma_format format;
        // Get audio [sampleRate] and [channels]
        result = ma_data_source_get_data_format(&decoder, &format, &channels, &sampleRate, NULL, 0);
        if (result != MA_SUCCESS)
        {
            printf("Failed to retrieve decoder data format.");
            ma_decoder_uninit(&decoder);
            return failedToGetDataFormat;
        }

        // Re-initialize decoder with f32 format
        if (format != ma_format_f32)
        {
            ma_decoder_uninit(&decoder);
            memset(&decoder, 0, sizeof(decoder));  // Zero out decoder struct before re-init
            
            // Update config with format settings
            decoderConfig = ma_decoder_config_init_default();  // Reset config
            decoderConfig.format = ma_format_f32;
            decoderConfig.channels = channels;
            decoderConfig.sampleRate = sampleRate;
#if !defined(NO_XIPH_LIBS)
            if (isOgg)
            {
                decoderConfig.ppCustomBackendVTables = pCustomBackendVTables;
                decoderConfig.customBackendCount = sizeof(pCustomBackendVTables) / sizeof(pCustomBackendVTables[0]);
            }
#endif

            // Re-init with updated config
            if (filePath != NULL)
            {
#ifdef _WIN32
                // On Windows, use wide string version for UTF-8 support
                wchar_t* wideName = utf8ToWide(filePath);
                if (!wideName)
                {
                    printf("Failed to convert filename to wide string.\n");
                    return noBackend;
                }
                result = ma_decoder_init_file_w(wideName, &decoderConfig, &decoder);
                delete[] wideName;
#else
                result = ma_decoder_init_file(filePath, &decoderConfig, &decoder);
#endif
            }
            else
            {
                result = ma_decoder_init_memory(buffer, dataSize, &decoderConfig, &decoder);
            }

            if (result != MA_SUCCESS)
            {
                printf("Failed to initialize decoder forcing f32.\n");
                return noBackend;
            }
        }

        ReadSamplesErrors ret = readSamplesFromDecoder(&decoder, startTime, endTime, numSamplesNeeded, average, pSamples);
        ma_decoder_uninit(&decoder);
        return ret;
    }
};
