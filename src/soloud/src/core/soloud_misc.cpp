/*
SoLoud audio engine
Copyright (c) 2020 Jari Komppa

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

#include "soloud_misc.h"
#include <math.h>
#include <string.h>

namespace SoLoud
{
	namespace Misc
	{
		namespace
		{
			struct FourierWaveTables
			{
				static constexpr int kSize = 4096;
				float fsquare[kSize + 1];
				float fsaw[kSize + 1];

				FourierWaveTables()
				{
					for (int idx = 0; idx <= kSize; ++idx)
					{
						const double phase = static_cast<double>(idx) / static_cast<double>(kSize);
						double sq = 0.0;
						for (int i = 1; i < 22; i += 2)
						{
							sq += (4.0 / (M_PI * i)) * sin(2.0 * M_PI * i * phase);
						}
						fsquare[idx] = static_cast<float>(sq * 0.5);

						double sw = 0.0;
						for (int i = 1; i < 15; ++i)
						{
							const double term = (1.0 / (M_PI * i)) * sin(2.0 * M_PI * i * phase);
							sw += (i & 1) ? term : -term;
						}
						fsaw[idx] = static_cast<float>(sw);
					}
				}
			};

			const FourierWaveTables &getFourierWaveTables()
			{
				static const FourierWaveTables kTables;
				return kTables;
			}

			// Linear interpolation in a table of kSize + 1 entries over one
			// period. The phase wraps into [0, 1), but p - floorf(p) rounds to
			// 1.0f for a tiny negative p, which would read one entry past the
			// table. A NaN or infinite p (the float to int conversion of the
			// position is undefined for it) starts the period over. The bit test
			// is immune to fast-math flags that fold NaN comparisons.
			float lookupFourierTable(const float *aTable, float p)
			{
				unsigned int bits;
				memcpy(&bits, &p, sizeof(bits));
				float wrapped = 0.0f;
				if ((bits & 0x7F800000u) != 0x7F800000u)
				{
					wrapped = p - floorf(p);
					if (!(wrapped < 1.0f))
						wrapped = 0.0f;
				}
				const float pos = wrapped * static_cast<float>(FourierWaveTables::kSize);
				const int idx = static_cast<int>(pos);
				const float frac = pos - static_cast<float>(idx);
				return aTable[idx] + frac * (aTable[idx + 1] - aTable[idx]);
			}
		}

		float generateWaveform(int aWaveform, float p)
		{
			constexpr float kTwoPi = 6.2831853071795864769f;
			switch (aWaveform)
			{
			default:
			case Soloud::WAVE_SQUARE:
				return p > 0.5f ? 0.5f : -0.5f;
			case Soloud::WAVE_SAW:
				return p - 0.5f;
			case Soloud::WAVE_SIN:
				return sinf(p * kTwoPi) * 0.5f;
			case Soloud::WAVE_TRIANGLE:
				return (p > 0.5f ? (1.0f - (p - 0.5f) * 2.0f) : p * 2.0f) - 0.5f;
			case Soloud::WAVE_BOUNCE:
				{
					const float s = sinf(p * kTwoPi) * 0.5f;
					return (p < 0.5f ? s : -s) - 0.5f;
				}
			case Soloud::WAVE_JAWS:
				return (p < 0.25f ? sinf(p * kTwoPi) * 0.5f : 0.0f) - 0.5f;
			case Soloud::WAVE_HUMPS:
				return (p < 0.5f ? sinf(p * kTwoPi) * 0.5f : 0.0f) - 0.5f;
			case Soloud::WAVE_FSQUARE:
				return lookupFourierTable(getFourierWaveTables().fsquare, p);
			case Soloud::WAVE_FSAW:
				return lookupFourierTable(getFourierWaveTables().fsaw, p);
			}
		}

		Prg::Prg()
		{
			srand(0);
		}

		void Prg::srand(int aSeed)
		{
			mIndex = 0;
			int i;
			for (i = 0; i < 16; i++)
				mState[i] = aSeed + i * aSeed + i;
		}

		// WELL512 implementation, public domain by Chris Lomont
		unsigned int Prg::rand()
		{
			unsigned int a, b, c, d;
			a = mState[mIndex];
			c = mState[(mIndex + 13) & 15];
			b = a ^ c ^ (a << 16) ^ (c << 15);
			c = mState[(mIndex + 9) & 15];
			c ^= (c >> 11);
			a = mState[mIndex] = b ^ c;
			d = a ^ ((a << 5) & 0xDA442D24UL);
			mIndex = (mIndex + 15) & 15;
			a = mState[mIndex];
			mState[mIndex] = a ^ b ^ d ^ (a << 2) ^ (b << 18) ^ (c << 28);
			return mState[mIndex];
		}

		float Prg::rand_float()
		{
			return (float)rand() * 2.3283064365386963e-10f;
		}

	}
};
