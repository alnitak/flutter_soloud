#include "stream_decoder.h"
#include "mp3_stream_decoder.h"
#if !defined(NO_OPUS_OGG_LIBS)
#   include "opus_stream_decoder.h"
#   include "vorbis_stream_decoder.h"
#endif
#include <cstring>

DetectedType StreamDecoder::detectAudioFormat(const std::vector<unsigned char>& buffer) {
    size_t size = buffer.size();
    if (size < 4) return DetectedType::BUFFER_NO_ENOUGH_DATA;
    
    // --- Detect OGG, OPUS and VORBIS containers ---
    else if (size >= 4 && std::memcmp(buffer.data(), "OggS", 4) == 0) {
        if (size < 27) return DetectedType::BUFFER_NO_ENOUGH_DATA; // need at least segment count
        uint8_t seg_count = buffer[26];
        if (size < 27 + seg_count) return DetectedType::BUFFER_NO_ENOUGH_DATA; // need full segment table
        
        size_t payload_offset = 27 + seg_count;
        if (size < payload_offset + 7) return DetectedType::BUFFER_NO_ENOUGH_DATA; // need enough for "OpusHead"/"vorbis"
        
        if (std::memcmp(buffer.data() + payload_offset, "OpusHead", 8) == 0) {
            return DetectedType::BUFFER_OGG_OPUS;
        }
        if (std::memcmp(buffer.data() + payload_offset, "\x01vorbis", 7) == 0) {
            return DetectedType::BUFFER_OGG_VORBIS;
        }
        return DetectedType::BUFFER_UNKNOWN;
    }

    // --- Detect MP3 ---
    else if (size >= 3 && std::memcmp(buffer.data(), "ID3", 3) == 0 ||
    MP3DecoderWrapper::checkForValidFrames(buffer)) {
        return DetectedType::BUFFER_MP3; // ID3 tag found
    }

    return DetectedType::BUFFER_UNKNOWN;
}

std::pair<std::vector<float>, DecoderError> StreamDecoder::decode(std::vector<unsigned char>& buffer,
    int* samplerate,
    int* channels,
    TrackChangeCallback metadataChangeCallback)
{
    if (!isFormatDetected) {
        detectedType = detectAudioFormat(buffer);
        if (detectedType == DetectedType::BUFFER_NO_ENOUGH_DATA)
            return {{}, DecoderError::NoError};

        if (detectedType == DetectedType::BUFFER_UNKNOWN) {
            return {{}, DecoderError::FormatNotSupported};
        }
        
        if (detectedType == DetectedType::BUFFER_OGG_OPUS
            || detectedType == DetectedType::BUFFER_OGG_VORBIS) {
            #if defined(NO_OPUS_OGG_LIBS)
                return {{}, DecoderError::NoOpusOggLibs};
            #else
                if (detectedType == DetectedType::BUFFER_OGG_VORBIS) {
                    mWrapper = std::make_unique<VorbisDecoderWrapper>();
                    isFormatDetected = static_cast<VorbisDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
                } else {
                    mWrapper = std::make_unique<OpusDecoderWrapper>();
                    isFormatDetected = static_cast<OpusDecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
                    if (!isFormatDetected)
                        return {{}, DecoderError::FailedToCreateDecoder};
                }
            #endif
        } else if (detectedType == DetectedType::BUFFER_MP3) {
            mWrapper = std::make_unique<MP3DecoderWrapper>();
            isFormatDetected = static_cast<MP3DecoderWrapper*>(mWrapper.get())->initializeDecoder(*samplerate, *channels);
        }
        if (metadataChangeCallback) {
            mWrapper->setTrackChangeCallback(metadataChangeCallback);
        }
    }
    
    if (mWrapper) {
        #if !defined(NO_OPUS_OGG_LIBS)
            if (detectedType == DetectedType::BUFFER_OGG_OPUS) {
                return static_cast<OpusDecoderWrapper*>(mWrapper.get())->decode(buffer, samplerate, channels);
            }
            else if (detectedType == DetectedType::BUFFER_OGG_VORBIS) {
                return static_cast<VorbisDecoderWrapper*>(mWrapper.get())->decode(buffer, samplerate, channels);
            }
        #endif
        if (detectedType == DetectedType::BUFFER_MP3) {
            return static_cast<MP3DecoderWrapper*>(mWrapper.get())->decode(buffer, samplerate, channels);
        }
    }
    return {};
}
