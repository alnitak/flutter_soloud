#ifndef NATIVE_STREAM_SOURCE_H
#define NATIVE_STREAM_SOURCE_H

#include "native_stream_decoder.h"
#include "soloud.h"

#include <cstdint>
#include <memory>
#include <string>

namespace SoLoud
{
	class NativeStream;

	namespace native_stream_detail
	{
		struct Shared;
		struct Channel;
	}

	/// One voice of a [NativeStream]. A worker thread owned by this instance
	/// opens its own decoder on the file and keeps a ~1 s PCM ring filled;
	/// [getAudio] and [seek] only read the ring and post requests, so the
	/// audio thread never touches the codec or the file.
	///
	/// The first frames of the file come from the source's head cache, which
	/// covers the worker's start-up and loops back to the start.
	class NativeStreamInstance : public AudioSourceInstance
	{
	public:
		explicit NativeStreamInstance(NativeStream *aParent);
		/// Never waits for the worker: it is told to stop and finishes (and
		/// closes its decoder) on its own once its current chunk is done.
		~NativeStreamInstance() override;

		unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize) override;
		result seek(time aSeconds, float *aScratch, unsigned int aScratchSize) override;
		result rewind() override;
		bool hasEnded() override;

		/// Frames playable from the current position without an underrun
		/// (test/diagnostics only).
		uint64_t bufferedFrames() const;

	private:
		void publishLoopHints();
		void requestFrame(uint64_t aFrame);
		bool findBuffered(uint64_t aFrame, uint64_t &aAvailable, bool &aComing) const;
		bool isBuffered(uint64_t aFrame) const;
		uint64_t locate(uint64_t aFrame, bool &aEndOfStream, bool &aStale);

		std::shared_ptr<const native_stream_detail::Shared> mShared;
		std::shared_ptr<native_stream_detail::Channel> mChannel;

		// Consumer state. getAudio() and seek() are serialized by the
		// engine's audio mutex, so these need no synchronization.
		uint64_t mFrame = 0;       // next frame to output
		bool mHeadActive = true;   // mFrame is served from the head cache
		bool mEnded = false;
		uint32_t mGeneration = 0;  // current request generation
		uint64_t mMarkerIndex = 0; // run marker containing mReadPos
		uint64_t mReadPos = 0;     // absolute ring read position
	};

	/// Streams an M4A/MP4 file from disk for `LoadMode.disk`: loading only
	/// reads the container metadata and decodes a short head cache, and each
	/// voice decodes the rest on demand on its own worker thread.
	class NativeStream : public AudioSource
	{
	public:
		NativeStream();
		~NativeStream() override;

		/// Opens [aFilename] with the platform decoder backend. Blocks on file
		/// I/O and codec start-up, so call it outside the engine's locks.
		result load(const char *aFilename);
		/// Same as [load] with a custom decoder backend (used by tests).
		result load(const char *aFilename, NativeStreamDecoderFactory aFactory);

		AudioSourceInstance *createInstance() override;
		time getLength();
		uint64_t getFrameCount() const;

		/// Worker threads still running, including ones whose voice already
		/// stopped and that are finishing their current chunk.
		static int liveWorkerCount();
		/// Frames output as silence because the worker fell behind.
		static uint64_t underrunFrameCount();

		std::shared_ptr<const native_stream_detail::Shared> mShared;
	};
}

#endif // NATIVE_STREAM_SOURCE_H
