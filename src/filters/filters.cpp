#include "filters.h"
#include "../active_sound.h" // Add this include for complete type definition

#include <iostream>
#include <algorithm>
#include <stdarg.h>

#include "pitch_shift_filter.h"
#include "soloud_biquadresonantfilter.h"
// #include "soloud_duckfilter.h"
#include "soloud_eqfilter.h"
#include "soloud_echofilter.h"
#include "soloud_lofifilter.h"
#include "soloud_flangerfilter.h"
// #include "soloud_dcremovalfilter.h"
#include "soloud_fftfilter.h"
#include "soloud_bassboostfilter.h"
#include "soloud_waveshaperfilter.h"
#include "soloud_robotizefilter.h"
#include "soloud_freeverbfilter.h"
#include "limiter.h"
#include "compressor.h"

Filters::Filters(SoLoud::Soloud *soloud, ActiveSound *sound)
    : mSoloud(soloud), mSound(sound) {}

int Filters::isFilterActive(FilterType filter)
{
    for (int i = 0; i < filters.size(); i++)
    {
        if (filters[i].get()->type == filter)
            return i;
    }
    return -1;
}

std::vector<std::string> Filters::getFilterParamNames(FilterType filterType)
{
    std::vector<std::string> ret;
    switch (filterType)
    {
    case BiquadResonantFilter:
    {
        SoLoud::BiquadResonantFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case EqFilter:
    {
        SoLoud::EqFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case EchoFilter:
    {
        SoLoud::EchoFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case LofiFilter:
    {
        SoLoud::LofiFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case FlangerFilter:
    {
        SoLoud::FlangerFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case BassboostFilter:
    {
        SoLoud::BassboostFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case WaveShaperFilter:
    {
        SoLoud::WaveShaperFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case RobotizeFilter:
    {
        SoLoud::RobotizeFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case FreeverbFilter:
    {
        SoLoud::FreeverbFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case PitchShiftFilter:
    {
        PitchShift f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case LimiterFilter:
    {
        Limiter f(mSoloud->mSamplerate);
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    case CompressorFilter:
    {
        Compressor f(mSoloud->mSamplerate);
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    break;
    }

    return ret;
}

PlayerErrors Filters::addFilter(FilterType filterType)
{
    int filtersSize = (int)(filters.size());

    if (filtersSize >= FILTERS_PER_STREAM)
        return maxNumberOfFiltersReached;

    // Check if the new filter is already here.
    // Only one kind of filter allowed.
    if (isFilterActive(filterType) >= 0)
        return filterAlreadyAdded;

    SoLoud::Filter* newFilter = nullptr;
    switch (filterType)
    {
    case BiquadResonantFilter:
        newFilter = new SoLoud::BiquadResonantFilter();
        break;
    case EqFilter:
        newFilter = new SoLoud::EqFilter();
        break;
    case EchoFilter:
        newFilter = new SoLoud::EchoFilter();
        break;
    case LofiFilter:
        newFilter = new SoLoud::LofiFilter();
        break;
    case FlangerFilter:
        newFilter = new SoLoud::FlangerFilter();
        break;
    case BassboostFilter:
        newFilter = new SoLoud::BassboostFilter();
        break;
    case WaveShaperFilter:
        newFilter = new SoLoud::WaveShaperFilter();
        break;
    case RobotizeFilter:
        newFilter = new SoLoud::RobotizeFilter();
        break;
    case FreeverbFilter:
        newFilter = new SoLoud::FreeverbFilter();
        break;
    case PitchShiftFilter:
        newFilter = new PitchShift();
        break;
    case LimiterFilter:
        newFilter = new Limiter(mSoloud->mSamplerate);
        break;
    case CompressorFilter:
        newFilter = new Compressor(mSoloud->mSamplerate);
        break;
    default:
        return filterNotFound;
    }

    if (mSound == nullptr)
    {
        mSoloud->setGlobalFilter(filtersSize, newFilter);
    }
    else
    {
        mSound->sound.get()->setFilter(filtersSize, newFilter);
    }

    // Create FilterObject taking ownership of raw pointer
    std::unique_ptr<FilterObject> nfo = std::make_unique<FilterObject>(filterType, newFilter);
    filters.push_back(std::move(nfo));

    return noError;
}

/// TODO remove all filters FilterType.none
bool Filters::removeFilter(FilterType filterType)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return false;

    if (mSound == nullptr)
    {
        mSoloud->setGlobalFilter(index, 0);
    }
    else
    {
        mSound->sound.get()->setFilter(index, 0);
    }

    filters[index].get()->filter.reset();

    /// shift filters down by 1 from [index]
    for (int i = index; i < filters.size() - 1; i++)
    {
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(i + 1, 0);
        else
            mSound->sound.get()->setFilter(i + 1, 0);
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(i, filters[i + 1].get()->filter.get());
        else
            mSound->sound.get()->setFilter(i, filters[i + 1].get()->filter.get());
    }
    /// remove the filter from the list
    filters.erase(filters.begin() + index);

    return true;
}

void Filters::setFilterParams(SoLoud::handle handle, FilterType filterType, int attributeId, float value)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return;
    mSoloud->setFilterParameter(handle, index, attributeId, value);
}

float Filters::getFilterParams(SoLoud::handle handle, FilterType filterType, int attributeId)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return 9999.0f;

    float ret = mSoloud->getFilterParameter(handle, index, attributeId);
    return ret;
}

void Filters::fadeFilterParameter(SoLoud::handle handle, FilterType filterType, int attributeId, float to, float time)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return;

    mSoloud->fadeFilterParameter(handle, index, attributeId, to, time);
}

void Filters::oscillateFilterParameter(
    SoLoud::handle handle,
    FilterType filterType,
    int attributeId,
    float from,
    float to,
    float time)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return;

    mSoloud->oscillateFilterParameter(handle, index, attributeId, from, to, time);
}
