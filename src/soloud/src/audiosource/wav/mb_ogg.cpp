#include "mb_ogg.h"

#if !defined(NO_XIPH_LIBS)

#include <stdint.h>
#include <stdlib.h>
#include <iostream>
#include <cstdio>

namespace SoLoud
{
    // ------------------------------------------------------------------
    // DataSource
    // ------------------------------------------------------------------
    unsigned int MBOggDecoder::DataSource::read(unsigned char *dst, unsigned int bytes)
    {
        if (file) return file->read(dst, bytes);
        if (mem) {
            unsigned int avail = memLen - memPos;
            if (avail > bytes) avail = bytes;
            memcpy(dst, mem + memPos, avail);
            memPos += avail;
            return avail;
        }
        return 0;
    }

    bool MBOggDecoder::DataSource::seek(int offset)
    {
        if (file) { file->seek(offset); return true; }
        if (mem) {
            if (offset < 0) offset = 0;
            memPos = (unsigned int)offset;
            if (memPos > memLen) memPos = memLen;
            return true;
        }
        return false;
    }

    unsigned int MBOggDecoder::DataSource::tell() const
    {
        if (file) return file->pos();
        return memPos;
    }

    unsigned int MBOggDecoder::DataSource::length() const
    {
        if (file) return file->length();
        return memLen;
    }

    bool MBOggDecoder::DataSource::eof() const
    {
        if (file) return file->eof() != 0;
        return memPos >= memLen;
    }

    // ------------------------------------------------------------------
    // Construction / Destruction
    // ------------------------------------------------------------------
    MBOggDecoder::MBOggDecoder()
        : mCodecType(CODEC_UNKNOWN),
          mChannels(0),
          mSampleRate(0),
          mLengthInSamples(0),
          mValid(false),
          mVorbisOpen(false),
          mOpusDecoder(nullptr),
          mOpusStreamInit(false),
          mOpusHeaderParsed(false),
          mOpusPacketCount(0),
          mOpusSkipSamples(0),
          mOpusTotalOutputSamples(0),
          mOpusTotalSamplesExpected(-1),
          mOpusMaxFrameSize(0),
          mOpusPcmReadPos(0),
          mOpusPreSkip(0),
          mFlacDecoder(nullptr),
          mFlacPcmReadPos(0),
          mFlacBitsPerSample(0)
    {
        memset(&mVorbisFile, 0, sizeof(mVorbisFile));
        memset(&mOpusOy, 0, sizeof(mOpusOy));
        memset(&mOpusOs, 0, sizeof(mOpusOs));
    }

    MBOggDecoder::~MBOggDecoder()
    {
        close();
    }

    void MBOggDecoder::close()
    {
        if (mVorbisOpen) {
            ov_clear(&mVorbisFile);
            mVorbisOpen = false;
        }
        if (mOpusDecoder) {
            opus_decoder_destroy(mOpusDecoder);
            mOpusDecoder = nullptr;
        }
        if (mOpusStreamInit) {
            ogg_stream_clear(&mOpusOs);
            mOpusStreamInit = false;
        }
        ogg_sync_clear(&mOpusOy);
        if (mFlacDecoder) {
            FLAC__stream_decoder_finish(mFlacDecoder);
            FLAC__stream_decoder_delete(mFlacDecoder);
            mFlacDecoder = nullptr;
        }
        mDataSource.file = nullptr;
        mDataSource.mem = nullptr;
        mDataSource.memLen = 0;
        mDataSource.memPos = 0;
        mCodecType = CODEC_UNKNOWN;
        mChannels = 0;
        mSampleRate = 0;
        mLengthInSamples = 0;
        mValid = false;
        mOpusHeaderParsed = false;
        mOpusPacketCount = 0;
        mOpusSkipSamples = 0;
        mOpusTotalOutputSamples = 0;
        mOpusTotalSamplesExpected = -1;
        mOpusPcmBuffer.clear();
        mOpusPcmReadPos = 0;
        mOpusPreSkip = 0;
        mFlacPcmBuffer.clear();
        mFlacPcmReadPos = 0;
        mFlacBitsPerSample = 0;
    }

    // ------------------------------------------------------------------
    // Open / Detection
    // ------------------------------------------------------------------
    bool MBOggDecoder::open(File *aFile)
    {
        close();
        mDataSource.file = aFile;
        mDataSource.mem = nullptr;
        mDataSource.memLen = 0;
        mDataSource.memPos = 0;


        if (!detectCodec()) {
            printf("[MBOggDecoder] detectCodec() failed\n");
            return false;
        }
        mDataSource.seek(0);

        printf("[MBOggDecoder] codec detected: %d (0=unknown, 1=vorbis, 2=opus, 3=flac)\n", mCodecType);

        bool ok = false;
        switch (mCodecType) {
            case CODEC_VORBIS: ok = initVorbis(); break;
            case CODEC_OPUS:   ok = initOpus();   break;
            case CODEC_FLAC:   ok = initFlac();   break;
            default: break;
        }
        mValid = ok;
        return ok;
    }

    bool MBOggDecoder::open(const unsigned char *aData, unsigned int aLength)
    {
        close();
        mDataSource.file = nullptr;
        mDataSource.mem = aData;
        mDataSource.memLen = aLength;
        mDataSource.memPos = 0;


        if (!detectCodec()) {
            printf("[MBOggDecoder] detectCodec() failed\n");
            return false;
        }
        mDataSource.seek(0);

        printf("[MBOggDecoder] codec detected: %d (0=unknown, 1=vorbis, 2=opus, 3=flac)\n", mCodecType);

        bool ok = false;
        switch (mCodecType) {
            case CODEC_VORBIS: ok = initVorbis(); break;
            case CODEC_OPUS:   ok = initOpus();   break;
            case CODEC_FLAC:   ok = initFlac();   break;
            default: break;
        }
        mValid = ok;
        return ok;
    }

    bool MBOggDecoder::detectCodec()
    {
        mCodecType = CODEC_UNKNOWN;
        unsigned int savedPos = mDataSource.tell();

        unsigned char probe[4096];
        unsigned int bytesRead = mDataSource.read(probe, 4096);
        if (bytesRead >= 4 && memcmp(probe, "OggS", 4) == 0) {
            ogg_sync_state oy;
            ogg_page og;
            ogg_sync_init(&oy);
            char *buf = ogg_sync_buffer(&oy, bytesRead);
            memcpy(buf, probe, bytesRead);
            ogg_sync_wrote(&oy, bytesRead);
            int pageOut = ogg_sync_pageout(&oy, &og);
            if (pageOut == 1) {
                ogg_stream_state os;
                ogg_packet op;
                if (ogg_stream_init(&os, ogg_page_serialno(&og)) == 0) {
                    ogg_stream_pagein(&os, &og);
                    int packetOut = ogg_stream_packetout(&os, &op);
                    if (packetOut == 1) {
                        if (op.bytes >= 8 && memcmp(op.packet, "OpusHead", 8) == 0) {
                            mCodecType = CODEC_OPUS;
                        } else if (op.bytes >= 7 && op.packet[0] == 0x01 && memcmp(op.packet + 1, "vorbis", 6) == 0) {
                            mCodecType = CODEC_VORBIS;
                        } else if (op.bytes >= 5 && op.packet[0] == 0x7f && memcmp(op.packet + 1, "FLAC", 4) == 0) {
                            mCodecType = CODEC_FLAC;
                        }
                    }
                    ogg_stream_clear(&os);
                }
            }
            ogg_sync_clear(&oy);
        }

        mDataSource.seek(savedPos);
        return mCodecType != CODEC_UNKNOWN;
    }

    // ------------------------------------------------------------------
    // Vorbis
    // ------------------------------------------------------------------
    bool MBOggDecoder::initVorbis()
    {
        ov_callbacks callbacks = {
            vorbisReadCb,
            vorbisSeekCb,
            vorbisCloseCb,
            vorbisTellCb
        };

        if (ov_open_callbacks(&mDataSource, &mVorbisFile, nullptr, 0, callbacks) != 0) {
            printf("[MBOggDecoder] initVorbis: ov_open_callbacks failed\n");
            return false;
        }

        mVorbisOpen = true;
        vorbis_info *info = ov_info(&mVorbisFile, -1);
        if (!info) {
            printf("[MBOggDecoder] initVorbis: ov_info returned null\n");
            ov_clear(&mVorbisFile);
            mVorbisOpen = false;
            return false;
        }

        mChannels = info->channels;
        mSampleRate = (int)info->rate;
        ogg_int64_t total = ov_pcm_total(&mVorbisFile, -1);
        mLengthInSamples = (total >= 0) ? (unsigned int)total : 0;
        printf("[MBOggDecoder] initVorbis: channels=%d, sampleRate=%d, length=%u\n", mChannels, mSampleRate, mLengthInSamples);
        return true;
    }

    unsigned int MBOggDecoder::readVorbis(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize)
    {
        unsigned int samplesRead = 0;
        int outChannels = mChannels;
        if (outChannels > MAX_CHANNELS) outChannels = MAX_CHANNELS;

        while (samplesRead < aSamples) {
            float **pcm = nullptr;
            int bitstream = 0;
            long ret = ov_read_float(&mVorbisFile, &pcm, aSamples - samplesRead, &bitstream);
            if (ret <= 0) break;

            for (int ch = 0; ch < outChannels; ch++) {
                memcpy(aBuffer + ch * aBufferSize + samplesRead, pcm[ch], sizeof(float) * ret);
            }
            samplesRead += (unsigned int)ret;
        }
        return samplesRead;
    }

    size_t MBOggDecoder::vorbisReadCb(void *ptr, size_t size, size_t nmemb, void *datasource)
    {
        DataSource *ds = static_cast<DataSource*>(datasource);
        unsigned int requested = (unsigned int)(size * nmemb);
        unsigned int read = ds->read((unsigned char*)ptr, requested);
        return read / size;
    }

    int MBOggDecoder::vorbisSeekCb(void *datasource, ogg_int64_t offset, int whence)
    {
        DataSource *ds = static_cast<DataSource*>(datasource);
        int absPos = 0;
        switch (whence) {
            case SEEK_SET: absPos = (int)offset; break;
            case SEEK_CUR: absPos = (int)(ds->tell() + offset); break;
            case SEEK_END: absPos = (int)(ds->length() + offset); break;
            default: return -1;
        }
        return ds->seek(absPos) ? 0 : -1;
    }

    int MBOggDecoder::vorbisCloseCb(void *datasource)
    {
        (void)datasource;
        return 0;
    }

    long MBOggDecoder::vorbisTellCb(void *datasource)
    {
        DataSource *ds = static_cast<DataSource*>(datasource);
        return (long)ds->tell();
    }

    // ------------------------------------------------------------------
    // Opus
    // ------------------------------------------------------------------
    bool MBOggDecoder::initOpus()
    {
        mSampleRate = 48000;
        mChannels = 2;
        mOpusPreSkip = 0;

        ogg_sync_init(&mOpusOy);
        mOpusStreamInit = false;
        mOpusHeaderParsed = false;
        mOpusPacketCount = 0;
        mOpusSkipSamples = 0;
        mOpusTotalOutputSamples = 0;
        mOpusTotalSamplesExpected = -1;
        mOpusPcmBuffer.clear();
        mOpusPcmReadPos = 0;
        mOpusDecoder = nullptr;

        // Parse headers to get channels and preSkip
        while (!mOpusHeaderParsed) {
            if (!feedOpusData()) {
                printf("[MBOggDecoder] initOpus: feedOpusData failed during header parse\n");
                ogg_sync_clear(&mOpusOy);
                return false;
            }
        }

        // Scan for total length from beginning
        mDataSource.seek(0);
        ogg_sync_state scanOy;
        ogg_page scanOg;
        ogg_sync_init(&scanOy);
        ogg_int64_t lastGranule = -1;

        while (true) {
            int ret = ogg_sync_pageout(&scanOy, &scanOg);
            if (ret == 1) {
                ogg_int64_t granule = ogg_page_granulepos(&scanOg);
                if (granule >= 0) lastGranule = granule;
            } else if (ret == 0) {
                char *buf = ogg_sync_buffer(&scanOy, 4096);
                unsigned int bytes = mDataSource.read((unsigned char*)buf, 4096);
                if (bytes == 0) break;
                ogg_sync_wrote(&scanOy, bytes);
            } else {
                continue;
            }
        }
        ogg_sync_clear(&scanOy);

        if (lastGranule >= 0) {
            int64_t trimmed = (int64_t)lastGranule - mOpusPreSkip;
            if (trimmed < 0) trimmed = 0;
            mLengthInSamples = (unsigned int)((trimmed * mSampleRate + 47999) / 48000);
        } else {
            mLengthInSamples = 0;
        }

        // Reset for actual decoding
        mDataSource.seek(0);
        ogg_sync_clear(&mOpusOy);
        ogg_sync_init(&mOpusOy);
        if (mOpusDecoder) {
            opus_decoder_destroy(mOpusDecoder);
            mOpusDecoder = nullptr;
        }
        mOpusStreamInit = false;
        mOpusHeaderParsed = false;
        mOpusPacketCount = 0;
        mOpusSkipSamples = 0;
        mOpusTotalOutputSamples = 0;
        mOpusTotalSamplesExpected = -1;
        mOpusPcmBuffer.clear();
        mOpusPcmReadPos = 0;

        // Re-parse headers
        while (!mOpusHeaderParsed) {
            if (!feedOpusData()) {
                printf("[MBOggDecoder] initOpus: feedOpusData failed during re-parse\n");
                ogg_sync_clear(&mOpusOy);
                return false;
            }
        }

        printf("[MBOggDecoder] initOpus: success - channels=%d, sampleRate=%d, length=%u\n", mChannels, mSampleRate, mLengthInSamples);
        return true;
    }

    bool MBOggDecoder::feedOpusData()
    {
        ogg_page og;
        int ret = ogg_sync_pageout(&mOpusOy, &og);

        if (ret == 1) {
            // Chained stream handling
            if (mOpusStreamInit && ogg_page_serialno(&og) != mOpusOs.serialno) {
                ogg_stream_clear(&mOpusOs);
                mOpusStreamInit = false;
                mOpusHeaderParsed = false;
                mOpusPacketCount = 0;
                mOpusSkipSamples = 0;
                mOpusTotalOutputSamples = 0;
                mOpusTotalSamplesExpected = -1;
                if (mOpusDecoder) opus_decoder_ctl(mOpusDecoder, OPUS_RESET_STATE);
            }

            if (!mOpusStreamInit) {
                if (ogg_stream_init(&mOpusOs, ogg_page_serialno(&og)) != 0)
                    return false;
                mOpusStreamInit = true;
            }

            if (ogg_stream_pagein(&mOpusOs, &og) < 0)
                return true; // skip corrupted page

            ogg_packet op;
            while (ogg_stream_packetout(&mOpusOs, &op) == 1) {
                if (!decodeOpusPacket(&op))
                    return false;
            }
            return true;
        }

        // Need more data or out of sync
        char *buf = ogg_sync_buffer(&mOpusOy, 4096);
        unsigned int bytes = mDataSource.read((unsigned char*)buf, 4096);
        if (bytes == 0)
            return mOpusHeaderParsed; // EOF: ok if headers were parsed
        ogg_sync_wrote(&mOpusOy, bytes);
        return true;
    }

    bool MBOggDecoder::decodeOpusPacket(ogg_packet *packet)
    {
        if (!mOpusHeaderParsed) {
            if (packet->bytes >= 8 && memcmp(packet->packet, "OpusHead", 8) == 0) {
                if (packet->bytes >= 19) {
                    unsigned char *data = (unsigned char*)packet->packet;
                    mChannels = data[9];
                    mOpusPreSkip = (uint16_t)(data[10] | (data[11] << 8));

                    int error = OPUS_OK;
                    if (mOpusDecoder) opus_decoder_destroy(mOpusDecoder);
                    mOpusDecoder = opus_decoder_create(48000, mChannels, &error);
                    if (error != OPUS_OK || !mOpusDecoder) {
                        mOpusDecoder = nullptr;
                        return false;
                    }
                    opus_decoder_ctl(mOpusDecoder, OPUS_RESET_STATE);
                    mOpusMaxFrameSize = 48000 * 60 / 1000;
                    mOpusOutputBuffer.resize(mOpusMaxFrameSize * mChannels);
                    mOpusSkipSamples = (int)((int64_t)mOpusPreSkip * 48000 + 47999) / 48000;
                }
            } else if (packet->bytes >= 8 && memcmp(packet->packet, "OpusTags", 8) == 0) {
                mOpusHeaderParsed = true;
                mOpusSkipSamples = (int)((int64_t)mOpusPreSkip * 48000 + 47999) / 48000;
            }
            mOpusPacketCount++;
            return true;
        }

        if (!mOpusDecoder) return true;

        int samples = opus_decode_float(mOpusDecoder,
                                        packet->packet,
                                        (opus_int32)packet->bytes,
                                        mOpusOutputBuffer.data(),
                                        mOpusMaxFrameSize,
                                        0);
        if (samples < 0) return true; // skip bad packet

        if (samples > 0) {
            int usable = samples;
            int skipped = 0;

            if (mOpusSkipSamples > 0) {
                int toSkip = std::min(mOpusSkipSamples, usable);
                mOpusSkipSamples -= toSkip;
                usable -= toSkip;
                skipped = toSkip;
            }

            if (usable <= 0) return true;

            size_t startIdx = (size_t)skipped * mChannels;
            size_t count = (size_t)usable * mChannels;
            mOpusPcmBuffer.insert(mOpusPcmBuffer.end(),
                                  mOpusOutputBuffer.begin() + startIdx,
                                  mOpusOutputBuffer.begin() + startIdx + count);
            mOpusTotalOutputSamples += usable;
        }
        return true;
    }

    unsigned int MBOggDecoder::readOpus(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize)
    {
        unsigned int samplesRead = 0;
        int outChannels = mChannels;
        if (outChannels > MAX_CHANNELS) outChannels = MAX_CHANNELS;

        while (samplesRead < aSamples) {
            size_t avail = (mOpusPcmBuffer.size() - mOpusPcmReadPos) / mChannels;
            if (avail > 0) {
                size_t toCopy = std::min((size_t)(aSamples - samplesRead), avail);
                for (int ch = 0; ch < outChannels; ch++) {
                    for (size_t i = 0; i < toCopy; i++) {
                        aBuffer[ch * aBufferSize + samplesRead + i] = mOpusPcmBuffer[mOpusPcmReadPos + i * mChannels + ch];
                    }
                }
                mOpusPcmReadPos += toCopy * mChannels;
                if (mOpusPcmReadPos >= mOpusPcmBuffer.size()) {
                    mOpusPcmBuffer.clear();
                    mOpusPcmReadPos = 0;
                }
                samplesRead += (unsigned int)toCopy;
                continue;
            }

            size_t prevSize = mOpusPcmBuffer.size();
            if (!feedOpusData()) break;
            if (mOpusPcmBuffer.size() == prevSize && mDataSource.eof()) break;
        }
        return samplesRead;
    }

    bool MBOggDecoder::rewindOpus()
    {
        mDataSource.seek(0);
        if (mOpusStreamInit) {
            ogg_stream_clear(&mOpusOs);
            mOpusStreamInit = false;
        }
        ogg_sync_clear(&mOpusOy);
        ogg_sync_init(&mOpusOy);
        if (mOpusDecoder) {
            opus_decoder_ctl(mOpusDecoder, OPUS_RESET_STATE);
        }
        mOpusHeaderParsed = false;
        mOpusPacketCount = 0;
        mOpusSkipSamples = 0;
        mOpusTotalOutputSamples = 0;
        mOpusTotalSamplesExpected = -1;
        mOpusPcmBuffer.clear();
        mOpusPcmReadPos = 0;

        while (!mOpusHeaderParsed) {
            if (!feedOpusData()) return false;
        }
        return true;
    }

    bool MBOggDecoder::seekOpus(unsigned int aSample)
    {
        if (!rewindOpus()) return false;
        unsigned int remaining = aSample;
        float temp[512 * MAX_CHANNELS];
        while (remaining > 0) {
            unsigned int toRead = remaining > 512 ? 512 : remaining;
            unsigned int got = readOpus(temp, toRead, toRead);
            if (got == 0) break;
            remaining -= got;
        }
        return remaining == 0;
    }

    // ------------------------------------------------------------------
    // FLAC
    // ------------------------------------------------------------------
    bool MBOggDecoder::initFlac()
    {
        // For OGG/FLAC, STREAMINFO may report total_samples=0.
        // Scan Ogg pages for the last granule position before initializing the decoder.
        unsigned int savedPos = mDataSource.tell();
        ogg_sync_state scanOy;
        ogg_page scanOg;
        ogg_sync_init(&scanOy);
        ogg_int64_t lastGranule = -1;

        while (true) {
            int ret = ogg_sync_pageout(&scanOy, &scanOg);
            if (ret == 1) {
                ogg_int64_t granule = ogg_page_granulepos(&scanOg);
                if (granule >= 0) lastGranule = granule;
            } else if (ret == 0) {
                char *buf = ogg_sync_buffer(&scanOy, 4096);
                unsigned int bytes = mDataSource.read((unsigned char*)buf, 4096);
                if (bytes == 0) break;
                ogg_sync_wrote(&scanOy, bytes);
            } else {
                continue;
            }
        }
        ogg_sync_clear(&scanOy);
        mDataSource.seek(savedPos);

        unsigned int scannedLength = 0;
        if (lastGranule >= 0) {
            scannedLength = (unsigned int)lastGranule;
        }

        mFlacDecoder = FLAC__stream_decoder_new();
        if (!mFlacDecoder) {
            printf("[MBOggDecoder] initFlac: FLAC__stream_decoder_new returned null\n");
            return false;
        }

        FLAC__stream_decoder_set_metadata_respond_all(mFlacDecoder);

        FLAC__StreamDecoderInitStatus status = FLAC__stream_decoder_init_ogg_stream(
            mFlacDecoder,
            flacReadCb,
            flacSeekCb,
            flacTellCb,
            flacLengthCb,
            flacEofCb,
            flacWriteCb,
            flacMetadataCb,
            flacErrorCb,
            this
        );

        if (status != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
            printf("[MBOggDecoder] initFlac: init_ogg_stream failed with status %d\n", status);
            FLAC__stream_decoder_delete(mFlacDecoder);
            mFlacDecoder = nullptr;
            return false;
        }

        if (!FLAC__stream_decoder_process_until_end_of_metadata(mFlacDecoder)) {
            printf("[MBOggDecoder] initFlac: process_until_end_of_metadata failed\n");
            FLAC__stream_decoder_finish(mFlacDecoder);
            FLAC__stream_decoder_delete(mFlacDecoder);
            mFlacDecoder = nullptr;
            return false;
        }

        printf("[MBOggDecoder] initFlac: metadata done - channels=%d, sampleRate=%d, bitsPerSample=%d, length=%u\n",
               mChannels, mSampleRate, mFlacBitsPerSample, mLengthInSamples);

        // If STREAMINFO didn't provide length, use scanned value
        if (mLengthInSamples == 0 && scannedLength > 0) {
            mLengthInSamples = scannedLength;
        }

        if (mChannels == 0 || mSampleRate == 0) {
            printf("[MBOggDecoder] initFlac: invalid stream info (channels=%d, sampleRate=%d)\n", mChannels, mSampleRate);
            FLAC__stream_decoder_finish(mFlacDecoder);
            FLAC__stream_decoder_delete(mFlacDecoder);
            mFlacDecoder = nullptr;
            return false;
        }

        printf("[MBOggDecoder] initFlac: success\n");
        return true;
    }

    unsigned int MBOggDecoder::readFlac(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize)
    {
        unsigned int samplesRead = 0;
        int outChannels = mChannels;
        if (outChannels > MAX_CHANNELS) outChannels = MAX_CHANNELS;

        while (samplesRead < aSamples) {
            size_t avail = (mFlacPcmBuffer.size() - mFlacPcmReadPos) / mChannels;
            if (avail > 0) {
                size_t toCopy = std::min((size_t)(aSamples - samplesRead), avail);
                for (int ch = 0; ch < outChannels; ch++) {
                    for (size_t i = 0; i < toCopy; i++) {
                        aBuffer[ch * aBufferSize + samplesRead + i] = mFlacPcmBuffer[mFlacPcmReadPos + i * mChannels + ch];
                    }
                }
                mFlacPcmReadPos += toCopy * mChannels;
                if (mFlacPcmReadPos >= mFlacPcmBuffer.size()) {
                    mFlacPcmBuffer.clear();
                    mFlacPcmReadPos = 0;
                }
                samplesRead += (unsigned int)toCopy;
                continue;
            }

            if (!FLAC__stream_decoder_process_single(mFlacDecoder)) break;
            FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(mFlacDecoder);
            if (state == FLAC__STREAM_DECODER_END_OF_STREAM) break;
            if (state == FLAC__STREAM_DECODER_ABORTED) break;
            if (mFlacPcmBuffer.size() == mFlacPcmReadPos) {
                // No progress - avoid infinite loop
                break;
            }
        }
        return samplesRead;
    }

    FLAC__StreamDecoderReadStatus MBOggDecoder::flacReadCb(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        unsigned int requested = (unsigned int)(*bytes);
        unsigned int read = self->mDataSource.read(buffer, requested);
        *bytes = read;
        if (read == 0) return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    FLAC__StreamDecoderSeekStatus MBOggDecoder::flacSeekCb(const FLAC__StreamDecoder *decoder, FLAC__uint64 absolute_byte_offset, void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        if (!self->mDataSource.seek((int)absolute_byte_offset))
            return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }

    FLAC__StreamDecoderTellStatus MBOggDecoder::flacTellCb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        *absolute_byte_offset = self->mDataSource.tell();
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }

    FLAC__StreamDecoderLengthStatus MBOggDecoder::flacLengthCb(const FLAC__StreamDecoder *decoder, FLAC__uint64 *stream_length, void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        *stream_length = self->mDataSource.length();
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }

    FLAC__bool MBOggDecoder::flacEofCb(const FLAC__StreamDecoder *decoder, void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        return self->mDataSource.eof() ? true : false;
    }

    FLAC__StreamDecoderWriteStatus MBOggDecoder::flacWriteCb(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        unsigned int channels = frame->header.channels;
        unsigned int blocksize = frame->header.blocksize;

        // Fallback: populate stream info from frame header if metadata callback missed it
        if (self->mChannels == 0) self->mChannels = (int)channels;
        if (self->mSampleRate == 0) self->mSampleRate = (int)frame->header.sample_rate;

        // Use frame header bit depth if available, otherwise fall back to STREAMINFO
        unsigned int bps = frame->header.bits_per_sample;
        if (bps == 0) bps = self->mFlacBitsPerSample;
        if (bps == 0) bps = 16; // safe fallback

        float divisor = 1.0f;
        if (bps > 0 && bps < 32) {
            divisor = (float)(1u << (bps - 1));
        } else if (bps == 32) {
            divisor = 2147483648.0f;
        }

        for (unsigned int i = 0; i < blocksize; i++) {
            for (unsigned int ch = 0; ch < channels; ch++) {
                self->mFlacPcmBuffer.push_back(static_cast<float>(buffer[ch][i]) / divisor);
            }
        }
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    void MBOggDecoder::flacMetadataCb(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
            self->mSampleRate = metadata->data.stream_info.sample_rate;
            self->mChannels = metadata->data.stream_info.channels;
            self->mFlacBitsPerSample = metadata->data.stream_info.bits_per_sample;
            self->mLengthInSamples = (unsigned int)metadata->data.stream_info.total_samples;
        }
    }

    void MBOggDecoder::flacErrorCb(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
    {
        (void)decoder;
        MBOggDecoder *self = static_cast<MBOggDecoder*>(client_data);
        printf("[MBOggDecoder] flacErrorCb: FLAC decoder error: %s (channels=%d, sampleRate=%d)\n",
               FLAC__StreamDecoderErrorStatusString[status],
               self ? self->mChannels : -1,
               self ? self->mSampleRate : -1);
    }

    // ------------------------------------------------------------------
    // Public interface
    // ------------------------------------------------------------------
    bool MBOggDecoder::isValid() const
    {
        return mValid;
    }

    MBOggDecoder::CodecType MBOggDecoder::getCodecType() const
    {
        return mCodecType;
    }

    int MBOggDecoder::getChannels() const
    {
        return mChannels;
    }

    int MBOggDecoder::getSampleRate() const
    {
        return mSampleRate;
    }

    unsigned int MBOggDecoder::getLengthInSamples() const
    {
        return mLengthInSamples;
    }

    unsigned int MBOggDecoder::read(float *aBuffer, unsigned int aSamples, unsigned int aBufferSize)
    {
        if (!mValid) return 0;
        switch (mCodecType) {
            case CODEC_VORBIS: return readVorbis(aBuffer, aSamples, aBufferSize);
            case CODEC_OPUS:   return readOpus(aBuffer, aSamples, aBufferSize);
            case CODEC_FLAC:   return readFlac(aBuffer, aSamples, aBufferSize);
            default: return 0;
        }
    }

    bool MBOggDecoder::seek(unsigned int aSample)
    {
        if (!mValid) return false;
        switch (mCodecType) {
            case CODEC_VORBIS: return ov_pcm_seek(&mVorbisFile, aSample) == 0;
            case CODEC_FLAC:
                mFlacPcmBuffer.clear();
                mFlacPcmReadPos = 0;
                return FLAC__stream_decoder_seek_absolute(mFlacDecoder, aSample) ? true : false;
            case CODEC_OPUS: return seekOpus(aSample);
            default: return false;
        }
    }

    bool MBOggDecoder::rewind()
    {
        if (!mValid) return false;
        switch (mCodecType) {
            case CODEC_VORBIS: return ov_pcm_seek(&mVorbisFile, 0) == 0;
            case CODEC_FLAC:
                mFlacPcmBuffer.clear();
                mFlacPcmReadPos = 0;
                return FLAC__stream_decoder_seek_absolute(mFlacDecoder, 0) ? true : false;
            case CODEC_OPUS: return rewindOpus();
            default: return false;
        }
    }
}

#endif // !defined(NO_XIPH_LIBS)
