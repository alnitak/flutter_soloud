/*
SoLoud audio engine
Copyright (c) 2013-2020 Jari Komppa

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#include <math.h>
#include <string.h>
#include "soloud.h"
#include "soloud_freeverbfilter.h"
#include "../../../simd/soloud_simd.h"


namespace SoLoud
{
	namespace FreeverbImpl
	{
		// Based on code written by Jezar at Dreampoint, June 2000 http://www.dreampoint.co.uk, 
		// which was placed in public domain. The code was massaged quite a bit by 
		// Jari Komppa, result in the license listed at top of this file.

		class Comb
		{
		public:
			Comb();
			void	setbuffer(float* aBuf, int aSize);
			float	process(float aInp);
			void	mute();
			void	setdamp(float val);
			void	setfeedback(float val);
			float	mFeedback;
			float	mFilterstore;
			float	mDamp1;
			float	mDamp2;
			float*  mBuffer;
			int		mBufsize;
			int		mBufidx;
		};

		class Allpass
		{
		public:
			Allpass();
			void	setbuffer(float* aBuf, int aSize);
			float   process(float aInp);
			void	mute();
			void	setfeedback(float aVal);
			float	mFeedback;
			float*  mBuffer;
			int		mBufsize;
			int		mBufidx;
		};

		const int	gNumcombs = 8;
		const int	gNumallpasses = 4;
		const float	gMuted = 0;
		const float	gFixedgain = 0.015f;
		const float gScalewet = 3;
		const float gScaledry = 1;
		const float gScaledamp = 0.4f;
		const float gScaleroom = 0.28f;
		const float gOffsetroom = 0.7f;
		const float gInitialroom = 0.5f;
		const float gInitialdamp = 0.5f;
		const float gInitialwet = 1 / gScalewet;
		const float gInitialdry = 0;
		const float gInitialwidth = 1;
		const float gInitialmode = 0;
		const float gFreezemode = 0.5f;
		const int	gStereospread = 23;

		// These values assume 44.1KHz sample rate
		// they will probably be OK for 48KHz sample rate
		// but would need scaling for 96KHz (or other) sample rates.
		// The values were obtained by listening tests.
		const int gCombtuningL1 = 1116;
		const int gCombtuningR1 = 1116 + gStereospread;
		const int gCombtuningL2 = 1188;
		const int gCombtuningR2 = 1188 + gStereospread;
		const int gCombtuningL3 = 1277;
		const int gCombtuningR3 = 1277 + gStereospread;
		const int gCombtuningL4 = 1356;
		const int gCombtuningR4 = 1356 + gStereospread;
		const int gCombtuningL5 = 1422;
		const int gCombtuningR5 = 1422 + gStereospread;
		const int gCombtuningL6 = 1491;
		const int gCombtuningR6 = 1491 + gStereospread;
		const int gCombtuningL7 = 1557;
		const int gCombtuningR7 = 1557 + gStereospread;
		const int gCombtuningL8 = 1617;
		const int gCombtuningR8 = 1617 + gStereospread;
		const int gAllpasstuningL1 = 556;
		const int gAllpasstuningR1 = 556 + gStereospread;
		const int gAllpasstuningL2 = 441;
		const int gAllpasstuningR2 = 441 + gStereospread;
		const int gAllpasstuningL3 = 341;
		const int gAllpasstuningR3 = 341 + gStereospread;
		const int gAllpasstuningL4 = 225;
		const int gAllpasstuningR4 = 225 + gStereospread;


		class Revmodel
		{
		public:
			struct UninitializedTag {};
			Revmodel();
			explicit Revmodel(UninitializedTag);
			void	bindBuffers();
			void	mute();
			void	process(float* aSampleData, long aNumSamples, long aStride);
			void	setroomsize(float aValue);
			void	setdamp(float aValue);
			void	setwet(float aValue);
			void	setdry(float aValue);
			void	setwidth(float aValue);
			void	setmode(float aValue);
			void	update();
			void	copyFrom(const Revmodel &aOther);

			float	mGain;
			float	mRoomsize, mRoomsize1;
			float	mDamp, mDamp1;
			float	mWet, mWet1, mWet2;
			float	mDry;
			float	mWidth;
			float	mMode;

			int		mDirty;

			// The following are all declared inline 
			// to remove the need for dynamic allocation
			// with its subsequent error-checking messiness

			// Comb filters
			Comb	mCombL[gNumcombs];
			Comb	mCombR[gNumcombs];

			// Allpass filters
			Allpass	mAllpassL[gNumallpasses];
			Allpass	mAllpassR[gNumallpasses];

			// Buffers for the combs
			float	mBufcombL1[gCombtuningL1];
			float	mBufcombR1[gCombtuningR1];
			float	mBufcombL2[gCombtuningL2];
			float	mBufcombR2[gCombtuningR2];
			float	mBufcombL3[gCombtuningL3];
			float	mBufcombR3[gCombtuningR3];
			float	mBufcombL4[gCombtuningL4];
			float	mBufcombR4[gCombtuningR4];
			float	mBufcombL5[gCombtuningL5];
			float	mBufcombR5[gCombtuningR5];
			float	mBufcombL6[gCombtuningL6];
			float	mBufcombR6[gCombtuningR6];
			float	mBufcombL7[gCombtuningL7];
			float	mBufcombR7[gCombtuningR7];
			float	mBufcombL8[gCombtuningL8];
			float	mBufcombR8[gCombtuningR8];

			// Buffers for the allpasses
			float	mBufallpassL1[gAllpasstuningL1];
			float	mBufallpassR1[gAllpasstuningR1];
			float	mBufallpassL2[gAllpasstuningL2];
			float	mBufallpassR2[gAllpasstuningR2];
			float	mBufallpassL3[gAllpasstuningL3];
			float	mBufallpassR3[gAllpasstuningR3];
			float	mBufallpassL4[gAllpasstuningL4];
			float	mBufallpassR4[gAllpasstuningR4];
		};

		Allpass::Allpass()
		{
			mBufidx = 0;
			mFeedback = 0;
			mBuffer = 0;
			mBufsize = 0;
		}

		float Allpass::process(float aInput)
		{
			float output;
			float bufout;

			bufout = mBuffer[mBufidx];

			output = -aInput + bufout;
			mBuffer[mBufidx] = aInput + (bufout * mFeedback);

			if (++mBufidx >= mBufsize) mBufidx = 0;

			return output;
		}

		void Allpass::setbuffer(float* aBuf, int aSize)
		{
			mBuffer = aBuf;
			mBufsize = aSize;
		}

		void Allpass::mute()
		{
			if (mBuffer && mBufsize > 0)
				memset(mBuffer, 0, sizeof(float) * mBufsize);
		}

		void Allpass::setfeedback(float aVal)
		{
			mFeedback = aVal;
		}

		Comb::Comb()
		{
			mFilterstore = 0;
			mBufidx = 0;
			mFeedback = 0;
			mDamp1 = 0;
			mDamp2 = 0;
			mBuffer = 0;
			mBufsize = 0;
		}

		float Comb::process(float aInput)
		{
			float output;

			output = mBuffer[mBufidx];

			mFilterstore = (output * mDamp2) + (mFilterstore * mDamp1);

			mBuffer[mBufidx] = aInput + (mFilterstore * mFeedback);

			if (++mBufidx >= mBufsize) mBufidx = 0;

			return output;
		}

		void Comb::setbuffer(float* aBuf, int aSize)
		{
			mBuffer = aBuf;
			mBufsize = aSize;
		}

		void Comb::mute()
		{
			if (mBuffer && mBufsize > 0)
				memset(mBuffer, 0, sizeof(float) * mBufsize);
		}

		void Comb::setdamp(float aVal)
		{
			mDamp1 = aVal;
			mDamp2 = 1 - aVal;
		}

		void Comb::setfeedback(float aVal)
		{
			mFeedback = aVal;
		}
	
		void Revmodel::bindBuffers()
		{
			// Tie the components to their buffers
			mCombL[0].setbuffer(mBufcombL1, gCombtuningL1);
			mCombR[0].setbuffer(mBufcombR1, gCombtuningR1);
			mCombL[1].setbuffer(mBufcombL2, gCombtuningL2);
			mCombR[1].setbuffer(mBufcombR2, gCombtuningR2);
			mCombL[2].setbuffer(mBufcombL3, gCombtuningL3);
			mCombR[2].setbuffer(mBufcombR3, gCombtuningR3);
			mCombL[3].setbuffer(mBufcombL4, gCombtuningL4);
			mCombR[3].setbuffer(mBufcombR4, gCombtuningR4);
			mCombL[4].setbuffer(mBufcombL5, gCombtuningL5);
			mCombR[4].setbuffer(mBufcombR5, gCombtuningR5);
			mCombL[5].setbuffer(mBufcombL6, gCombtuningL6);
			mCombR[5].setbuffer(mBufcombR6, gCombtuningR6);
			mCombL[6].setbuffer(mBufcombL7, gCombtuningL7);
			mCombR[6].setbuffer(mBufcombR7, gCombtuningR7);
			mCombL[7].setbuffer(mBufcombL8, gCombtuningL8);
			mCombR[7].setbuffer(mBufcombR8, gCombtuningR8);
			mAllpassL[0].setbuffer(mBufallpassL1, gAllpasstuningL1);
			mAllpassR[0].setbuffer(mBufallpassR1, gAllpasstuningR1);
			mAllpassL[1].setbuffer(mBufallpassL2, gAllpasstuningL2);
			mAllpassR[1].setbuffer(mBufallpassR2, gAllpasstuningR2);
			mAllpassL[2].setbuffer(mBufallpassL3, gAllpasstuningL3);
			mAllpassR[2].setbuffer(mBufallpassR3, gAllpasstuningR3);
			mAllpassL[3].setbuffer(mBufallpassL4, gAllpasstuningL4);
			mAllpassR[3].setbuffer(mBufallpassR4, gAllpasstuningR4);
		}

		Revmodel::Revmodel(UninitializedTag)
		{
			mGain = 0;
			mRoomsize = 0;
			mRoomsize1 = 0;
			mDamp = 0;
			mDamp1 = 0;
			mWet = 0;
			mWet1 = 0;
			mWet2 = 0;
			mDry = 0;
			mWidth = 0;
			mMode = 0;
			mDirty = 1;
			bindBuffers();
		}

		Revmodel::Revmodel()
		{
			mGain = 0;
			mRoomsize = 0;
			mRoomsize1 = 0;
			mDamp = 0;
			mDamp1 = 0;
			mWet = 0;
			mWet1 = 0;
			mWet2 = 0;
			mDry = 0;
			mWidth = 0;
			mMode = 0;

			mDirty = 1;

			bindBuffers();

			// Set default values
			mAllpassL[0].setfeedback(0.5f);
			mAllpassR[0].setfeedback(0.5f);
			mAllpassL[1].setfeedback(0.5f);
			mAllpassR[1].setfeedback(0.5f);
			mAllpassL[2].setfeedback(0.5f);
			mAllpassR[2].setfeedback(0.5f);
			mAllpassL[3].setfeedback(0.5f);
			mAllpassR[3].setfeedback(0.5f);
			setwet(gInitialwet);
			setroomsize(gInitialroom);
			setdry(gInitialdry);
			setdamp(gInitialdamp);
			setwidth(gInitialwidth);
			setmode(gInitialmode);			

			// Buffer will be full of rubbish - so we MUST mute them
			mute();
		}

		void Revmodel::mute()
		{			
			if (mMode >= gFreezemode)
				return;

			for (int i = 0; i < gNumcombs; i++)
			{
				mCombL[i].mute();
				mCombR[i].mute();
			}
			for (int i = 0; i < gNumallpasses; i++)
			{
				mAllpassL[i].mute();
				mAllpassR[i].mute();
			}
		}

		void Revmodel::process(float* aSampleData, long aNumSamples, long aStride)
		{
			if (aNumSamples <= 0)
				return;
			soloud_simd::ScopedFlushDenormals flushDenormals;

			float* inputL = aSampleData;
			float* inputR = aSampleData + aStride;

			if (mDirty)
				update();
			mDirty = 0;

			const auto vDamp1 = soloud_simd::f32x4::splat(mCombL[0].mDamp1);
			const auto vDamp2 = soloud_simd::f32x4::splat(mCombL[0].mDamp2);
			const auto vFeedback = soloud_simd::f32x4::splat(mCombL[0].mFeedback);

			auto fsL0 = soloud_simd::f32x4::make(
				mCombL[0].mFilterstore, mCombL[1].mFilterstore, mCombL[2].mFilterstore, mCombL[3].mFilterstore);
			auto fsL1 = soloud_simd::f32x4::make(
				mCombL[4].mFilterstore, mCombL[5].mFilterstore, mCombL[6].mFilterstore, mCombL[7].mFilterstore);
			auto fsR0 = soloud_simd::f32x4::make(
				mCombR[0].mFilterstore, mCombR[1].mFilterstore, mCombR[2].mFilterstore, mCombR[3].mFilterstore);
			auto fsR1 = soloud_simd::f32x4::make(
				mCombR[4].mFilterstore, mCombR[5].mFilterstore, mCombR[6].mFilterstore, mCombR[7].mFilterstore);

			long rem = aNumSamples;
			while (rem > 0)
			{
				// Maximum contiguous span before any of the 16 comb delay lines wraps.
				long chunk = rem;
				for (int i = 0; i < gNumcombs; i++)
				{
					const long remL = mCombL[i].mBufsize - mCombL[i].mBufidx;
					const long remR = mCombR[i].mBufsize - mCombR[i].mBufidx;
					if (remL < chunk) chunk = remL;
					if (remR < chunk) chunk = remR;
				}

				float* bL0 = mCombL[0].mBuffer + mCombL[0].mBufidx;
				float* bL1 = mCombL[1].mBuffer + mCombL[1].mBufidx;
				float* bL2 = mCombL[2].mBuffer + mCombL[2].mBufidx;
				float* bL3 = mCombL[3].mBuffer + mCombL[3].mBufidx;
				float* bL4 = mCombL[4].mBuffer + mCombL[4].mBufidx;
				float* bL5 = mCombL[5].mBuffer + mCombL[5].mBufidx;
				float* bL6 = mCombL[6].mBuffer + mCombL[6].mBufidx;
				float* bL7 = mCombL[7].mBuffer + mCombL[7].mBufidx;

				float* bR0 = mCombR[0].mBuffer + mCombR[0].mBufidx;
				float* bR1 = mCombR[1].mBuffer + mCombR[1].mBufidx;
				float* bR2 = mCombR[2].mBuffer + mCombR[2].mBufidx;
				float* bR3 = mCombR[3].mBuffer + mCombR[3].mBufidx;
				float* bR4 = mCombR[4].mBuffer + mCombR[4].mBufidx;
				float* bR5 = mCombR[5].mBuffer + mCombR[5].mBufidx;
				float* bR6 = mCombR[6].mBuffer + mCombR[6].mBufidx;
				float* bR7 = mCombR[7].mBuffer + mCombR[7].mBufidx;

				float wb[4];
				for (long s = 0; s < chunk; s++)
				{
					const float inL = inputL[s];
					const float inR = inputR[s];
					const float input = (inL + inR) * mGain;
					const auto vInput = soloud_simd::f32x4::splat(input);

					const auto oL0 = soloud_simd::f32x4::make(bL0[s], bL1[s], bL2[s], bL3[s]);
					const auto oL1 = soloud_simd::f32x4::make(bL4[s], bL5[s], bL6[s], bL7[s]);
					const auto oR0 = soloud_simd::f32x4::make(bR0[s], bR1[s], bR2[s], bR3[s]);
					const auto oR1 = soloud_simd::f32x4::make(bR4[s], bR5[s], bR6[s], bR7[s]);

					fsL0 = soloud_simd::add(soloud_simd::mul(oL0, vDamp2), soloud_simd::mul(fsL0, vDamp1));
					fsL1 = soloud_simd::add(soloud_simd::mul(oL1, vDamp2), soloud_simd::mul(fsL1, vDamp1));
					fsR0 = soloud_simd::add(soloud_simd::mul(oR0, vDamp2), soloud_simd::mul(fsR0, vDamp1));
					fsR1 = soloud_simd::add(soloud_simd::mul(oR1, vDamp2), soloud_simd::mul(fsR1, vDamp1));

					soloud_simd::add(vInput, soloud_simd::mul(fsL0, vFeedback)).storeu(wb);
					bL0[s] = wb[0]; bL1[s] = wb[1]; bL2[s] = wb[2]; bL3[s] = wb[3];
					soloud_simd::add(vInput, soloud_simd::mul(fsL1, vFeedback)).storeu(wb);
					bL4[s] = wb[0]; bL5[s] = wb[1]; bL6[s] = wb[2]; bL7[s] = wb[3];

					soloud_simd::add(vInput, soloud_simd::mul(fsR0, vFeedback)).storeu(wb);
					bR0[s] = wb[0]; bR1[s] = wb[1]; bR2[s] = wb[2]; bR3[s] = wb[3];
					soloud_simd::add(vInput, soloud_simd::mul(fsR1, vFeedback)).storeu(wb);
					bR4[s] = wb[0]; bR5[s] = wb[1]; bR6[s] = wb[2]; bR7[s] = wb[3];

					float outL = soloud_simd::reduce_add(soloud_simd::add(oL0, oL1));
					float outR = soloud_simd::reduce_add(soloud_simd::add(oR0, oR1));

					for (int i = 0; i < gNumallpasses; i++)
					{
						outL = mAllpassL[i].process(outL);
						outR = mAllpassR[i].process(outR);
					}

					inputL[s] = outL * mWet1 + outR * mWet2 + inL * mDry;
					inputR[s] = outR * mWet1 + outL * mWet2 + inR * mDry;
				}

				for (int i = 0; i < gNumcombs; i++)
				{
					mCombL[i].mBufidx += (int)chunk;
					if (mCombL[i].mBufidx >= mCombL[i].mBufsize) mCombL[i].mBufidx = 0;
					mCombR[i].mBufidx += (int)chunk;
					if (mCombR[i].mBufidx >= mCombR[i].mBufsize) mCombR[i].mBufidx = 0;
				}

				inputL += chunk;
				inputR += chunk;
				rem -= chunk;
			}

			float st[4];
			fsL0.storeu(st);
			for (int i = 0; i < 4; i++) mCombL[i].mFilterstore = st[i];
			fsL1.storeu(st);
			for (int i = 0; i < 4; i++) mCombL[4 + i].mFilterstore = st[i];
			fsR0.storeu(st);
			for (int i = 0; i < 4; i++) mCombR[i].mFilterstore = st[i];
			fsR1.storeu(st);
			for (int i = 0; i < 4; i++) mCombR[4 + i].mFilterstore = st[i];
		}

		void Revmodel::update()
		{
			// Recalculate internal values after parameter change

			int i;

			mWet1 = mWet * (mWidth / 2 + 0.5f);
			mWet2 = mWet * ((1 - mWidth) / 2);

			if (mMode >= gFreezemode)
			{
				mRoomsize1 = 1;
				mDamp1 = 0;
				mGain = gMuted;
			}
			else
			{
				mRoomsize1 = mRoomsize;
				mDamp1 = mDamp;
				mGain = gFixedgain;
			}

			for (i = 0; i < gNumcombs; i++)
			{
				mCombL[i].setfeedback(mRoomsize1);
				mCombR[i].setfeedback(mRoomsize1);
			}

			for (i = 0; i < gNumcombs; i++)
			{
				mCombL[i].setdamp(mDamp1);
				mCombR[i].setdamp(mDamp1);
			}
		}

		void Revmodel::setroomsize(float aValue)
		{
			mRoomsize = (aValue * gScaleroom) + gOffsetroom;
			mDirty = 1;
		}

		void Revmodel::setdamp(float aValue)
		{
			mDamp = aValue * gScaledamp;
			mDirty = 1;
		}

		void Revmodel::setwet(float aValue)
		{
			mWet = aValue * gScalewet;
			mDirty = 1;
		}

		void Revmodel::setdry(float aValue)
		{
			mDry = aValue * gScaledry;
		}

		void Revmodel::setwidth(float aValue)
		{
			mWidth = aValue;
			mDirty = 1;
		}

		void Revmodel::setmode(float aValue)
		{
			mMode = aValue;
			mDirty = 1;
		}

		void Revmodel::copyFrom(const Revmodel &aOther)
		{
			mGain = aOther.mGain;
			mRoomsize = aOther.mRoomsize;
			mRoomsize1 = aOther.mRoomsize1;
			mDamp = aOther.mDamp;
			mDamp1 = aOther.mDamp1;
			mWet = aOther.mWet;
			mWet1 = aOther.mWet1;
			mWet2 = aOther.mWet2;
			mDry = aOther.mDry;
			mWidth = aOther.mWidth;
			mMode = aOther.mMode;
			mDirty = aOther.mDirty;
			for (int i = 0; i < gNumcombs; i++)
			{
				mCombL[i].mFeedback = aOther.mCombL[i].mFeedback;
				mCombL[i].mFilterstore = aOther.mCombL[i].mFilterstore;
				mCombL[i].mDamp1 = aOther.mCombL[i].mDamp1;
				mCombL[i].mDamp2 = aOther.mCombL[i].mDamp2;
				mCombL[i].mBufidx = aOther.mCombL[i].mBufidx;
				memcpy(mCombL[i].mBuffer, aOther.mCombL[i].mBuffer, sizeof(float) * mCombL[i].mBufsize);

				mCombR[i].mFeedback = aOther.mCombR[i].mFeedback;
				mCombR[i].mFilterstore = aOther.mCombR[i].mFilterstore;
				mCombR[i].mDamp1 = aOther.mCombR[i].mDamp1;
				mCombR[i].mDamp2 = aOther.mCombR[i].mDamp2;
				mCombR[i].mBufidx = aOther.mCombR[i].mBufidx;
				memcpy(mCombR[i].mBuffer, aOther.mCombR[i].mBuffer, sizeof(float) * mCombR[i].mBufsize);
			}
			for (int i = 0; i < gNumallpasses; i++)
			{
				mAllpassL[i].mFeedback = aOther.mAllpassL[i].mFeedback;
				mAllpassL[i].mBufidx = aOther.mAllpassL[i].mBufidx;
				memcpy(mAllpassL[i].mBuffer, aOther.mAllpassL[i].mBuffer, sizeof(float) * mAllpassL[i].mBufsize);

				mAllpassR[i].mFeedback = aOther.mAllpassR[i].mFeedback;
				mAllpassR[i].mBufidx = aOther.mAllpassR[i].mBufidx;
				memcpy(mAllpassR[i].mBuffer, aOther.mAllpassR[i].mBuffer, sizeof(float) * mAllpassR[i].mBufsize);
			}
		}
	}

	FreeverbFilterInstance::FreeverbFilterInstance(FreeverbFilter *aParent)
	{	
		initParams(5);
		
		mParent = aParent;

		mModel = new FreeverbImpl::Revmodel();

		mParam[FREEZE] = aParent->mMode;
		mParam[ROOMSIZE] = aParent->mRoomSize;
		mParam[DAMP] = aParent->mDamp;
		mParam[WIDTH] = aParent->mWidth;
		mParam[WET] = 1;
	}

	void FreeverbFilterInstance::filter(float* aBuffer, unsigned int aSamples, unsigned int aBufferSize, unsigned int aChannels, float /*aSamplerate*/, time aTime)
	{
		if (aChannels == 0 || aSamples == 0)
			return;
		updateParams(aTime);
		if (mParamChanged)
		{
			mModel->setdamp(mParam[DAMP]);
			mModel->setmode(mParam[FREEZE]);
			mModel->setroomsize(mParam[ROOMSIZE]);
			mModel->setwidth(mParam[WIDTH]);
			mModel->setwet(mParam[WET]);
			mModel->setdry(1 - mParam[WET]);
			mParamChanged = 0;
		}
		if (aChannels == 1)
		{
			// Safe mono fallback: duplicate mono channel into stack scratch so
			// Revmodel::process never reads/writes out of bounds past channel 0.
			float scratchR[SAMPLE_GRANULARITY];
			unsigned int offset = 0;
			while (offset < aSamples)
			{
				const unsigned int chunk = (aSamples - offset > SAMPLE_GRANULARITY)
					? SAMPLE_GRANULARITY : (aSamples - offset);
				memcpy(scratchR, aBuffer + offset, sizeof(float) * chunk);
				// Pass scratchR via stride? Instead process chunk with contiguous pair:
				float pair[SAMPLE_GRANULARITY * 2];
				memcpy(pair, aBuffer + offset, sizeof(float) * chunk);
				memcpy(pair + SAMPLE_GRANULARITY, scratchR, sizeof(float) * chunk);
				mModel->process(pair, (long)chunk, SAMPLE_GRANULARITY);
				for (unsigned int i = 0; i < chunk; i++)
				{
					aBuffer[offset + i] = 0.5f * (pair[i] + pair[SAMPLE_GRANULARITY + i]);
				}
				offset += chunk;
			}
			return;
		}
		mModel->process(aBuffer, aSamples, aBufferSize);
	}

	struct FreeverbFilterSnapshot : public FilterStateSnapshot
	{
		FreeverbImpl::Revmodel mModel;
		uint64_t mParamChanged;
		float mParam[5];
		Fader mParamFader[5];
		FreeverbFilterSnapshot() : mModel(FreeverbImpl::Revmodel::UninitializedTag()), mParamChanged(0) {}
	};

	static const char kFreeverbSnapshotKind = 0;

	FilterStateSnapshot *FreeverbFilterInstance::captureState()
	{
		FreeverbFilterSnapshot *s = new FreeverbFilterSnapshot();
		s->mKind = &kFreeverbSnapshotKind;
		captureStateInto(s);
		return s;
	}

	bool FreeverbFilterInstance::captureStateInto(FilterStateSnapshot *aState)
	{
		// Only a snapshot this type produced can be refilled. The reverb tank
		// is a fixed-size member of the snapshot, so no size check is needed.
		if (aState == NULL || aState->mKind != &kFreeverbSnapshotKind)
			return false;
		FreeverbFilterSnapshot *s = static_cast<FreeverbFilterSnapshot *>(aState);
		if (mModel)
			s->mModel.copyFrom(*mModel);
		s->mParamChanged = mParamChanged;
		for (int i = 0; i < 5; i++)
		{
			s->mParam[i] = mParam[i];
			s->mParamFader[i] = mParamFader[i];
		}
		return true;
	}

	void FreeverbFilterInstance::restoreState(FilterStateSnapshot *aSnapshot)
	{
		FreeverbFilterSnapshot *s = static_cast<FreeverbFilterSnapshot *>(aSnapshot);
		if (mModel)
			mModel->copyFrom(s->mModel);
		mParamChanged = s->mParamChanged;
		for (int i = 0; i < 5; i++)
		{
			mParam[i] = s->mParam[i];
			mParamFader[i] = s->mParamFader[i];
		}
	}

	FreeverbFilterInstance::~FreeverbFilterInstance()
	{
		delete mModel;
	}

	FreeverbFilter::FreeverbFilter()
	{
		setParams(0, 0.5, 0.5, 1);
	}

	result FreeverbFilter::setParams(float aFreeze, float aRoomSize, float aDamp, float aWidth)
	{
		if (aFreeze < 0 || aFreeze > 1 || aRoomSize <= 0 || aDamp < 0 || aWidth <= 0)
			return INVALID_PARAMETER;

		mMode = aFreeze;
		mRoomSize = aRoomSize;
		mDamp = aDamp;
		mWidth = aWidth;

		return 0;
	}

	int FreeverbFilter::getParamCount()
	{
		return 5;
	}

	const char* FreeverbFilter::getParamName(unsigned int aParamIndex)
	{
		switch (aParamIndex)
		{
		case FREEZE: return "Freeze";
		case ROOMSIZE: return "Room size";
		case DAMP: return "Damp";
		case WIDTH: return "Width";
		}
		return "Wet";
	}

	unsigned int FreeverbFilter::getParamType(unsigned int aParamIndex)
	{
		if (aParamIndex == FREEZE)
			return BOOL_PARAM;
		return FLOAT_PARAM;
	}

	float FreeverbFilter::getParamMax(unsigned int aParamIndex)
	{
		return 1;
	}

	float FreeverbFilter::getParamMin(unsigned int aParamIndex)
	{
		return 0;
	}


	FreeverbFilter::~FreeverbFilter()
	{
	}

	FreeverbFilterInstance *FreeverbFilter::createInstance()
	{
		return new FreeverbFilterInstance(this);
	}
}
