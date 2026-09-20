#include "stream_decoder.h"
#include "mp3_stream_decoder.h"
#include "wav_stream_decoder.h"
#include "aac_stream_decoder.h"
#if !defined(NO_XIPH_LIBS)
#   include "opus_stream_decoder.h"
#   include "vorbis_stream_decoder.h"
#   include "flac_stream_decoder.h"
#   include "ogg_flac_stream_decoder.h"
#endif
#include "../native_decoder/native_audio_decoder.h"
#include <cstring>

void StreamDecoder::setBufferIcyMetaInt(int icyMetaInt) {
    mIcyMetaInt = icyMetaInt;
    if (mWrapper) {
        if (mWrapper->detectedType == DetectedType::BUFFER_MP3_STREAM || mWrapper->detectedType == DetectedType::BUFFER_MP3_WITH_ID3) {
            static_cast<MP3DecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
        } else if (mWrapper->detectedType == DetectedType::BUFFER_AAC) {
            static_cast<AACDecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
        }
#if !defined(NO_XIPH_LIBS)
        else if (mWrapper->detectedType == DetectedType::BUFFER_FLAC) {
            static_cast<FlacDecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
        } else if (mWrapper->detectedType == DetectedType::BUFFER_OGG_FLAC) {
            static_cast<OggFlacDecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
        }
#endif
    }
}

// Helper to get the size of an ID3v2 tag. Returns 0 if not an ID3v2 tag.
static size_t getID3TagSize(const std::vector<unsigned char>& buffer) {
    if (buffer.size() >= 10 && memcmp(buffer.data(), "ID3", 3) == 0) {
        // It's an ID3v2 tag. The size is a 28-bit integer, stored as 4x7-bit bytes (synchsafe).
        uint32_t size = ((buffer[6] & 0x7f) << 21) |
                       ((buffer[7] & 0x7f) << 14) |
                       ((buffer[8] & 0x7f) << 7) |
                       (buffer[9] & 0x7f);
        return size + 10; // +10 for the header.
    }
    return 0;
}

DetectedType StreamDecoder::detectAudioFormat(const std::vector<unsigned char>& buffer) {
    size_t size = buffer.size();
    if (size < 4) return DetectedType::BUFFER_NO_ENOUGH_DATA;
    
    // --- Detect OGG, OPUS and VORBIS containers ---
    else if (size >= 4 && std::memcmp(buffer.data(), "OggS", 4) == 0) {
        // Scan through multiple Ogg pages to find the format
        size_t scan_offset = 0;
        
        while (scan_offset + 27 < size) {  // 27 is minimum Ogg page header size
            // Check for Ogg page signature
            if (std::memcmp(buffer.data() + scan_offset, "OggS", 4) != 0) {
                scan_offset++;
                continue;
            }
            
            // Get segment count for this page
            uint8_t seg_count = buffer[scan_offset + 26];
            size_t page_offset = scan_offset + 27;  // Start of segment table
            
            // Check if we have enough data to read segment table
            if (page_offset + seg_count >= size) {
                return DetectedType::BUFFER_NO_ENOUGH_DATA;
            }
            
            // Calculate payload size for this page
            size_t payload_size = 0;
            for (int i = 0; i < seg_count; i++) {
                payload_size += buffer[page_offset + i];
            }
            
            // Move to payload start
            size_t payload_offset = page_offset + seg_count;
            
            // Check if we have enough data for the payload
            if (payload_offset + payload_size > size) {
                return DetectedType::BUFFER_NO_ENOUGH_DATA;
            }
            
            // Check for Opus in this page
            if (payload_size >= 8 && 
                std::memcmp(buffer.data() + payload_offset, "OpusHead", 8) == 0) {
                return DetectedType::BUFFER_OGG_OPUS;
            }

            // Check for FLAC in this page
            if (payload_size >= 13 &&
                buffer[payload_offset] == 0x7F &&
                std::memcmp(buffer.data() + payload_offset + 1, "FLAC", 4) == 0 &&
                std::memcmp(buffer.data() + payload_offset + 9, "fLaC", 4) == 0) {
                return DetectedType::BUFFER_OGG_FLAC;
            }
            
            // Check for Vorbis in this page
            // Look through the entire payload for the Vorbis pattern.
            // The loop bound stays unsigned-safe for payloads shorter than
            // the 7-byte pattern.
            for (size_t i = 0; i + 7 <= payload_size; i++) {
                if (buffer[payload_offset + i] == 0x01 && 
                    std::memcmp(buffer.data() + payload_offset + i + 1, "vorbis", 6) == 0) {
                    return DetectedType::BUFFER_OGG_VORBIS;
                }
            }
            
            // Move to next potential page
            scan_offset = payload_offset + payload_size;
        }
        
        // If we've checked all available data and found nothing, consider it unknown
        return DetectedType::BUFFER_UNKNOWN;
    }

    // --- Detect FLAC ---
    else if (size >= 4 && std::memcmp(buffer.data(), "fLaC", 4) == 0) {
        return DetectedType::BUFFER_FLAC;
    }
    
    // --- Detect WAV ---
    else if (WavDecoderWrapper::checkForValidWavHeader(buffer)) {
        return DetectedType::BUFFER_WAV;
    }

    // --- Detect ID3 (MP3 or AAC) ---
    else if (size >= 3 && getID3TagSize(buffer) != 0) {
        size_t id3Size = getID3TagSize(buffer);
        if (size < id3Size + 4) {
            return DetectedType::BUFFER_NO_ENOUGH_DATA;
        }
        const unsigned char *payload = buffer.data() + id3Size;
        size_t payloadSize = size - id3Size;
        if (NativeAudioDecoder::isAacAdts(payload, payloadSize) ||
            AACDecoderWrapper::checkForValidFrames(std::vector<unsigned char>(payload, payload + payloadSize))) {
            return DetectedType::BUFFER_AAC;
        }
        return DetectedType::BUFFER_MP3_WITH_ID3; // ID3 tag found
    } 
    else if (MP3DecoderWrapper::checkForValidFrames(buffer)) {
        return DetectedType::BUFFER_MP3_STREAM;
    }

    // --- Detect M4A / MP4 ---
    else if (NativeAudioDecoder::isM4aOrMp4(buffer.data(), size)) {
        return DetectedType::BUFFER_M4A;
    }

    // --- Detect AAC ADTS ---
    else if (NativeAudioDecoder::isAacAdts(buffer.data(), size) ||
             AACDecoderWrapper::checkForValidFrames(buffer)) {
        return DetectedType::BUFFER_AAC;
    }

    // --- Detect AC-3 / E-AC-3 ---
    else if (NativeAudioDecoder::isAc3OrEac3(buffer.data(), size) ||
             AACDecoderWrapper::checkForValidAc3Frames(buffer)) {
        int syncIdx = NativeAudioDecoder::findAc3Syncword(buffer.data(), size);
        if (syncIdx >= 0) {
            if (NativeAudioDecoder::isEac3(buffer.data() + syncIdx, size - syncIdx)) {
                return DetectedType::BUFFER_EAC3;
            }
            return DetectedType::BUFFER_AC3;
        }
        if (NativeAudioDecoder::isEac3(buffer.data(), size)) {
            return DetectedType::BUFFER_EAC3;
        }
        return DetectedType::BUFFER_AC3;
    }

    return DetectedType::BUFFER_UNKNOWN;
}

std::pair<std::vector<float>, DecoderError> StreamDecoder::decode(
    std::vector<unsigned char>& buffer,
    int* samplerate,
    int* channels,
    TrackChangeCallback metadataChangeCallback,
    size_t maxOutputSamples)
{

    if (!isFormatDetected) {
        DetectedType detectedType = detectAudioFormat(buffer);
        if (detectedType == DetectedType::BUFFER_NO_ENOUGH_DATA)
            return {{}, DecoderError::NoError};

        if (detectedType == DetectedType::BUFFER_UNKNOWN) {
            return {{}, DecoderError::FormatNotSupported};
        }
        
        if (detectedType == DetectedType::BUFFER_OGG_OPUS
            || detectedType == DetectedType::BUFFER_OGG_VORBIS
            || detectedType == DetectedType::BUFFER_OGG_FLAC
            || detectedType == DetectedType::BUFFER_FLAC) {
            #if defined(NO_XIPH_LIBS)
                return {{}, DecoderError::NoXiphLibs};
            #else
                if (detectedType == DetectedType::BUFFER_OGG_VORBIS) {
                    mWrapper = std::make_unique<VorbisDecoderWrapper>();
                    isFormatDetected = static_cast<VorbisDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
                    if (!isFormatDetected) {
                        return {{}, DecoderError::FailedToCreateDecoder};
                    }
                } else if (detectedType == DetectedType::BUFFER_OGG_FLAC) {
                    mWrapper = std::make_unique<OggFlacDecoderWrapper>();
                    isFormatDetected = static_cast<OggFlacDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
                    if (!isFormatDetected) {
                        return {{}, DecoderError::FailedToCreateDecoder};
                    }
                    static_cast<OggFlacDecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
                } else if (detectedType == DetectedType::BUFFER_FLAC) {
                    mWrapper = std::make_unique<FlacDecoderWrapper>();
                    isFormatDetected = static_cast<FlacDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
                    if (!isFormatDetected) {
                        return {{}, DecoderError::FailedToCreateDecoder};
                    }
                    static_cast<FlacDecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
                } else {
                    mWrapper = std::make_unique<OpusDecoderWrapper>();
                    isFormatDetected = static_cast<OpusDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
                    if (!isFormatDetected) {
                        return {{}, DecoderError::FailedToCreateDecoder};
                    }
                }
            #endif
        } else if (detectedType == DetectedType::BUFFER_MP3_WITH_ID3 || detectedType == DetectedType::BUFFER_MP3_STREAM) {
            mWrapper = std::make_unique<MP3DecoderWrapper>();
            isFormatDetected = static_cast<MP3DecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
            if (!isFormatDetected) {
                return {{}, DecoderError::FailedToCreateDecoder};
            }
            static_cast<MP3DecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
        } else if (detectedType == DetectedType::BUFFER_WAV) {
            mWrapper = std::make_unique<WavDecoderWrapper>();
            isFormatDetected = static_cast<WavDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
            if (!isFormatDetected) {
                return {{}, DecoderError::FailedToCreateDecoder};
            }
        } else if (detectedType == DetectedType::BUFFER_AAC) {
            mWrapper = std::make_unique<AACDecoderWrapper>(DetectedType::BUFFER_AAC);
            isFormatDetected = static_cast<AACDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
            if (!isFormatDetected) {
                fprintf(stderr, "[flutter_soloud] Failed to initialize AAC stream decoder (not supported or invalid format).\n");
                return {{}, DecoderError::FormatNotSupported};
            }
            static_cast<AACDecoderWrapper*>(mWrapper.get())->setIcyMetaInt(mIcyMetaInt);
        } else if (detectedType == DetectedType::BUFFER_AC3 || detectedType == DetectedType::BUFFER_EAC3) {
            mWrapper = std::make_unique<AACDecoderWrapper>(detectedType);
            isFormatDetected = static_cast<AACDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
            if (!isFormatDetected) {
                fprintf(stderr, "[flutter_soloud] Failed to initialize %s stream decoder (format not supported on this device).\n",
                        detectedType == DetectedType::BUFFER_AC3 ? "AC-3" : "E-AC-3");
                return {{}, DecoderError::FormatNotSupported};
            }
        }

        // Safety guard: ensure wrapper exists and format was initialized
        if (!mWrapper || !isFormatDetected) {
            return {{}, DecoderError::FormatNotSupported};
        }

        if (metadataChangeCallback) {
            mWrapper->setTrackChangeCallback(metadataChangeCallback);
        }
        mWrapper->detectedType = detectedType;
        mWrapper->setTotalAudioSizeBytes(mTotalAudioSizeBytes);

        // Forward deferred end-of-stream signal. When total data is below the
        // 32 KB addData threshold, setDataEnded() is called before the wrapper
        // exists and the signal is stored in mDataEndedPending. Deliver it now.
        if (mDataEndedPending) {
            mWrapper->setDataEnded();
        }
    }
    
    if (mWrapper) {
        return mWrapper->decode(buffer, samplerate, channels, maxOutputSamples);
    }
    return {};
}

DetectedType StreamDecoder::getWrapperType()
{
    if (mWrapper)
        return mWrapper->detectedType;
    return DetectedType::BUFFER_UNKNOWN;
}

bool StreamDecoder::canSeekToTime(double seconds) const
{
    if (mWrapper)
        return mWrapper->canSeekToTime(seconds);
    return false;
}

uint64_t StreamDecoder::timeToByteOffset(double seconds)
{
    if (mWrapper)
        return mWrapper->timeToByteOffset(seconds);
    return 0;
}

double StreamDecoder::getDuration() const
{
    if (mWrapper)
        return mWrapper->getDuration();
    return -1.0;
}

int StreamDecoder::preSkip() const
{
    if (mWrapper)
        return mWrapper->preSkip();
    return 0;
}

int StreamDecoder::granuleSampleRate() const
{
    if (mWrapper)
        return mWrapper->granuleSampleRate();
    return 0;
}

void StreamDecoder::prepareForSeek(uint64_t targetSample)
{
    if (mWrapper)
        mWrapper->prepareForSeek(targetSample);
}
