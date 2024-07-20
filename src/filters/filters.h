#ifndef FILTERS_H
#define FILTERS_H

#include "../active_sound.h"
#include "pitch_shift_filter.h"

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

#include <vector>
#include <string>
#include <memory>

struct FilterObject
{
    FilterType type;
    SoLoud::Filter *filter;
    bool operator==(FilterType const &i)
    {
        return (i == type);
    }
};

/// Class to manage global filters.
class Filters
{
    /// TODO(marco): Soloud.setGlobalFilter()
    /// Sets, or clears, the global filter.
    ///
    /// Setting the global filter to NULL will clear the global filter.
    /// The default maximum number of global filters active is 4, but this
    /// can be changed in a global constant in soloud.h (and rebuilding SoLoud).
public:
    Filters(SoLoud::Soloud *soloud, ActiveSound *sound);
    ~Filters();

    int isFilterActive(FilterType filter);
    
    PlayerErrors addFilter(FilterType filterType);
    
    bool removeFilter(FilterType filterType);
    
    std::vector<std::string> getFilterParamNames(FilterType filterType);
    
    /// If [handle]==0 the operation is done to global filters.
    void setFilterParams(SoLoud::handle handle, FilterType filterType, int attributeId, float value);
    
    /// If [handle]==0 the operation is done to global filters.
    float getFilterParams(SoLoud::handle handle, FilterType filterType, int attributeId);
    
    /// If [handle]==0 the operation is done to global filters.
    void fadeFilterParameter(SoLoud::handle handle, FilterType filterType, int attributeId, float to, float time);
    
    /// If [handle]==0 the operation is done to global filters.
    void oscillateFilterParameter(
        SoLoud::handle handle,
        FilterType filterType,
        int attributeId,
        float from,
        float to,
        float time);

private:
    /// main SoLoud engine, the one used by player.cpp
    SoLoud::Soloud *mSoloud;

    /// The sound to manage filters for. If null the filters are managed globally.
    ActiveSound *mSound;

    std::vector<FilterObject> filters;

    std::unique_ptr<SoLoud::BiquadResonantFilter> mBiquadResonantFilter;
    /// not yet available
    // std::unique_ptr<SoLoud::DuckFilter> mDuckFilter;
    std::unique_ptr<SoLoud::EqFilter> mEqFilter;
    std::unique_ptr<PitchShift> mPitchFilter;
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
