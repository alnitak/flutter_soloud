// Standalone tests for src/vst3/vst3_param_changes.h (built only with
// SOLOUD_ENABLE_VST3): the lock-free SPSC queue that carries parameter
// changes and MIDI notes from the API thread to the audio thread, and the
// preallocated IParameterChanges / IParamValueQueue / IEventList handed to
// the plug-in.
//
//   - LockFreeSpscQueue: FIFO, capacity - 1 items, full / empty, wraps,
//     clear(); one producer + one consumer thread with numbered events:
//     nothing lost, duplicated, reordered or torn (run with --tsan);
//   - PreallocatedParamValueQueue: points in sampleOffset order, a point at
//     the last offset replaces it, a full queue keeps the newest value in its
//     last point, bounds of getPoint(), reset();
//   - PreallocatedParameterChanges: one queue per parameter id (several
//     points for the same parameter land in the same queue), kMaxQueues
//     limit, clear(), getParameterData() bounds;
//   - PreallocatedEventList: kMaxEvents limit, getEvent() bounds;
//   - queryInterface of all three.
//
//   ./test/common/vst3_param_changes_test.sh [--asan|--tsan] [--strict]

#include "vst3/vst3_param_changes.h"

#include "../support/robustness_checks.h"

#include <thread>
#include <vector>

using namespace SoLoud;
using Steinberg::int32;
using Steinberg::kResultFalse;
using Steinberg::kResultTrue;
using Steinberg::Vst::ParamValue;

namespace {

void testQueueBasics() {
  std::printf("LockFreeSpscQueue basics\n");
  LockFreeSpscQueue<int, 8> q;
  int v = -1;
  CHECK(!q.pop(v), "empty queue pops nothing");
  int pushed = 0;
  while (q.push(pushed)) ++pushed;
  CHECK(pushed == 7, "capacity 8 holds 7 items (got %d)", pushed);
  CHECK(!q.push(99), "push into a full queue fails");
  for (int i = 0; i < 7; ++i) {
    CHECK(q.pop(v) && v == i, "FIFO order: want %d got %d", i, v);
  }
  CHECK(!q.pop(v), "drained");

  // Many wraps with a varying fill level.
  int next = 0, expect = 0, bad = 0;
  for (int round = 0; round < 1000; ++round) {
    const int n = round % 8;
    for (int i = 0; i < n; ++i)
      if (q.push(next)) ++next;
    const int m = (round * 3) % 8;
    for (int i = 0; i < m; ++i)
      if (q.pop(v)) {
        if (v != expect) ++bad;
        ++expect;
      }
  }
  while (q.pop(v)) {
    if (v != expect) ++bad;
    ++expect;
  }
  CHECK(bad == 0 && expect == next, "%d items out of order across wraps",
        bad);

  for (int i = 0; i < 5; ++i) q.push(i);
  q.clear();
  CHECK(!q.pop(v), "clear() empties the queue");
  CHECK(q.push(42) && q.pop(v) && v == 42, "usable after clear()");
  int filled = 0;
  while (q.push(filled)) ++filled;
  CHECK(filled == 7, "full capacity after clear() (%d)", filled);
}

void testQueueThreaded() {
  std::printf("LockFreeSpscQueue: producer + consumer threads\n");
  constexpr int kTotal = 300000;
  LockFreeSpscQueue<Vst3ParamChangeEvent, 64> q;
  std::thread producer([&] {
    for (int i = 0; i < kTotal;) {
      Vst3ParamChangeEvent e{static_cast<Steinberg::Vst::ParamID>(i),
                             i * 0.25, i % 4096};
      if (q.push(e))
        ++i;
      else
        std::this_thread::yield();
    }
  });
  int expect = 0, bad = 0, torn = 0;
  Vst3ParamChangeEvent e{};
  while (expect < kTotal) {
    if (!q.pop(e)) {
      std::this_thread::yield();
      continue;
    }
    if (static_cast<int>(e.paramId) != expect) ++bad;
    if (e.normalizedValue != e.paramId * 0.25 ||
        e.sampleOffset != static_cast<int32_t>(e.paramId % 4096))
      ++torn;
    expect = static_cast<int>(e.paramId) + 1;
  }
  producer.join();
  CHECK(bad == 0, "%d events lost / duplicated / reordered", bad);
  CHECK(torn == 0, "%d torn events", torn);
  CHECK(!q.pop(e), "nothing left");

  // MIDI notes the same way, with the consumer clearing now and then (the
  // read index is the consumer's): what it pops after a clear() is still in
  // order.
  std::printf("LockFreeSpscQueue: clear() from the consumer during use\n");
  LockFreeSpscQueue<Vst3MidiNoteEvent, 32> mq;
  std::atomic<bool> done{false};
  std::thread midiProducer([&] {
    for (int i = 0; i < 100000;) {
      Vst3MidiNoteEvent n{};
      n.type = static_cast<uint8_t>(i & 1);
      n.pitch = static_cast<int16_t>(i % 128);
      n.noteId = i;
      n.sampleOffset = i;
      if (mq.push(n))
        ++i;
      else
        std::this_thread::yield();
    }
    done.store(true);
  });
  int last = -1, disorder = 0, pops = 0;
  Vst3MidiNoteEvent n{};
  while (true) {
    const bool producerDone = done.load();
    if (++pops % 97 == 0) {
      mq.clear();
      continue;
    }
    if (!mq.pop(n)) {
      if (producerDone) break;
      std::this_thread::yield();
      continue;
    }
    if (n.noteId <= last || n.sampleOffset != n.noteId ||
        n.pitch != n.noteId % 128)
      ++disorder;
    last = n.noteId;
  }
  midiProducer.join();
  CHECK(disorder == 0, "%d MIDI events out of order / torn around clear()",
        disorder);
}

void testParamValueQueue() {
  std::printf("PreallocatedParamValueQueue\n");
  PreallocatedParamValueQueue q;
  q.reset(17);
  CHECK(q.getParameterId() == 17, "id");
  CHECK(q.getPointCount() == 0, "empty after reset");
  int32 off = -1, idx = -1;
  ParamValue val = -1;
  CHECK(q.getPoint(0, off, val) == kResultFalse, "getPoint on empty queue");
  CHECK(q.getPoint(-1, off, val) == kResultFalse, "negative index");

  // Increasing offsets keep their order.
  for (int i = 0; i < 5; ++i) {
    CHECK(q.addPoint(i * 10, i * 0.1, idx) == kResultTrue && idx == i,
          "addPoint %d -> index %d", i, idx);
  }
  CHECK(q.getPointCount() == 5, "5 points");
  bool sorted = true;
  int32 prev = -1;
  for (int i = 0; i < q.getPointCount(); ++i) {
    q.getPoint(i, off, val);
    if (off < prev) sorted = false;
    prev = off;
    CHECK(val == i * 0.1, "point %d value %g", i, val);
  }
  CHECK(sorted, "points are in sampleOffset order");
  CHECK(q.getPoint(5, off, val) == kResultFalse, "index == count rejected");

  // A point at the offset of the last one replaces it.
  CHECK(q.addPoint(40, 0.9, idx) == kResultTrue && idx == 4,
        "same offset replaces the last point (index %d)", idx);
  CHECK(q.getPointCount() == 5, "still 5 points");
  q.getPoint(4, off, val);
  CHECK(off == 40 && val == 0.9, "last point now 0.9 (got %d, %g)", off, val);

  // Every change the host makes is at offset 0: they collapse to one point
  // holding the newest value.
  q.reset(3);
  for (int i = 0; i < 100; ++i) q.addPoint(0, i / 100.0, idx);
  CHECK(q.getPointCount() == 1, "same-offset changes collapse (%d points)",
        q.getPointCount());
  q.getPoint(0, off, val);
  CHECK(val == 0.99, "newest value kept (%g)", val);

  // Full queue: the last point takes the newest value.
  q.reset(4);
  for (int i = 0; i < PreallocatedParamValueQueue::kMaxPoints + 10; ++i)
    q.addPoint(i, i * 0.01, idx);
  CHECK(q.getPointCount() == PreallocatedParamValueQueue::kMaxPoints,
        "count capped at kMaxPoints (%d)", q.getPointCount());
  CHECK(idx == PreallocatedParamValueQueue::kMaxPoints - 1,
        "overflowing add reports the last index (%d)", idx);
  q.getPoint(PreallocatedParamValueQueue::kMaxPoints - 1, off, val);
  const int last = PreallocatedParamValueQueue::kMaxPoints + 9;
  CHECK(val == last * 0.01 && off == last,
        "last point holds the newest value (%d, %g)", off, val);
  sorted = true;
  prev = -1;
  for (int i = 0; i < q.getPointCount(); ++i) {
    q.getPoint(i, off, val);
    if (off < prev) sorted = false;
    prev = off;
  }
  CHECK(sorted, "still in sampleOffset order when full");

  // Offsets added out of order are kept as given. The host only ever adds
  // offset 0, so this is informational: VST3 wants the points sorted.
  q.reset(5);
  q.addPoint(20, 0.2, idx);
  q.addPoint(10, 0.1, idx);
  q.getPoint(0, off, val);
  std::printf("  note: an out-of-order addPoint is %s\n",
              off == 10 ? "sorted in" : "appended as given (host adds offset 0 only)");

  void *obj = nullptr;
  CHECK(q.queryInterface(Steinberg::Vst::IParamValueQueue::iid, &obj) ==
                kResultTrue &&
            obj == static_cast<Steinberg::Vst::IParamValueQueue *>(&q),
        "queryInterface IParamValueQueue");
  CHECK(q.queryInterface(Steinberg::Vst::IEventList::iid, &obj) ==
                Steinberg::kNoInterface &&
            obj == nullptr,
        "queryInterface of an unrelated iid");
  CHECK(q.addRef() == 1 && q.release() == 1, "static ref count");
}

void testParameterChanges() {
  std::printf("PreallocatedParameterChanges\n");
  PreallocatedParameterChanges pc;
  CHECK(pc.getParameterCount() == 0, "empty");
  CHECK(pc.getParameterData(0) == nullptr, "no data at 0");
  CHECK(pc.getParameterData(-1) == nullptr, "no data at -1");

  int32 idx = -1, pidx = -1;
  auto *a = pc.addParameterData(100, idx);
  CHECK(a != nullptr && idx == 0, "first id at index 0");
  auto *b = pc.addParameterData(200, idx);
  CHECK(b != nullptr && idx == 1 && b != a, "second id at index 1");
  // Several points for the same parameter go to the same queue.
  a->addPoint(0, 0.1, pidx);
  a->addPoint(32, 0.2, pidx);
  auto *a2 = pc.addParameterData(100, idx);
  CHECK(a2 == a && idx == 0, "same id returns the same queue");
  a2->addPoint(64, 0.3, pidx);
  CHECK(a->getPointCount() == 3, "3 points on parameter 100 (%d)",
        a->getPointCount());
  CHECK(pc.getParameterCount() == 2, "2 parameters");
  CHECK(pc.getParameterData(1) == b, "getParameterData(1)");
  CHECK(pc.getParameterData(2) == nullptr, "getParameterData(count)");

  for (int i = 2; i < PreallocatedParameterChanges::kMaxQueues; ++i)
    CHECK(pc.addParameterData(1000 + i, idx) != nullptr, "queue %d", i);
  CHECK(pc.getParameterCount() == PreallocatedParameterChanges::kMaxQueues,
        "all queues used");
  CHECK(pc.addParameterData(99999, idx) == nullptr,
        "queue 129 refused when full");
  CHECK(pc.addParameterData(200, idx) == b && idx == 1,
        "an existing id is still found when full");

  pc.clear();
  CHECK(pc.getParameterCount() == 0, "clear() empties");
  auto *c = pc.addParameterData(100, idx);
  CHECK(c != nullptr && idx == 0 && c->getPointCount() == 0,
        "a queue reused after clear() starts empty (%d points)",
        c ? c->getPointCount() : -1);
  CHECK(c->getParameterId() == 100, "and has the new id");

  void *obj = nullptr;
  CHECK(pc.queryInterface(Steinberg::Vst::IParameterChanges::iid, &obj) ==
                kResultTrue &&
            obj != nullptr,
        "queryInterface IParameterChanges");
  CHECK(pc.queryInterface(Steinberg::FUnknown::iid, &obj) == kResultTrue,
        "queryInterface FUnknown");
}

void testEventList() {
  std::printf("PreallocatedEventList\n");
  PreallocatedEventList el;
  Steinberg::Vst::Event e{};
  CHECK(el.getEventCount() == 0, "empty");
  CHECK(el.getEvent(0, e) == kResultFalse, "getEvent on empty");
  for (int i = 0; i < PreallocatedEventList::kMaxEvents; ++i) {
    Steinberg::Vst::Event in{};
    in.sampleOffset = i;
    in.type = Steinberg::Vst::Event::kNoteOnEvent;
    in.noteOn.pitch = static_cast<int16_t>(i % 128);
    CHECK(el.addEvent(in) == kResultTrue, "add %d", i);
  }
  Steinberg::Vst::Event extra{};
  CHECK(el.addEvent(extra) == kResultFalse, "event 129 refused");
  CHECK(el.getEventCount() == PreallocatedEventList::kMaxEvents, "count");
  CHECK(el.getEvent(127, e) == kResultTrue && e.sampleOffset == 127,
        "last event intact");
  CHECK(el.getEvent(128, e) == kResultFalse, "getEvent(count) rejected");
  CHECK(el.getEvent(-1, e) == kResultFalse, "getEvent(-1) rejected");
  el.clear();
  CHECK(el.getEventCount() == 0, "clear() empties");
  void *obj = nullptr;
  CHECK(el.queryInterface(Steinberg::Vst::IEventList::iid, &obj) ==
                kResultTrue &&
            obj != nullptr,
        "queryInterface IEventList");
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  testQueueBasics();
  testQueueThreaded();
  testParamValueQueue();
  testParameterChanges();
  testEventList();
  return robustness::finish("vst3_param_changes_test");
}
