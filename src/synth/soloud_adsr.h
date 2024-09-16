/*
SoLoud audio engine
Copyright (c) 2013-2021 Jari Komppa

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

#ifndef ADSR_H
#define ADSR_H

#include "soloud.h"

class ADSR
{
public:
	double mA, mD, mS, mR;

	ADSR()
	{
		mA = 0.0;
		mD = 0.0;
		mS = 1.0;
		mR = 0.0;
	}

	ADSR(double aA, double aD, double aS, double aR)
	{
		mA = aA;
		mD = aD;
		mS = aS;
		mR = aR;
	}

	double val(double aT, double aRelTime)
	{
		if (aT < mA)
		{
			return aT / mA;
		}
		aT -= mA;
		if (aT < mD)
		{
			return 1.0 - ((aT / mD)) * (1.0 - mS);
		}
		aT -= mD;
		if (aT < aRelTime)
			return mS;
		aT -= aRelTime;
		if (aT >= mR)
		{
			return 0.0;
		}
		return (1.0 - aT / mR) * mS;
	}
};

#endif