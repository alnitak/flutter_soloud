#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_FLOAT_OUTPUT
#include "../soloud/src/audiosource/wav/dr_mp3.h"

#include "mp3_stream_decoder.h"
#include "../common.h"
#include <algorithm>

MP3DecoderWrapper::MP3DecoderWrapper()
    : cachedSampleRate(0),
      cachedChannels(0),
      mIcyMetaInt(0),
      bytes_until_meta(16000),
      lastMetadata(""),
      ID3TagsProcessed(false)
{
    drmp3dec_init(&mp3dec);
}

MP3DecoderWrapper::~MP3DecoderWrapper()
{
    // drmp3dec doesn't need explicit cleanup
}

bool MP3DecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    // Initialization is done lazily in decode()
    return true;
}

void MP3DecoderWrapper::setIcyMetaInt(int icyMetaInt)
{
    if (mIcyMetaInt == icyMetaInt)
        return;

    mIcyMetaInt = icyMetaInt;
    bytes_until_meta = mIcyMetaInt;
}

void MP3DecoderWrapper::processIcyStream(std::vector<unsigned char>& buffer)
{
    size_t bufferSize = buffer.size();
    size_t readingPos = 0;

    while (readingPos < bufferSize)
    {
        size_t bytes_to_read = MIN((size_t)bytes_until_meta, bufferSize - readingPos);

        // Append audio data to our internal buffer
        inputBuffer.insert(inputBuffer.end(),
                          buffer.begin() + readingPos,
                          buffer.begin() + readingPos + bytes_to_read);
        readingPos += bytes_to_read;
        bytes_until_meta -= bytes_to_read;

        if (bytes_until_meta == 0 && readingPos < bufferSize)
        {
            // Time for metadata
            int len_byte = buffer[readingPos];
            int metadata_len = len_byte * 16;
            readingPos++; // Skip metadata length byte

            if (readingPos + metadata_len <= bufferSize)
            {
                if (len_byte > 0)
                {
                    // Extract and process metadata
                    std::string title(reinterpret_cast<const char*>(buffer.data() + readingPos), metadata_len);
                    if (lastMetadata != title)
                    {
                        AudioMetadata metadata;
                        metadata.type = DetectedType::BUFFER_MP3_STREAM;
                        metadata.mp3Metadata.title = title;
                        lastMetadata = title;
                        if (onTrackChange)
                            onTrackChange(metadata);
                    }
                }
                readingPos += metadata_len;
                bytes_until_meta = mIcyMetaInt;
            }
            else
            {
                // Not enough data for the full metadata block.
                readingPos--; // Rewind to include the metadata length byte for the next round.
                break;
            }
        }
    }

    // Remove the processed part from the input buffer
    buffer.erase(buffer.begin(), buffer.begin() + readingPos);
}

void MP3DecoderWrapper::parseID3v2Tags(const unsigned char* rawData, size_t totalTagSize)
{
    if (ID3TagsProcessed || onTrackChange == nullptr || totalTagSize < 20) {
        return;
    }

    AudioMetadata metadata;
    metadata.type = DetectedType::BUFFER_MP3_WITH_ID3;

    size_t pos = 10; // Skip ID3v2 header

    while (pos + 10 < totalTagSize) {
        char frame_id[5] = {0};
        memcpy(frame_id, rawData + pos, 4);

        if (frame_id[0] == 0) break; // Padding or end of tags

        // Frame size (synchsafe integer in ID3v2.3/4)
        uint32_t frame_size = ((rawData[pos + 4] & 0x7f) << 21) |
                              ((rawData[pos + 5] & 0x7f) << 14) |
                              ((rawData[pos + 6] & 0x7f) << 7) |
                              (rawData[pos + 7] & 0x7f);

        pos += 10; // Move to frame content

        if (pos + frame_size > totalTagSize) break; // Malformed tag

        if (frame_id[0] == 'T') { // Common text frames
            size_t text_start = pos + 1; // Skip encoding byte
            if (text_start < pos + frame_size) {
                std::string value(reinterpret_cast<const char*>(rawData + text_start), frame_size - 1);

                if (strcmp(frame_id, "TIT2") == 0)      metadata.mp3Metadata.title = value;
                else if (strcmp(frame_id, "TPE1") == 0) metadata.mp3Metadata.artist = value;
                else if (strcmp(frame_id, "TALB") == 0) metadata.mp3Metadata.album = value;
                else if (strcmp(frame_id, "TYER") == 0) metadata.mp3Metadata.date = value;
                else if (strcmp(frame_id, "TCON") == 0) metadata.mp3Metadata.genre = value;
            }
        }

        pos += frame_size;
    }

    onTrackChange(metadata);
    ID3TagsProcessed = true;
}

std::pair<std::vector<float>, DecoderError> MP3DecoderWrapper::decode(
    std::vector<unsigned char>& buffer,
    int* samplerate,
    int* channels)
{
    // For ICY streams, process the buffer to strip metadata first
    if (detectedType == DetectedType::BUFFER_MP3_STREAM && mIcyMetaInt > 0) {
        processIcyStream(buffer);
    } else if (!buffer.empty()) {
        // For non-ICY streams, just append to input buffer
        inputBuffer.insert(inputBuffer.end(), buffer.begin(), buffer.end());
        buffer.clear();
    }

    if (inputBuffer.empty()) {
        return {{}, DecoderError::NoError};
    }

    // --- Handle ID3v2 tags ---
    if (!ID3TagsProcessed && inputBuffer.size() >= 10) {
        if (memcmp(inputBuffer.data(), "ID3", 3) == 0) {
            uint32_t tagSize = ((inputBuffer[6] & 0x7f) << 21) |
                               ((inputBuffer[7] & 0x7f) << 14) |
                               ((inputBuffer[8] & 0x7f) << 7)  |
                               (inputBuffer[9] & 0x7f);
            uint32_t totalTagLength = tagSize + 10;

            // Wait for complete ID3 tag
            if (inputBuffer.size() < totalTagLength) {
                return {{}, DecoderError::NoError};
            }

            // Parse and trigger metadata callback
            parseID3v2Tags(inputBuffer.data(), totalTagLength);

            // Remove ID3 tag from buffer
            inputBuffer.erase(inputBuffer.begin(), inputBuffer.begin() + totalTagLength);
        }
        ID3TagsProcessed = true;
    }

    // --- Decode frames using minimp3 low-level API ---
    std::vector<float> output;

    while (inputBuffer.size() >= 4) {
        // Find MP3 sync word (0xFF followed by 0xE0+)
        size_t syncPos = 0;
        while (syncPos + 1 < inputBuffer.size()) {
            if (inputBuffer[syncPos] == 0xFF &&
                (inputBuffer[syncPos + 1] & 0xE0) == 0xE0) {
                break;
            }
            syncPos++;
        }

        if (syncPos > 0) {
            inputBuffer.erase(inputBuffer.begin(), inputBuffer.begin() + syncPos);
            if (inputBuffer.size() < 4) break;
        }

        // Parse MP3 header to calculate frame size BEFORE calling decode
        // This is critical: calling decode with incomplete data corrupts
        // the decoder's bit reservoir state, causing subsequent decode failures.
        unsigned char h0 = inputBuffer[0];
        unsigned char h1 = inputBuffer[1];
        unsigned char h2 = inputBuffer[2];

        // Validate sync word
        if (h0 != 0xFF || (h1 & 0xE0) != 0xE0) {
            inputBuffer.erase(inputBuffer.begin());
            continue;
        }

        int version = (h1 >> 3) & 0x03;  // 00=2.5, 01=reserved, 10=2, 11=1
        int layer = (h1 >> 1) & 0x03;    // 00=reserved, 01=III, 10=II, 11=I
        int bitrateIdx = (h2 >> 4) & 0x0F;
        int srIdx = (h2 >> 2) & 0x03;
        int padding = (h2 >> 1) & 0x01;

        // Validate header fields
        if (version == 1 || layer == 0 || bitrateIdx == 0 || bitrateIdx == 15 || srIdx == 3) {
            inputBuffer.erase(inputBuffer.begin());
            continue;
        }

        // Bitrate table (kbps) for MPEG1/2/2.5 Layer I/II/III
        static const int kBitrates[4][4][16] = {
            {{0},{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},{0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0}},
            {{0},{0},{0},{0}},
            {{0},{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0},{0,32,48,56,64,80,96,112,128,144,160,176,192,224,256,0}},
            {{0},{0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0},{0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0},{0,32,64,96,128,160,192,224,256,288,320,352,384,416,448,0}}
        };
        static const int kSampleRates[4][4] = {
            {11025,12000,8000,0}, {0,0,0,0}, {22050,24000,16000,0}, {44100,48000,32000,0}
        };

        int bitrate = kBitrates[version][layer][bitrateIdx] * 1000;
        int sampleRate = kSampleRates[version][srIdx];
        if (bitrate == 0 || sampleRate == 0) {
            inputBuffer.erase(inputBuffer.begin());
            continue;
        }

        // Calculate frame size
        int frameSize = (layer == 3)
            ? (12 * bitrate / sampleRate + padding) * 4  // Layer I
            : ((version == 3 ? 144 : 72) * bitrate / sampleRate + padding);  // Layer II/III

        // Wait for complete frame before decoding
        if ((size_t)frameSize > inputBuffer.size()) {
            break;
        }

        drmp3dec_frame_info info;
        float pcm[DRMP3_MAX_SAMPLES_PER_FRAME * 2];

        int samples = drmp3dec_decode_frame(
            &mp3dec,
            inputBuffer.data(),
            static_cast<int>(inputBuffer.size()),
            pcm,
            &info
        );

        if (info.frame_bytes == 0) {
            inputBuffer.erase(inputBuffer.begin());
            continue;
        }

        if (samples > 0) {
            cachedSampleRate = info.sample_rate;
            cachedChannels = info.channels;
            output.insert(output.end(), pcm, pcm + samples * info.channels);
        }

        inputBuffer.erase(inputBuffer.begin(), inputBuffer.begin() + info.frame_bytes);
    }

    *samplerate = cachedSampleRate;
    *channels = cachedChannels;

    return {output, DecoderError::NoError};
}

bool MP3DecoderWrapper::checkForValidFrames(const std::vector<unsigned char>& buffer)
{
    if (buffer.size() < 4) {
        return false;
    }

    // Use a temporary drmp3 to validate (this is only for format detection)
    drmp3 temp_decoder;
    struct TempData {
        const unsigned char* buffer;
        size_t size;
        size_t pos;
    };
    TempData temp_data = { buffer.data(), buffer.size(), 0 };

    auto temp_on_read = [](void* pUserData, void* pBufferOut, size_t bytesToRead) -> size_t {
        TempData* data = (TempData*)pUserData;
        size_t bytes_remaining = data->size - data->pos;
        size_t bytes_to_copy = MIN(bytesToRead, bytes_remaining);
        if (bytes_to_copy > 0) {
            memcpy(pBufferOut, data->buffer + data->pos, bytes_to_copy);
            data->pos += bytes_to_copy;
        }
        return bytes_to_copy;
    };

    if (!drmp3_init(&temp_decoder, temp_on_read, nullptr, nullptr, nullptr, &temp_data, nullptr)) {
        return false;
    }

    // Try to read just one frame to confirm validity
    drmp3_uint64 frames_read = drmp3_read_pcm_frames_f32(&temp_decoder, 1, nullptr);

    drmp3_uninit(&temp_decoder);

    return frames_read > 0;
}
