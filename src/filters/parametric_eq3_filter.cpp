#include <string.h>
#include <math.h>
#include <string>
#include <algorithm>
#include "soloud.h"
#include "parametric_eq3_filter.h"

// TODO: make this globally in the SoLoud Engine and let the user set it. To be used also for visualization?
#define STFT_WINDOW_SIZE 1024  // Increased for better frequency resolution. Must be a power of 2.
#define STFT_WINDOW_HALF (STFT_WINDOW_SIZE / 2)
#define STFT_WINDOW_TWICE (STFT_WINDOW_SIZE * 2)
#define FFT_SCALE (1.0f / STFT_WINDOW_SIZE)

ParametricEq3Instance::ParametricEq3Instance(ParametricEq3 *aParent)
{
    mParent = aParent;
    // initialize filter parameters (wet + bands)
    initParams(aParent->getParamCount());
    // ensure internal Wet param is set to 1, not used in EQ (yet?)
    mParam[0] = 1.0f;

    // copy band gains into parameter slots (params[1..bands])
    mBands = aParent->mBands;
    for (int i = 0; i < mBands; i++)
    {
        mParam[1 + i] = aParent->mGain[i];
    }

    // Initialize FFT setup for complex transforms
    mFFTSetup = pffft_new_setup(STFT_WINDOW_SIZE, PFFFT_COMPLEX);
    
    // Allocate aligned buffers for FFT
    mFFTBuffer = (float*)pffft_aligned_malloc(STFT_WINDOW_TWICE * sizeof(float));
    mFFTWork = (float*)pffft_aligned_malloc(STFT_WINDOW_TWICE * sizeof(float));
    mTemp = (float*)pffft_aligned_malloc(STFT_WINDOW_TWICE * sizeof(float));

    // Initialize channel buffers and offsets
    for (int i = 0; i < mParent->mChannels; i++)
    {
        mInputBuffer[i] = nullptr;  // Will be lazily initialized when needed
        mMixBuffer[i] = nullptr;    // Will be lazily initialized when needed
        mInputOffset[i] = STFT_WINDOW_SIZE;
        mMixOffset[i] = STFT_WINDOW_HALF;
        mReadOffset[i] = 0;
    }

    // Precompute band centers and boundaries (boundaries are midpoints)
    mBandCenter.resize(mBands);
    mBandBoundary.resize(mBands + 1);
    for (int i = 0; i < mBands; i++) mBandCenter[i] = aParent->mFreq[i];
    // boundaries: first = 0, last = +inf (will be clamped to Nyquist when used)
    mBandBoundary[0] = 0.0f;
    for (int i = 0; i < mBands - 1; i++)
    {
        mBandBoundary[i + 1] = 0.5f * (mBandCenter[i] + mBandCenter[i + 1]);
    }
    mBandBoundary[mBands] = 1e9f; // effectively infinity for the upper bound
}

void ParametricEq3Instance::comp2MagPhase(float* aFFTBuffer, unsigned int aSamples)
{
    for (unsigned int i = 0; i < aSamples; i++)
    {
        float re = aFFTBuffer[i * 2];
        float im = aFFTBuffer[i * 2 + 1];
        aFFTBuffer[i * 2] = sqrtf(re * re + im * im);
        aFFTBuffer[i * 2 + 1] = atan2f(im, re);
    }
}

void ParametricEq3Instance::magPhase2Comp(float* aFFTBuffer, unsigned int aSamples)
{
    for (unsigned int i = 0; i < aSamples; i++)
    {
        float mag = aFFTBuffer[i * 2];
        float phase = aFFTBuffer[i * 2 + 1];
        aFFTBuffer[i * 2] = mag * cosf(phase);
        aFFTBuffer[i * 2 + 1] = mag * sinf(phase);
    }
}

ParametricEq3Instance::~ParametricEq3Instance()
{
    // Free PFFFT resources
    if (mFFTSetup != nullptr)
    {
        pffft_destroy_setup(mFFTSetup);
        mFFTSetup = nullptr;
    }
    
    // Free aligned buffers
    if (mFFTBuffer != nullptr)
    {
        pffft_aligned_free(mFFTBuffer);
        mFFTBuffer = nullptr;
    }
    if (mFFTWork != nullptr)
    {
        pffft_aligned_free(mFFTWork);
        mFFTWork = nullptr;
    }
    if (mTemp != nullptr)
    {
        pffft_aligned_free(mTemp);
        mTemp = nullptr;
    }
    
    // Free channel buffers
    for (int i = 0; i < mParent->mChannels; i++)
    {
        if (mInputBuffer[i] != nullptr)
        {
            delete[] mInputBuffer[i];
            mInputBuffer[i] = nullptr;
        }
        if (mMixBuffer[i] != nullptr)
        {
            delete[] mMixBuffer[i];
            mMixBuffer[i] = nullptr;
        }
    }
}

void ParametricEq3Instance::filterChannel(float *aBuffer, unsigned int aSamples, float aSamplerate, SoLoud::time aTime, unsigned int aChannel, unsigned int aChannels)
{
    if (aChannel == 0)
    {
        updateParams(aTime);
    }

    // Lazy initialization of buffers for this channel
    if (mInputBuffer[aChannel] == nullptr)
    {
        mInputBuffer[aChannel] = new float[STFT_WINDOW_TWICE]();  // () initializes to zero
        mMixBuffer[aChannel] = new float[STFT_WINDOW_TWICE]();
    }

	unsigned int ofs = 0;
	unsigned int chofs = 0;
	unsigned int inputofs = mInputOffset[aChannel];
	unsigned int mixofs = mMixOffset[aChannel];
	unsigned int readofs = mReadOffset[aChannel];

	while (ofs < aSamples)
	{
		int samples = STFT_WINDOW_HALF - (inputofs & (STFT_WINDOW_HALF - 1));
		if (ofs + samples > aSamples) samples = aSamples - ofs;
		for (int i = 0; i < samples; i++)
		{
			mInputBuffer[aChannel][chofs + ((inputofs + STFT_WINDOW_HALF) & (STFT_WINDOW_TWICE - 1))] = aBuffer[ofs + i];
			mMixBuffer[aChannel][chofs + ((inputofs + STFT_WINDOW_HALF) & (STFT_WINDOW_TWICE - 1))] = 0;
			inputofs++;
		}
					
		if ((inputofs & (STFT_WINDOW_HALF - 1)) == 0)
		{
			// Copy input to FFT buffer (interleaved real/imag format)
			for (int i = 0; i < STFT_WINDOW_SIZE; i++)
			{
				float sample = mInputBuffer[aChannel][chofs + ((inputofs + STFT_WINDOW_TWICE - STFT_WINDOW_HALF + i) & (STFT_WINDOW_TWICE - 1))];
				mFFTBuffer[i * 2] = sample;     // Real part
				mFFTBuffer[i * 2 + 1] = 0.0f;   // Imaginary part
			}
			

            // Forward FFT (ordered output: interleaved complex numbers)
            pffft_transform_ordered(mFFTSetup, mFFTBuffer, mTemp, mFFTWork, PFFFT_FORWARD);

            // Apply EQ on the transformed complex bins (process all N complex bins)
            fftFilterChannel(mTemp, STFT_WINDOW_SIZE, aSamplerate, aTime, aChannel, aChannels);


            // Inverse FFT (ordered output)
            pffft_transform_ordered(mFFTSetup, mTemp, mFFTBuffer, mFFTWork, PFFFT_BACKWARD);
			
			// Apply scaling and Hann window for overlap-add
			for (int i = 0; i < STFT_WINDOW_SIZE; i++)
			{
                float window = 0.5f * (1.0f - cosf((2.0f * M_PI * i) / STFT_WINDOW_SIZE));
                float sample = mFFTBuffer[i * 2] * FFT_SCALE * window;  // Only use real part
                mMixBuffer[aChannel][chofs + (mixofs & (STFT_WINDOW_TWICE - 1))] += sample;
				mixofs++;					
			}
			mixofs -= STFT_WINDOW_HALF;
		}
		
		for (int i = 0; i < samples; i++)
		{
			aBuffer[ofs + i] = mMixBuffer[aChannel][chofs + (readofs & (STFT_WINDOW_TWICE - 1))];
			readofs++;
		}
		
		ofs += samples;
	}
	mInputOffset[aChannel] = inputofs;
	mReadOffset[aChannel] = readofs;
	mMixOffset[aChannel] = mixofs;
}

void ParametricEq3Instance::fftFilterChannel(float *aFFTBuffer, unsigned int aSamples, float aSamplerate, SoLoud::time /*aTime*/, unsigned int /*aChannel*/, unsigned int /*aChannels*/)
{
    comp2MagPhase(aFFTBuffer, aSamples);
    // Triangular interpolation across user-configured bands.
    // mBandCenter holds band centers, mBandBoundary has midpoints between centers (size mBands+1)
    float nyquist = aSamplerate * 0.5f;
    for (unsigned int i = 0; i < aSamples; i++)
    {
        float current_freq = (float)i * aSamplerate / (float)(aSamples*2);

        float gain = 0.0f;
        float weight_sum = 0.0f;

        for (int b = 0; b < mBands; b++)
        {
            float center = mBandCenter[b];
            float low = mBandBoundary[b];
            float high = mBandBoundary[b + 1];
            // clamp upper boundary to nyquist
            if (high > nyquist) high = nyquist;

            // half width for triangular window
            float halfwidth = std::max(center - low, high - center);
            float weight = 0.0f;
            if (halfwidth > 0.0f)
            {
                float d = fabsf(current_freq - center);
                float w = 1.0f - (d / halfwidth);
                if (w > 0.0f) weight = w;
            }
            else
            {
                // degenerate: treat only exact center
                weight = (fabsf(current_freq - center) < 1e-6f) ? 1.0f : 0.0f;
            }

            float bandGain = mParam[1 + b]; // param index 1..mBands -> band gains
            gain += bandGain * weight;
            weight_sum += weight;
        }

        if (weight_sum > 0.0f)
        {
            gain /= weight_sum; // normalize so overlapping triangles sum to 1
        }
        else
        {
            gain = 0.0f; // default: unity if no band matches
        }

        aFFTBuffer[i*2] *= gain;
    }

    magPhase2Comp(aFFTBuffer, aSamples);
}

SoLoud::result ParametricEq3::setParam(unsigned int aParamIndex, float aValue)
{
    // Not used. Parameters are read in the ParametricEq3Instance and set in the FilterInstance
    return SoLoud::SO_NO_ERROR;
}

int ParametricEq3::getParamCount()
{
    return 1 + mBands; // wet + per-band gains
}

const char* ParametricEq3::getParamName(unsigned int aParamIndex)
{
    if (aParamIndex == 0) return "Wet";
    static thread_local std::string s;
    unsigned int band = aParamIndex; // 1-based
    s = std::string("Band") + std::to_string(band);
    return s.c_str();
}

unsigned int ParametricEq3::getParamType(unsigned int aParamIndex)
{
    return FLOAT_PARAM;
}

float ParametricEq3::getParamMax(unsigned int aParamIndex)
{
    if (aParamIndex == 0) return 1;
    return 4.0f;
}

float ParametricEq3::getParamMin(unsigned int aParamIndex)
{
    if (aParamIndex == 0) return 0;
    return 0.0f;
}

ParametricEq3::ParametricEq3(int channels, int bands)
{
    mChannels = channels;
    mBands = std::max(1, bands);

    // resize vectors
    mGain.assign(mBands, 1.0f);
    mFreq.resize(mBands);

    // default frequency distribution: geometric spacing between 60Hz and 10000Hz
    float f0 = 60.0f;
    float f1 = 10000.0f;
    if (mBands == 1)
    {
        mFreq[0] = 1000.0f;
    }
    else
    {
        for (int i = 0; i < mBands; i++)
        {
            float t = (float)i / (float)(mBands - 1);
            mFreq[i] = f0 * powf(f1 / f0, t);
        }
    }
}

SoLoud::FilterInstance *ParametricEq3::createInstance()
{
	return new ParametricEq3Instance(this);
}