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
        : sampleRate(sampleRate), channels(channels), streamInitialized(false) {
        
        // Create Opus decoder
        decoder = EM_ASM_INT({
            var error = new Int32Array(1);
            var decoder = Module_opus._opus_decoder_create($0, $1, error);
            if (error[0] !== 0) {
                throw new Error("Failed to create Opus decoder");
            }
            return decoder;
        }, sampleRate, channels);

        // Initialize Ogg sync state
        syncState = EM_ASM_INT({
            return Module_ogg._ogg_sync_init();
        }, 0);

        // Allocate Ogg structures
        oggPage = EM_ASM_INT({
            return Module._malloc(280); // typical ogg_page size
        }, 0);

        oggPacket = EM_ASM_INT({
            return Module._malloc(32);  // typical ogg_packet size
        }, 0);
    }

    ~OpusDecoderWrapper() {
        EM_ASM({
            if ($0) Module_opus._opus_decoder_destroy($0);
            if ($1) Module_ogg._ogg_sync_clear($1);
            if ($2) Module_ogg._ogg_stream_clear($2);
            if ($3) Module._free($3);  // free oggPage
            if ($4) Module._free($4);  // free oggPacket
        }, decoder, syncState, streamState, oggPage, oggPacket);
    }

    std::vector<float> decode(const unsigned char* inputData, size_t inputSize) {
        std::vector<float> decodedData;

        // Write data to Ogg sync buffer
        char* buffer = (char*)EM_ASM_INT({
            var buffer = Module_ogg._ogg_sync_buffer($0, $1);
            return buffer;
        }, syncState, inputSize);

        memcpy(buffer, inputData, inputSize);

        EM_ASM({
            Module_ogg._ogg_sync_wrote($0, $1);
        }, syncState, inputSize);

        // Process pages
        while (EM_ASM_INT({
            return Module_ogg._ogg_sync_pageout($0, $1);
        }, syncState, oggPage) == 1) {
            
            // Initialize stream state if needed
            if (!streamInitialized) {
                streamState = EM_ASM_INT({
                    var serialno = Module_ogg._ogg_page_serialno($0);
                    return Module_ogg._ogg_stream_init(serialno);
                }, oggPage);
                streamInitialized = true;
            }

            // Submit page to stream
            EM_ASM({
                Module_ogg._ogg_stream_pagein($0, $1);
            }, streamState, oggPage);

            // Extract packets
            while (EM_ASM_INT({
                return Module_ogg._ogg_stream_packetout($0, $1);
            }, streamState, oggPacket) == 1) {
                
                // Get packet data
                unsigned char* packetData = (unsigned char*)EM_ASM_INT({
                    return Module.HEAP32[($0 + 4) >> 2]; // op->packet
                }, oggPacket);
                
                int packetSize = EM_ASM_INT({
                    return Module.HEAP32[($0 + 8) >> 2]; // op->bytes
                }, oggPacket);

                // Process packet
                auto decoded = decodePacket(packetData, packetSize);
                decodedData.insert(decodedData.end(), decoded.begin(), decoded.end());
            }
        }

        return decodedData;
    }

private:
    std::vector<float> decodePacket(const unsigned char* packetData, size_t packetSize) {
        std::vector<float> packetPcm;

        // Handle header packets
        if (!headerParsed) {
            if (packetCount == 0 || packetCount == 1) {
                headerParsed = true;
                packetCount++;
                return packetPcm;
            }
        }

        if (packetSize < 1) return packetPcm;

        // Decode audio packet
        const int maxFrameSize = sampleRate / 50;
        std::vector<float> outputBuffer(maxFrameSize * channels);

        int samples = EM_ASM_INT({
            return Module_opus._opus_decode_float(
                $0,    // decoder
                $1,    // input
                $2,    // input size
                $3,    // output
                $4,    // max size
                0      // decodeFEC
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
    int streamState;
    int oggPage;
    int oggPacket;
    int sampleRate;
    int channels;
    bool streamInitialized;
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