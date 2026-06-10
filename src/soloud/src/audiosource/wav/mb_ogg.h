#ifndef MB_OGG_H
#define MB_OGG_H

#include "soloud_file.h"

#if !defined(NO_XIPH_LIBS)

#include <vector>
#include <cstring>
#include <algorithm>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <opus/opus.h>
#include <FLAC/stream_decoder.h>

namespace SoLoud
{
    class MBOggDecoder
    {
    public:
        enum CodecType
        {
            CODEC_UNKNOWN = -1,
            CODEC_VORBIS = 0,
            CODEC_OPUS = 1,
            CODEC_FLAC = 2
        };

        MBOggDecoder();
        ~MBOggDecoder();

        bool open(File *aFile);
        bool open(const unsigned char *aData, unsigned int aLength);
        void close();

        bool isValid() const;
        CodecType getCodecType() const;
        int getChannels() const;
        int getSampleRate() const;
        unsigned int getLengthInSamples() const;

        unsigned int read(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize);
        bool seek(unsigned int aSample);
        bool rewind();

    private:
        struct DataSource
        {
            File *file;
            const unsigned char *mem;
            unsigned int memLen;
            unsigned int memPos;

            DataSource() : file(nullptr), mem(nullptr), memLen(0), memPos(0) {}

            unsigned int read(unsigned char *dst, unsigned int bytes);
            bool seek(int offset);
            unsigned int tell() const;
            unsigned int length() const;
            bool eof() const;
        };

        bool detectCodec();
        bool initVorbis();
        bool initOpus();
        bool initFlac();
        bool rewindOpus();

        unsigned int readVorbis(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize);
        unsigned int readOpus(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize);
        unsigned int readFlac(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize);

        bool seekOpus(unsigned int aSample);

        bool feedOpusData();
        bool decodeOpusPacket(ogg_packet *packet);

        static size_t vorbisReadCb(void *ptr, size_t size, size_t nmemb, void *datasource);
        static int vorbisSeekCb(void *datasource, ogg_int64_t offset, int whence);
        static int vorbisCloseCb(void *datasource);
        static long vorbisTellCb(void *datasource);

        static FLAC__StreamDecoderReadStatus flacReadCb(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data);
        static FLAC__StreamDecoderSeekStatus flacSeekCb(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data);
        static FLAC__StreamDecoderTellStatus flacTellCb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data);
        static FLAC__StreamDecoderLengthStatus flacLengthCb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data);
        static FLAC__bool flacEofCb(const FLAC__StreamDecoder *decoder, void *client_data);
        static FLAC__StreamDecoderWriteStatus flacWriteCb(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data);
        static void flacMetadataCb(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);
        static void flacErrorCb(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

        DataSource mDataSource;
        CodecType mCodecType;
        int mChannels;
        int mSampleRate;
        unsigned int mLengthInSamples;
        bool mValid;

        // Vorbis
        OggVorbis_File mVorbisFile;
        bool mVorbisOpen;

        // Opus
        OpusDecoder *mOpusDecoder;
        ogg_sync_state mOpusOy;
        ogg_stream_state mOpusOs;
        bool mOpusStreamInit;
        bool mOpusHeaderParsed;
        int mOpusPacketCount;
        int mOpusSkipSamples;
        int64_t mOpusTotalOutputSamples;
        int64_t mOpusTotalSamplesExpected;
        int mOpusMaxFrameSize;
        std::vector<float> mOpusOutputBuffer;
        std::vector<float> mOpusPcmBuffer;
        size_t mOpusPcmReadPos;
        int mOpusPreSkip;

        // FLAC
        FLAC__StreamDecoder *mFlacDecoder;
        std::vector<float> mFlacPcmBuffer;
        size_t mFlacPcmReadPos;
        unsigned int mFlacBitsPerSample;
    };
}

#endif // !defined(NO_XIPH_LIBS)

#endif // MB_OGG_H
