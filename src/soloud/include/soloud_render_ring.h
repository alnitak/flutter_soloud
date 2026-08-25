/*
* SoLoud audio engine
*
* Render-ahead ring buffer (flutter_soloud "Option B" retrofit).
*
* Interleaved float32 ring sitting between the engine mixer and the output
* device. The mixer (or a retroactive re-mix) is the single writer, always
* serialized by the engine audio mutex; the miniaudio device callback is the
* single reader and never blocks: positions are monotonic 64-bit frame
* counters read through atomics, so the read side is lock-free SPSC.
*
* Monotonic counters (never wrapped) remove the empty/full ambiguity; the
* buffer index is `position % mCapacityFrames`. 64-bit frame positions wrap
* after ~13 million years at 44.1 kHz, which is considered adequate.
*/

#ifndef SOLOUD_RENDER_RING_H
#define SOLOUD_RENDER_RING_H

#include <atomic>
#include <cstring>
#include <vector>

namespace SoLoud
{
	class RenderRing
	{
	public:
		RenderRing() :
			mChannels(0),
			mCapacityFrames(0),
			mWritePos(0),
			mReadPos(0)
		{
		}

		// (Re)allocate the ring. Not thread-safe: call at init time, before the
		// device callback can run, or with the device stopped.
		void init(unsigned int aCapacityFrames, unsigned int aChannels)
		{
			mBuffer.assign((size_t)aCapacityFrames * aChannels, 0.0f);
			mChannels = aChannels;
			mCapacityFrames = aCapacityFrames;
			mWritePos.store(0, std::memory_order_release);
			mReadPos.store(0, std::memory_order_release);
		}

		void clear()
		{
			mBuffer.clear();
			mChannels = 0;
			mCapacityFrames = 0;
			mWritePos.store(0, std::memory_order_release);
			mReadPos.store(0, std::memory_order_release);
		}

		bool isInited() const
		{
			return mCapacityFrames != 0;
		}

		unsigned int getCapacity() const
		{
			return mCapacityFrames;
		}

		unsigned int getChannels() const
		{
			return mChannels;
		}

		// Frames mixed but not yet consumed by the device.
		unsigned int availableToRead() const
		{
			unsigned long long w = mWritePos.load(std::memory_order_acquire);
			unsigned long long r = mReadPos.load(std::memory_order_acquire);
			return (unsigned int)(w - r);
		}

		// Frames the writer may still append without overwriting unread data.
		unsigned int availableToWrite() const
		{
			return mCapacityFrames - availableToRead();
		}

		unsigned long long getWritePosition() const
		{
			return mWritePos.load(std::memory_order_acquire);
		}

		unsigned long long getReadPosition() const
		{
			return mReadPos.load(std::memory_order_acquire);
		}

		// Append frames (writer side). Returns frames actually written; the
		// caller (top-up logic) guarantees space, so a short write only happens
		// on a logic error and is clamped rather than corrupting unread data.
		unsigned int write(const float *aSrc, unsigned int aFrames)
		{
			unsigned int space = availableToWrite();
			if (aFrames > space)
				aFrames = space;
			if (aFrames == 0)
				return 0;
			unsigned long long w = mWritePos.load(std::memory_order_relaxed);
			unsigned int offset = (unsigned int)(w % mCapacityFrames);
			unsigned int first = mCapacityFrames - offset;
			if (first > aFrames)
				first = aFrames;
			std::memcpy(mBuffer.data() + (size_t)offset * mChannels, aSrc,
						(size_t)first * mChannels * sizeof(float));
			unsigned int second = aFrames - first;
			if (second > 0)
			{
				std::memcpy(mBuffer.data(), aSrc + (size_t)first * mChannels,
							(size_t)second * mChannels * sizeof(float));
			}
			// Release: the samples must be visible before the new write head.
			mWritePos.store(w + aFrames, std::memory_order_release);
			return aFrames;
		}

		// Overwrite frames at an absolute position (Phase 3 retroactive
		// re-mix). aPos must be >= read head; frames at/behind the read head
		// belong to the device and are never touched.
		void rewrite(unsigned long long aPos, const float *aSrc, unsigned int aFrames)
		{
			unsigned int offset = (unsigned int)(aPos % mCapacityFrames);
			unsigned int first = mCapacityFrames - offset;
			if (first > aFrames)
				first = aFrames;
			std::memcpy(mBuffer.data() + (size_t)offset * mChannels, aSrc,
						(size_t)first * mChannels * sizeof(float));
			unsigned int second = aFrames - first;
			if (second > 0)
			{
				std::memcpy(mBuffer.data(), aSrc + (size_t)first * mChannels,
							(size_t)second * mChannels * sizeof(float));
			}
		}

		// Move the write head back (Phase 3). Caller guarantees
		// aNewWritePos >= read head and <= current write head.
		void rewindWrite(unsigned long long aNewWritePos)
		{
			mWritePos.store(aNewWritePos, std::memory_order_release);
		}

		// Consume frames into aDst (reader/device side). Returns frames
		// actually read; the caller silence-fills any remainder (underrun).
		unsigned int read(float *aDst, unsigned int aFrames)
		{
			unsigned int avail = availableToRead();
			if (aFrames > avail)
				aFrames = avail;
			if (aFrames == 0)
				return 0;
			unsigned long long r = mReadPos.load(std::memory_order_relaxed);
			unsigned int offset = (unsigned int)(r % mCapacityFrames);
			unsigned int first = mCapacityFrames - offset;
			if (first > aFrames)
				first = aFrames;
			std::memcpy(aDst, mBuffer.data() + (size_t)offset * mChannels,
						(size_t)first * mChannels * sizeof(float));
			unsigned int second = aFrames - first;
			if (second > 0)
			{
				std::memcpy(aDst + (size_t)first * mChannels, mBuffer.data(),
							(size_t)second * mChannels * sizeof(float));
			}
			mReadPos.store(r + aFrames, std::memory_order_release);
			return aFrames;
		}

	private:
		std::vector<float> mBuffer;
		unsigned int mChannels;
		unsigned int mCapacityFrames;
		std::atomic<unsigned long long> mWritePos;
		std::atomic<unsigned long long> mReadPos;
	};
};

#endif // SOLOUD_RENDER_RING_H
