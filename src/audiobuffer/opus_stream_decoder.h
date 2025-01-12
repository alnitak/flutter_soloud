#ifndef OPUS_STREAM_DECODER_H
#define OPUS_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <fstream>
#include <deque>
#include <ogg/ogg.h>
#include <opus/opus.h>
#endif

#ifdef __EMSCRIPTEN__

class OpusDecoderWrapper {
public:
    OpusDecoderWrapper(int sampleRate, int channels)
        : sampleRate(sampleRate), channels(channels) {
        
        decoder = EM_ASM_INT({
            var error = new Int32Array(1);
            var decoder = Module_opus._opus_decoder_create($0, $1, error);
            if (error[0] !== 0) {
                throw new Error("Failed to create Opus decoder: " + error[0]);
            }
            console.log("Opus decoder created with sample rate: " + $0 + " and channels: " + $1);
            return decoder;
        }, sampleRate, channels);

        EM_ASM({
            var oy = Module_ogg._ogg_sync_init();
            Module.HEAP32[$0 >> 2] = oy;
        }, &syncState);
    }

    ~OpusDecoderWrapper() {
        EM_ASM({
            Module_opus._opus_decoder_destroy($0);
            Module_ogg._ogg_sync_clear($1);
        }, decoder, syncState);
    }

    std::vector<float> decode(const unsigned char* inputData, size_t inputSize) {
        std::vector<float> decodedData;

        EM_ASM({
            var inputPtr = $0;
            var inputSize = $1;
            var syncState = $2;
            
            // Write data to ogg sync buffer
            var buffer = Module_ogg._ogg_sync_buffer(syncState, inputSize);
            Module.HEAPU8.set(Module.HEAPU8.subarray(inputPtr, inputPtr + inputSize), buffer);
            Module_ogg._ogg_sync_wrote(syncState, inputSize);
            
            // Process pages
            var streamState = 0;
            var streamInitialized = false;
            
            while (Module_ogg._ogg_sync_pageout(syncState, og) === 1) {
                if (!streamInitialized) {
                    streamState = Module_ogg._ogg_stream_init(Module_ogg._ogg_page_serialno(og));
                    streamInitialized = true;
                }
                
                if (Module_ogg._ogg_stream_pagein(streamState, og) < 0) {
                    throw new Error("Error reading Ogg page");
                }
                
                while (Module_ogg._ogg_stream_packetout(streamState, op) === 1) {
                    // Process packet here
                    var packetData = Module.HEAPU8.subarray(op.packet, op.packet + op.bytes);
                    var decoded = this.decodePacket(packetData, op.bytes);
                    decodedData.push(...decoded);
                }
            }
            
            if (streamInitialized) {
                Module_ogg._ogg_stream_clear(streamState);
            }
        }, inputData, inputSize, syncState);

        return decodedData;
    }

private:
    std::vector<float> decodePacket(const unsigned char* packetData, size_t packetSize) {
        std::vector<float> packetPcm;

        if (!headerParsed) {
            if (packetCount == 0 || packetCount == 1) {
                headerParsed = true;
                packetCount++;
                return packetPcm;
            }
        }

        if (packetSize < 1) return packetPcm;

        const int maxFrameSize = sampleRate / 50;
        std::vector<float> outputBuffer(maxFrameSize * channels);

        int samples = EM_ASM_INT({
            var decoder = $0;
            var packetData = $1;
            var packetSize = $2;
            var outputPtr = $3;
            var maxFrameSize = $4;
            
            return Module_opus._opus_decode_float(
                decoder,
                packetData,
                packetSize,
                outputPtr,
                maxFrameSize,
                0
            );
        }, decoder, packetData, packetSize, outputBuffer.data(), maxFrameSize);

        if (samples > 0) {
            packetPcm.insert(packetPcm.end(), 
                           outputBuffer.begin(), 
                           outputBuffer.begin() + samples * channels);
        }
        return packetPcm;
    }

    int decoder;
    int syncState;
    int sampleRate;
    int channels;
    bool headerParsed{false};
    int packetCount{0};
};

#else

/// Wrapper class for Opus stream decoder
///
/// The supported sampleRate for Opus format are 8, 12, 16, 24 amd 48 KHz.
/// The channels is the number of channels in the audio data. Only 1 or 2 allowed.
class OpusDecoderWrapper {
public:
    OpusDecoderWrapper(int sampleRate, int channels)
        : sampleRate(sampleRate), channels(channels), streamInitialized(false) {
        int error;
        decoder = opus_decoder_create(sampleRate, channels, &error);
        if (error != OPUS_OK) {
            throw std::runtime_error("Failed to create Opus decoder: " + std::string(opus_strerror(error)));
        }

        ogg_sync_init(&oy);
    }

    ~OpusDecoderWrapper() {
        if (decoder) opus_decoder_destroy(decoder);
        
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

        const int maxFrameSize = sampleRate / 50; // Max 20ms frame size
        std::vector<float> outputBuffer(maxFrameSize * channels);

        // Try decoding the packet
        int samples = opus_decode_float(decoder, 
                                      packetData, 
                                      packetSize,
                                      outputBuffer.data(), 
                                      maxFrameSize, 
                                      0);

        if (samples < 0) {
            std::cerr << "Warning: Failed to decode packet: " << opus_strerror(samples) << std::endl;
            return packetPcm;  // Skip invalid packet instead of throwing
        }

        if (samples > 0) {
            packetPcm.insert(packetPcm.end(), 
                           outputBuffer.begin(), 
                           outputBuffer.begin() + samples * channels);
        }
        return packetPcm;
    }

    OpusDecoder* decoder;
    int sampleRate;
    int channels;

    // Ogg state variables
    ogg_sync_state oy;
    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;
    bool streamInitialized;

    // Header parsing state
    bool headerParsed{false};
    int packetCount{0};
};

#endif // __EMSCRIPTEN__

#endif // OPUS_STREAM_DECODER_H