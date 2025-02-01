#ifndef FILTERS_H
#define FILTERS_H

#include "../active_sound_fwd.h"
#include "filters_fwd.h"
#include "enums.h"

#include "soloud.h"
#include "soloud_filter.h"

#include <vector>
#include <string>
#include <memory>

struct FilterObject
{
    FilterType type;
    std::unique_ptr<SoLoud::Filter> filter;

    FilterObject(FilterType t, SoLoud::Filter* f)
        : type(t), filter(f) {}

    bool operator==(FilterType const &i)
    {
        return (i == type);
    }
};

/// Class to manage filters.
class Filters
{
    /// Setting the filter to NULL will clear the filter.
    /// The default maximum number of global filters active is 4, but this
    /// can be changed in a global constant in soloud.h (and rebuilding SoLoud).
public:
    Filters(SoLoud::Soloud *soloud, ActiveSound *sound);
    ~Filters() {}

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
    const ActiveSound *mSound;

    std::vector<std::unique_ptr<FilterObject>> filters;
};

#endif // PLAYER_H
