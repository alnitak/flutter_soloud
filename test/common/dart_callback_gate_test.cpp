// Standalone tests for src/dart_callback_gate.h, the lock + generation gate
// every Dart trampoline call passes through.
//
//   - claim / isLive / retireAll / retire / isOwnedBy semantics;
//   - a retired registration never becomes live again, even after a new
//     claim (simulated hot restart / replacement engine);
//   - retireAll() waits for in-flight invocations and no invocation starts
//     with the retired generation afterwards (several invoker threads);
//   - generations claimed concurrently with invocations: a stale generation
//     is never reported live;
//   - lastClaimedGeneration() and currentGeneration().
//
// Build and run from the repository root with:
//
//   ./test/common/dart_callback_gate_test.sh [--asan|--tsan] [--strict]

#include "dart_callback_gate.h"

#include "../support/robustness_checks.h"

#include <chrono>
#include <thread>
#include <vector>

using namespace dart_callbacks;
using namespace std::chrono_literals;

namespace {

void testBasics() {
  std::printf("basics\n");
  CHECK(currentGeneration() == kNoGeneration, "nothing claimed yet");
  {
    InvocationPass pass;
    CHECK(!pass.isLive(kNoGeneration), "kNoGeneration is never live");
    CHECK(!pass.isLive(1), "unclaimed generation 1 is not live");
  }

  uint64_t g1;
  {
    Registration r;
    g1 = r.claim(42);
    CHECK(g1 != kNoGeneration, "claim returns a real generation");
    CHECK(r.generation() == g1, "generation() is the claimed one");
    CHECK(r.isOwnedBy(42), "owner is 42");
    CHECK(!r.isOwnedBy(43), "43 is not the owner");
    CHECK(!r.isOwnedBy(kNoEngineId), "kNoEngineId is not the owner");
  }
  CHECK(currentGeneration() == g1, "currentGeneration after claim");
  CHECK(lastClaimedGeneration() == g1, "lastClaimedGeneration after claim");
  {
    InvocationPass pass;
    CHECK(pass.isLive(g1), "claimed generation is live");
    CHECK(!pass.isLive(kNoGeneration), "kNoGeneration still not live");
    CHECK(!pass.isLive(g1 + 1), "future generation not live");
  }

  {
    Registration r;
    CHECK(!r.retire(43), "retire by a non-owner refuses");
    CHECK(r.generation() == g1, "non-owner retire left it live");
    CHECK(r.retire(42), "owner retire succeeds");
    CHECK(r.generation() == kNoGeneration, "retired");
    CHECK(!r.isOwnedBy(42), "no owner after retire");
    CHECK(!r.retire(42), "second retire is refused (nothing live)");
  }
  {
    InvocationPass pass;
    CHECK(!pass.isLive(g1), "retired generation is not live");
  }
  CHECK(currentGeneration() == kNoGeneration, "currentGeneration after retire");
  CHECK(lastClaimedGeneration() == g1, "lastClaimedGeneration keeps g1");

  // Hot restart: a new claim must not revive the old generation.
  uint64_t g2;
  {
    Registration r;
    g2 = r.claim(42);
  }
  CHECK(g2 > g1, "generations are monotonic (%llu > %llu)",
        (unsigned long long)g2, (unsigned long long)g1);
  {
    InvocationPass pass;
    CHECK(!pass.isLive(g1), "old generation stays dead after a new claim");
    CHECK(pass.isLive(g2), "new generation live");
  }

  // Replacement engine claims over a live registration: the previous one
  // dies, and the previous owner can no longer retire it.
  uint64_t g3;
  {
    Registration r;
    g3 = r.claim(7);
    CHECK(!r.isOwnedBy(42), "old owner no longer owns");
    CHECK(!r.retire(42), "old owner cannot retire the replacement");
    CHECK(r.generation() == g3, "replacement still live");
  }
  {
    InvocationPass pass;
    CHECK(!pass.isLive(g2), "replaced generation is dead");
    CHECK(pass.isLive(g3), "replacement live");
  }

  // kNoEngineId hosts (desktop / web) match registrations made that way.
  {
    Registration r;
    r.claim(kNoEngineId);
    CHECK(r.isOwnedBy(kNoEngineId), "kNoEngineId owns its own claim");
    CHECK(!r.isOwnedBy(7), "7 does not");
    CHECK(r.retire(kNoEngineId), "kNoEngineId can retire its claim");
    CHECK(!r.isOwnedBy(kNoEngineId), "and then owns nothing");
  }
  {
    Registration r;
    r.retireAll();
    CHECK(r.generation() == kNoGeneration, "retireAll on nothing is fine");
  }
}

// Invoker threads loop taking an InvocationPass and "calling" while live.
// The retirer retires and, inside the same exclusive section, raises a flag.
// No invocation may observe the flag while live, and when retireAll() returns
// nothing may be in flight.
void testRetireWaitsAndBlocks() {
  std::printf("retire waits for in-flight calls and blocks new ones\n");
  constexpr int kInvokers = 6;
  constexpr int kRounds = 200;

  std::atomic<int> inFlight{0};
  std::atomic<long> liveCalls{0};
  std::atomic<int> violationsAfterRetire{0};
  std::atomic<int> inFlightAtRetire{0};
  std::atomic<bool> stop{false};
  std::atomic<uint64_t> generation{0};
  std::atomic<bool> retired{true};

  std::vector<std::thread> invokers;
  for (int t = 0; t < kInvokers; ++t) {
    invokers.emplace_back([&] {
      while (!stop.load()) {
        uint64_t g = generation.load();
        InvocationPass pass;
        if (pass.isLive(g)) {
          if (retired.load()) ++violationsAfterRetire;
          ++inFlight;
          ++liveCalls;
          std::this_thread::yield();
          if (retired.load()) ++violationsAfterRetire;
          --inFlight;
        }
      }
    });
  }

  for (int round = 0; round < kRounds; ++round) {
    {
      Registration r;
      generation.store(r.claim(round + 1));
      retired.store(false);
    }
    std::this_thread::sleep_for(50us);
    {
      Registration r;
      if (inFlight.load() != 0) ++inFlightAtRetire;
      if ((round & 1) == 0)
        r.retireAll();
      else
        r.retire(round + 1);
      retired.store(true);
    }
    if (inFlight.load() != 0) ++inFlightAtRetire;
  }
  stop.store(true);
  for (auto &t : invokers) t.join();

  CHECK(violationsAfterRetire.load() == 0,
        "%d invocations ran with a retired generation",
        violationsAfterRetire.load());
  CHECK(inFlightAtRetire.load() == 0,
        "%d retirements overlapped an in-flight call", inFlightAtRetire.load());
  CHECK(liveCalls.load() > 0, "some live calls happened (%ld)",
        liveCalls.load());
}

// A pass held for a while must block retirement until it is released.
void testRetireBlocksOnHeldPass() {
  std::printf("retire blocks while a pass is held\n");
  uint64_t g;
  {
    Registration r;
    g = r.claim(1);
  }
  std::atomic<bool> passTaken{false};
  std::atomic<bool> released{false};
  std::atomic<bool> retiredBeforeRelease{false};

  std::thread holder([&] {
    InvocationPass pass;
    CHECK(pass.isLive(g), "pass sees the live generation");
    passTaken.store(true);
    std::this_thread::sleep_for(100ms);
    released.store(true);
  });
  while (!passTaken.load()) std::this_thread::yield();
  auto start = std::chrono::steady_clock::now();
  {
    Registration r;
    if (!released.load()) retiredBeforeRelease.store(true);
    r.retireAll();
  }
  auto waited = std::chrono::steady_clock::now() - start;
  holder.join();
  CHECK(!retiredBeforeRelease.load(),
        "Registration acquired while a pass was still held");
  CHECK(waited >= 50ms, "retire waited only %lld ms",
        (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
            waited)
            .count());
}

// Claims racing with invocations: a generation that has been replaced or
// retired is never live inside a pass.
void testConcurrentClaims() {
  std::printf("concurrent claims never revive a stale generation\n");
  constexpr int kInvokers = 4;
  constexpr int kClaims = 3000;

  std::atomic<uint64_t> newest{0};
  // Every generation <= deadUpTo has been replaced or retired. Written only
  // under the exclusive gate.
  std::atomic<uint64_t> deadUpTo{0};
  std::atomic<int> staleLive{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> invokers;
  for (int t = 0; t < kInvokers; ++t) {
    invokers.emplace_back([&, t] {
      uint64_t mine = 0;
      int n = 0;
      while (!stop.load()) {
        // Keep an old snapshot for a while, then refresh.
        if ((++n % (8 + t)) == 0) mine = newest.load();
        InvocationPass pass;
        if (pass.isLive(mine) && mine <= deadUpTo.load()) ++staleLive;
      }
    });
  }

  std::thread retirer([&] {
    for (int i = 0; i < kClaims / 10 && !stop.load(); ++i) {
      std::this_thread::sleep_for(20us);
      Registration r;
      deadUpTo.store(lastClaimedGeneration());
      r.retireAll();
    }
  });

  uint64_t last = lastClaimedGeneration();
  bool monotonic = true;
  for (int i = 0; i < kClaims; ++i) {
    Registration r;
    deadUpTo.store(lastClaimedGeneration());
    uint64_t g = r.claim(i % 3);
    if (g <= last) monotonic = false;
    last = g;
    newest.store(g);
  }
  retirer.join();
  stop.store(true);
  for (auto &t : invokers) t.join();

  CHECK(staleLive.load() == 0, "%d stale generations reported live",
        staleLive.load());
  CHECK(monotonic, "claim() returned a non-increasing generation");
  CHECK(lastClaimedGeneration() >= last, "lastClaimedGeneration behind");

  // currentGeneration() from many threads while claims/retires happen must
  // only ever return kNoGeneration or a claimed generation.
  std::atomic<int> bogus{0};
  stop.store(false);
  std::vector<std::thread> readers;
  for (int t = 0; t < 3; ++t)
    readers.emplace_back([&] {
      while (!stop.load()) {
        uint64_t c = currentGeneration();
        if (c != kNoGeneration && c > lastClaimedGeneration()) ++bogus;
      }
    });
  for (int i = 0; i < 2000; ++i) {
    Registration r;
    if (i % 2)
      r.claim(1);
    else
      r.retireAll();
  }
  stop.store(true);
  for (auto &t : readers) t.join();
  CHECK(bogus.load() == 0, "currentGeneration returned an unclaimed value");
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  testBasics();
  testRetireWaitsAndBlocks();
  testRetireBlocksOnHeldPass();
  testConcurrentClaims();
  return robustness::finish("dart_callback_gate_test");
}
