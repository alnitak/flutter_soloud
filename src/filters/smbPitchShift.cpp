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
 * (one octave down) and 2. (one octave up). A value of exactly 1 does not
 *change the pitch. numSampsToProcess tells the routine how many samples in
 *indata[0... numSampsToProcess-1] should be pitch shifted and moved to
 *outdata[0 ... numSampsToProcess-1]. The two buffers can be identical (ie. it
 *can process the data in-place). fftFrameSize defines the FFT frame size used
 *for the processing. Typical values are 1024, 2048 and 4096. It may be any
 *value <= MAX_FRAME_LENGTH but it MUST be a power of 2. osamp is the STFT
 * oversampling factor which also determines the overlap between adjacent STFT
 * frames. It should at least be 4 for moderate scaling ratios. A value of 32 is
 * recommended for best quality. sampleRate takes the sample rate for the signal
 * in unit Hz, ie. 44100 for 44.1 kHz audio. The data passed to the routine in
 * indata[] should be in the range [-1.0, 1.0), which is also the output range
 * for the data, make sure you scale the data accordingly (for 16bit signed
 *integers you would have to divide (and multiply) by 32768).
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
#include "../common.h"
#include "soloud.h"

#include <math.h>
#include <string.h>

#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923
#endif

namespace {

// -----------------------------------------------------------------------------------------------------------------

/*

    12/12/02, smb

    PLEASE NOTE:

    There have been some reports on domain errors when the atan2() function was
   used as in the above code. Usually, a domain error should not interrupt the
   program flow (maybe except in Debug mode) but rather be handled "silently"
   and a global variable should be set according to this error. However, on some
   occasions people ran into this kind of scenario, so a replacement atan2()
   function is provided here.

    If you are experiencing domain errors and your program stops, simply replace
   all instances of atan2() with calls to the smbAtan2() function below.

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

template <typename T> T CopySign(T v, T x) { return (x >= 0) ? v : -v; }

FFI_PLUGIN_EXPORT double smbAtan2(double y, double x) {
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

} // namespace

// -----------------------------------------------------------------------------------------------------------------

CSmbPitchShift::CSmbPitchShift() {
  mFFTSetup = nullptr;
  mFFTWork = nullptr;
  mCurrentFFTSize = 0;
}

CSmbPitchShift::~CSmbPitchShift() {
  if (mFFTSetup != nullptr) {
    pffft_destroy_setup(mFFTSetup);
    mFFTSetup = nullptr;
  }
  if (mFFTWork != nullptr) {
    pffft_aligned_free(mFFTWork);
    mFFTWork = nullptr;
  }
  if (gFFTworksp != nullptr) {
    pffft_aligned_free(gFFTworksp);
    gFFTworksp = nullptr;
  }
}

void CSmbPitchShift::initFFT(long fftSize) {
  if (mCurrentFFTSize == fftSize) {
    return; // Already initialized for this size
  }

  // Clean up existing resources
  if (mFFTSetup != nullptr) {
    pffft_destroy_setup(mFFTSetup);
  }
  if (mFFTWork != nullptr) {
    pffft_aligned_free(mFFTWork);
  }
  if (gFFTworksp != nullptr) {
    pffft_aligned_free(gFFTworksp);
  }

  // Initialize new pffft setup for complex transforms
  mFFTSetup = pffft_new_setup(fftSize, PFFFT_COMPLEX);

  // Allocate aligned work buffers (2 * fftSize for complex interleaved format)
  mFFTWork = (float *)pffft_aligned_malloc(2 * fftSize * sizeof(float));
  gFFTworksp = (float *)pffft_aligned_malloc(2 * fftSize * sizeof(float));

  mCurrentFFTSize = fftSize;
  mWindowFFTSize = 0; // Force window regeneration
  gInit = false;      // Reinitialize state for the new frame size
  gRover = 0;
}

void CSmbPitchShift::smbPitchShift(float pitchShift, long numSampsToProcess,
                                   long fftFrameSize, long osamp,
                                   float sampleRate, float *indata,
                                   float *outdata)
/*
    Routine smbPitchShift(). See top of file for explanation
    Purpose: doing pitch shifting while maintaining duration using the Short
    Time Fourier Transform.
    Author: (c)1999-2015 Stephan M. Bernsee <s.bernsee [AT] zynaptiq [DOT] com>
*/
{
  // Initialize FFT if needed (handles size changes)
  initFFT(fftFrameSize);

  /* set up some handy variables */
  const long fftFrameSize2 = fftFrameSize / 2;
  const long stepSize = fftFrameSize / osamp;
  const double freqPerBin = sampleRate / (double)fftFrameSize;
  const double expct = 2. * M_PI * (double)stepSize / (double)fftFrameSize;
  const long inFifoLatency = fftFrameSize - stepSize;
  if (!gRover)
    gRover = inFifoLatency;

  /* initialize our static arrays */
  if (!gInit) {
    memset(gInFIFO, 0, MAX_FRAME_LENGTH * sizeof(float));
    memset(gOutFIFO, 0, MAX_FRAME_LENGTH * sizeof(float));
    memset(gFFTworksp, 0, 2 * mCurrentFFTSize * sizeof(float));
    memset(gLastPhase, 0, (MAX_FRAME_LENGTH / 2 + 1) * sizeof(float));
    memset(gSumPhase, 0, (MAX_FRAME_LENGTH / 2 + 1) * sizeof(float));
    memset(gOutputAccum, 0, 2 * MAX_FRAME_LENGTH * sizeof(float));
    memset(gAnaFreq, 0, MAX_FRAME_LENGTH * sizeof(float));
    memset(gAnaMagn, 0, MAX_FRAME_LENGTH * sizeof(float));
    memset(gSynMagn, 0, MAX_FRAME_LENGTH * sizeof(float));
    memset(gSynFreq, 0, MAX_FRAME_LENGTH * sizeof(float));

    gInit = true;
  }

  if (mWindowFFTSize != fftFrameSize) {
    mWindowFFTSize = fftFrameSize;
    for (long k = 0; k < fftFrameSize; k++) {
      gWindow[k] =
          -.5f * cosf(2.f * static_cast<float>(M_PI) * static_cast<float>(k) /
                      static_cast<float>(fftFrameSize)) +
          .5f;
    }
  }

  /* main processing loop */
  for (long i = 0; i < numSampsToProcess; i++) {

    /* As long as we have not yet collected enough data just read in */
    gInFIFO[gRover] = indata[i];
    outdata[i] = gOutFIFO[gRover - inFifoLatency];
    gRover++;

    /* now we have enough data for processing */
    if (gRover >= fftFrameSize) {
      gRover = inFifoLatency;

      /* do windowing and re,im interleave */
      for (long k = 0; k < fftFrameSize; k++) {
        gFFTworksp[2 * k] = gInFIFO[k] * gWindow[k];
        gFFTworksp[2 * k + 1] = 0.;
      }

      /* ***************** ANALYSIS ******************* */
      /* do transform using pffft */
      pffft_transform_ordered(mFFTSetup, gFFTworksp, gFFTworksp, mFFTWork,
                              PFFFT_FORWARD);

      /* this is the analysis step */
      for (long k = 0; k <= fftFrameSize2; k++) {

        /* de-interlace FFT buffer */
        const auto real = gFFTworksp[2 * k];
        const auto imag = gFFTworksp[2 * k + 1];

        /* compute magnitude and phase */
        const auto magn = 2. * hypotf(real, imag);
        const auto phase = atan2(imag, real);

        /* compute phase difference */
        double tmp = phase - gLastPhase[k];
        gLastPhase[k] = phase;

        /* subtract expected phase difference */
        tmp -= (double)k * expct;

        /* map delta phase into +/- Pi interval */
        /* wrap to -pi..pi range */
        long qpd = static_cast<long>(tmp / M_PI);
        if (qpd >= 0)
          qpd += qpd & 1;
        else
          qpd -= qpd & 1;
        tmp -= M_PI * static_cast<double>(qpd);

        /* get deviation from bin frequency from the +/- Pi interval */
        tmp = osamp * tmp / (2. * M_PI);

        /* compute the k-th partials' true frequency */
        tmp = (k + tmp) * freqPerBin;

        /* store magnitude and true frequency in analysis arrays */
        gAnaMagn[k] = magn;
        gAnaFreq[k] = tmp;
      }

      /* ***************** PROCESSING ******************* */
      /* this does the actual pitch shifting */
      memset(gSynMagn, 0, fftFrameSize * sizeof(float));
      memset(gSynFreq, 0, fftFrameSize * sizeof(float));
      for (long k = 0; k <= fftFrameSize2; k++) {

        const long index = static_cast<long>(k * pitchShift);

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
        tmp = 2. * M_PI * tmp / osamp;

        /* add the overlap phase advance back in */
        tmp += (double)k * expct;

        /* accumulate delta phase to get bin phase */
        gSumPhase[k] += tmp;
        const auto phase = gSumPhase[k];

        /* get real and imag part and re-interleave */
        gFFTworksp[2 * k] = magn * cosf(phase);
        gFFTworksp[2 * k + 1] = magn * sinf(phase);
      }

      /* zero negative frequencies */
      for (long k = fftFrameSize + 2; k < 2 * fftFrameSize; k++)
        gFFTworksp[k] = 0.;

      /* do inverse transform using pffft */
      pffft_transform_ordered(mFFTSetup, gFFTworksp, gFFTworksp, mFFTWork,
                              PFFFT_BACKWARD);

      /* do windowing and add to output accumulator */
      // PFFFT note: Transforms are not scaled -
      // PFFFT_BACKWARD(PFFFT_FORWARD(x)) = N*x So we divide by fftFrameSize (N)
      // instead of fftFrameSize2 (N/2)
      for (long k = 0; k < fftFrameSize; k++) {
        gOutputAccum[k] +=
            2. * gWindow[k] * gFFTworksp[2 * k] / (fftFrameSize * osamp);
      }
      for (long k = 0; k < stepSize; k++)
        gOutFIFO[k] = gOutputAccum[k];

      /* shift accumulator */
      memmove(gOutputAccum, gOutputAccum + stepSize,
              fftFrameSize * sizeof(float));

      /* move input FIFO */
      for (long k = 0; k < inFifoLatency; k++)
        gInFIFO[k] = gInFIFO[k + stepSize];
    }
  }
}
