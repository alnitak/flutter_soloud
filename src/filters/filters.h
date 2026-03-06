#ifndef FILTERS_H
#define FILTERS_H

#include "../active_sound_fwd.h"
#include "../enums.h"
#include "filters_fwd.h"

#include "../soloud/include/soloud.h"
#include "../soloud/include/soloud_filter.h"

#include <memory>
#include <string>
#include <vector>

struct FilterObject {
  FilterType type;
  std::unique_ptr<SoLoud::Filter> filter;

  FilterObject(FilterType t, SoLoud::Filter *f) : type(t), filter(f) {}

  bool operator==(FilterType const &i) { return (i == type); }
};

/// Class to manage filters.
class Filters {
  /// Setting the filter to NULL will clear the filter.
  /// The default maximum number of global filters active is 4, but this
  /// can be changed in a global constant in soloud.h (and rebuilding SoLoud).
public:
  Filters(SoLoud::Soloud *soloud, ActiveSound *sound, BusData *busData);
  ~Filters() {}

  int isFilterActive(FilterType filter);

  PlayerErrors addFilter(FilterType filterType);

  bool removeFilter(FilterType filterType);

  std::vector<std::string> getFilterParamNames(FilterType filterType);

  /// If [handle]==0 the operation is done to global filters.
  void setFilterParams(SoLoud::handle handle, FilterType filterType,
                       int attributeId, float value);

  /// If [handle]==0 the operation is done to global filters.
  float getFilterParams(SoLoud::handle handle, FilterType filterType,
                        int attributeId);

  /// If [handle]==0 the operation is done to global filters.
  void fadeFilterParameter(SoLoud::handle handle, FilterType filterType,
                           int attributeId, float to, float time);

  /// If [handle]==0 the operation is done to global filters.
  void oscillateFilterParameter(SoLoud::handle handle, FilterType filterType,
                                int attributeId, float from, float to,
                                float time);

private:
  /// main SoLoud engine, the one used by player.cpp
  SoLoud::Soloud *mSoloud;

  /// The sound to manage filters for. If this and mBus are null the filters
  /// are managed globally.
  const ActiveSound *mSound;

  /// The bus to manage filters for. If this and mSound are null the filters
  /// are managed globally.
  BusData *mBusData;

  std::vector<std::unique_ptr<FilterObject>> filters;
};

/// Storage for mixing bus instances, keyed by auto-incrementing ID.
struct BusData {
  unsigned int id;
  SoLoud::Bus bus;
  // From SoLoud docs:
  // "Only one instance of a mixing bus can play at the same time,
  // however; trying to play the same bus several times stops the earlier
  // instance."
  // This implies that only one handle is needed for each bus and it is stored
  // here.
  SoLoud::handle handle; // set when bus is played on engine
  Filters filters;

  explicit BusData(unsigned int busId, SoLoud::Soloud *soloud)
      : id(busId), filters(soloud, nullptr, this) {}
};

#endif // PLAYER_H
