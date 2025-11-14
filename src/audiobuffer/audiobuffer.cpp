#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <mutex>

// #if defined(_IS_WIN_)
// #define NOMINMAX
// #include <windows.h>
// #include <windows.h>
// #endif

#include "../common.h"
#include "audiobuffer.h"
#include "metadata_ffi.h"
#include "../common.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// TODO: readSamplesFromBuffer as for waveform

namespace SoLoud
{
	std::mutex buffer_lock_mutex;
	std::mutex check_buffer_mutex;

	BufferStreamInstance::BufferStreamInstance(BufferStream *aParent)
	{
		mParent = aParent;
		mOffset = 0;
		samplerateAlreadySet = false;
	}

	BufferStreamInstance::~BufferStreamInstance()
	{
	}

	unsigned int BufferStreamInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{
		std::lock_guard<std::mutex> lock(buffer_lock_mutex);

		// When using BufferType::AUTO, samplerate and channels are got from the stream. Hence we need to update them
		// regardless of how are set by setBufferStream. But these parameters need to be set after the play
		// function is called and the instance of this class is created.
		if (!samplerateAlreadySet && mParent->autoTypeSamplerate != 0.f) {
			mBaseSamplerate = mParent->autoTypeSamplerate;
			mSamplerate = mParent->autoTypeSamplerate;
			mChannels = mParent->autoTypeChannels;
			samplerateAlreadySet = true;
		}

		// This happens when using RELEASED buffer type
		if (mParent->mBuffer.getFloatsBufferSize() == 0)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead);
			// Calculate mStreamPosition based on mOffset
			mStreamPosition = mOffset / (float)(mBaseSamplerate * mChannels);

			// This is not nice to do in the audio callback, but I didn't
			// find a better way to get lenght and pause the sound and the
			// `checkBuffering` function is fast enough.
			if (!mParent->mIsBuffering)
			{
				mParent->mThePlayer->soloud.unlockAudioMutex_internal();
				mParent->checkBuffering(0);
				mParent->mThePlayer->soloud.lockAudioMutex_internal();
			}
			return 0;
		}

		unsigned int bufferSize = mParent->mBuffer.getFloatsBufferSize();
		float *buffer = reinterpret_cast<float *>(mParent->mBuffer.buffer.data());
		int samplesToRead = aSamplesToRead;
		if (mOffset + (unsigned int)samplesToRead * mChannels > bufferSize)
		{
			samplesToRead = (bufferSize - mOffset) / mChannels;
		}
		if (samplesToRead <= 0)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead * mChannels);
			// Calculate mStreamPosition based on mOffset
			mStreamPosition = mOffset / (float)(mBaseSamplerate * mChannels);

			// This is not nice to do in the audio callback, but I didn't
			// find a better way to get lenght and pause the sound and the
			// `chakeBuffering` function is fast enough.
			if (!mParent->mIsBuffering)
			{
				mParent->mThePlayer->soloud.unlockAudioMutex_internal();
				mParent->checkBuffering(0);
				mParent->mThePlayer->soloud.lockAudioMutex_internal();
			}
			return 0;
		}

		if (samplesToRead != aSamplesToRead)
		{
			memset(aBuffer, 0, sizeof(float) * aSamplesToRead * mChannels);
		}

		if (mChannels == 1)
		{
			// Optimization: if we have a mono audio source, we can just copy all the data in one go.
			memcpy(aBuffer, buffer + mOffset, sizeof(float) * samplesToRead);
		}
		else
		{
			// From SoLoud documentation:
			// So, if 1024 samples are requested from a stereo audio source, the first 1024 floats
			// should be for the first channel, and the next 1024 samples should be for the second channel.
			unsigned int i, j;
			for (j = 0; j < mChannels; j++)
			{
				for (i = 0; i < samplesToRead; i++)
				{
					aBuffer[j * aSamplesToRead + i] = buffer[mOffset + i * mChannels + j];
				}
			}
		}

		unsigned int totalBytesRead = samplesToRead * mChannels * sizeof(float);
		size_t samplesRemoved = mParent->mBuffer.removeData(totalBytesRead);

		// Update stream position regardless of buffering type
		mStreamTime += samplesToRead / (float)mBaseSamplerate;

		// If buffering type is RELEASED, adjust mSampleCount and don't increment mOffset
		if (mParent->mBuffer.bufferingType == BufferingType::RELEASED)
		{
			mParent->mSampleCount -= samplesRemoved / mParent->mPCMformat.bytesPerSample;
			// For RELEASED type, streamPosition is always at the start of the remaining buffer
			mStreamPosition = 0;
			mParent->mBytesConsumed += totalBytesRead;
		}
		else
		{
			mOffset += samplesToRead * mChannels;
			// For PRESERVED type, streamPosition advances with the offset
			// mStreamPosition = mOffset / (float)(sizeof(float) * mBaseSamplerate * mChannels);
			mStreamPosition = mOffset / (float)(mBaseSamplerate * mChannels);
		}

		return samplesToRead;
	}

	result BufferStreamInstance::seek(double aSeconds, float *mScratch, unsigned int mScratchSize)
	{
		if (aSeconds <= 0.0)
		{
			rewind();
			return SO_NO_ERROR;
		}
		if (mParent->mBuffer.bufferingType == BufferingType::RELEASED)
		{
			// Seeking not supported in RELEASED mode since data is discarded
			// TODO: Support seeking forward in RELEASED mode
			return INVALID_PARAMETER;
		}
		double offset = aSeconds - mStreamPosition;
		if (offset <= 0)
		{
			if (rewind() != SO_NO_ERROR)
			{
				// can't do generic seek backwards unless we can rewind.
				return NOT_IMPLEMENTED;
			}
			offset = aSeconds;
		}
		long samples_to_discard = (long)floor(mBaseSamplerate * offset * mChannels);

		while (samples_to_discard)
		{
			long samples = mScratchSize / mChannels;
			if (samples > samples_to_discard)
				samples = samples_to_discard;
			getAudio(mScratch, samples, samples);
			samples_to_discard -= samples;
		}
		int pos = (int)floor(mBaseSamplerate * mChannels * aSeconds);
		mOffset = pos;
		mStreamPosition = float(pos) / (float)(mBaseSamplerate * mChannels);
		return SO_NO_ERROR;
	}

	result BufferStreamInstance::rewind()
	{
		mOffset = 0;
		mStreamPosition = 0.0f;
		return 0;
	}

	bool BufferStreamInstance::hasEnded()
	{
		auto b = mParent->mBuffer.bufferingType == BufferingType::PRESERVED;
		// PRESERVED
		if (b && mParent->dataIsEnded &&
			mOffset >= mParent->mSampleCount)
		{
			return 1;
		}
		else
			// RELEASED
			if (!b && mParent->dataIsEnded && mParent->mBuffer.getFloatsBufferSize() == 0)
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

	BufferStream::BufferStream() {}

	BufferStream::~BufferStream()
	{
		// stop();
		// resetBuffer();
	}

	PlayerErrors BufferStream::setBufferStream(
		Player *aPlayer,
		ActiveSound *aParent,
		unsigned int maxBufferSize,
		BufferingType bufferingType,
		SoLoud::time bufferingTimeNeeds,
		PCMformat pcmFormat,
		dartOnBufferingCallback_t onBufferingCallback,
		dartOnMetadataCallback_t onMetadataCallback)
	{
		/// maxBufferSize must be a number divisible by channels * sizeof(float)
		if (maxBufferSize % (pcmFormat.channels * sizeof(float)) != 0)
			maxBufferSize -= maxBufferSize % (pcmFormat.channels * sizeof(float));

		// Force OPUS to AUTO since Mp3, Ogg Opus and Ogg Vorbis are auto detected
		// There is no more need to use BufferType::OPUS
		if (pcmFormat.dataType == BufferType::OPUS)
			pcmFormat.dataType = BufferType::AUTO;

		mInstance = nullptr;
		autoTypeChannels = 0;
		autoTypeSamplerate = 0.f;
		mBytesReceived = 0;
		mUncompressedBytesReceived = 0;
		mSampleCount = 0;
		mBytesConsumed = 0;
		dataIsEnded = false;
		mThePlayer = aPlayer;
		mParent = aParent;
		mPCMformat.sampleRate = pcmFormat.sampleRate;
		mPCMformat.channels = pcmFormat.channels;
		mPCMformat.bytesPerSample = pcmFormat.bytesPerSample;
		mPCMformat.dataType = pcmFormat.dataType;
		mBuffer.clear();
		mBuffer.setSizeInBytes(maxBufferSize);
		mBufferingTimeNeeds = bufferingTimeNeeds;
		mChannels = pcmFormat.channels;
		mBaseSamplerate = (float)pcmFormat.sampleRate;
		mOnBufferingCallback = onBufferingCallback;
		mOnMetadataCallback = onMetadataCallback;
		buffer = std::vector<unsigned char>();
		mBuffer.setBufferType(bufferingType);
		mIsBuffering = true;
		mIcyMetaInt = 16000; // for mp3 streaming audio only. Most online streaming use 16000

		if (pcmFormat.dataType == BufferType::AUTO)
		{
			streamDecoder = std::make_unique<StreamDecoder>();
		}

#if defined(NO_OPUS_OGG_LIBS)
		if (pcmFormat.dataType == BufferType::OPUS)
		{
			return PlayerErrors::failedToCreateOpusDecoder;
		}
#endif

		return PlayerErrors::noError;
	}

	void BufferStream::resetBuffer()
	{
		std::lock_guard<std::mutex> lock(buffer_lock_mutex);
		buffer.clear();
		mBuffer.clear();
		mSampleCount = 0;
		mBytesReceived = 0;
		mUncompressedBytesReceived = 0;

		for (int i = 0; i < mParent->handle.size(); i++)
		{
			mThePlayer->soloud.seek(mParent->handle[i].handle, 0.0f);
			mParent->handle[i].bufferingTime = 0.0f;
		}
	}

	void BufferStream::setDataIsEnded()
	{
		// Eventually add any remaining data
		if (buffer.size() > 0)
		{
			addData(nullptr, 0, true);
		}

		buffer.clear();
		dataIsEnded = true;
		checkBuffering(0);
	}

	void BufferStream::setBufferIcyMetaInt(int icyMetaInt)
	{
		mIcyMetaInt = icyMetaInt;
		streamDecoder->setBufferIcyMetaInt(icyMetaInt);
	}

	PlayerErrors BufferStream::addData(const void *aData, unsigned int aDataLen, bool dontAdd)
	{
		if (dataIsEnded)
		{
			return PlayerErrors::streamEndedAlready;
		}

		size_t bytesWritten = 0;
		bool allDataAdded = -1;
		int32_t bufferDataToAdd = 0;

		if (!dontAdd)
		{
			buffer.insert(buffer.end(),
						  static_cast<const unsigned char *>(aData),
						  static_cast<const unsigned char *>(aData) + aDataLen);
			mBytesReceived += aDataLen;
			// For PCM data we must align the data to the bytes per sample.
			if (!(mPCMformat.dataType == BufferType::AUTO))
			{
				int alignment = mPCMformat.bytesPerSample * mPCMformat.channels;
				bufferDataToAdd = (int)(buffer.size() / alignment) * alignment;
			}
			else
			{
				// Performing some buffering. We need some data to be added expecially when using opus or mp3.
				if (buffer.size() > 1024 * 32) // 32 KB of data.
				{
					// When using opus,ogg or mp3 we don't need to align.
					bufferDataToAdd = buffer.size();
				}
				else
				{
					// Return if there is not enough data to add.
					return PlayerErrors::noError;
				}
			}
			
		}
		else
		{
			bufferDataToAdd = buffer.size();
		}

		// It's time to decode the data already stored in the buffer
		if (mPCMformat.dataType == BufferType::AUTO)
		{
			int sampleRate = mThePlayer->mSampleRate;
			int channels = mThePlayer->mChannels;
			// Ogg Opus will decode to the sampleRate and channels of the current engine settings and
			// the AudioSource will be set to use them.
			// For the mp3 this AudioSource will impose the mp3 settings (the engine will convert to its settings).
			auto [decoded, error] = streamDecoder->decode(buffer, &sampleRate, &channels,
					[&](AudioMetadata meta)
					{
					//   meta.debug();
						if (this->mOnMetadataCallback != nullptr)
							this->callOnMetadataCallback(meta);
					});

			// Handle decoder errors
			switch (error)
			{
			case DecoderError::FormatNotSupported:
				return PlayerErrors::audioFormatNotSupported;
			case DecoderError::NoOpusOggLibs:
				return PlayerErrors::opusOggVorbisLibsNotFound;
			case DecoderError::FailedToCreateDecoder:
				return PlayerErrors::failedToCreateOpusDecoder;
			case DecoderError::ErrorReadingOggOpusPage:
				return PlayerErrors::failedToDecodeOpusPacket;
			default:
				break;
			}

			if (!decoded.empty())
			{
				if (autoTypeSamplerate == 0.f)
				{
					if (sampleRate != -1)
					{
						mPCMformat.sampleRate = sampleRate;
						mBaseSamplerate = sampleRate;
						autoTypeSamplerate = sampleRate;
					}
					if (channels != -1)
					{
						mPCMformat.channels = channels;
						mChannels = channels;
						autoTypeChannels = channels;
					}
				}

				bytesWritten = mBuffer.addData(
								   BufferType::PCM_F32LE,
								   decoded.data(),
								   decoded.size(),
								   &allDataAdded) *
							   sizeof(float);
			}
			else
			{
				// Continue buffering. Maybe we are still adding artwork image data.
				return PlayerErrors::noError;
			}
		}
		else
		{
			// PCM data
			bytesWritten = mBuffer.addData(
							   mPCMformat.dataType,
							   buffer.data(),
							   bufferDataToAdd / mPCMformat.bytesPerSample,
							   &allDataAdded) *
						   mPCMformat.bytesPerSample;
			// Remove the processed data from the buffer
			if (bytesWritten > 0)
			{
				buffer.erase(buffer.begin(), buffer.begin() + bufferDataToAdd);
			}
		}

		if (mIsBuffering)
			checkBuffering(bytesWritten);
		mUncompressedBytesReceived += bytesWritten;

		mSampleCount += bytesWritten / mPCMformat.bytesPerSample;

		// data has been added to the buffer, but not all because reached its full capacity.
		// So mark this stream as ended and no more data can be added.
		if (!allDataAdded)
		{
			dataIsEnded = true;
			return PlayerErrors::pcmBufferFull;
		}

		return PlayerErrors::noError;
	}

	/// Check if some handles was paused for buffering and unpause them or restart them
	/// if needed after adding [afterAddingBytesCount] bytes.
	void BufferStream::checkBuffering(unsigned int afterAddingBytesCount)
	{
		std::lock_guard<std::mutex> lock(check_buffer_mutex);

		// If a handle reaches the end and data is not ended, we have to wait for it has enough data
		// to reach [TIME_FOR_BUFFERING] and restart playing it.
		SoLoud::time currBufferTime = getLength();
		SoLoud::time addedDataTime = (afterAddingBytesCount / mPCMformat.bytesPerSample) / (mBaseSamplerate * mChannels);

		for (int i = 0; i < mParent->handle.size(); i++)
		{
			SoLoud::handle handle = mParent->handle[i].handle;
			SoLoud::time pos = mBuffer.bufferingType == BufferingType::RELEASED ? getStreamTimeConsumed() : mThePlayer->getPosition(handle);
			// SoLoud::time pos = mThePlayer->getPosition(handle);
			bool isPaused = mThePlayer->getPause(handle);

			// printf("checkBuffering -- bufferLength: %lf, currBufferTime: %lf\n",
			// 	bufferLength, currBufferTime);

			// This handle needs to wait for [TIME_FOR_BUFFERING]. Pause it.
			if (pos >= currBufferTime + addedDataTime && !isPaused)
			{
				mParent->handle[i].bufferingTime = currBufferTime;
				mThePlayer->setPause(handle, true);
				isPaused = true;
				callOnBufferingCallback(true, handle, currBufferTime);
			}
			else
				// This handle has reached [TIME_FOR_BUFFERING]. Unpause it.
				if (currBufferTime + addedDataTime - mParent->handle[i].bufferingTime >= mBufferingTimeNeeds && isPaused)
				{
					mThePlayer->setPause(handle, false);
					isPaused = false;
					mParent->handle[i].bufferingTime = currBufferTime + addedDataTime;
					callOnBufferingCallback(false, handle, currBufferTime);
				}
			// If data is ended and the handle is paused, unpause it to listen to the rest of the data.
			if (dataIsEnded && isPaused)
			{
				mThePlayer->setPause(handle, false);
				isPaused = false;
				mParent->handle[i].bufferingTime = MAX_DOUBLE;
				callOnBufferingCallback(false, handle, currBufferTime);
			}
		}
	}

	void BufferStream::callOnMetadataCallback(AudioMetadata &metadata)
	{
		if (mOnMetadataCallback != nullptr)
		{
			AudioMetadataFFI ffi = this->convertMetadataToFFI(metadata);
			// metadata.debug();
#ifdef __EMSCRIPTEN__
			// Call the Dart callback stored on globalThis, if it exists.
			// The `dartOnMetadataCallback_$hash` function is created in
			// `setBufferStream()` in `bindings_player_web.dart` and it's
			// meant to call the Dart callback passed to `setBufferStream()`.
			// It will pass the JS pointer to the AudioMetadata struct.
			EM_ASM_({
				// Compose the function name for this soundHash
				var functionName = "dartOnMetadataCallback_" + $1;
				if (typeof window[functionName] === "function") {
					window[functionName]($0); // Call it with the pointer
				} else {
				} }, &ffi, mParent->soundHash);
#else
			mOnMetadataCallback(ffi);
#endif
		}
	}

	void BufferStream::callOnBufferingCallback(bool isBuffering, unsigned int handle, double time)
	{
		if (mOnBufferingCallback != nullptr)
		{
#ifdef __EMSCRIPTEN__
			// Call the Dart callback stored on globalThis, if it exists.
			// The `dartOnBufferingCallback_$hash` function is created in
			// `setBufferStream()` in `bindings_player_web.dart` and it's
			// meant to call the Dart callback passed to `setBufferStream()`.
			EM_ASM({
					// Compose the function name for this soundHash
					var functionName = "dartOnBufferingCallback_" + $3;
					if (typeof window[functionName] === "function") {
						var buffering = $0 == 1 ? true : false;
						window[functionName](buffering, $1, $2); // Call it
					} else {
						console.log("EM_ASM 'dartOnBufferingCallback_$hash' not found.");
					} }, isBuffering, handle, time, mParent->soundHash);
#else
			mOnBufferingCallback(isBuffering, handle, time);
#endif
		}
		mIsBuffering = isBuffering;
	}

	BufferingType BufferStream::getBufferingType()
	{
		return mBuffer.bufferingType;
	}

	SoLoud::time BufferStream::getLength()
	{
		if (mBaseSamplerate == 0 || mUncompressedBytesReceived == 0 || mPCMformat.bytesPerSample == 0)
			return 0;
		return (mUncompressedBytesReceived / mPCMformat.bytesPerSample) / (mBaseSamplerate * mPCMformat.channels);
	}

	/// Get the time consumed by this stream of type RELEASED
	SoLoud::time BufferStream::getStreamTimeConsumed()
	{
		return (double)(mBytesConsumed / mPCMformat.bytesPerSample) / (mBaseSamplerate * mPCMformat.channels);
	}

	AudioMetadataFFI BufferStream::convertMetadataToFFI(const AudioMetadata &metadata)
	{
		AudioMetadataFFI ffi = {};

		// Set detected type
		switch (metadata.type)
		{
		case BUFFER_OGG_OPUS:
			ffi.detectedType = DetectedTypeFFI::OGG_OPUS;
			break;
		case BUFFER_OGG_VORBIS:
			ffi.detectedType = DetectedTypeFFI::OGG_VORBIS;
			break;
		case BUFFER_OGG_FLAC:
			ffi.detectedType = DetectedTypeFFI::OGG_FLAC;
			break;
		case BUFFER_MP3_WITH_ID3:
			ffi.detectedType = DetectedTypeFFI::MP3_WITH_ID3;
			break;
		case BUFFER_MP3_STREAM:
			ffi.detectedType = DetectedTypeFFI::MP3_STREAM;
			break;
		default:
			ffi.detectedType = DetectedTypeFFI::UNKNOWN;
		}

		// Convert MP3 metadata
		strncpy(ffi.mp3Metadata.title, metadata.mp3Metadata.title.c_str(), MAX_STRING_LENGTH - 1);
		strncpy(ffi.mp3Metadata.artist, metadata.mp3Metadata.artist.c_str(), MAX_STRING_LENGTH - 1);
		strncpy(ffi.mp3Metadata.album, metadata.mp3Metadata.album.c_str(), MAX_STRING_LENGTH - 1);
		strncpy(ffi.mp3Metadata.date, metadata.mp3Metadata.date.c_str(), MAX_STRING_LENGTH - 1);
		strncpy(ffi.mp3Metadata.genre, metadata.mp3Metadata.genre.c_str(), MAX_STRING_LENGTH - 1);

		// Convert OGG metadata
		strncpy(ffi.oggMetadata.vendor, metadata.oggMetadata.vendor.c_str(), MAX_STRING_LENGTH - 1);
		ffi.oggMetadata.commentsCount = MIN((int)metadata.oggMetadata.comments.size(), MAX_COMMENTS);

		int i = 0;
		for (const auto &comment : metadata.oggMetadata.comments)
		{
			if (i >= MAX_COMMENTS)
				break;
			strncpy(ffi.oggMetadata.comments[i].key, comment.first.c_str(), MAX_STRING_LENGTH - 1);
			strncpy(ffi.oggMetadata.comments[i].value, comment.second.c_str(), MAX_STRING_LENGTH - 1);
			i++;
		}

		// Convert Vorbis info
		ffi.oggMetadata.vorbisInfo = {
			metadata.oggMetadata.vorbisInfo.version,
			metadata.oggMetadata.vorbisInfo.channels,
			metadata.oggMetadata.vorbisInfo.rate,
			metadata.oggMetadata.vorbisInfo.bitrate_upper,
			metadata.oggMetadata.vorbisInfo.bitrate_nominal,
			metadata.oggMetadata.vorbisInfo.bitrate_lower,
			metadata.oggMetadata.vorbisInfo.bitrate_window};

		// Convert Opus info
		ffi.oggMetadata.opusInfo = {
			metadata.oggMetadata.opusInfo.version,
			metadata.oggMetadata.opusInfo.channels,
			metadata.oggMetadata.opusInfo.pre_skip,
			metadata.oggMetadata.opusInfo.input_sample_rate,
			metadata.oggMetadata.opusInfo.output_gain,
			metadata.oggMetadata.opusInfo.mapping_family,
			metadata.oggMetadata.opusInfo.stream_count,
			metadata.oggMetadata.opusInfo.coupled_count,
			{0}, // Initialize channel_mapping array to zeros
			(int)metadata.oggMetadata.opusInfo.channel_mapping.size()};

		// Convert Flac info
		ffi.oggMetadata.flacInfo = {
			metadata.oggMetadata.flacInfo.min_blocksize,
			metadata.oggMetadata.flacInfo.max_blocksize,
			metadata.oggMetadata.flacInfo.min_framesize,
			metadata.oggMetadata.flacInfo.max_framesize,
			metadata.oggMetadata.flacInfo.sample_rate,
			metadata.oggMetadata.flacInfo.channels,
			metadata.oggMetadata.flacInfo.bits_per_sample,
			metadata.oggMetadata.flacInfo.total_samples};

		// Copy channel mapping
		for (size_t i = 0; i < metadata.oggMetadata.opusInfo.channel_mapping.size() && i < MAX_CHANNEL_MAPPING; i++)
		{
			ffi.oggMetadata.opusInfo.channel_mapping[i] = metadata.oggMetadata.opusInfo.channel_mapping[i];
		}

		return ffi;
	}

	AudioSourceInstance *BufferStream::createInstance()
	{
		mInstance = new BufferStreamInstance(this);
		return mInstance;
	}
};
