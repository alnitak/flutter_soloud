#include "native_stream_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#if !NATIVE_STREAM_SUPPORTED
std::unique_ptr<NativeStreamDecoder> openPlatformNativeStreamDecoder(const std::string &, std::string &error)
{
	error = "Streaming M4A/MP4 decoding is not available on this platform";
	return nullptr;
}
#endif

namespace SoLoud
{
	namespace native_stream_detail
	{
		// Decoded at load time and played while a voice's worker starts up,
		// and again whenever a voice seeks or loops back into it.
		constexpr double kHeadSeconds = 0.75;
		// Per-voice ring of decoded PCM.
		constexpr double kRingSeconds = 1.0;
		// Frames decoded per worker step.
		constexpr unsigned int kChunkFrames = 2048;
		// Pending run markers per voice (seeks and loop wraps in flight).
		constexpr unsigned int kMarkers = 64;
		// Longest a voice holds its position waiting for the first frames of
		// a request before it keeps time again (see NativeStreamInstance).
		constexpr double kMaxWaitSeconds = 1.0;
		// Margin on the checkpoint history for voices playing faster than
		// their natural rate. Rollbacks past the history degrade to
		// going-forward behavior.
		constexpr double kHistorySpeedMargin = 2.0;
		// How long an idle worker (ring full, paused voice, end of stream)
		// sleeps before checking again. A voice's new request wakes it early.
		constexpr auto kIdleWait = std::chrono::milliseconds(20);

		// A request is a frame and a generation packed in one atomic word,
		// so the worker always reads a consistent pair.
		constexpr unsigned int kFrameBits = 40;
		constexpr uint64_t kFrameMask = (uint64_t(1) << kFrameBits) - 1;
		constexpr uint32_t kGenerationMask = (uint32_t(1) << (64 - kFrameBits)) - 1;

		std::atomic<int> gLiveWorkers{0};
		std::atomic<uint64_t> gUnderrunFrames{0};

		/// Immutable after [NativeStream::load]; shared with instances and
		/// their workers, so it outlives the source if a worker is still
		/// finishing.
		struct Shared
		{
			std::string path;
			NativeStreamDecoderFactory factory;
			unsigned int channels = 0;
			float sampleRate = 0;
			uint64_t totalFrames = 0;
			uint64_t headFrames = 0;
			std::vector<float> head; // interleaved
		};

		/// Where a run of contiguous frames starts in the ring. A run begins
		/// at every seek and loop wrap; an end marker carries no frames and
		/// says the stream ends at [frame].
		struct Marker
		{
			uint64_t ringPos = 0;
			uint64_t frame = 0;
			uint32_t generation = 0;
			bool end = false;
		};

		/// The single-producer (worker) / single-consumer (voice) ring and
		/// the requests the voice posts to the worker.
		struct Channel
		{
			Channel(unsigned int aChannels, uint64_t aCapacity)
				: channels(aChannels), capacity(aCapacity), ring(new float[aChannels * aCapacity]) {}

			const unsigned int channels;
			const uint64_t capacity; // frames
			std::unique_ptr<float[]> ring;
			std::atomic<uint64_t> writePos{0};
			// The worker may overwrite ring frames before readPos and marker
			// slots before markerRead: the voice's history start, not its
			// read position.
			std::atomic<uint64_t> readPos{0};

			Marker markers[kMarkers];
			std::atomic<uint64_t> markerWrite{0};
			std::atomic<uint64_t> markerRead{0};

			std::atomic<uint64_t> request{0}; // generation << kFrameBits | frame
			std::atomic<bool> looping{false};
			std::atomic<uint64_t> loopStart{0};
			std::atomic<uint64_t> loopEnd{0}; // 0: the natural end
			std::atomic<bool> stop{false};
			std::atomic<bool> failed{false};

			std::mutex wakeMutex;
			std::condition_variable wake;

			void notify() { wake.notify_one(); }
		};

		/// A voice's consumer state at a mix checkpoint. The frames after it
		/// are still in the ring while it lies within the voice's history.
		struct NativeStreamSnapshot : SourceStateSnapshot
		{
			uint64_t frame = 0;
			bool headActive = true;
			bool ended = false;
			uint32_t generation = 0;
			uint64_t markerIndex = 0;
			uint64_t readPos = 0;
			bool waiting = true;
			uint64_t waitFrames = 0;
		};

		uint64_t packRequest(uint32_t aGeneration, uint64_t aFrame)
		{
			return (uint64_t(aGeneration & kGenerationMask) << kFrameBits) | (aFrame & kFrameMask);
		}

		/// Decodes into the ring until told to stop. Owns the decoder, so the
		/// codec and the file are only ever touched on this thread.
		void workerMain(std::shared_ptr<const Shared> aShared, std::shared_ptr<Channel> aChannel)
		{
			const Shared &shared = *aShared;
			Channel &ch = *aChannel;
			const unsigned int channels = shared.channels;

			std::string error;
			std::unique_ptr<NativeStreamDecoder> decoder = shared.factory(shared.path, error);
			if (!decoder || decoder->format().channels != channels)
			{
				ch.failed.store(true, std::memory_order_release);
				decoder.reset();
				gLiveWorkers.fetch_sub(1);
				return;
			}

			std::vector<float> chunk(size_t(kChunkFrames) * channels);
			uint64_t request = 0;
			uint32_t generation = ~0u; // nothing requested yet
			uint64_t position = 0;     // next frame the decoder returns
			uint64_t streamEnd = shared.totalFrames; // lowered if decoding ends early
			bool needRun = false;      // a run marker must precede the next frames
			bool endMarked = false;    // this run's end marker was queued
			Marker pendingEnd;
			bool needEnd = false;

			auto waitForWork = [&] {
				std::unique_lock<std::mutex> lock(ch.wakeMutex);
				ch.wake.wait_for(lock, kIdleWait, [&] {
					return ch.stop.load(std::memory_order_acquire) ||
						ch.request.load(std::memory_order_acquire) != request;
				});
			};
			auto pushMarker = [&](const Marker &aMarker) -> bool {
				const uint64_t w = ch.markerWrite.load(std::memory_order_relaxed);
				if (w - ch.markerRead.load(std::memory_order_acquire) >= kMarkers)
					return false;
				ch.markers[w % kMarkers] = aMarker;
				ch.markerWrite.store(w + 1, std::memory_order_release);
				return true;
			};
			auto seekTo = [&](uint64_t aFrame) -> bool {
				position = aFrame;
				needRun = true;
				endMarked = false;
				needEnd = false;
				return decoder->seek(aFrame);
			};

			while (!ch.stop.load(std::memory_order_acquire))
			{
				request = ch.request.load(std::memory_order_acquire);
				const uint32_t requestGeneration = uint32_t(request >> kFrameBits);
				if (requestGeneration != generation)
				{
					generation = requestGeneration;
					if (!seekTo(request & kFrameMask))
					{
						ch.failed.store(true, std::memory_order_release);
						break;
					}
				}

				// Markers go out before the frames they describe.
				if (needEnd)
				{
					if (!pushMarker(pendingEnd))
					{
						waitForWork();
						continue;
					}
					needEnd = false;
				}

				const bool looping = ch.looping.load(std::memory_order_relaxed);
				const uint64_t loopStart = ch.loopStart.load(std::memory_order_relaxed);
				const uint64_t loopEnd = ch.loopEnd.load(std::memory_order_relaxed);
				const bool boundedLoop = looping && loopEnd > loopStart && loopEnd < streamEnd;
				// A run that reached loopEnd stops there (and wraps below); one
				// already past it (loop set late) continues to the stream end.
				const uint64_t limit = (boundedLoop && position <= loopEnd) ? loopEnd : streamEnd;

				if (position >= limit)
				{
					if (limit == streamEnd && !endMarked)
					{
						pendingEnd = Marker{ch.writePos.load(std::memory_order_relaxed), position, generation, true};
						needEnd = true;
						endMarked = true;
						continue;
					}
					// Wrap a looping run without waiting for the voice to ask, so
					// loops are gapless. Frames inside the head cache are
					// played from there.
					const uint64_t wrapTo = std::max(loopStart, shared.headFrames);
					if (looping && wrapTo < (boundedLoop ? loopEnd : streamEnd))
					{
						if (!seekTo(wrapTo))
						{
							ch.failed.store(true, std::memory_order_release);
							break;
						}
						continue;
					}
					// Not looping, or the whole loop is in the head cache. Loop
					// settings are re-read on the next pass.
					waitForWork();
					continue;
				}

				const uint64_t writePos = ch.writePos.load(std::memory_order_relaxed);
				const uint64_t freeFrames = ch.capacity - (writePos - ch.readPos.load(std::memory_order_acquire));
				const uint64_t want = std::min<uint64_t>({kChunkFrames, freeFrames, limit - position});
				if (want < std::min<uint64_t>(kChunkFrames, limit - position))
				{
					waitForWork();
					continue;
				}
				if (needRun)
				{
					if (!pushMarker(Marker{writePos, position, generation, false}))
					{
						waitForWork();
						continue;
					}
					needRun = false;
				}

				const long got = decoder->read(chunk.data(), unsigned(want));
				if (got <= 0)
				{
					// End of the decodable stream (or a decoder error) before the
					// container's length: the stream ends here.
					streamEnd = position;
					continue;
				}

				const uint64_t frames = std::min<uint64_t>(uint64_t(got), want);
				const uint64_t start = writePos % ch.capacity;
				const uint64_t first = std::min<uint64_t>(frames, ch.capacity - start);
				std::memcpy(ch.ring.get() + start * channels, chunk.data(), size_t(first * channels) * sizeof(float));
				if (frames > first)
				{
					std::memcpy(ch.ring.get(), chunk.data() + first * channels,
						size_t((frames - first) * channels) * sizeof(float));
				}
				ch.writePos.store(writePos + frames, std::memory_order_release);
				position += frames;
			}

			// The decoder (and with it the file and codec) is released here,
			// on the worker, never on the thread that stopped the voice.
			decoder.reset();
			gLiveWorkers.fetch_sub(1);
		}
	}

	using namespace native_stream_detail;

	NativeStreamInstance::NativeStreamInstance(NativeStream *aParent, uint64_t aHistoryFrames)
		: mShared(aParent->mShared), mHistoryFrames(aHistoryFrames)
	{
		if (!mShared)
		{
			mEnded = true;
			return;
		}
		// The head cache holds the whole sound: no worker needed.
		if (mShared->headFrames >= mShared->totalFrames)
			return;

		const uint64_t capacity = std::max<uint64_t>(
			uint64_t(std::ceil(mShared->sampleRate * kRingSeconds)), uint64_t(kChunkFrames) * 2) + mHistoryFrames;
		mChannel = std::make_shared<Channel>(mShared->channels, capacity);
		// Start decoding where the head cache ends.
		mChannel->request.store(packRequest(mGeneration, mShared->headFrames), std::memory_order_relaxed);

		gLiveWorkers.fetch_add(1);
		try
		{
			std::thread(workerMain, mShared, mChannel).detach();
		}
		catch (const std::system_error &)
		{
			gLiveWorkers.fetch_sub(1);
			mChannel->failed.store(true, std::memory_order_release);
		}
	}

	NativeStreamInstance::~NativeStreamInstance()
	{
		if (mChannel)
		{
			mChannel->stop.store(true, std::memory_order_release);
			mChannel->notify();
		}
	}

	void NativeStreamInstance::publishLoopHints()
	{
		if (!mChannel)
			return;
		const bool looping = (mFlags & LOOPING) != 0;
		uint64_t loopStart = 0;
		uint64_t loopEnd = 0;
		if (looping)
		{
			loopStart = mLoopStartFrame >= 0
				? uint64_t(mLoopStartFrame)
				: (mLoopPoint > 0 ? uint64_t(std::floor(mLoopPoint * mBaseSamplerate)) : 0);
			if (mLoopEndFrame >= 0)
				loopEnd = uint64_t(mLoopEndFrame);
			else if (mLoopEndPoint > 0)
				loopEnd = uint64_t(std::ceil(mLoopEndPoint * mBaseSamplerate));
		}
		mChannel->loopStart.store(loopStart, std::memory_order_relaxed);
		mChannel->loopEnd.store(loopEnd, std::memory_order_relaxed);
		mChannel->looping.store(looping, std::memory_order_relaxed);
	}

	void NativeStreamInstance::requestFrame(uint64_t aFrame)
	{
		// Everything buffered so far is superseded: free the ring now so the
		// worker can start on the new request without waiting for the next
		// getAudio(). Markers and frames still in flight from the old request
		// land past this point and are skipped as stale.
		// The history goes too: no snapshot can be restored across a new
		// request (canRestoreSourceState() checks the generation).
		Channel &ch = *mChannel;
		mMarkerIndex = ch.markerWrite.load(std::memory_order_acquire);
		mReadPos = ch.writePos.load(std::memory_order_acquire);
		mRetainMarker = mMarkerIndex;
		mRetainPos = mReadPos;
		ch.markerRead.store(mMarkerIndex, std::memory_order_release);
		ch.readPos.store(mReadPos, std::memory_order_release);

		mWaiting = true;
		mWaitFrames = 0;
		mGeneration = (mGeneration + 1) & kGenerationMask;
		ch.request.store(packRequest(mGeneration, aFrame), std::memory_order_release);
		ch.notify();
	}

	// Lets the worker overwrite what lies more than mHistoryFrames behind the
	// read position. The history holds at most half the run markers, so many
	// short loop wraps can't keep the worker from queueing new runs.
	void NativeStreamInstance::release()
	{
		Channel &ch = *mChannel;
		uint64_t target = mReadPos > mHistoryFrames ? mReadPos - mHistoryFrames : 0;
		if (target < mRetainPos)
			target = mRetainPos;
		while (mRetainMarker < mMarkerIndex)
		{
			const uint64_t next = ch.markers[(mRetainMarker + 1) % kMarkers].ringPos;
			if (next > target && mMarkerIndex - mRetainMarker <= kMarkers / 2)
				break;
			mRetainMarker++;
			if (next > target)
				target = next;
		}
		mRetainPos = target;
		ch.markerRead.store(mRetainMarker, std::memory_order_release);
		ch.readPos.store(mRetainPos, std::memory_order_release);
	}

	// Finds [aFrame] in a run of the current generation ahead of the read
	// position. [aAvailable] is how many frames from it are in the ring now;
	// [aComing] is set when it isn't there yet but the run being written
	// reaches it soon.
	bool NativeStreamInstance::findBuffered(uint64_t aFrame, uint64_t &aAvailable, bool &aComing) const
	{
		aAvailable = 0;
		aComing = false;
		const Channel &ch = *mChannel;
		const uint64_t w = ch.writePos.load(std::memory_order_acquire);
		const uint64_t mw = ch.markerWrite.load(std::memory_order_acquire);
		for (uint64_t i = mMarkerIndex; i < mw; i++)
		{
			const Marker &m = ch.markers[i % kMarkers];
			const bool last = i + 1 >= mw;
			const uint64_t runEnd = last ? w : ch.markers[(i + 1) % kMarkers].ringPos;
			if (m.generation != mGeneration || m.end || aFrame < m.frame)
				continue;
			const uint64_t pos = m.ringPos + (aFrame - m.frame);
			if (pos < std::max(mReadPos, m.ringPos))
				continue;
			if (pos < runEnd)
			{
				aAvailable = runEnd - pos;
				return true;
			}
			// The last run is still being written: a frame shortly ahead of
			// it arrives sooner than a new seek would.
			if (last && pos - runEnd < (ch.capacity - mHistoryFrames) / 2)
			{
				aComing = true;
				return false;
			}
		}
		return false;
	}

	bool NativeStreamInstance::isBuffered(uint64_t aFrame) const
	{
		uint64_t available;
		bool coming;
		return findBuffered(aFrame, available, coming) || coming;
	}

	// Moves the read position to [aFrame] within the current generation's
	// runs, dropping frames before it. Returns how many contiguous frames
	// starting at [aFrame] are readable now.
	uint64_t NativeStreamInstance::locate(uint64_t aFrame, bool &aEndOfStream, bool &aStale)
	{
		aEndOfStream = false;
		aStale = false;
		Channel &ch = *mChannel;
		const uint64_t w = ch.writePos.load(std::memory_order_acquire);
		const uint64_t mw = ch.markerWrite.load(std::memory_order_acquire);
		uint64_t result = 0;
		while (mMarkerIndex < mw)
		{
			const Marker &m = ch.markers[mMarkerIndex % kMarkers];
			const bool last = mMarkerIndex + 1 >= mw;
			const uint64_t runEnd = last ? w : ch.markers[(mMarkerIndex + 1) % kMarkers].ringPos;
			if (mReadPos < m.ringPos)
				mReadPos = m.ringPos;

			if (m.generation == mGeneration)
			{
				if (m.end)
				{
					if (aFrame >= m.frame)
					{
						aEndOfStream = true;
						break;
					}
					if (last)
					{
						// The worker stopped at the end before reaching back to
						// [aFrame] (looping was enabled late, say).
						aStale = true;
						break;
					}
				}
				else if (aFrame >= m.frame)
				{
					const uint64_t pos = m.ringPos + (aFrame - m.frame);
					if (pos >= mReadPos && pos < runEnd)
					{
						mReadPos = pos;
						result = runEnd - pos;
						break;
					}
					if (last && pos >= runEnd)
					{
						// Not decoded yet: drop what precedes it and wait,
						// unless a seek gets there sooner.
						mReadPos = runEnd;
						aStale = pos - runEnd >= (ch.capacity - mHistoryFrames) / 2;
						break;
					}
				}
				else if (last)
				{
					// The worker is decoding past [aFrame].
					aStale = true;
					break;
				}
			}
			else if (last)
			{
				// A run of a superseded request: drop it.
				mReadPos = runEnd;
				break;
			}

			mReadPos = runEnd;
			mMarkerIndex++;
		}
		release();
		return result;
	}

	unsigned int NativeStreamInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int aBufferSize)
	{
		if (!mShared)
			return 0;
		const Shared &shared = *mShared;
		const unsigned int channels = shared.channels;
		publishLoopHints();

		unsigned int out = 0;
		while (out < aSamplesToRead)
		{
			if (mFrame >= shared.totalFrames)
			{
				mEnded = true;
				break;
			}
			const unsigned int remaining = unsigned(std::min<uint64_t>(aSamplesToRead - out, shared.totalFrames - mFrame));

			if (mHeadActive)
			{
				if (mFrame >= shared.headFrames)
				{
					mHeadActive = false;
					continue;
				}
				const unsigned int n = unsigned(std::min<uint64_t>(remaining, shared.headFrames - mFrame));
				const float *src = shared.head.data() + mFrame * channels;
				for (unsigned int i = 0; i < n; i++)
					for (unsigned int c = 0; c < channels; c++)
						aBuffer[c * aBufferSize + out + i] = src[i * channels + c];
				out += n;
				mFrame += n;
				continue;
			}

			if (!mChannel || mChannel->failed.load(std::memory_order_acquire))
			{
				mEnded = true;
				break;
			}

			bool endOfStream = false;
			bool stale = false;
			const uint64_t available = locate(mFrame, endOfStream, stale);
			if (endOfStream)
			{
				mEnded = true;
				break;
			}
			if (stale)
				requestFrame(mFrame);

			if (available == 0)
			{
				for (unsigned int c = 0; c < channels; c++)
					std::memset(aBuffer + c * aBufferSize + out, 0, remaining * sizeof(float));
				gUnderrunFrames.fetch_add(remaining, std::memory_order_relaxed);
				out += remaining;
				if (mWaiting && mWaitFrames < uint64_t(shared.sampleRate * kMaxWaitSeconds))
				{
					// The request's first frames aren't decoded yet: hold the
					// position, so a seek or start-up doesn't skip any audio.
					// The engine already advanced the stream position for
					// these frames; take that back too.
					mWaitFrames += remaining;
					mStreamPosition = std::max(0.0, mStreamPosition - double(remaining) / shared.sampleRate);
					continue;
				}
				// Underrun mid-playback: play silence but keep time moving, so
				// the voice stays in step and the frames are skipped when
				// they arrive.
				mFrame += remaining;
				continue;
			}

			const Channel &ch = *mChannel;
			const unsigned int n = unsigned(std::min<uint64_t>(remaining, available));
			for (unsigned int i = 0; i < n; i++)
			{
				const float *src = ch.ring.get() + ((mReadPos + i) % ch.capacity) * channels;
				for (unsigned int c = 0; c < channels; c++)
					aBuffer[c * aBufferSize + out + i] = src[c];
			}
			mReadPos += n;
			release();
			mWaiting = false;
			mWaitFrames = 0;
			out += n;
			mFrame += n;
		}
		return out;
	}

	result NativeStreamInstance::seek(time aSeconds, float *, unsigned int)
	{
		if (!mShared)
			return INVALID_PARAMETER;
		const Shared &shared = *mShared;
		// The epsilon undoes the rounding of frame -> seconds conversions
		// (loop start offsets arrive as frame / rate), which would otherwise
		// land one frame early.
		const double exact = aSeconds * shared.sampleRate + 1e-6;
		const uint64_t frame = !(exact > 0) ? 0
			: exact >= double(shared.totalFrames) ? shared.totalFrames
			: uint64_t(std::floor(exact));

		mFrame = frame;
		mEnded = frame >= shared.totalFrames;
		mStreamPosition = aSeconds;
		mHeadActive = frame < shared.headFrames;

		// The worker needs the loop settings before the new request, or a
		// loop enabled together with this seek is only noticed after the
		// worker decoded past its end.
		publishLoopHints();
		// The first frame the ring has to provide.
		const uint64_t needed = mHeadActive ? shared.headFrames : frame;
		if (mChannel && needed < shared.totalFrames && !isBuffered(needed))
			requestFrame(needed);
		return SO_NO_ERROR;
	}

	SourceStateSnapshot *NativeStreamInstance::captureSourceState()
	{
		auto *s = new NativeStreamSnapshot();
		s->frame = mFrame;
		s->headActive = mHeadActive;
		s->ended = mEnded;
		s->generation = mGeneration;
		s->markerIndex = mMarkerIndex;
		s->readPos = mReadPos;
		s->waiting = mWaiting;
		s->waitFrames = mWaitFrames;
		return s;
	}

	bool NativeStreamInstance::canRestoreSourceState(SourceStateSnapshot *aState)
	{
		const auto *s = static_cast<const NativeStreamSnapshot *>(aState);
		// Without a worker everything comes from the head cache.
		if (!mChannel)
			return true;
		// Same request, and the ring still holds every frame and marker from
		// the snapshot's read position on.
		return s->generation == mGeneration && s->readPos >= mRetainPos && s->readPos <= mReadPos &&
			s->markerIndex >= mRetainMarker && s->markerIndex <= mMarkerIndex;
	}

	void NativeStreamInstance::restoreSourceState(SourceStateSnapshot *aState)
	{
		const auto *s = static_cast<const NativeStreamSnapshot *>(aState);
		if (!canRestoreSourceState(aState))
		{
			// The rollback gate asks first, so this is a fallback: resume
			// from the snapshot's frame, decoding it again if needed.
			const double position = mStreamPosition;
			seek(double(s->frame) / mShared->sampleRate, nullptr, 0);
			mStreamPosition = position;
			return;
		}
		mFrame = s->frame;
		mHeadActive = s->headActive;
		mEnded = s->ended;
		mMarkerIndex = s->markerIndex;
		mReadPos = s->readPos;
		mWaiting = s->waiting;
		mWaitFrames = s->waitFrames;
	}

	result NativeStreamInstance::rewind()
	{
		return seek(0, nullptr, 0);
	}

	bool NativeStreamInstance::hasEnded()
	{
		return mEnded;
	}

	uint64_t NativeStreamInstance::bufferedFrames() const
	{
		if (!mShared)
			return 0;
		uint64_t frames = 0;
		uint64_t frame = mFrame;
		if (mHeadActive && frame < mShared->headFrames)
		{
			frames = mShared->headFrames - frame;
			frame = mShared->headFrames;
		}
		uint64_t available = 0;
		bool coming;
		if (mChannel && frame < mShared->totalFrames)
			findBuffered(frame, available, coming);
		return frames + available;
	}

	NativeStream::NativeStream() {}

	NativeStream::~NativeStream()
	{
		stop();
	}

	result NativeStream::load(const char *aFilename)
	{
		return load(aFilename, openPlatformNativeStreamDecoder);
	}

	result NativeStream::load(const char *aFilename, NativeStreamDecoderFactory aFactory)
	{
		stop();
		mShared.reset();
		if (aFilename == nullptr || !aFactory)
			return INVALID_PARAMETER;

		auto shared = std::make_shared<Shared>();
		shared->path = aFilename;
		shared->factory = std::move(aFactory);

		std::string error;
		std::unique_ptr<NativeStreamDecoder> decoder = shared->factory(shared->path, error);
		if (!decoder)
			return FILE_LOAD_FAILED;
		const NativeStreamFormat format = decoder->format();
		if (format.channels == 0 || format.channels > MAX_CHANNELS || !(format.sampleRate > 0) ||
			format.totalFrames == 0 || format.totalFrames > kFrameMask)
			return FILE_LOAD_FAILED;

		shared->channels = format.channels;
		shared->sampleRate = format.sampleRate;
		shared->totalFrames = format.totalFrames;

		// Decoding the head also proves the codec can decode this file.
		const uint64_t headFrames = std::min<uint64_t>(
			format.totalFrames, uint64_t(std::ceil(format.sampleRate * kHeadSeconds)));
		shared->head.resize(size_t(headFrames) * format.channels);
		uint64_t decoded = 0;
		while (decoded < headFrames)
		{
			const long got = decoder->read(shared->head.data() + decoded * format.channels,
				unsigned(std::min<uint64_t>(headFrames - decoded, kChunkFrames)));
			if (got < 0)
				return FILE_LOAD_FAILED;
			if (got == 0)
				break;
			decoded += uint64_t(got);
		}
		if (decoded == 0)
			return FILE_LOAD_FAILED;
		if (decoded < headFrames)
		{
			// The stream is shorter than the container claims.
			shared->totalFrames = decoded;
			shared->head.resize(size_t(decoded) * format.channels);
		}
		shared->headFrames = decoded;

		mChannels = format.channels;
		mBaseSamplerate = format.sampleRate;
		mShared = std::move(shared);
		return SO_NO_ERROR;
	}

	AudioSourceInstance *NativeStream::createInstance()
	{
		// With the render-ahead ring, checkpoints can roll a voice back by up
		// to the ring's depth plus one mix quantum (in output frames); keep
		// that much consumed audio, in source frames, with some speed-up
		// margin and the resampler's read-ahead.
		uint64_t history = 0;
		if (mShared && mSoloud && mSoloud->isRenderAheadEnabled() && mSoloud->mSamplerate > 0)
		{
			const double window = double(mSoloud->mRenderAheadFrames) + 2.0 * mSoloud->mBufferSize +
				4.0 * mSoloud->mDevicePeriodFrames;
			history = uint64_t(std::ceil(window * mShared->sampleRate / mSoloud->mSamplerate * kHistorySpeedMargin)) +
				2 * SAMPLE_GRANULARITY;
		}
		return new NativeStreamInstance(this, history);
	}

	time NativeStream::getLength()
	{
		if (!mShared || mShared->sampleRate <= 0)
			return 0;
		return double(mShared->totalFrames) / mShared->sampleRate;
	}

	uint64_t NativeStream::getFrameCount() const
	{
		return mShared ? mShared->totalFrames : 0;
	}

	int NativeStream::liveWorkerCount()
	{
		return gLiveWorkers.load();
	}

	uint64_t NativeStream::underrunFrameCount()
	{
		return gUnderrunFrames.load(std::memory_order_relaxed);
	}
}
