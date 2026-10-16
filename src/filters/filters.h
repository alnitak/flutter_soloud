#ifndef FILTERS_H
#define FILTERS_H

#include "../active_sound_fwd.h"
#include "../enums.h"
#include "filters_fwd.h"

#include "../soloud/include/soloud.h"
#include "../soloud/include/soloud_filter.h"
#include "custom_filter.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

struct FilterObject {
  FilterType type;
  std::unique_ptr<SoLoud::Filter> filter;
  int customFilterId;

  FilterObject(FilterType t, SoLoud::Filter *f, int cid = -1)
      : type(t), filter(f), customFilterId(cid) {}

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

  int isFilterActive(FilterType filter, int customFilterId = 0);

  PlayerErrors addFilter(FilterType filterType, int customFilterId = -1);

  bool removeFilter(FilterType filterType, int customFilterId = 0);

  SoLoud::CustomFilter *getCustomFilter(int customFilterId);

  SoLoud::CustomFilter *getOrCreateCustomFilter(int customFilterId);

  std::vector<std::string> getFilterParamNames(FilterType filterType);

  /// If [handle]==0 the operation is done to global filters.
  void setFilterParams(SoLoud::handle handle, FilterType filterType,
                       int attributeId, float value, int customFilterId = 0);

  /// If [handle]==0 the operation is done to global filters.
  float getFilterParams(SoLoud::handle handle, FilterType filterType,
                        int attributeId, int customFilterId = 0);

  /// If [handle]==0 the operation is done to global filters.
  void fadeFilterParameter(SoLoud::handle handle, FilterType filterType,
                           int attributeId, float to, float time,
                           int customFilterId = 0);

  /// If [handle]==0 the operation is done to global filters.
  void oscillateFilterParameter(SoLoud::handle handle, FilterType filterType,
                                int attributeId, float from, float to,
                                float time, int customFilterId = 0);

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

  /// Pre-configured CustomFilter instances indexed by slot (`0..7`) so
  /// parameter definitions and callbacks can be configured before `activate()`
  /// and preserved across `deactivate()` / `activate()` cycles.
  std::array<std::unique_ptr<SoLoud::CustomFilter>, FILTERS_PER_STREAM>
      mPendingCustomFilters;
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

  // Members are destroyed in reverse declaration order, so [filters] (which
  // owns the Filter objects) would be freed before ~Bus stops the bus voice.
  // Meanwhile the audio thread keeps running the bus voice's filter
  // instances, which dereference their freed parent filter (mParent).
  // Stop the bus first so its voice and filter instances are gone before
  // the filters are deleted.
  ~BusData() { bus.stop(); }
};

#endif // PLAYER_H
