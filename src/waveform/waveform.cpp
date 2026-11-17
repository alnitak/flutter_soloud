#include "../soloud/src/backend/miniaudio/miniaudio.h"
#if !defined(NO_XIPH_LIBS)
    #include "miniaudio_libvorbis.h"
#endif
#include "soloud_common.h"
#include "waveform.h"
#include "../simd/simd_pcm.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>

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

        // Keep the read chunk L1-cache-resident instead of allocating
        // potentially megabytes when stepFrames is large.
        const ma_uint64 kMaxChunkFrames = 4096;
        const ma_uint64 chunkCapFrames = average
            ? std::min<ma_uint64>(stepFrames, kMaxChunkFrames)
            : std::min<ma_uint64>(stepFrames, kMaxChunkFrames);
        std::vector<float> tempBuffer(static_cast<size_t>(chunkCapFrames) * channels);

        if (!average && stepFrames > 1)
        {
            // Point-sampling mode: read 1 frame per bar and seek directly to
            // the next bar's start frame instead of decoding and discarding
            // (stepFrames - 1) frames per bar.
            bool seekSupported = true;
            unsigned long id = 0;
            for (ma_uint64 i = 0; i < totalFrames && id < numSamplesNeeded; i += stepFrames, id++)
            {
                if (id > 0)
                {
                    const ma_uint64 targetFrame = startFrame + i;
                    if (seekSupported && ma_decoder_seek_to_pcm_frame(decoder, targetFrame) == MA_SUCCESS)
                    {
                        // Seek succeeded directly.
                    }
                    else
                    {
                        seekSupported = false;
                        // Fallback for unseekable decoders: skip (stepFrames - 1) frames.
                        ma_uint64 toSkip = stepFrames - 1;
                        while (toSkip > 0)
                        {
                            ma_uint64 skipped = 0;
                            const ma_uint64 req = std::min<ma_uint64>(toSkip, chunkCapFrames);
                            ma_result skipRes = ma_decoder_read_pcm_frames(decoder, tempBuffer.data(), req, &skipped);
                            if (skipRes != MA_SUCCESS && skipRes != MA_AT_END)
                                return failedToReadPcmFrames;
                            if (skipped == 0)
                                break;
                            toSkip -= skipped;
                        }
                    }
                }

                ma_uint64 framesRead = 0;
                result = ma_decoder_read_pcm_frames(decoder, tempBuffer.data(), 1, &framesRead);
                if (result != MA_SUCCESS && result != MA_AT_END)
                {
                    printf("Failed to read PCM frames.\n");
                    return failedToReadPcmFrames;
                }
                if (framesRead == 0)
                    break;
                pSamples[id] = tempBuffer[0];
            }
            return readSamplesNoError;
        }

        // Read all PCM data between startFrame and endFrame
        unsigned long id = 0;
        for (ma_uint64 i = 0; i < totalFrames && id < numSamplesNeeded; i += stepFrames, id++)
        {
            if (average)
            {
                double sum = 0.0;
                ma_uint64 remaining = stepFrames;
                ma_uint64 totalBarFrames = 0;
                while (remaining > 0)
                {
                    const ma_uint64 req = std::min<ma_uint64>(remaining, chunkCapFrames);
                    ma_uint64 framesRead = 0;
                    result = ma_decoder_read_pcm_frames(decoder, tempBuffer.data(), req, &framesRead);
                    if (result != MA_SUCCESS && result != MA_AT_END)
                    {
                        printf("Failed to read PCM frames.\n");
                        return failedToReadPcmFrames;
                    }
                    if (framesRead == 0)
                        break;
                    const size_t sampleCount = static_cast<size_t>(framesRead) * channels;
                    size_t done = 0;
                    while (done < sampleCount)
                    {
                        const unsigned int chunk = static_cast<unsigned int>(
                            (sampleCount - done) > 0x40000000u ? 0x40000000u : (sampleCount - done));
                        sum += SoLoud::sumOfSquaresF32(tempBuffer.data() + done, chunk);
                        done += chunk;
                    }
                    totalBarFrames += framesRead;
                    remaining -= framesRead;
                    if (result == MA_AT_END)
                        break;
                }
                if (totalBarFrames == 0)
                    break;
                pSamples[id] = sqrtf(static_cast<float>(sum / static_cast<double>(totalBarFrames * channels)));
            }
            else
            {
                // stepFrames == 1
                ma_uint64 framesRead = 0;
                result = ma_decoder_read_pcm_frames(decoder, tempBuffer.data(), 1, &framesRead);
                if (result != MA_SUCCESS && result != MA_AT_END)
                {
                    printf("Failed to read PCM frames.\n");
                    return failedToReadPcmFrames;
                }
                if (framesRead == 0)
                    break;
                pSamples[id] = tempBuffer[0];
            }
        }
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
        // Request f32 output directly on first init (channels=0, sampleRate=0
        // preserves the file's native channel count and sample rate) so we do
        // not open and parse the file/memory buffer twice.
        ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 0, 0);
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
        
        // Init the decoder with file or memory (always passing &decoderConfig
        // so f32 output format is negotiated in a single initialization pass).
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
            printf("Failed to initialize decoder.\n");
            return noBackend;
        }

        ma_uint32 sampleRate;
        ma_uint32 channels;
        ma_format format;
        // Validate audio [sampleRate] and [channels]
        result = ma_data_source_get_data_format(&decoder, &format, &channels, &sampleRate, NULL, 0);
        if (result != MA_SUCCESS)
        {
            printf("Failed to retrieve decoder data format.");
            ma_decoder_uninit(&decoder);
            return failedToGetDataFormat;
        }

        ReadSamplesErrors ret = readSamplesFromDecoder(&decoder, startTime, endTime, numSamplesNeeded, average, pSamples);
        ma_decoder_uninit(&decoder);
        return ret;
    }
};
