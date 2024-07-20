/****************************************************************************
*
* NAME: smbPitchShift.cpp
* VERSION: 1.2
* HOME URL: http://blogs.zynaptiq.com/bernsee
* KNOWN BUGS: none
*
* SYNOPSIS: Routine for doing pitch shifting while maintaining
* duration using the Short Time Fourier Transform.
*
* DESCRIPTION: The routine takes a pitchShift factor value which is between 0.5
* (one octave down) and 2. (one octave up). A value of exactly 1 does not change
* the pitch. numSampsToProcess tells the routine how many samples in indata[0...
* numSampsToProcess-1] should be pitch shifted and moved to outdata[0 ...
* numSampsToProcess-1]. The two buffers can be identical (ie. it can process the
* data in-place). fftFrameSize defines the FFT frame size used for the
* processing. Typical values are 1024, 2048 and 4096. It may be any value <=
* MAX_FRAME_LENGTH but it MUST be a power of 2. osamp is the STFT
* oversampling factor which also determines the overlap between adjacent STFT
* frames. It should at least be 4 for moderate scaling ratios. A value of 32 is
* recommended for best quality. sampleRate takes the sample rate for the signal 
* in unit Hz, ie. 44100 for 44.1 kHz audio. The data passed to the routine in 
* indata[] should be in the range [-1.0, 1.0), which is also the output range 
* for the data, make sure you scale the data accordingly (for 16bit signed integers
* you would have to divide (and multiply) by 32768). 
*
* COPYRIGHT 1999-2015 Stephan M. Bernsee <s.bernsee [AT] zynaptiq [DOT] com>
*
* 						The Wide Open License (WOL)
*
* Permission to use, copy, modify, distribute and sell this software and its
* documentation for any purpose is hereby granted without fee, provided that
* the above copyright notice and this license appear in all source copies. 
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF
* ANY KIND. See http://www.dspguru.com/wol.htm for more information.
*
*****************************************************************************/ 

#include "smbPitchShift.h"
#include "soloud.h"

#include <string.h>
#include <math.h>
#include <stdio.h>

#include <memory>

namespace {

#if defined(SOLOUD_SSE_INTRINSICS)
#include <emmintrin.h>
#include <pmmintrin.h>
#include <xmmintrin.h>
void smbFft(float *fftBuffer, long fftFrameSize, long sign)
/* 
    FFT routine, (C)1996 S.M.Bernsee. Sign = -1 is FFT, 1 is iFFT (inverse)
    Fills fftBuffer[0...2*fftFrameSize-1] with the Fourier transform of the
    time domain data in fftBuffer[0...2*fftFrameSize-1]. The FFT array takes
    and returns the cosine and sine parts in an interleaved manner, ie.
    fftBuffer[0] = cosPart[0], fftBuffer[1] = sinPart[0], asf. fftFrameSize
    must be a power of 2. It expects a complex input signal (see footnote 2),
    ie. when working with 'common' audio signals our input signal has to be
    passed as {in[0],0.,in[1],0.,in[2],0.,...} asf. In that case, the transform
    of the frequencies of interest is in fftBuffer[0...fftFrameSize].
*/
{
    const auto number = 2 * fftFrameSize - 2;
    for (long i = 2, j = 0; i < number; i += 2) {
        for (long bitm = fftFrameSize; bitm != 1; bitm >>= 1)
        {
            if (j & bitm)
                j &= ~bitm;
            else
            {
                j |= bitm;
                break;
            }
        }
        if (i < j) {
            auto p1 = fftBuffer+i; 
            auto p2 = fftBuffer+j;
            auto temp = *p1; *(p1++) = *p2;
            *(p2++) = temp; temp = *p1;
            *p1 = *p2; *p2 = temp;
        }
    }
    for (long k = 0, le = 2; k < (long)(log(fftFrameSize)/log(2.)+.5); k++) {
        le <<= 1;
        const auto le2 = le>>1;
        /* __declspec(align(8)) */ struct { float r, i; } u{ 1.0, 0.0 };

        const float arg = M_PI / (le2>>1);
        const float wr = cos(arg);
        const float wi = sign*sin(arg);
        for (long j = 0; j < le2; j += 2) {
            auto p1r = fftBuffer+j; 
            auto p2r = p1r+le2;

            __m128 u_ = _mm_castpd_ps(_mm_movedup_pd(_mm_load_sd((const double*)&u)));

            __m128 ldup = _mm_moveldup_ps(u_);
            __m128 hdup = _mm_movehdup_ps(u_);

            long i = j;
            for (; i < 2*fftFrameSize - le; i += le * 2) 
            {
                __m128 p2 = _mm_loadh_pi(_mm_castpd_ps(_mm_load_sd((const double*)p2r)), (const __m64*)(p2r + le));
                __m128 part1 = _mm_mul_ps(p2, ldup);
                __m128 part2 = _mm_mul_ps(p2, hdup);
                part2 = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(part2), _MM_SHUFFLE(2, 3, 0, 1)));
                __m128 t = _mm_addsub_ps(part1, part2);

                __m128 p1 = _mm_loadh_pi(_mm_castpd_ps(_mm_load_sd((const double*)p1r)), (const __m64*)(p1r + le));
                __m128 buffer = _mm_sub_ps(p1, t);

                _mm_storeh_pi((__m64*)(p2r + le), buffer);
                _mm_storel_pi((__m64*)p2r, buffer);

                buffer = _mm_add_ps(p1, t);
                _mm_storeh_pi((__m64*)(p1r + le), buffer);
                _mm_storel_pi((__m64*)p1r, buffer);

                p1r += le * 2; 
                p2r += le * 2; 
            }


            if (i < 2 * fftFrameSize) {
                const auto p1i = p1r + 1;
                const auto p2i = p2r + 1;
                const float tr = *p2r * u.r - *p2i * u.i;
                const float ti = *p2r * u.i + *p2i * u.r;
                *p2r = *p1r - tr; *p2i = *p1i - ti;
                *p1r += tr; *p1i += ti;
            }


            const float tr = u.r*wr - u.i*wi;
            u.i = u.r*wi + u.i*wr;
            u.r = tr;
        }
    }
}
#else
/// Without SIMD
void smbFft(float *fftBuffer, long fftFrameSize, long sign)
/* 
    FFT routine, (C)1996 S.M.Bernsee. Sign = -1 is FFT, 1 is iFFT (inverse)
    Fills fftBuffer[0...2*fftFrameSize-1] with the Fourier transform of the
    time domain data in fftBuffer[0...2*fftFrameSize-1]. The FFT array takes
    and returns the cosine and sine parts in an interleaved manner, ie.
    fftBuffer[0] = cosPart[0], fftBuffer[1] = sinPart[0], asf. fftFrameSize
    must be a power of 2. It expects a complex input signal (see footnote 2),
    ie. when working with 'common' audio signals our input signal has to be
    passed as {in[0],0.,in[1],0.,in[2],0.,...} asf. In that case, the transform
    of the frequencies of interest is in fftBuffer[0...fftFrameSize].
*/
{
    float wr, wi, arg, *p1, *p2, temp;
    float tr, ti, ur, ui, *p1r, *p1i, *p2r, *p2i;
    long i, bitm, j, le, le2, k;

    for (i = 2; i < 2*fftFrameSize-2; i += 2) {
        for (bitm = 2, j = 0; bitm < 2*fftFrameSize; bitm <<= 1) {
            if (i & bitm) j++;
            j <<= 1;
        }
        if (i < j) {
            p1 = fftBuffer+i; p2 = fftBuffer+j;
            temp = *p1; *(p1++) = *p2;
            *(p2++) = temp; temp = *p1;
            *p1 = *p2; *p2 = temp;
        }
    }
    for (k = 0, le = 2; k < (long)(log(fftFrameSize)/log(2.)+.5); k++) {
        le <<= 1;
        le2 = le>>1;
        ur = 1.0;
        ui = 0.0;
        arg = M_PI / (le2>>1);
        wr = cos(arg);
        wi = sign*sin(arg);
        for (j = 0; j < le2; j += 2) {
            p1r = fftBuffer+j; p1i = p1r+1;
            p2r = p1r+le2; p2i = p2r+1;
            for (i = j; i < 2*fftFrameSize; i += le) {
                tr = *p2r * ur - *p2i * ui;
                ti = *p2r * ui + *p2i * ur;
                *p2r = *p1r - tr; *p2i = *p1i - ti;
                *p1r += tr; *p1i += ti;
                p1r += le; p1i += le;
                p2r += le; p2i += le;
            }
            tr = ur*wr - ui*wi;
            ui = ur*wi + ui*wr;
            ur = tr;
        }
    }
}
#endif

// -----------------------------------------------------------------------------------------------------------------

/*

    12/12/02, smb
    
    PLEASE NOTE:
    
    There have been some reports on domain errors when the atan2() function was used
    as in the above code. Usually, a domain error should not interrupt the program flow
    (maybe except in Debug mode) but rather be handled "silently" and a global variable
    should be set according to this error. However, on some occasions people ran into
    this kind of scenario, so a replacement atan2() function is provided here.
    
    If you are experiencing domain errors and your program stops, simply replace all
    instances of atan2() with calls to the smbAtan2() function below.
    
*/

// Approximation was taken from:
// http://www-labs.iro.umontreal.ca/~mignotte/IFT2425/Documents/EfficientApproximationArctgFunction.pdf
//
// |Error = fast_atan2(y, x) - atan2f(y, x)| < 0.00468 rad
//
// Octants:
//         pi/2
//       ` 3 | 2 /
//        `  |  /
//       4 ` | / 1
//   pi -----+----- 0
//       5 / | ` 8
//        /  |  `
//       / 6 | 7 `
//         3pi/2

template<typename T> T CopySign(T v, T x)
{
    return (x >= 0) ? v : -v;
}

double smbAtan2(double y, double x)
{
    constexpr double scaling_constant = 0.28086;

    if (x == 0.) {
        // Special case atan2(0.0, 0.0) = 0.0
        if (y == 0.) {
            return 0.;
        }

        // x is zero so we are either at pi/2 for (y > 0) or -pi/2 for (y < 0)
        return CopySign(M_PI_2, y);
    }

    // Calculate quotient of y and x
    const auto div = y / x;

    // Determine in which octants we can be, if |y| is smaller than |x| (|div|<1)
    // then we are either in 1,4,5 or 8 else we are in 2,3,6 or 7.
    if (fabs(div) < 1.) {
        // We are in 1,4,5 or 8

        const auto atan = div / (1. + scaling_constant * div * div);

        // If we are in 4 or 5 we need to add pi or -pi respectively
        if (x < 0.) {
            return CopySign(M_PI, y) + atan;
        }
        return atan;
    }

    // We are in 2,3,6 or 7
    return CopySign(M_PI_2, y) - div / (div * div + scaling_constant);
}

//double smbAtan2(double x, double y)
//{
//  double signx;
//  if (x > 0.) signx = 1.;  
//  else signx = -1.;
//  
//  if (x == 0.) return 0.;
//  if (y == 0.) return signx * M_PI / 2.;
//  
//  return atan2(x, y);
//}


} // namespace


// -----------------------------------------------------------------------------------------------------------------


void CSmbPitchShift::smbPitchShift(float pitchShift, long numSampsToProcess, long fftFrameSize, long osamp, float sampleRate, float *indata, float *outdata)
/*
    Routine smbPitchShift(). See top of file for explanation
    Purpose: doing pitch shifting while maintaining duration using the Short
    Time Fourier Transform.
    Author: (c)1999-2015 Stephan M. Bernsee <s.bernsee [AT] zynaptiq [DOT] com>
*/
{
    /* set up some handy variables */
    const long fftFrameSize2 = fftFrameSize/2;
    const long stepSize = fftFrameSize/osamp;
    const double freqPerBin = sampleRate/(double)fftFrameSize;
    const double expct = 2.*M_PI*(double)stepSize/(double)fftFrameSize;
    const long inFifoLatency = fftFrameSize-stepSize;
    if (!gRover) 
        gRover = inFifoLatency;

    /* initialize our static arrays */
    if (!gInit) {
        memset(gInFIFO, 0, MAX_FRAME_LENGTH*sizeof(float));
        memset(gOutFIFO, 0, MAX_FRAME_LENGTH*sizeof(float));
        memset(gFFTworksp, 0, 2*MAX_FRAME_LENGTH*sizeof(float));
        memset(gLastPhase, 0, (MAX_FRAME_LENGTH/2+1)*sizeof(float));
        memset(gSumPhase, 0, (MAX_FRAME_LENGTH/2+1)*sizeof(float));
        memset(gOutputAccum, 0, 2*MAX_FRAME_LENGTH*sizeof(float));
        memset(gAnaFreq, 0, MAX_FRAME_LENGTH*sizeof(float));
        memset(gAnaMagn, 0, MAX_FRAME_LENGTH*sizeof(float));

        memset(gErrors, 0, MAX_FRAME_LENGTH * sizeof(float));

        gInit = true;
    }

    auto window = std::make_unique<double[]>(fftFrameSize);
    for (long k = 0; k < fftFrameSize; k++) {
        window[k] = -.5*cos(2.*M_PI*(double)k / (double)fftFrameSize) + .5;
    }

    /* main processing loop */
    for (long i = 0; i < numSampsToProcess; i++){

        /* As long as we have not yet collected enough data just read in */
        gInFIFO[gRover] = indata[i];
        outdata[i] = gOutFIFO[gRover-inFifoLatency];
        gRover++;

        /* now we have enough data for processing */
        if (gRover >= fftFrameSize) {
            gRover = inFifoLatency;

            /* do windowing and re,im interleave */
            for (long k = 0; k < fftFrameSize;k++) {
                gFFTworksp[2*k] = gInFIFO[k] * window[k];
                gFFTworksp[2*k+1] = 0.;
            }


            /* ***************** ANALYSIS ******************* */
            /* do transform */
            smbFft(gFFTworksp, fftFrameSize, -1);

            /* this is the analysis step */
            for (long k = 0; k <= fftFrameSize2; k++) {

                /* de-interlace FFT buffer */
                const auto real = gFFTworksp[2*k];
                const auto imag = gFFTworksp[2*k+1];

                /* compute magnitude and phase */
                const auto magn = 2.*hypotf(real, imag);
                const auto phase = smbAtan2(imag,real);

                /* compute phase difference */
                double tmp = phase - gLastPhase[k];
                gLastPhase[k] = phase;

                /* subtract expected phase difference */
                tmp -= (double)k*expct;

                /* map delta phase into +/- Pi interval */
                /* get deviation from bin frequency from the +/- Pi interval */
                tmp /= (2.*M_PI);
                tmp = osamp * (tmp - floor(tmp + 0.5)); // faster than round

                /* compute the k-th partials' true frequency */
                tmp = (k + tmp) * freqPerBin;

                /* store magnitude and true frequency in analysis arrays */
                gAnaMagn[k] = magn;
                gAnaFreq[k] = tmp;

            }

            /* ***************** PROCESSING ******************* */
            /* this does the actual pitch shifting */
            memset(gSynMagn, 0, fftFrameSize*sizeof(float));
            memset(gSynFreq, 0, fftFrameSize*sizeof(float));
            for (long k = 0; k <= fftFrameSize2; k++) {

                //const long index = k*pitchShift;
                const auto originalIndex = k*pitchShift + gErrors[k];
                const long index = originalIndex;
                gErrors[k] = originalIndex - index;

                if (index <= fftFrameSize2) {
                    const bool useSynFreq = gSynMagn[index] < gAnaMagn[k];

                    gSynMagn[index] += gAnaMagn[k];
                    if (useSynFreq) {
                        gSynFreq[index] = gAnaFreq[k] * pitchShift;
                    }
                } 
            }
            
            /* ***************** SYNTHESIS ******************* */
            /* this is the synthesis step */
            for (long k = 0; k <= fftFrameSize2; k++) {

                /* get magnitude and true frequency from synthesis arrays */
                const auto magn = gSynMagn[k];
                double tmp = gSynFreq[k];

                /* get bin deviation from freq deviation */
                tmp /= freqPerBin;

                /* subtract bin mid frequency */
                tmp -= k;

                /* take osamp into account */
                tmp = 2.*M_PI*tmp/osamp;

                /* add the overlap phase advance back in */
                tmp += (double)k*expct;

                /* accumulate delta phase to get bin phase */
                gSumPhase[k] += tmp;
                const auto phase = gSumPhase[k];

                /* get real and imag part and re-interleave */
                gFFTworksp[2*k] = magn*cosf(phase);
                gFFTworksp[2*k+1] = magn*sinf(phase);
            } 

            /* zero negative frequencies */
            for (long k = fftFrameSize+2; k < 2*fftFrameSize; k++) gFFTworksp[k] = 0.;

            /* do inverse transform */
            smbFft(gFFTworksp, fftFrameSize, 1);

            /* do windowing and add to output accumulator */ 
            for(long k=0; k < fftFrameSize; k++) {
                gOutputAccum[k] += 2. * window[k] * gFFTworksp[2*k]/(fftFrameSize2*osamp);
            }
            for (long k = 0; k < stepSize; k++) gOutFIFO[k] = gOutputAccum[k];

            /* shift accumulator */
            memmove(gOutputAccum, gOutputAccum+stepSize, fftFrameSize*sizeof(float));

            /* move input FIFO */
            for (long k = 0; k < inFifoLatency; k++) gInFIFO[k] = gInFIFO[k+stepSize];
        }
    }
}
