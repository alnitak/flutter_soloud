#include "filters.h"

#include <iostream>
#include <algorithm>
#include <stdarg.h>

Filters::Filters(SoLoud::Soloud *soloud, ActiveSound *sound)
    : mSoloud(soloud), mSound(sound), filters({}) {}

Filters::~Filters() {}

int Filters::isFilterActive(FilterType filter)
{
    auto it = std::find(filters.begin(), filters.end(), filter);

    if (it != filters.end())
    {
        int index = it - filters.begin();
        return index;
    }
    else
    {
        return -1;
    }
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
    }

    return ret;
}

PlayerErrors Filters::addFilter(FilterType filterType)
{
    if ((int)filters.size() >= FILTERS_PER_STREAM)
        return maxNumberOfFiltersReached;

    // Check if the new filter is already here.
    // Only one kind of filter allowed.
    if (isFilterActive(filterType) >= 0)
        return filterAlreadyAdded;

    const unsigned int filtersSize = static_cast<unsigned int>(filters.size());
    switch (filterType)
    {
    case BiquadResonantFilter:
        if (!mBiquadResonantFilter)
            mBiquadResonantFilter = std::make_unique<SoLoud::BiquadResonantFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mBiquadResonantFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mBiquadResonantFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mBiquadResonantFilter.get())});
        break;
    case EqFilter:
        if (!mEqFilter)
            mEqFilter = std::make_unique<SoLoud::EqFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mEqFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mEqFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mEqFilter.get())});
        break;
    case EchoFilter:
        if (!mEchoFilter)
            mEchoFilter = std::make_unique<SoLoud::EchoFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mEchoFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mEchoFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mEchoFilter.get())});
        break;
    case LofiFilter:
        if (!mLofiFilter)
            mLofiFilter = std::make_unique<SoLoud::LofiFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mLofiFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mLofiFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mLofiFilter.get())});
        break;
    case FlangerFilter:
        if (!mFlangerFilter)
            mFlangerFilter = std::make_unique<SoLoud::FlangerFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mFlangerFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mFlangerFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mFlangerFilter.get())});
        break;
    case BassboostFilter:
        if (!mBassboostFilter)
            mBassboostFilter = std::make_unique<SoLoud::BassboostFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mBassboostFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mBassboostFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mBassboostFilter.get())});
        break;
    case WaveShaperFilter:
        if (!mWaveShaperFilter)
            mWaveShaperFilter = std::make_unique<SoLoud::WaveShaperFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mWaveShaperFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mWaveShaperFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mWaveShaperFilter.get())});
        break;
    case RobotizeFilter:
        if (!mRobotizeFilter)
            mRobotizeFilter = std::make_unique<SoLoud::RobotizeFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mRobotizeFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mRobotizeFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mRobotizeFilter.get())});
        break;
    case FreeverbFilter:
        if (!mFreeverbFilter)
            mFreeverbFilter = std::make_unique<SoLoud::FreeverbFilter>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mFreeverbFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mFreeverbFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mFreeverbFilter.get())});
        break;
    case PitchShiftFilter:
        if (!mPitchFilter)
            mPitchFilter = std::make_unique<PitchShift>();
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(filtersSize, mPitchFilter.get());
        else
            mSound->sound.get()->setFilter(filtersSize, mPitchFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mPitchFilter.get())});
        break;
    default:
        return filterNotFound;
    }
    return noError;
}

/// TODO remove all filters FilterType.none
bool Filters::removeFilter(FilterType filterType)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return false;

    // TODO check if also mEchoFilter is disposed
    mSoloud->setGlobalFilter(index, 0);
    switch (filterType)
    {
    case BiquadResonantFilter:
        mBiquadResonantFilter.reset();
        break;
    case EqFilter:
        mEqFilter.reset();
        break;
    case EchoFilter:
        mEchoFilter.reset();
        break;
    case LofiFilter:
        mLofiFilter.reset();
        break;
    case FlangerFilter:
        mFlangerFilter.reset();
        break;
    case BassboostFilter:
        mBassboostFilter.reset();
        break;
    case WaveShaperFilter:
        mWaveShaperFilter.reset();
        break;
    case RobotizeFilter:
        mRobotizeFilter.reset();
        break;
    case FreeverbFilter:
        mFreeverbFilter.reset();
        break;
    case PitchShiftFilter:
        mPitchFilter.reset();
        break;
    }

    /// shift filters down by 1 from [index]
    for (int i = index; i < filters.size() - 1; i++)
    {
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(i + 1, 0);
        else
            mSound->sound.get()->setFilter(i + 1, 0);
        if (mSound == nullptr)
            mSoloud->setGlobalFilter(i, filters[i + 1].filter);
        else
            mSound->sound.get()->setFilter(i, filters[i + 1].filter);
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
