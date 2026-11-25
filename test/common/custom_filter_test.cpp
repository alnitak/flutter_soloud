// Standalone tests for SoLoud::CustomFilter (src/filters/custom_filter.cpp)
// driven through the plugin's Filters class (src/filters/filters.cpp), the
// layer bindings.cpp uses for addCustomFilter / removeCustomFilter /
// setCustomFilter*, on a real engine with SoLoud's null driver.
//
//   - 8 slots in each scope (global, bus, sound); a 9th is refused; the
//     execution order is sound slots, then bus slots, then global slots, each
//     scope in activation order (which is not the customFilterId order);
//     removing a slot shifts the later ones down, re-adding appends it;
//   - bypass (wet 0: the callback is not called and the signal is untouched),
//     wet 1 (in place), partial wet (dry/wet blend, also for a block larger
//     than the dry scratch);
//   - parameter fade and oscillation: the values the callback sees move as
//     asked and the changed-parameter mask follows;
//   - the callback replaced while the mixer runs on another thread, with the
//     locks setCustomFilterCallback takes: the old callback never runs after
//     the swap, and a callback always gets its own user data;
//   - a slot removed (and re-added) from the API thread during the mix, in
//     every scope: its callback never runs after removeFilter() returns.
//     Run with --tsan.
//
//   ./test/common/custom_filter_test.sh [--asan|--tsan] [--strict]

#include "active_sound.h"
#include "filters/custom_filter.h"
#include "filters/filters.h"
#include "synth/custom_source.h"

#include "../support/robustness_checks.h"

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr unsigned int kRate = 48000;
constexpr unsigned int kBlock = 512;
enum Scope { kSound = 0, kBus = 1, kGlobal = 2 };
const char *kScopeName[] = {"sound", "bus", "global"};

// ---- a DC source ----------------------------------------------------------
float gSourceLevel = 0.5f;
unsigned int dcSource(float *buf, unsigned int samples, unsigned int stride,
                      unsigned int channels, float, double, double, uint32_t *,
                      void *) {
  for (unsigned int c = 0; c < channels; ++c)
    for (unsigned int i = 0; i < samples; ++i)
      buf[c * stride + i] = gSourceLevel;
  return samples;
}

// ---- filter callbacks -----------------------------------------------------
struct Slot {
  int tag = 0;     // scope * 16 + customFilterId
  char owner = 0;  // which callback this user data belongs to
  float setTo = std::nanf(""); // when not NaN, every sample becomes this
  float add = 0.0f;
  std::atomic<int> calls{0};
  std::atomic<int> wrongOwner{0};
  std::atomic<bool> retired{false}; // set once the slot must be silent
  std::atomic<int> callsAfterRetire{0};
  // Parameter trace (PARAM_1) and its changed bit, per call.
  std::vector<float> p1;
  std::vector<int> p1Changed;
  std::atomic<unsigned int> lastParamCount{0};
};

std::mutex gLogMutex;
std::vector<int> gLog;
bool gLogging = false;

void apply(float *buf, unsigned int samples, unsigned int stride,
           unsigned int channels, Slot *s) {
  for (unsigned int c = 0; c < channels; ++c)
    for (unsigned int i = 0; i < samples; ++i) {
      float &v = buf[c * stride + i];
      if (!std::isnan(s->setTo)) v = s->setTo;
      v += s->add;
    }
}

void callbackA(float *buf, unsigned int samples, unsigned int stride,
               unsigned int channels, float, double, const float *params,
               uint64_t mask, unsigned int count, void *ud) {
  auto *s = static_cast<Slot *>(ud);
  if (s == nullptr) return;
  ++s->calls;
  if (s->owner != 'A') ++s->wrongOwner;
  if (s->retired.load()) ++s->callsAfterRetire;
  s->lastParamCount.store(count);
  if (count > 1) {
    s->p1.push_back(params[1]);
    s->p1Changed.push_back((mask >> 1) & 1);
  }
  if (gLogging) {
    std::lock_guard<std::mutex> l(gLogMutex);
    gLog.push_back(s->tag);
  }
  apply(buf, samples, stride, channels, s);
}

void callbackB(float *buf, unsigned int samples, unsigned int stride,
               unsigned int channels, float, double, const float *, uint64_t,
               unsigned int, void *ud) {
  auto *s = static_cast<Slot *>(ud);
  if (s == nullptr) return;
  ++s->calls;
  if (s->owner != 'B') ++s->wrongOwner;
  if (s->retired.load()) ++s->callsAfterRetire;
  apply(buf, samples, stride, channels, s);
}

// ---- the rig: engine, a sound with its Filters, a bus with its Filters ----
struct Rig {
  explicit Rig(unsigned int block = kBlock) : block(block) {
    const SoLoud::result r =
        soloud.init(0, SoLoud::Soloud::NULLDRIVER, kRate, block, 2);
    CHECK(r == SoLoud::SO_NO_ERROR, "engine init (%d)", r);
    out.assign(block * 2, 0.0f);
    global = std::make_unique<Filters>(&soloud, nullptr, nullptr);
    sound.sound = std::make_unique<SoLoud::CustomAudioSource>(
        kRate, 2, dcSource, nullptr, nullptr, false);
    sound.soundType = TYPE_CUSTOM_SOURCE;
    sound.soundHash = 1;
    sound.filters = std::make_unique<Filters>(&soloud, &sound, nullptr);
    bus = std::make_unique<BusData>(1, &soloud);
  }
  ~Rig() {
    soloud.stopAll();
    sound.handle.clear();
    soloud.deinit();
  }
  Filters &filters(Scope s) {
    return s == kSound ? *sound.filters
                       : (s == kBus ? bus->filters : *global);
  }
  // The sound plays on the bus, the bus on the engine, as the plugin does.
  void play() {
    bus->handle = soloud.play(bus->bus);
    SoLoud::handle h = bus->bus.play(*sound.sound);
    sound.handle.push_back({h, 0.0, false});
  }
  SoLoud::handle voice() const {
    return sound.handle.empty() ? 0 : sound.handle[0].handle;
  }
  SoLoud::handle handleFor(Scope s) const {
    return s == kSound ? voice() : (s == kBus ? bus->handle : 0);
  }
  void mix(int blocks = 1) {
    for (int i = 0; i < blocks; ++i) soloud.mix(out.data(), block);
  }
  // What setCustomFilterCallback in bindings.cpp does around setCallback.
  void setCallbackLocked(SoLoud::CustomFilter *cf,
                         SoLoudFilterDspCallback_t cb, void *ud,
                         unsigned int numParams) {
    soloud.lockAudioMutex_internal();
    soloud.lockGlobalFilters_internal();
    cf->setCallback(cb, ud, numParams);
    cf->setWebJsCallbackName(nullptr);
    soloud.unlockGlobalFilters_internal();
    soloud.unlockAudioMutex_internal();
  }
  float left(unsigned int frame) const { return out[frame * 2]; }

  unsigned int block;
  SoLoud::Soloud soloud;
  std::vector<float> out;
  std::unique_ptr<Filters> global;
  ActiveSound sound;
  std::unique_ptr<BusData> bus;
};

// Configure customFilterId [id] of [scope] with [cb]/[slot] and activate it.
PlayerErrors activate(Rig &rig, Scope scope, int id, Slot &slot,
                      SoLoudFilterDspCallback_t cb = callbackA,
                      unsigned int params = 2) {
  slot.tag = scope * 16 + id;
  slot.owner = cb == callbackA ? 'A' : 'B';
  SoLoud::CustomFilter *cf = rig.filters(scope).getOrCreateCustomFilter(id);
  if (cf == nullptr) return filterNotFound;
  rig.setCallbackLocked(cf, cb, &slot, params);
  return rig.filters(scope).addFilter(CustomFilter, id);
}

void testSlotsAndOrder() {
  std::printf("8 slots per scope, execution order\n");
  Rig rig;
  Slot slots[3][8];
  for (int s = 0; s < 3; ++s) {
    for (int id = 0; id < 8; ++id) {
      const PlayerErrors e = activate(rig, Scope(s), id, slots[s][id]);
      CHECK(e == noError, "%s slot %d: %d", kScopeName[s], id, e);
    }
    Slot extra;
    CHECK(rig.filters(Scope(s)).addFilter(CustomFilter, -1) ==
              maxNumberOfFiltersReached,
          "%s: a 9th custom filter is refused", kScopeName[s]);
    CHECK(rig.filters(Scope(s)).addFilter(CustomFilter, 8) ==
                  invalidParameter ||
              rig.filters(Scope(s)).addFilter(CustomFilter, 8) ==
                  maxNumberOfFiltersReached,
          "%s: customFilterId 8 is refused", kScopeName[s]);
    CHECK(rig.filters(Scope(s)).addFilter(CustomFilter, 3) ==
                  filterAlreadyAdded ||
              rig.filters(Scope(s)).addFilter(CustomFilter, 3) ==
                  maxNumberOfFiltersReached,
          "%s: an active id cannot be added twice", kScopeName[s]);
  }
  rig.play();
  rig.mix(); // first block: lifecycle / ramps
  {
    std::lock_guard<std::mutex> l(gLogMutex);
    gLog.clear();
  }
  gLogging = true;
  rig.mix();
  gLogging = false;
  std::vector<int> want;
  for (int s = 0; s < 3; ++s)
    for (int id = 0; id < 8; ++id) want.push_back(s * 16 + id);
  CHECK(gLog == want,
        "order: sound 0..7, bus 0..7, global 0..7 (got %zu calls, first %d)",
        gLog.size(), gLog.empty() ? -1 : gLog[0]);
  for (int s = 0; s < 3; ++s)
    for (int id = 0; id < 8; ++id)
      CHECK(slots[s][id].wrongOwner == 0 &&
                slots[s][id].lastParamCount == 2,
            "%s slot %d arguments", kScopeName[s], id);
}

void testActivationOrderAndShift() {
  std::printf("activation order, remove shifts, re-add appends\n");
  for (int s = 0; s < 3; ++s) {
    Rig rig;
    Slot a, b, c;
    activate(rig, Scope(s), 5, a);
    activate(rig, Scope(s), 2, b);
    activate(rig, Scope(s), 7, c);
    rig.play();
    rig.mix();
    auto order = [&]() {
      {
        std::lock_guard<std::mutex> l(gLogMutex);
        gLog.clear();
      }
      gLogging = true;
      rig.mix();
      gLogging = false;
      return gLog;
    };
    const int base = s * 16;
    CHECK(order() == (std::vector<int>{base + 5, base + 2, base + 7}),
          "%s: runs in activation order 5, 2, 7", kScopeName[s]);
    CHECK(rig.filters(Scope(s)).isFilterActive(CustomFilter, 2) == 1,
          "%s: id 2 at slot 1", kScopeName[s]);
    CHECK(rig.filters(Scope(s)).removeFilter(CustomFilter, 2),
          "%s: remove id 2", kScopeName[s]);
    CHECK(order() == (std::vector<int>{base + 5, base + 7}),
          "%s: 5, 7 after removing 2", kScopeName[s]);
    CHECK(rig.filters(Scope(s)).isFilterActive(CustomFilter, 7) == 1,
          "%s: id 7 shifted to slot 1", kScopeName[s]);
    // The parked filter lost its callback: re-adding without a new one
    // runs nothing for it.
    const int bCalls = b.calls;
    CHECK(rig.filters(Scope(s)).addFilter(CustomFilter, 2) == noError,
          "%s: re-add id 2", kScopeName[s]);
    CHECK(order() == (std::vector<int>{base + 5, base + 7}),
          "%s: re-added id 2 has no callback until one is set",
          kScopeName[s]);
    CHECK(b.calls == bCalls, "%s: old callback not revived", kScopeName[s]);
    rig.setCallbackLocked(rig.filters(Scope(s)).getCustomFilter(2),
                          callbackA, &b, 2);
    CHECK(order() == (std::vector<int>{base + 5, base + 7, base + 2}),
          "%s: re-added id 2 runs last", kScopeName[s]);
    CHECK(!rig.filters(Scope(s)).removeFilter(CustomFilter, 4),
          "%s: removing an inactive id fails", kScopeName[s]);
  }
}

float steadyLevel(Rig &rig) {
  rig.mix(3);
  return rig.left(rig.block / 2);
}

void testWet() {
  std::printf("bypass, wet 1, partial wet\n");
  for (unsigned int block : {kBlock, 4 * kBlock}) {
    for (int s = 0; s < 3; ++s) {
      Rig rig(block);
      Slot slot;
      slot.add = 0.25f;
      activate(rig, Scope(s), 0, slot);
      rig.play();
      const SoLoud::handle h = rig.handleFor(Scope(s));
      Filters &f = rig.filters(Scope(s));

      f.setFilterParams(h, CustomFilter, SoLoud::CustomFilter::WET, 0.0f, 0);
      rig.mix(2);
      const int calls = slot.calls;
      const float dry = steadyLevel(rig);
      CHECK(slot.calls == calls, "%s/%u: wet 0 does not call the callback",
            kScopeName[s], block);

      f.setFilterParams(h, CustomFilter, SoLoud::CustomFilter::WET, 1.0f, 0);
      const float wet = steadyLevel(rig);
      CHECK(slot.calls > calls, "%s/%u: wet 1 calls it", kScopeName[s],
            block);
      CHECK(wet > dry + 0.01f, "%s/%u: wet 1 adds (%g -> %g)", kScopeName[s],
            block, dry, wet);

      f.setFilterParams(h, CustomFilter, SoLoud::CustomFilter::WET, 0.25f, 0);
      const float quarter = steadyLevel(rig);
      const float want = dry + (wet - dry) * 0.25f;
      CHECK(std::fabs(quarter - want) < 1e-3f,
            "%s/%u: wet 0.25 blends (%g, want %g)", kScopeName[s], block,
            quarter, want);
      // The whole block, not only its middle (chunked blend).
      bool uniform = true;
      for (unsigned int i = 0; i < rig.block; ++i)
        if (std::fabs(rig.left(i) - quarter) > 1e-3f) uniform = false;
      CHECK(uniform, "%s/%u: blend is uniform across the block",
            kScopeName[s], block);
      float got = -1.0f;
      CHECK(f.getFilterParams(h, CustomFilter, SoLoud::CustomFilter::WET,
                              &got, 0) == noError &&
                std::fabs(got - 0.25f) < 1e-6f,
            "%s/%u: wet reads back (%g)", kScopeName[s], block, got);
    }
  }
}

void testFadeAndOscillate() {
  std::printf("parameter fade and oscillation\n");
  for (int s = 0; s < 3; ++s) {
    Rig rig;
    Slot slot;
    activate(rig, Scope(s), 0, slot, callbackA, 2);
    rig.play();
    const SoLoud::handle h = rig.handleFor(Scope(s));
    Filters &f = rig.filters(Scope(s));
    rig.mix(2);
    slot.p1.clear();
    slot.p1Changed.clear();
    // 0 -> 10 over 0.1 s (about 9.4 blocks).
    f.fadeFilterParameter(h, CustomFilter, SoLoud::CustomFilter::PARAM_1,
                          10.0f, 0.1f, 0);
    rig.mix(16);
    bool monotonic = true;
    for (size_t i = 1; i < slot.p1.size(); ++i)
      if (slot.p1[i] < slot.p1[i - 1]) monotonic = false;
    CHECK(slot.p1.size() >= 14, "%s: about one value per block (%zu)",
          kScopeName[s], slot.p1.size());
    CHECK(monotonic, "%s: fade is monotonic", kScopeName[s]);
    CHECK(!slot.p1.empty() && slot.p1.back() == 10.0f,
          "%s: fade reaches 10 (%g)", kScopeName[s],
          slot.p1.empty() ? -1.0f : slot.p1.back());
    CHECK(slot.p1.size() > 4 && slot.p1[1] > 0.0f && slot.p1[1] < 10.0f,
          "%s: intermediate values (%g)", kScopeName[s],
          slot.p1.size() > 1 ? slot.p1[1] : -1.0f);
    int changedDuring = 0, changedAfter = 0;
    for (size_t i = 0; i < slot.p1.size(); ++i) {
      if (i > 0 && slot.p1[i] != slot.p1[i - 1]) changedDuring += !slot.p1Changed[i];
      if (i > 1 && slot.p1[i] == slot.p1[i - 1] &&
          slot.p1[i - 1] == slot.p1[i - 2])
        changedAfter += slot.p1Changed[i];
    }
    CHECK(changedDuring == 0,
          "%s: %d blocks with a new value but no changed bit", kScopeName[s],
          changedDuring);
    CHECK(changedAfter == 0,
          "%s: %d blocks flag a change once the value is steady",
          kScopeName[s], changedAfter);

    // Oscillation 0 <-> 1 with a 0.05 s period.
    slot.p1.clear();
    slot.p1Changed.clear();
    f.oscillateFilterParameter(h, CustomFilter,
                               SoLoud::CustomFilter::PARAM_1, 0.0f, 1.0f,
                               0.05f, 0);
    rig.mix(40);
    float lo = 100.0f, hi = -100.0f;
    int turns = 0;
    for (size_t i = 0; i < slot.p1.size(); ++i) {
      lo = std::min(lo, slot.p1[i]);
      hi = std::max(hi, slot.p1[i]);
      if (i > 1 && (slot.p1[i] - slot.p1[i - 1]) *
                           (slot.p1[i - 1] - slot.p1[i - 2]) <
                       0.0f)
        ++turns;
    }
    CHECK(lo >= -1e-4f && hi <= 1.0f + 1e-4f,
          "%s: oscillation stays in [0, 1] (%g..%g)", kScopeName[s], lo, hi);
    CHECK(lo < 0.2f && hi > 0.8f, "%s: oscillation spans the range (%g..%g)",
          kScopeName[s], lo, hi);
    CHECK(turns >= 4, "%s: oscillation turns around (%d)", kScopeName[s],
          turns);
  }
}

// The mixer runs on its own thread for the threaded tests.
struct MixerThread {
  explicit MixerThread(Rig &rig) : rig(rig) {
    thread = std::thread([this] {
      std::vector<float> buf(this->rig.block * 2);
      while (!stop.load()) {
        this->rig.soloud.mix(buf.data(), this->rig.block);
        ++blocks;
      }
    });
  }
  ~MixerThread() {
    stop.store(true);
    thread.join();
  }
  void waitBlocks(int n) {
    const int target = blocks.load() + n;
    while (blocks.load() < target) std::this_thread::yield();
  }
  Rig &rig;
  std::atomic<bool> stop{false};
  std::atomic<int> blocks{0};
  std::thread thread;
};

void testCallbackSwap() {
  std::printf("callback replaced while the mixer runs\n");
  for (int s = 0; s < 3; ++s) {
    Rig rig;
    rig.play();
    Slot slotA, slotB;
    activate(rig, Scope(s), 3, slotA, callbackA);
    slotB.tag = slotA.tag;
    slotB.owner = 'B';
    MixerThread mixer(rig);
    SoLoud::CustomFilter *cf = rig.filters(Scope(s)).getCustomFilter(3);
    for (int round = 0; round < 100; ++round) {
      mixer.waitBlocks(1);
      Slot &from = (round % 2 == 0) ? slotA : slotB;
      Slot &to = (round % 2 == 0) ? slotB : slotA;
      to.retired.store(false);
      rig.setCallbackLocked(cf, round % 2 == 0 ? callbackB : callbackA, &to,
                            2);
      from.retired.store(true);
      mixer.waitBlocks(1);
    }
    CHECK(slotA.calls > 0 && slotB.calls > 0, "%s: both callbacks ran",
          kScopeName[s]);
    CHECK(slotA.wrongOwner == 0 && slotB.wrongOwner == 0,
          "%s: a callback got the other's user data (%d, %d)", kScopeName[s],
          slotA.wrongOwner.load(), slotB.wrongOwner.load());
    CHECK(slotA.callsAfterRetire == 0 && slotB.callsAfterRetire == 0,
          "%s: replaced callback ran after the swap (%d, %d)", kScopeName[s],
          slotA.callsAfterRetire.load(), slotB.callsAfterRetire.load());
  }
}

void testRemoveDuringMix() {
  std::printf("slot removed and re-added from the API thread during the mix\n");
  for (int s = 0; s < 3; ++s) {
    Rig rig;
    rig.play();
    Slot keep0, keep1, victim;
    activate(rig, Scope(s), 0, keep0);
    activate(rig, Scope(s), 4, victim);
    activate(rig, Scope(s), 1, keep1);
    MixerThread mixer(rig);
    int refused = 0;
    for (int round = 0; round < 150; ++round) {
      mixer.waitBlocks(1);
      if (!rig.filters(Scope(s)).removeFilter(CustomFilter, 4)) ++refused;
      // From here on the victim's callback must never run.
      victim.retired.store(true);
      mixer.waitBlocks(1);
      if (round % 3 == 0) {
        // Remove one of the others too, so the shift happens mid-mix.
        rig.filters(Scope(s)).removeFilter(CustomFilter, 0);
        keep0.retired.store(true);
        mixer.waitBlocks(1);
        keep0.retired.store(false);
        if (activate(rig, Scope(s), 0, keep0) != noError) ++refused;
      }
      victim.retired.store(false);
      if (activate(rig, Scope(s), 4, victim) != noError) ++refused;
    }
    CHECK(refused == 0, "%s: %d add/remove calls refused", kScopeName[s],
          refused);
    CHECK(victim.callsAfterRetire == 0,
          "%s: removed slot's callback ran %d times after removeFilter()",
          kScopeName[s], victim.callsAfterRetire.load());
    CHECK(keep0.callsAfterRetire == 0,
          "%s: removed slot 0 ran %d times after removeFilter()",
          kScopeName[s], keep0.callsAfterRetire.load());
    CHECK(keep1.calls > 0 && keep1.wrongOwner == 0,
          "%s: the untouched slot kept running", kScopeName[s]);
  }
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  testSlotsAndOrder();
  testActivationOrderAndShift();
  testWet();
  testFadeAndOscillate();
  testCallbackSwap();
  testRemoveDuringMix();
  return robustness::finish("custom_filter_test");
}
