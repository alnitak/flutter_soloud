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

#if defined(SOLOUD_SSE_INTRINSICS)
#include <emmintrin.h>
#elif defined(SOLOUD_NEON_INTRINSICS)
#include <arm_neon.h>
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
#include <wasm_simd128.h>
#endif

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

		// Overwrite frames at an absolute position with a short linear crossfade
		// from the existing ring samples at the leading edge (used when a
		// retroactive re-mix starts mid-quantum at readPos so per-quantum
		// panAndExpand volume ramps never create a step jump at readPos).
		void rewriteCrossfade(unsigned long long aPos, const float *aSrc, unsigned int aFrames, unsigned int aCrossfadeFrames)
		{
			if (aFrames == 0 || mCapacityFrames == 0 || mChannels == 0)
				return;
			unsigned int xfade = aCrossfadeFrames < aFrames ? aCrossfadeFrames : aFrames;
			const float invDenom = 1.0f / (float)(xfade + 1);
			unsigned int f = 0;
#if defined(SOLOUD_SSE_INTRINSICS) || defined(SOLOUD_NEON_INTRINSICS) || defined(SOLOUD_WASM_SIMD_INTRINSICS)
			if (mChannels == 2)
			{
				while (f + 2 <= xfade)
				{
					const unsigned int ringFrame0 = (unsigned int)((aPos + f) % mCapacityFrames);
					if (ringFrame0 + 2 > mCapacityFrames)
						break;
					const float a0 = (float)(f + 1) * invDenom;
					const float a1 = (float)(f + 2) * invDenom;
					float *dst = mBuffer.data() + (size_t)ringFrame0 * 2;
					const float *src = aSrc + (size_t)f * 2;
#if defined(SOLOUD_SSE_INTRINSICS)
					const __m128 alpha = _mm_set_ps(a1, a1, a0, a0);
					const __m128 invAlpha = _mm_sub_ps(_mm_set1_ps(1.0f), alpha);
					const __m128 oldV = _mm_loadu_ps(dst);
					const __m128 newV = _mm_loadu_ps(src);
					_mm_storeu_ps(dst, _mm_add_ps(_mm_mul_ps(oldV, invAlpha), _mm_mul_ps(newV, alpha)));
#elif defined(SOLOUD_NEON_INTRINSICS)
					const float alphaArr[4] = { a0, a0, a1, a1 };
					const float invAlphaArr[4] = { 1.0f - a0, 1.0f - a0, 1.0f - a1, 1.0f - a1 };
					const float32x4_t alpha = vld1q_f32(alphaArr);
					const float32x4_t invAlpha = vld1q_f32(invAlphaArr);
					const float32x4_t oldV = vld1q_f32(dst);
					const float32x4_t newV = vld1q_f32(src);
					vst1q_f32(dst, vmlaq_f32(vmulq_f32(oldV, invAlpha), newV, alpha));
#elif defined(SOLOUD_WASM_SIMD_INTRINSICS)
					const v128_t alpha = wasm_f32x4_make(a0, a0, a1, a1);
					const v128_t invAlpha = wasm_f32x4_sub(wasm_f32x4_splat(1.0f), alpha);
					const v128_t oldV = wasm_v128_load(dst);
					const v128_t newV = wasm_v128_load(src);
					wasm_v128_store(dst, wasm_f32x4_add(wasm_f32x4_mul(oldV, invAlpha), wasm_f32x4_mul(newV, alpha)));
#endif
					f += 2;
				}
			}
#endif
			for (; f < xfade; f++)
			{
				const float alpha = (float)(f + 1) * invDenom;
				const float invAlpha = 1.0f - alpha;
				const size_t ringIdx = (size_t)((aPos + f) % mCapacityFrames) * mChannels;
				const size_t srcIdx = (size_t)f * mChannels;
				for (unsigned int c = 0; c < mChannels; c++)
				{
					mBuffer[ringIdx + c] = mBuffer[ringIdx + c] * invAlpha + aSrc[srcIdx + c] * alpha;
				}
			}
			if (aFrames > xfade)
			{
				rewrite(aPos + xfade, aSrc + (size_t)xfade * mChannels, aFrames - xfade);
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
