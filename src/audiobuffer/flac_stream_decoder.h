#ifndef FLAC_STREAM_DECODER_H
#define FLAC_STREAM_DECODER_H

#include "stream_decoder.h"
#include <FLAC/stream_decoder.h>
#include <ogg/ogg.h>
#include <vector>
#include <string>

class FlacDecoderWrapper : public IDecoderWrapper {
public:
    FlacDecoderWrapper();
    ~FlacDecoderWrapper() override;

    // call this only once before decoding
    void setIcyMetaInt(int icyMetaInt);

    bool initializeDecoder(int engineSamplerate, int engineChannels) override;
    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* sampleRate, int* channels, size_t maxOutputSamples = 0) override;
    void setDataEnded() override { m_dataEnded = true; }

    bool canSeekToTime(double seconds) const override;
    uint64_t timeToByteOffset(double seconds) override;
    double getDuration() const override;

private:
    static FLAC__StreamDecoderReadStatus read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
    static FLAC__StreamDecoderTellStatus tell_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
    static void metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
    static void error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);
    static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data);

    void getMetadata();

    FLAC__StreamDecoder *m_pFlacDecoder;
    bool m_streamInfoProcessed;
    
    std::vector<unsigned char> m_audioData;
    size_t m_read_pos;

    ogg_sync_state m_oy;
    ogg_stream_state m_os;
    bool m_streamInitialized;
    bool m_dataEnded;
    uint64_t m_streamStartOffset;

    std::vector<float> m_decodedPcm;
    int m_channels;
    int m_samplerate;
    int m_bitsPerSample;

    uint64_t mTotalSamples = 0;
    uint64_t mTotalEncodedBytes = 0;

    AudioMetadata m_metadata;

    // ICY Metadata state
    int mIcyMetaInt;
    int mAudioBytesCount;
    int mIcyMetaSize;
    std::vector<unsigned char> mIcyMetadata;
    std::string mStreamTitle;
};

#endif // FLAC_STREAM_DECODER_H
