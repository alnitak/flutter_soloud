#include "filters.h"

#include <iostream>
#include <algorithm>
#include <stdarg.h>

Filters::Filters(SoLoud::Soloud *soloud) : mSoloud(soloud)
{
}

Filters::~Filters()
{
}

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
    case LofiFilter:
    {
        SoLoud::LofiFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    case FlangerFilter:
    {
        SoLoud::FlangerFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    case DCRemovalFilter:
    {
        SoLoud::DCRemovalFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    case BassboostFilter:
    {
        SoLoud::BassboostFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    case WaveShaperFilter:
    {
        SoLoud::WaveShaperFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
    case RobotizeFilter:
    {
        SoLoud::RobotizeFilter f;
        int nParams = f.getParamCount();
        for (int i = 0; i < nParams; i++)
        {
            ret.push_back(f.getParamName(i));
        }
    }
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
    }

    return ret;
}

bool Filters::addGlobalFilter(FilterType filterType)
{
    if (filters.size() >= FILTERS_PER_STREAM)
        return false;

    // Check if the new filter is already be here.
    // Only one kind of filter allowed
    if (isFilterActive(filterType) >= 0)
        return false;

    switch (filterType)
    {
    case BiquadResonantFilter:
        if (!mBiquadResonantFilter)
            mBiquadResonantFilter = std::make_unique<SoLoud::BiquadResonantFilter>();
        mSoloud->setGlobalFilter(filters.size(), mBiquadResonantFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mBiquadResonantFilter.get())});
        break;
    case EqFilter:
        if (!mEqFilter)
            mEqFilter = std::make_unique<SoLoud::EqFilter>();
        mSoloud->setGlobalFilter(filters.size(), mEqFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mEqFilter.get())});
        break;
    case EchoFilter:
        if (!mEchoFilter)
            mEchoFilter = std::make_unique<SoLoud::EchoFilter>();
        mSoloud->setGlobalFilter(filters.size(), mEchoFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mEchoFilter.get())});
        break;
    case LofiFilter:
        if (!mLofiFilter)
            mLofiFilter = std::make_unique<SoLoud::LofiFilter>();
        mSoloud->setGlobalFilter(filters.size(), mLofiFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mLofiFilter.get())});
        break;
    case FlangerFilter:
        if (!mFlangerFilter)
            mFlangerFilter = std::make_unique<SoLoud::FlangerFilter>();
        mSoloud->setGlobalFilter(filters.size(), mFlangerFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mFlangerFilter.get())});
        break;
    case DCRemovalFilter:
        if (!mDCRemovalFilter)
            mDCRemovalFilter = std::make_unique<SoLoud::DCRemovalFilter>();
        mSoloud->setGlobalFilter(filters.size(), mDCRemovalFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mDCRemovalFilter.get())});
        break;
    case BassboostFilter:
        if (!mBassboostFilter)
            mBassboostFilter = std::make_unique<SoLoud::BassboostFilter>();
        mSoloud->setGlobalFilter(filters.size(), mBassboostFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mBassboostFilter.get())});
        break;
    case WaveShaperFilter:
        if (!mWaveShaperFilter)
            mWaveShaperFilter = std::make_unique<SoLoud::WaveShaperFilter>();
        mSoloud->setGlobalFilter(filters.size(), mWaveShaperFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mWaveShaperFilter.get())});
        break;
    case RobotizeFilter:
        if (!mRobotizeFilter)
            mRobotizeFilter = std::make_unique<SoLoud::RobotizeFilter>();
        mSoloud->setGlobalFilter(filters.size(), mRobotizeFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mRobotizeFilter.get())});
        break;
    case FreeverbFilter:
        if (!mFreeverbFilter)
            mFreeverbFilter = std::make_unique<SoLoud::FreeverbFilter>();
        mSoloud->setGlobalFilter(filters.size(), mFreeverbFilter.get());
        filters.push_back({filterType, static_cast<SoLoud::Filter *>(mFreeverbFilter.get())});
        break;
    default:
        return false;
    }
    return true;
}
/// TODO remove all filters FilterType.none
bool Filters::removeGlobalFilter(FilterType filterType)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return false;

    // TODO check if also mEchoFilter is disposed
    switch (filterType)
    {
    case BiquadResonantFilter:
        mSoloud->setGlobalFilter(index, 0);
        mBiquadResonantFilter.reset();
        break;
    case EqFilter:
        mSoloud->setGlobalFilter(index, 0);
        mEqFilter.reset();
        break;
    case EchoFilter:
        mSoloud->setGlobalFilter(index, 0);
        mEchoFilter.reset();
        break;
    case LofiFilter:
        mSoloud->setGlobalFilter(index, 0);
        mLofiFilter.reset();
        break;
    case FlangerFilter:
        mSoloud->setGlobalFilter(index, 0);
        mFlangerFilter.reset();
        break;
    case DCRemovalFilter:
        mSoloud->setGlobalFilter(index, 0);
        mDCRemovalFilter.reset();
        break;
    case BassboostFilter:
        mSoloud->setGlobalFilter(index, 0);
        mBassboostFilter.reset();
        break;
    case WaveShaperFilter:
        mSoloud->setGlobalFilter(index, 0);
        mWaveShaperFilter.reset();
        break;
    case RobotizeFilter:
        mSoloud->setGlobalFilter(index, 0);
        mRobotizeFilter.reset();
        break;
    case FreeverbFilter:
        mSoloud->setGlobalFilter(index, 0);
        mFreeverbFilter.reset();
        break;
    }

    /// shift filters down by 1 fron [index]
    for (int i = index; i < filters.size() - 1; i++)
    {
        mSoloud->setGlobalFilter(i + 1, 0);
        mSoloud->setGlobalFilter(i, filters[i].filter);
    }
    /// remove the filter from the list
    filters.erase(filters.begin() + index);

    return true;
}

void Filters::setFxParams(FilterType filterType, int attributeId, float value)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return;

    mSoloud->setFilterParameter(0, index, attributeId, value);
}

float Filters::getFxParams(FilterType filterType, int attributeId)
{
    int index = isFilterActive(filterType);
    if (index < 0)
        return 0.0f;

    float ret = mSoloud->getFilterParameter(0, index, attributeId);
    return ret;
}
