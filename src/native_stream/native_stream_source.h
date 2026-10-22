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
	/// opens its own decoder on the file once the voice first plays or seeks
	/// (a voice that stays paused holds no codec), and keeps a ~1 s PCM ring
	/// filled; [getAudio] and [seek] only read the ring and post requests, so
	/// the audio thread never touches the codec or the file.
	///
	/// The first frames of the file come from the source's head cache, which
	/// covers the worker's start-up and loops back to the start.
	///
	/// When the worker falls behind, what happens depends on why:
	///  * Waiting for the first frames of a new request (start-up past the
	///    head cache, or a seek outside what is buffered): the voice holds its
	///    position and plays silence, so nothing after the seek target is
	///    skipped. Its stream position holds too. After a second it gives up
	///    waiting and keeps time instead, so a stuck decoder can't hold a
	///    voice forever.
	///  * An underrun in the middle of playback: the voice plays silence but
	///    keeps time, so it stays in step with other voices and the engine
	///    clock, and the frames it missed are skipped.
	///
	/// With the engine's render-ahead ring enabled, the ring also keeps a
	/// history of consumed frames covering the retroactive re-mix window, so
	/// checkpoints can rewind a voice exactly (see captureSourceState()).
	class NativeStreamInstance : public AudioSourceInstance
	{
	public:
		/// [aHistoryFrames] consumed frames are kept for checkpoint rollbacks.
		NativeStreamInstance(NativeStream *aParent, uint64_t aHistoryFrames);
		/// Never waits for the worker: it is told to stop and finishes (and
		/// closes its decoder) on its own once its current chunk is done.
		~NativeStreamInstance() override;

		unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize) override;
		result seek(time aSeconds, float *aScratch, unsigned int aScratchSize) override;
		result rewind() override;
		bool hasEnded() override;
		SourceStateSnapshot *captureSourceState() override;
		void restoreSourceState(SourceStateSnapshot *aState) override;
		bool canRestoreSourceState(SourceStateSnapshot *aState) override;

		/// Frames playable from the current position without an underrun
		/// (test/diagnostics only).
		uint64_t bufferedFrames() const;

	private:
		void publishLoopHints();
		void activate();
		void requestFrame(uint64_t aFrame);
		void release();
		void kickIfLow(unsigned int aFrames);
		bool findBuffered(uint64_t aFrame, uint64_t &aAvailable, bool &aComing) const;
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
		bool mWaiting = true;      // no frame of the current request played yet
		uint64_t mWaitFrames = 0;  // frames held back while waiting
		bool mFailed = false;      // the worker failed and its frames are played
		bool mActive = false;      // the worker was told to open its decoder
		uint64_t mKickDue = 0;     // frames until the next worker wakeup

		// Ring positions and markers before these may be overwritten by the
		// worker. They trail the read position by up to mHistoryFrames so
		// checkpoint snapshots can rewind into consumed frames.
		const uint64_t mHistoryFrames;
		uint64_t mRetainPos = 0;
		uint64_t mRetainMarker = 0;
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
		/// The file stays open until the source and its voices are gone, and
		/// every voice decodes that same file, even if [aFilename] is deleted
		/// or replaced in the meantime.
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
		/// Times a worker woke from waiting for work (test/diagnostics only).
		static uint64_t workerWakeupCount();

		std::shared_ptr<const native_stream_detail::Shared> mShared;
	};
}

#endif // NATIVE_STREAM_SOURCE_H
