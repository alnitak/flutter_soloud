#ifndef OPUS_STREAM_DECODER_H
#define OPUS_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <deque>
#include "opus/opus.h"
#include "ogg/ogg.h"
#include "speex/speex_resampler.h"

/// Wrapper class for Opus stream decoder
///
/// The supported inputSampleRate for Opus format are 8, 12, 16, 24 amd 48 KHz.
/// The outputSampleRate is the desired output sample rate.
/// The inputChannels and outputChannels are the number of channels in the input and output audio data. Only 1 or 2 allowed.
class OpusDecoderWrapper {
public:
    OpusDecoderWrapper(int inputSampleRate, int outputSampleRate, int inputChannels, int outputChannels)
        : inputSampleRate(inputSampleRate), outputSampleRate(outputSampleRate), 
          inputChannels(inputChannels), outputChannels(outputChannels), streamInitialized(false) {
        int error;
        decoder = opus_decoder_create(inputSampleRate, inputChannels, &error);
        if (error != OPUS_OK) {
            throw std::runtime_error("Failed to create Opus decoder: " + std::string(opus_strerror(error)));
        }

        resampler = speex_resampler_init(outputChannels, inputSampleRate, outputSampleRate, 
                                       SPEEX_RESAMPLER_QUALITY_DEFAULT, &error);
        if (error != RESAMPLER_ERR_SUCCESS) {
            throw std::runtime_error("Failed to create resampler: " + std::string(speex_resampler_strerror(error)));
        }

        ogg_sync_init(&oy);
    }

    ~OpusDecoderWrapper() {
        if (decoder) opus_decoder_destroy(decoder);
        if (resampler) speex_resampler_destroy(resampler);
        
        if (streamInitialized) {
            ogg_stream_clear(&os);
        }
        ogg_sync_clear(&oy);
    }

    std::vector<float> decode(const unsigned char* inputData, size_t inputSize) {
        std::vector<float> decodedData;
        
        // Write data into ogg sync buffer
        char* buffer = ogg_sync_buffer(&oy, inputSize);
        memcpy(buffer, inputData, inputSize);
        ogg_sync_wrote(&oy, inputSize);

        // Read and process pages
        while (ogg_sync_pageout(&oy, &og) == 1) {
            if (!streamInitialized) {
                ogg_stream_init(&os, ogg_page_serialno(&og));
                streamInitialized = true;
            }
            
            if (ogg_stream_pagein(&os, &og) < 0) {
                throw std::runtime_error("Error reading Ogg page");
            }
            
            // Extract packets from page
            while (ogg_stream_packetout(&os, &op) == 1) {
                auto packetData = decodePacket(op.packet, op.bytes);
                decodedData.insert(decodedData.end(), packetData.begin(), packetData.end());
            }
        }

        return decodedData;
    }

private:
    std::vector<float> decodePacket(const unsigned char* packetData, size_t packetSize) {
        std::vector<float> packetPcm;
        
        // Skip header packets (first 2 packets in Ogg Opus stream)
        if (!headerParsed) {
            if (packetCount == 0) {
                // OpusHead packet
                if (packetSize < 8) return packetPcm;
                headerParsed = true;
            } else if (packetCount == 1) {
                // OpusTags packet
                headerParsed = true;
            }
            packetCount++;
            return packetPcm;
        }

        // Validate packet
        if (packetSize < 1) {
            return packetPcm;
        }

        const int maxFrameSize = inputSampleRate / 50; // Max 20ms frame size
        std::vector<float> inputBuffer(maxFrameSize * inputChannels);
        const int outputFrameSize = (maxFrameSize * outputSampleRate + inputSampleRate - 1) / inputSampleRate;
        std::vector<float> outputBuffer(outputFrameSize * outputChannels);

        // Try decoding the packet
        int samples = opus_decode_float(decoder, 
                                      packetData, 
                                      packetSize,
                                      inputBuffer.data(), 
                                      maxFrameSize, 
                                      0);

        if (samples < 0) {
            std::cerr << "Warning: Failed to decode packet: " << opus_strerror(samples) << std::endl;
            return packetPcm;  // Skip invalid packet instead of throwing
        }

        if (samples > 0) {
            spx_uint32_t inLen = samples;
            spx_uint32_t outLen = outputFrameSize;
            
            if (inputChannels == 2 && outputChannels == 2) {
                int error = speex_resampler_process_interleaved_float(
                    resampler,
                    inputBuffer.data(),
                    &inLen,
                    outputBuffer.data(),
                    &outLen);
                
                if (error != RESAMPLER_ERR_SUCCESS) {
                    throw std::runtime_error("Resampling failed: " + 
                                           std::string(speex_resampler_strerror(error)));
                }

                packetPcm.insert(packetPcm.end(), 
                               outputBuffer.begin(), 
                               outputBuffer.begin() + outLen * outputChannels);
            } else if (inputChannels == 2 && outputChannels == 1) {
                // Process stereo to mono
                std::vector<float> monoInput(samples);
                for (int i = 0; i < samples; i++) {
                    // Mix down stereo to mono by averaging channels
                    monoInput[i] = (inputBuffer[i * 2] + inputBuffer[i * 2 + 1]) * 0.5f;
                }

                int error = speex_resampler_process_float(
                    resampler,
                    0,  // channel 0
                    monoInput.data(),
                    &inLen,
                    outputBuffer.data(),
                    &outLen);

                if (error != RESAMPLER_ERR_SUCCESS) {
                    throw std::runtime_error("Resampling failed: " + std::string(speex_resampler_strerror(error)));
                }
                packetPcm.insert(packetPcm.end(), outputBuffer.begin(), outputBuffer.begin() + outLen);
            } else if (inputChannels == 1 && outputChannels == 2) {
                // Process mono input (existing mono->stereo code)
                int error = speex_resampler_process_float(resampler, 
                    0,
                    inputBuffer.data(),
                    &inLen,
                    outputBuffer.data(),
                    &outLen);
                
                if (error != RESAMPLER_ERR_SUCCESS) {
                    throw std::runtime_error("Resampling failed: " + std::string(speex_resampler_strerror(error)));
                }

                // Convert mono to stereo
                std::vector<float> stereoOutput(outLen * 2);
                for (spx_uint32_t i = 0; i < outLen; ++i) {
                    stereoOutput[i * 2] = outputBuffer[i];
                    stereoOutput[i * 2 + 1] = outputBuffer[i];
                }
                packetPcm.insert(packetPcm.end(), stereoOutput.begin(), stereoOutput.end());
            } else if (inputChannels == 1 && outputChannels == 1) {
                // Direct mono-to-mono processing
                int error = speex_resampler_process_float(
                    resampler,
                    0,  // channel 0
                    inputBuffer.data(),
                    &inLen,
                    outputBuffer.data(),
                    &outLen);

                if (error != RESAMPLER_ERR_SUCCESS) {
                    throw std::runtime_error("Resampling failed: " + 
                                           std::string(speex_resampler_strerror(error)));
                }

                packetPcm.insert(packetPcm.end(), 
                               outputBuffer.begin(), 
                               outputBuffer.begin() + outLen);
            }
        }
        return packetPcm;
    }

    OpusDecoder* decoder;
    int inputSampleRate;
    int outputSampleRate;
    int inputChannels;
    int outputChannels;
    SpeexResamplerState* resampler;

    // Add Ogg state as member variables
    ogg_sync_state oy;
    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;
    bool streamInitialized;

    // Add new member variables
    bool headerParsed{false};
    int packetCount{0};
};

#endif // OPUS_STREAM_DECODER_H