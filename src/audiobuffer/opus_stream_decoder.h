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
    int sampleRate;
    int channels;

public:
    OpusDecoderWrapper(int sampleRate, int channels) 
        : sampleRate(sampleRate), channels(channels)
    {
        this->sampleRate = sampleRate;
        this->channels = channels;
        EM_ASM({
            // Allocate and initialize Ogg structures in Module memory
            var oyPtr = Module._malloc(Module_ogg._ogg_sync_state_size || 280);
            var osPtr = Module._malloc(Module_ogg._ogg_stream_state_size || 464);
            var ogPtr = Module._malloc(Module_ogg._ogg_page_size || 72);
            var opPtr = Module._malloc(Module_ogg._ogg_packet_size || 32);
            
            // Initialize Opus decoder
            var error = 0;
            var errorPtr = Module._malloc(4);
            Module.setValue(errorPtr, 0, 'i32');
            console.log("@@@@@@@1  ", $0, "  ", $1, "  ", errorPtr);
            var decoderPtr = Module_opus._opus_decoder_create($0, $1, errorPtr);
            var errorValue = Module.getValue(errorPtr, 'i32');
            Module._free(errorPtr);
            console.log("@@@@@@@2  ",$0,"  ",$1,"  ",errorValue);
            
            if (errorValue !== 0) {
                var errorStr = UTF8ToString(Module_opus._opus_strerror(errorValue));
                throw new Error("Failed to create Opus decoder: " + errorStr);
            }

            // Initialize Ogg sync state
            Module_ogg._ogg_sync_init(oyPtr);
            
            // Store pointers in Module for later use
            Module.OpusDecoderState = {};
            Module.OpusDecoderState.decoder = decoderPtr;
            Module.OpusDecoderState.oy = oyPtr;
            Module.OpusDecoderState.os = osPtr;
            Module.OpusDecoderState.og = ogPtr;
            Module.OpusDecoderState.op = opPtr;
            Module.OpusDecoderState.streamInitialized = false;
            Module.OpusDecoderState.headerParsed = false;
            Module.OpusDecoderState.packetCount = 0;
        }, sampleRate, channels);
    }

    ~OpusDecoderWrapper() {
        EM_ASM({
            if (Module.OpusDecoderState) {
                // Clean up Opus decoder
                if (Module.OpusDecoderState.decoder) {
                    Module_opus._opus_decoder_destroy(Module.OpusDecoderState.decoder);
                }
                
                // Clean up Ogg streams
                if (Module.OpusDecoderState.streamInitialized) {
                    Module_ogg._ogg_stream_clear(Module.OpusDecoderState.os);
                }
                Module_ogg._ogg_sync_clear(Module.OpusDecoderState.oy);
                
                // Free allocated memory
                Module._free(Module.OpusDecoderState.oy);
                Module._free(Module.OpusDecoderState.os);
                Module._free(Module.OpusDecoderState.og);
                Module._free(Module.OpusDecoderState.op);
                
                delete Module.OpusDecoderState;
            }
        });
    }

    std::vector<float> decode(const unsigned char* inputData, size_t inputSize) {
        std::vector<float> result;
        const int localSampleRate = this->sampleRate;  // Create local copies
        const int localChannels = this->channels;
        
        // Copy input data to WASM memory
        unsigned char* inputBuffer = (unsigned char*)EM_ASM_PTR({
            var ptr = Module._malloc($0);
            Module.HEAPU8.set(new Uint8Array(HEAPU8.buffer, $1, $0), ptr);
            return ptr;
        }, inputSize, inputData);

        // Create output buffer in WASM memory with local variable
        float* outputBuffer = (float*)EM_ASM_PTR({
            return Module._malloc($0 * $1 * 4); // float = 4 bytes
        }, localSampleRate/50, localChannels);

        EM_ASM_({
            var state = Module.OpusDecoderState;
            var inputSize = $1;
            var inputData = $0;
            var outputPtr = $2;
            var result = $3;
            var sr = $4;
            var ch = $5;
            
            // Write data to Ogg sync buffer
            var buffer = Module_ogg._ogg_sync_buffer(state.oy, inputSize);
            Module._memcpy(buffer, inputData, inputSize);
            Module_ogg._ogg_sync_wrote(state.oy, inputSize);
            
            // Process pages
            while (Module_ogg._ogg_sync_pageout(state.oy, state.og) === 1) {
                if (!state.streamInitialized) {
                    var serialno = Module_ogg._ogg_page_serialno(state.og);
                    Module_ogg._ogg_stream_init(state.os, serialno);
                    state.streamInitialized = true;
                }
                
                if (Module_ogg._ogg_stream_pagein(state.os, state.og) < 0) {
                    throw new Error("Error reading Ogg page");
                }
                
                while (Module_ogg._ogg_stream_packetout(state.os, state.op) === 1) {
                    var packetData = Module.getValue(state.op + 0, '*');
                    var packetSize = Module.getValue(state.op + 24, 'i64');
                    
                    if (!state.headerParsed) {
                        if (state.packetCount === 0) {
                            if (packetSize >= 8) state.headerParsed = true;
                        } else if (state.packetCount === 1) {
                            state.headerParsed = true;
                        }
                        state.packetCount++;
                        continue;
                    }
                    
                    if (packetSize < 1) continue;
                    
                    var maxFrameSize = sr / 50;
                    
                    var samples = Module_opus._opus_decode_float(
                        state.decoder,
                        packetData,
                        packetSize,
                        outputPtr,
                        maxFrameSize,
                        0
                    );
                    
                    if (samples > 0) {
                        var size = samples * ch;
                        var heapView = new Float32Array(Module.HEAPF32.buffer, outputPtr, size);
                        for (var i = 0; i < size; i++) {
                            Module.HEAPF32[(result>>2) + i] = heapView[i];
                        }
                    }
                }
            }
        }, inputBuffer, inputSize, outputBuffer, result.data(), localSampleRate, localChannels);

        // Free temporary buffers
        EM_ASM_({
            Module._free($0); // Free input buffer
            Module._free($1); // Free output buffer
        }, inputBuffer, outputBuffer);
        
        return result;
    }

};

#else

/// Wrapper class for Opus stream decoder
///
/// The supported sampleRate for Opus format are 8, 12, 16, 24 amd 48 KHz.
/// The channels is the number of channels in the audio data. Only 1 or 2 allowed.
class OpusDecoderWrapper
{
public:
    OpusDecoderWrapper(int sampleRate, int channels)
        : sampleRate(sampleRate), channels(channels), streamInitialized(false)
    {
        int error;
        decoder = opus_decoder_create(sampleRate, channels, &error);
        if (error != OPUS_OK)
        {
            throw std::runtime_error("Failed to create Opus decoder: " + std::string(opus_strerror(error)));
        }

        ogg_sync_init(&oy);
    }

    ~OpusDecoderWrapper()
    {
        if (decoder)
            opus_decoder_destroy(decoder);

        if (streamInitialized)
        {
            ogg_stream_clear(&os);
        }
        ogg_sync_clear(&oy);
    }

    std::vector<float> decode(const unsigned char *inputData, size_t inputSize)
    {
        std::vector<float> decodedData;

        // Write data into ogg sync buffer
        char *buffer = ogg_sync_buffer(&oy, inputSize);
        memcpy(buffer, inputData, inputSize);
        ogg_sync_wrote(&oy, inputSize);

        // Read and process pages
        while (ogg_sync_pageout(&oy, &og) == 1)
        {
            if (!streamInitialized)
            {
                ogg_stream_init(&os, ogg_page_serialno(&og));
                streamInitialized = true;
            }

            if (ogg_stream_pagein(&os, &og) < 0)
            {
                throw std::runtime_error("Error reading Ogg page");
            }

            // Extract packets from page
            while (ogg_stream_packetout(&os, &op) == 1)
            {
                auto packetData = decodePacket(op.packet, op.bytes);
                decodedData.insert(decodedData.end(), packetData.begin(), packetData.end());
            }
        }

        return decodedData;
    }

private:
    std::vector<float> decodePacket(const unsigned char *packetData, size_t packetSize)
    {
        std::vector<float> packetPcm;

        // Skip header packets (first 2 packets in Ogg Opus stream)
        if (!headerParsed)
        {
            if (packetCount == 0)
            {
                // OpusHead packet
                if (packetSize < 8)
                    return packetPcm;
                headerParsed = true;
            }
            else if (packetCount == 1)
            {
                // OpusTags packet
                headerParsed = true;
            }
            packetCount++;
            return packetPcm;
        }

        // Validate packet
        if (packetSize < 1)
        {
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

        if (samples < 0)
        {
            std::cerr << "Warning: Failed to decode packet: " << opus_strerror(samples) << std::endl;
            return packetPcm; // Skip invalid packet instead of throwing
        }

        if (samples > 0)
        {
            packetPcm.insert(packetPcm.end(),
                             outputBuffer.begin(),
                             outputBuffer.begin() + samples * channels);
        }
        return packetPcm;
    }

    OpusDecoder *decoder;
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