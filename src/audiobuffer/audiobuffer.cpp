
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "soloud.h"
#include "../soloud/src/audiosource/wav/dr_flac.h"
#include "../soloud/src/audiosource/wav/dr_mp3.h"
#include "../soloud/src/audiosource/wav/dr_wav.h"
#include "soloud_wavstream.h"
#include "soloud_file.h"
#include "../soloud/src/audiosource/wav/stb_vorbis.h"


#include "audiobuffer.h"

namespace SoLoud
{
	size_t drflac_read_func2(void* pUserData, void* pBufferOut, size_t bytesToRead)
	{
		File *fp = (File*)pUserData;
		return fp->read((unsigned char*)pBufferOut, (unsigned int)bytesToRead);
	}

	size_t drmp3_read_func2(void* pUserData, void* pBufferOut, size_t bytesToRead)
	{
		File *fp = (File*)pUserData;
		return fp->read((unsigned char*)pBufferOut, (unsigned int)bytesToRead);
	}

	size_t drwav_read_func2(void* pUserData, void* pBufferOut, size_t bytesToRead)
	{
		File *fp = (File*)pUserData;
		return fp->read((unsigned char*)pBufferOut, (unsigned int)bytesToRead);
	}

	drflac_bool32 drflac_seek_func2(void* pUserData, int offset, drflac_seek_origin origin)
	{
		File *fp = (File*)pUserData;
		if (origin != drflac_seek_origin_start)
			offset += fp->pos();
		fp->seek(offset);
		return 1;
	}

	drmp3_bool32 drmp3_seek_func2(void* pUserData, int offset, drmp3_seek_origin origin)
	{
		File *fp = (File*)pUserData;
		if (origin != drmp3_seek_origin_start)
			offset += fp->pos();
		fp->seek(offset);
		return 1;
	}

	drmp3_bool32 drwav_seek_func2(void* pUserData, int offset, drwav_seek_origin origin)
	{
		File *fp = (File*)pUserData;
		if (origin != drwav_seek_origin_start)
			offset += fp->pos();
		fp->seek(offset);
		return 1;
	}

	BufferStreamInstance::BufferStreamInstance(BufferStream *aParent)
	{
		mOggFrameSize = 0;
		mParent = aParent;
		mOffset = 0;
		mCodec.mOgg = 0;
		mCodec.mFlac = 0;
		mFile = 0;
		if (aParent->mMemFile)
		{
			MemoryFile *mf = new MemoryFile();
			mFile = mf;
			mf->openMem(aParent->mMemFile->getMemPtr(), aParent->mMemFile->length(), false, false);
		}
		else
		if (aParent->mStreamFile)
		{
			mFile = aParent->mStreamFile;
			mFile->seek(0); // stb_vorbis assumes file offset to be at start of ogg
		}
		else
		{
			return;
		}
		
		if (mFile)
		{
			if (mParent->mFiletype == BUFFERSTREAM_WAV)
			{
				mCodec.mWav = new drwav;
				if (!drwav_init(mCodec.mWav, drwav_read_func2, drwav_seek_func2, (void*)mFile, NULL))
				{
					delete mCodec.mWav;
					mCodec.mWav = 0;
					if (mFile != mParent->mStreamFile)
						delete mFile;
					mFile = 0;
				}
			}
			else
			if (mParent->mFiletype == BUFFERSTREAM_OGG)
			{
				int e;

				mCodec.mOgg = stb_vorbis_open_file((Soloud_Filehack *)mFile, 0, &e, 0);

				if (!mCodec.mOgg)
				{
					if (mFile != mParent->mStreamFile)
						delete mFile;
					mFile = 0;
				}
				mOggFrameSize = 0;
				mOggFrameOffset = 0;
				mOggOutputs = 0;
			}
			else
			if (mParent->mFiletype == BUFFERSTREAM_FLAC)
			{
				mCodec.mFlac = drflac_open(drflac_read_func2, drflac_seek_func2, (void*)mFile, NULL);
				if (!mCodec.mFlac)
				{
					if (mFile != mParent->mStreamFile)
						delete mFile;
					mFile = 0;
				}
			}
			else
			if (mParent->mFiletype == BUFFERSTREAM_MP3)
			{
				mCodec.mMp3 = new drmp3;
				if (!drmp3_init(mCodec.mMp3, drmp3_read_func2, drmp3_seek_func2, (void*)mFile, NULL))
				{
					delete mCodec.mMp3;
					mCodec.mMp3 = 0;
					if (mFile != mParent->mStreamFile)
						delete mFile;
					mFile = 0;
				}
			}
			else
			if (mParent->mFiletype == BUFFERSTREAM_PCM)
			{
				
			}
			else
			{
				if (mFile != mParent->mStreamFile)
					delete mFile;
				mFile = NULL;
				return;
			}
		}
	}
 
	BufferStreamInstance::~BufferStreamInstance()
	{
		switch (mParent->mFiletype)
		{
		case BUFFERSTREAM_OGG:
			if (mCodec.mOgg)
			{
				stb_vorbis_close(mCodec.mOgg);
			}
			break;
		case BUFFERSTREAM_FLAC:
			if (mCodec.mFlac)
			{
				drflac_close(mCodec.mFlac);
			}
			break;
		case BUFFERSTREAM_MP3:
			if (mCodec.mMp3)
			{
				drmp3_uninit(mCodec.mMp3);
				delete mCodec.mMp3;
				mCodec.mMp3 = 0;
			}
			break;
		case BUFFERSTREAM_WAV:
			if (mCodec.mWav)
			{
				drwav_uninit(mCodec.mWav);
				delete mCodec.mWav;
				mCodec.mWav = 0;
			}
			break;
		case BUFFERSTREAM_PCM:
			if (mParent->mBuffer.getCurrentBufferSizeInBytes() > 0)
			{
				mParent->mBuffer.clear();
			}
			break;
		}
		if (mFile != mParent->mStreamFile)
		{
			delete mFile;
		}
	}

	static int getOggData(float **aOggOutputs, float *aBuffer, int aSamples, int aPitch, int aFrameSize, int aFrameOffset, int aChannels)
	{			
		if (aFrameSize <= 0)
			return 0;

		int samples = aSamples;
		if (aFrameSize - aFrameOffset < samples)
		{
			samples = aFrameSize - aFrameOffset;
		}

		int i;
		for (i = 0; i < aChannels; i++)
		{
			memcpy(aBuffer + aPitch * i, aOggOutputs[i] + aFrameOffset, sizeof(float) * samples);
		}
		return samples;
	}

	

	unsigned int BufferStreamInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{			
		unsigned int offset = 0;
		float tmp[512 * MAX_CHANNELS];
		if (mFile == NULL)
			return 0;
		switch (mParent->mFiletype)
		{
		case BUFFERSTREAM_FLAC:
			{
				unsigned int i, j, k;

				for (i = 0; i < aSamplesToRead; i += 512)
				{
					unsigned int blockSize = (aSamplesToRead - i) > 512 ? 512 : aSamplesToRead - i;
					offset += (unsigned int)drflac_read_pcm_frames_f32(mCodec.mFlac, blockSize, tmp);

					for (j = 0; j < blockSize; j++)
					{
						for (k = 0; k < mChannels; k++)
						{
							aBuffer[k * aSamplesToRead + i + j] = tmp[j * mCodec.mFlac->channels + k];
						}
					}
				}
				mOffset += offset;
				return offset;
			}
			break;
		case BUFFERSTREAM_MP3:
			{
				unsigned int i, j, k;

				for (i = 0; i < aSamplesToRead; i += 512)
				{
					unsigned int blockSize = (aSamplesToRead - i) > 512 ? 512 : aSamplesToRead - i;
					offset += (unsigned int)drmp3_read_pcm_frames_f32(mCodec.mMp3, blockSize, tmp);

					for (j = 0; j < blockSize; j++)
					{
						for (k = 0; k < mChannels; k++)
						{
							aBuffer[k * aSamplesToRead + i + j] = tmp[j * mCodec.mMp3->channels + k];
						}
					}
				}
				mOffset += offset;
				return offset;
			}
		break;
		case BUFFERSTREAM_OGG:
			{
				if (mOggFrameOffset < mOggFrameSize)
				{
					int b = getOggData(mOggOutputs, aBuffer, aSamplesToRead, aBufferSize, mOggFrameSize, mOggFrameOffset, mChannels);
					mOffset += b;
					offset += b;
					mOggFrameOffset += b;
				}

				while (offset < aSamplesToRead)
				{
					mOggFrameSize = stb_vorbis_get_frame_float(mCodec.mOgg, NULL, &mOggOutputs);
					mOggFrameOffset = 0;
					int b = getOggData(mOggOutputs, aBuffer + offset, aSamplesToRead - offset, aBufferSize, mOggFrameSize, mOggFrameOffset, mChannels);
					mOffset += b;
					offset += b;
					mOggFrameOffset += b;

					if (mOffset >= mParent->mSampleCount || b == 0)
					{
						mOffset += offset;
						return offset;
					}
				}
			}
			break;
		case BUFFERSTREAM_WAV:
			{
				unsigned int i, j, k;

				for (i = 0; i < aSamplesToRead; i += 512)
				{
					unsigned int blockSize = (aSamplesToRead - i) > 512 ? 512 : aSamplesToRead - i;
					offset += (unsigned int)drwav_read_pcm_frames_f32(mCodec.mWav, blockSize, tmp);

					for (j = 0; j < blockSize; j++)
					{
						for (k = 0; k < mChannels; k++)
						{
							aBuffer[k * aSamplesToRead + i + j] = tmp[j * mCodec.mWav->channels + k];
						}
					}
				}
				mOffset += offset;
				return offset;
			}
			break;
		case BUFFERSTREAM_PCM:
			{
				unsigned int bufferSize = mParent->mBuffer.getCurrentBufferSize();
				int samplesToRead = mOffset + aSamplesToRead > bufferSize ? bufferSize - mOffset : aSamplesToRead;
				if (samplesToRead <= 0)
					return 0;
				memcpy(aBuffer, mParent->mBuffer.buffer.data() + mOffset, sizeof(float) * samplesToRead);
				mOffset += samplesToRead;
				return samplesToRead;
			}
			break;
		}
		return aSamplesToRead;
	}

	result BufferStreamInstance::seek(double aSeconds, float *mScratch, unsigned int mScratchSize)
	{
		if (mCodec.mOgg)
		{
			int pos = (int)floor(mBaseSamplerate * aSeconds);
			double newPosition;

			switch (mParent->mFiletype)
			{
			case BUFFERSTREAM_OGG:
				stb_vorbis_seek(mCodec.mOgg, pos);
				// Since the position that we just sought to might not be *exactly*
				// the position we asked for, we're re-calculating the position just
				// for the sake of correctness.
				mOffset = stb_vorbis_get_sample_offset(mCodec.mOgg);
				newPosition = float(mOffset / mBaseSamplerate);
				mStreamPosition = newPosition;
				return 0;
			case BUFFERSTREAM_FLAC:
				drflac_seek_to_pcm_frame(mCodec.mFlac, pos);
				mOffset = pos;
				mStreamPosition = float(pos / mBaseSamplerate);
				return 0;
			case BUFFERSTREAM_MP3:
				// When using TYPE_WAVSTREAM for mp3 and seeking backward,
				// dr_mp3.h uses `drmp3_seek_to_pcm_frame__brute_force()` function which
				// move to the start of the stream and then move forward to [time]. This
				// implies some lag and queue subsequent seeks request if the previous seek is not yet
				// complete (especially when using a slider) impacting the main UI thread.
				// To have no lags, please use TYPE_WAV instead if possible!
				drmp3_seek_to_pcm_frame(mCodec.mMp3, pos);
				mOffset = pos;
				mStreamPosition = float(pos / mBaseSamplerate);
				return 0;
			case BUFFERSTREAM_WAV:
				drwav_seek_to_pcm_frame(mCodec.mWav, pos);
				mOffset = pos;
				mStreamPosition = float(pos / mBaseSamplerate);
				return 0;
			default:
				break;
			}
		}
		else
		{
			return AudioSourceInstance::seek(aSeconds, mScratch, mScratchSize);
		}

		return NOT_IMPLEMENTED;
	}


	result BufferStreamInstance::rewind()
	{
		switch (mParent->mFiletype)
		{
		case BUFFERSTREAM_OGG:
			if (mCodec.mOgg)
			{
				stb_vorbis_seek_start(mCodec.mOgg);
			}
			break;
		case BUFFERSTREAM_FLAC:
			if (mCodec.mFlac)
			{
				drflac_seek_to_pcm_frame(mCodec.mFlac, 0);
			}
			break;
		case BUFFERSTREAM_MP3:
			if (mCodec.mMp3)
			{
				drmp3_seek_to_pcm_frame(mCodec.mMp3, 0);
			}
			break;
		case BUFFERSTREAM_WAV:
			if (mCodec.mWav)
			{
				drwav_seek_to_pcm_frame(mCodec.mWav, 0);
			}
			break;
		}
		mOffset = 0;
		mStreamPosition = 0.0f;
		return 0;
	}

	bool BufferStreamInstance::hasEnded()
	{
		if (mOffset >= mParent->mSampleCount)
		{
			return 1;
		}
		return 0;
	}

	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////
	// //////////////////////////////////////////////////////////////

	BufferStream::BufferStream()
	{
		mSampleCount = 0;
		mFiletype = BUFFERSTREAM_WAV;
		mMemFile = 0;
		mStreamFile = 0;
	}

	BufferStream::~BufferStream()
	{
		stop();
		delete mMemFile;
	}
	
#define MAKEDWORD(a,b,c,d) (((d) << 24) | ((c) << 16) | ((b) << 8) | (a))

	result BufferStream::loadwav(File * fp)
	{
		fp->seek(0);
		drwav decoder;

		if (!drwav_init(&decoder, drwav_read_func2, drwav_seek_func2, (void*)fp, NULL))
			return FILE_LOAD_FAILED;

		mChannels = decoder.channels;
		if (mChannels > MAX_CHANNELS)
		{
			mChannels = MAX_CHANNELS;
		}

		mBaseSamplerate = (float)decoder.sampleRate;
		mSampleCount = (unsigned int)decoder.totalPCMFrameCount;
		mFiletype = BUFFERSTREAM_WAV;
		drwav_uninit(&decoder);

		return SO_NO_ERROR;
	}

	result BufferStream::loadogg(File * fp)
	{
		fp->seek(0);
		int e;
		stb_vorbis *v;
		v = stb_vorbis_open_file((Soloud_Filehack *)fp, 0, &e, 0);
		if (v == NULL)
			return FILE_LOAD_FAILED;
		stb_vorbis_info info = stb_vorbis_get_info(v);
		mChannels = info.channels;
		if (info.channels > MAX_CHANNELS)
		{
			mChannels = MAX_CHANNELS;
		}
		mBaseSamplerate = (float)info.sample_rate;
		int samples = stb_vorbis_stream_length_in_samples(v);
		stb_vorbis_close(v);
		mFiletype = BUFFERSTREAM_OGG;

		mSampleCount = samples;

		return 0;
	}

	result BufferStream::loadflac(File * fp)
	{
		fp->seek(0);
		drflac* decoder = drflac_open(drflac_read_func2, drflac_seek_func2, (void*)fp, NULL);

		if (decoder == NULL)
			return FILE_LOAD_FAILED;
		
		mChannels = decoder->channels;
		if (mChannels > MAX_CHANNELS)
		{
			mChannels = MAX_CHANNELS;
		}

		mBaseSamplerate = (float)decoder->sampleRate;
		mSampleCount = (unsigned int)decoder->totalPCMFrameCount;
		mFiletype = BUFFERSTREAM_FLAC;
		drflac_close(decoder);

		return SO_NO_ERROR;
	}

	result BufferStream::loadmp3(File * fp)
	{
		fp->seek(0);
		drmp3 decoder;
		if (!drmp3_init(&decoder, drmp3_read_func2, drmp3_seek_func2, (void*)fp, NULL))
			return FILE_LOAD_FAILED;


		mChannels = decoder.channels;
		if (mChannels > MAX_CHANNELS)
		{
			mChannels = MAX_CHANNELS;
		}

		drmp3_uint64 samples = drmp3_get_pcm_frame_count(&decoder);

		mBaseSamplerate = (float)decoder.sampleRate;
		mSampleCount = (unsigned int)samples;
		mFiletype = BUFFERSTREAM_MP3;
		drmp3_uninit(&decoder);

		return SO_NO_ERROR;
	}

	result BufferStream::loadpcm(File * fp, const PCMformat pcmFormat)
	{
		fp->seek(0);

		mChannels = pcmFormat.channels;
		if (mChannels > MAX_CHANNELS)
		{
			mChannels = MAX_CHANNELS;
		}

		mBaseSamplerate = (float)pcmFormat.sampleRate;
		// TODO verify this: I suppose fp->length() is in bytes
		mSampleCount = fp->length() / pcmFormat.channels / pcmFormat.bytesPerSample;
		mFiletype = BUFFERSTREAM_PCM;

		// add to buffer
		addData(fp->getMemPtr(), fp->length());

		return SO_NO_ERROR;
	}

	result BufferStream::loadMem(
		const unsigned char *aData,
		unsigned int aDataLen,
		unsigned int maxBufferSize,
		bool aCopy,
		bool aTakeOwnership,
		PCMformat pcmFormat)
	{
		delete mMemFile;
		mStreamFile = 0;
		mMemFile = 0;
		mSampleCount = 0;
		mPCMformat.sampleRate = pcmFormat.sampleRate;
		mPCMformat.channels = pcmFormat.channels;
		mPCMformat.bytesPerSample = pcmFormat.bytesPerSample;
		mPCMformat.dataType = pcmFormat.dataType;
		mBuffer.setSizeInBytes(maxBufferSize);

		if (aData == NULL || aDataLen == 0)
			return INVALID_PARAMETER;

		MemoryFile *mf = new MemoryFile();
		int res = mf->openMem(aData, aDataLen, aCopy, aTakeOwnership);
		if (res != SO_NO_ERROR)
		{
			delete mf;
			return res;
		}

		res = parse(mf);

		if (res != SO_NO_ERROR)
		{
			delete mf;
			return res;
		}

		mMemFile = mf;

		return 0;
	}

	void BufferStream::addData(const void *data, unsigned int aDataLen)
	{
		// add to buffer
		switch (mPCMformat.dataType)
		{
		case 0:
			mBuffer.addData((float*)data, mSampleCount * mChannels);
			break;
		case 1:
			mBuffer.addData((int8_t*)data, mSampleCount * mChannels);
			break;
		case 2:
			mBuffer.addData((int16_t*)data, mSampleCount * mChannels);
			break;
		case 3:
			mBuffer.addData((int32_t*)data, mSampleCount * mChannels);
			break;
		}
		mSampleCount = aDataLen / mPCMformat.bytesPerSample / mChannels;
	}

	result BufferStream::parse(File *aFile)
	{
		int tag = aFile->read32();
		int res = SO_NO_ERROR;
		if (tag == MAKEDWORD('O', 'g', 'g', 'S'))
		{
			res = loadogg(aFile);
		}
		else
		if (tag == MAKEDWORD('R', 'I', 'F', 'F'))
		{
			res = loadwav(aFile);
		}
		else
		if (tag == MAKEDWORD('f', 'L', 'a', 'C'))
		{
			res = loadflac(aFile);
		}
		else
		if (loadmp3(aFile) == SO_NO_ERROR)
		{
			res = SO_NO_ERROR;
		}
		else
		{
			// Treat as a PCM buffer stream
			res = loadpcm(aFile, mPCMformat);
		}
		return res;
	}

	AudioSourceInstance *BufferStream::createInstance()
	{
		return new BufferStreamInstance(this);
	}

	double BufferStream::getLength()
	{
		if (mBaseSamplerate == 0)
			return 0;
		return mSampleCount / mBaseSamplerate;
	}
};
