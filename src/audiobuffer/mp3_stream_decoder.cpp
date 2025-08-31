// TODO: don't know why, bu on iOS and MacOS these defines should be put in the header!
#include "../common.h"
#if !defined(__APPLE__)
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_ONLY_MP3
#if !defined(__EMSCRIPTEN__)
#define MINIMP3_ONLY_SIMD
#endif
#define MINIMP3_IMPLEMENTATION
#endif
#include "minimp3.h"

#include "mp3_stream_decoder.h"

MP3DecoderWrapper::MP3DecoderWrapper()
    : bytes_until_meta(16000), // most common value
    metadata_remaining(0),
    metadata_buffer(""),
    ID3TagsFound(false)
{	
}

MP3DecoderWrapper::~MP3DecoderWrapper()
{
}

void MP3DecoderWrapper::cleanup()
{
}

void MP3DecoderWrapper::setMp3BufferIcyMetaInt(int icyMetaInt)
{
    if (mIcyMetaInt == icyMetaInt) return;

    mIcyMetaInt = icyMetaInt;
    bytes_until_meta = mIcyMetaInt;
}

bool MP3DecoderWrapper::extractID3Tags(const std::vector<unsigned char>& buffer, AudioMetadata& metadata) {
    // Look for ID3v2 tag. ID3v1 is not supported (metadata is at the end of the file)
    if (buffer.size() > 10 && memcmp(buffer.data(), "ID3", 3) == 0) {
        size_t pos = 10;  // Skip ID3v2 header
        uint32_t size = ((buffer[6] & 0x7f) << 21) |
                       ((buffer[7] & 0x7f) << 14) |
                       ((buffer[8] & 0x7f) << 7) |
                       (buffer[9] & 0x7f);
        
        while (pos < size + 10 && pos + 10 < buffer.size()) {
            char frame_id[5] = {0};
            memcpy(frame_id, buffer.data() + pos, 4);
            uint32_t frame_size = ((buffer[pos+4] & 0x7f) << 21) |
                                ((buffer[pos+5] & 0x7f) << 14) |
                                ((buffer[pos+6] & 0x7f) << 7) |
                                (buffer[pos+7] & 0x7f);
            
            pos += 10;  // Skip frame header
            if (pos + frame_size > buffer.size())
                break;

            // Skip text encoding byte for text frames
            size_t text_start = (frame_id[0] == 'T') ? 1 : 0;
            std::string value(reinterpret_cast<const char *>(buffer.data() + pos + text_start), frame_size - text_start);
            if (frame_id[0] != 0)
                printf("ID3 tag: %s = %s\n", frame_id, value.c_str());

            if (strcmp(frame_id, "TIT2") == 0) metadata.mp3Metadata.title = value;
            else if (strcmp(frame_id, "TPE1") == 0) metadata.mp3Metadata.artist = value;
            else if (strcmp(frame_id, "TALB") == 0) metadata.mp3Metadata.album = value;
            else if (strcmp(frame_id, "TYER") == 0) metadata.mp3Metadata.date = value;
            else if (strcmp(frame_id, "TCON") == 0) metadata.mp3Metadata.genre = value;
            
            pos += frame_size;
        }
        metadata.type = BUFFER_MP3_WITH_ID3;
        return true;
    }
    return false; 
}



int corruptedFrames = 0;
std::pair<std::vector<float>, DecoderError> MP3DecoderWrapper::decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels)
{
    if (buffer.empty())
        return {{}, DecoderError::NoError};

    std::vector<float> decodedData;
    int bytes_left = buffer.size();
    const uint8_t *mp3_ptr = buffer.data();
    *samplerate = -1;
    *channels = -1;

    // TODO: Wait at least 2 valid mp3 frames to start decoding if we haven't found them yet
    

    // With MP3 with ID3 TAG, the metadata is extracted with the extractID3Tags function
    if (detectedType == DetectedType::BUFFER_MP3_WITH_ID3 && !ID3TagsFound) {
        // Check for new metadata
        AudioMetadata newMetadata;
        if (extractID3Tags(buffer, newMetadata)) {
            ID3TagsFound = true;
            if (onTrackChange) onTrackChange(newMetadata);
        }
    }

    // TODO: with MP3 stream, the metadata is stored inside the stream every [mIcyMetaInt] bytes.
    // Here [audioBuffer] is the original [buffer] stripped of the metadata and ready
    // to be decoded by minimp3.
    // If a frame has been truncated at the end of [buffer], don't include it
    // in the [audioBuffer]. In the [audioBuffer] there should be only valid mp3 frames.
    // The [buffer] should be cleared of decoded frames and metadata.
    // Maybe it is needed a new method to manage the metadata. [bytes_until_meta] is initialized to [mIcyMetaInt] and while frames are decoded, it is decremented.
    // When it reaches zero, the next byte indicates the length of the metadata block,
    // and it must multiplied by 16 to know the size of the metadata. At the end of the metadata block a 
    // new mp3 frame should start and [bytes_until_meta] should be reset to [mIcyMetaInt].


    // TODO: all the audio data has been decoded. Maybe a frame has been truncated at the end and 
    // we need to wait for more data to decode it. So leave it in the [buffer], and all decoded frames
    // are erased from the [audioBuffer] and from [buffer].

    
    return {decodedData, DecoderError::NoError};
}

bool MP3DecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    cleanup(); // Ensure clean state
    mp3dec_init(&decoder);
    return true;
}

bool MP3DecoderWrapper::checkForValidFrames(const std::vector<unsigned char>& buffer)
{
    int bytes_left = buffer.size();
    const uint8_t *mp3_ptr = buffer.data();
    int validFrames = 0;
    mp3dec_t decoder;

    mp3dec_init(&decoder);
    if (bytes_left > 0) {
        const uint8_t *frame_ptr = mp3_ptr;
        int remaining = bytes_left;

         while (remaining >= 4 && validFrames < 2) {  // Need at least 4 bytes for header check
            if (hdr_valid(frame_ptr))
            {
                validFrames++;
                // Skip to next possible frame using current frame's length
                mp3dec_frame_info_t temp_info;
                mp3d_sample_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
                mp3dec_decode_frame(&decoder, frame_ptr, remaining, temp_pcm, &temp_info);
                if (temp_info.frame_bytes <= 0) break;
                frame_ptr += temp_info.frame_bytes;
                remaining -= temp_info.frame_bytes;
            }
            else
            {
                frame_ptr++;
                remaining--;
            }
        }
    }
    return validFrames >= 2;
}
