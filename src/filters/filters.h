#ifndef FILTERS_H
#define FILTERS_H

#include "soloud.h"
#include "soloud_filter.h"
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

#include "../enums.h"

#include <vector>
#include <string>
#include <memory>

typedef enum FilterType
{
    BiquadResonantFilter,
    EqFilter,
    EchoFilter,
    LofiFilter,
    FlangerFilter,
    BassboostFilter,
    WaveShaperFilter,
    RobotizeFilter,
    FreeverbFilter
} FilterType_t;

struct FilterObject {
    FilterType type;
    SoLoud::Filter *filter;
    bool operator==(FilterType const &i) {
        return (i == type);
    }
};

class Filters {
    /// TODO(marco): Soloud.setGlobalFilter()
    /// Sets, or clears, the global filter.
    ///
    /// Setting the global filter to NULL will clear the global filter. 
    /// The default maximum number of global filters active is 4, but this 
    /// can be changed in a global constant in soloud.h (and rebuilding SoLoud).
public:
    Filters(SoLoud::Soloud *soloud);
    ~Filters();

    int isFilterActive(FilterType filter);
    PlayerErrors addGlobalFilter(FilterType filterType);
    bool removeGlobalFilter(FilterType filterType);
    std::vector<std::string> getFilterParamNames(FilterType filterType);
    void setFxParams(FilterType filterType, int attributeId, float value);
    float getFxParams(FilterType filterType, int attributeId);

private:
    /// main SoLoud engine, the one used by player.cpp
    SoLoud::Soloud *mSoloud;

    std::vector<FilterObject> filters;

    std::unique_ptr<SoLoud::BiquadResonantFilter> mBiquadResonantFilter;
    /// not yet available
    // std::unique_ptr<SoLoud::DuckFilter> mDuckFilter;
    std::unique_ptr<SoLoud::EqFilter> mEqFilter;
    std::unique_ptr<SoLoud::EchoFilter> mEchoFilter;
    std::unique_ptr<SoLoud::LofiFilter> mLofiFilter;
    std::unique_ptr<SoLoud::FlangerFilter> mFlangerFilter;
    /// not yet available
    // std::unique_ptr<SoLoud::DCRemovalFilter> mDCRemovalFilter;
    /// not yet available
    std::unique_ptr<SoLoud::FFTFilter> mFFTFilter;
    std::unique_ptr<SoLoud::BassboostFilter> mBassboostFilter;
    std::unique_ptr<SoLoud::WaveShaperFilter> mWaveShaperFilter;
    std::unique_ptr<SoLoud::RobotizeFilter> mRobotizeFilter;
    std::unique_ptr<SoLoud::FreeverbFilter> mFreeverbFilter;
};

#endif // PLAYER_H
