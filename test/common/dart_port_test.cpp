// Standalone tests for src/mixeroutput/dart_port.h: how capture packet
// tokens reach a Dart isolate's native port.
//
// The Dart VM is replaced by a fake API table in the layout
// NativeApi.initializeApiDLData hands over (major 2, a name -> function list
// ending in a null name) whose Dart_PostInteger is a fake that records the
// posts and reports some ports as closed. Packets are real ones from
// MixerOutput::copyPacket, so a discarded packet can be told from one that
// is still claimable.
//
//   - resolve(): null data, wrong major version, null function list, a table
//     without Dart_PostInteger, a table where it is not first;
//   - send(): no token (nothing to do), unresolved post function, port 0, a
//     closed port, an open port, a token that does not fit in int64: a failed
//     send discards the packet and returns false, a successful one leaves it
//     claimable;
//   - resolve() and send() from many threads at once.
//
//   ./test/common/dart_port_test.sh [--asan|--tsan] [--strict]

#include "mixeroutput/dart_port.h"

#include "../support/robustness_checks.h"

#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// ---- fake Dart VM ---------------------------------------------------------
struct Entry {
  const char *name;
  void (*function)();
};
struct Api {
  int major;
  int minor;
  const Entry *functions;
};

std::atomic<int> gPosts{0};
std::atomic<int64_t> gLastPort{0};
std::atomic<int64_t> gLastMessage{0};
// Ports >= 1000 are "closed": their isolate is gone.
bool fakePostInteger(int64_t port, int64_t message) {
  ++gPosts;
  gLastPort.store(port);
  gLastMessage.store(message);
  return port < 1000;
}
bool otherFunction(int64_t, int64_t) { return true; }

using Fn = void (*)();
const Entry kFullTable[] = {
    {"Dart_PostCObject", reinterpret_cast<Fn>(&otherFunction)},
    {"Dart_NewNativePort", reinterpret_cast<Fn>(&otherFunction)},
    {"Dart_PostInteger", reinterpret_cast<Fn>(&fakePostInteger)},
    {"Dart_CloseNativePort", reinterpret_cast<Fn>(&otherFunction)},
    {nullptr, nullptr}};
const Entry kNoPostTable[] = {
    {"Dart_PostCObject", reinterpret_cast<Fn>(&otherFunction)},
    {"Dart_PostIntegerX", reinterpret_cast<Fn>(&otherFunction)},
    {nullptr, nullptr}};
const Entry kEmptyTable[] = {{nullptr, nullptr}};

uint8_t *newPacket(uint8_t fill) {
  uint8_t data[16];
  std::memset(data, fill, sizeof(data));
  return MixerOutput::copyPacket(data, sizeof(data), 1);
}

// True when the packet is still claimable; claims (and frees) it.
bool claimable(uint8_t *token) {
  uint32_t captureId = 0;
  size_t length = 0;
  uint8_t *bytes = MixerOutput::takePacket(token, captureId, &length);
  if (bytes == nullptr) return false;
  std::free(bytes);
  return true;
}

void testResolve() {
  std::printf("resolve\n");
  CHECK(mixer_output_port::resolve(nullptr) == nullptr, "null data");
  Api wrongMajor{1, 0, kFullTable};
  CHECK(mixer_output_port::resolve(&wrongMajor) == nullptr, "major 1");
  Api futureMajor{3, 0, kFullTable};
  CHECK(mixer_output_port::resolve(&futureMajor) == nullptr, "major 3");
  Api noList{2, 0, nullptr};
  CHECK(mixer_output_port::resolve(&noList) == nullptr, "null list");
  Api empty{2, 0, kEmptyTable};
  CHECK(mixer_output_port::resolve(&empty) == nullptr, "empty list");
  Api noPost{2, 0, kNoPostTable};
  CHECK(mixer_output_port::resolve(&noPost) == nullptr,
        "list without Dart_PostInteger (a longer name does not match)");
  Api full{2, 5, kFullTable};
  CHECK(mixer_output_port::resolve(&full) == &fakePostInteger,
        "Dart_PostInteger found in the middle of the list");
}

void testSend() {
  std::printf("send\n");
  Api full{2, 0, kFullTable};
  const auto post = mixer_output_port::resolve(&full);

  // No token: nothing to send and nothing wrong.
  gPosts = 0;
  CHECK(mixer_output_port::send(post, 5, nullptr), "null token is fine");
  CHECK(mixer_output_port::send(nullptr, 0, nullptr),
        "null token with nothing resolved is fine");
  CHECK(gPosts == 0, "no post for a null token");

  // Unresolved port function.
  uint8_t *t = newPacket(1);
  CHECK(t != nullptr, "packet created");
  CHECK(!mixer_output_port::send(nullptr, 5, t), "unresolved post fails");
  CHECK(!claimable(t), "packet discarded after an unresolved send");

  // Port 0.
  t = newPacket(2);
  gPosts = 0;
  CHECK(!mixer_output_port::send(post, 0, t), "port 0 fails");
  CHECK(gPosts == 0, "port 0 is never posted to");
  CHECK(!claimable(t), "packet discarded after port 0");

  // Closed port.
  t = newPacket(3);
  gPosts = 0;
  CHECK(!mixer_output_port::send(post, 1234, t), "closed port fails");
  CHECK(gPosts == 1 && gLastPort == 1234, "the post was attempted");
  CHECK(!claimable(t), "packet discarded after a closed port");

  // Open port.
  t = newPacket(4);
  gPosts = 0;
  CHECK(mixer_output_port::send(post, 77, t), "open port succeeds");
  CHECK(gPosts == 1 && gLastPort == 77 &&
            gLastMessage == static_cast<int64_t>(reinterpret_cast<uintptr_t>(t)),
        "token posted as the message");
  CHECK(claimable(t), "packet still claimable after a successful send");
  CHECK(!claimable(t), "and only once");

  // A token that does not fit in a Dart int: refused before posting. It is
  // not a real packet, so discarding it must be a harmless no-op.
  if (sizeof(uintptr_t) == 8) {
    uint8_t *huge = reinterpret_cast<uint8_t *>(
        static_cast<uintptr_t>(0x8000000000000010ull));
    gPosts = 0;
    CHECK(!mixer_output_port::send(post, 5, huge), "token > INT64_MAX fails");
    CHECK(gPosts == 0, "and is never posted");
  }

  // A token sent twice: the second (closed port) discards the packet the
  // first one delivered; a second discard of the same token is harmless.
  t = newPacket(5);
  CHECK(mixer_output_port::send(post, 1, t), "first send");
  CHECK(!mixer_output_port::send(post, 2000, t), "second send, closed");
  CHECK(!mixer_output_port::send(post, 2000, t), "third send, closed again");
  CHECK(!claimable(t), "packet gone");

  // A made-up token is discarded without effect.
  CHECK(!mixer_output_port::send(nullptr, 5,
                                 reinterpret_cast<uint8_t *>(0x0BAD0BAD)),
        "made-up token with nothing resolved fails cleanly");
}

void testThreads() {
  std::printf("resolve and send from many threads\n");
  Api full{2, 0, kFullTable};
  constexpr int kThreads = 8;
  constexpr int kPerThread = 2000;
  std::atomic<int> wrongResolve{0}, wrongSend{0}, leaked{0}, lost{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        const auto post = mixer_output_port::resolve(&full);
        if (post != &fakePostInteger) ++wrongResolve;
        uint8_t *token = newPacket(static_cast<uint8_t>(i));
        if (token == nullptr) continue; // pending queue full: fine
        const bool open = ((i + t) % 3) != 0;
        const int64_t port = open ? 1 + t : 1000 + t;
        const bool ok = mixer_output_port::send(post, port, token);
        if (ok != open) ++wrongSend;
        const bool still = claimable(token);
        if (open && !still) ++lost;
        if (!open && still) ++leaked;
      }
    });
  }
  for (auto &th : threads) th.join();
  CHECK(wrongResolve == 0, "%d resolves returned the wrong function",
        wrongResolve.load());
  CHECK(wrongSend == 0, "%d sends returned the wrong result",
        wrongSend.load());
  CHECK(lost == 0, "%d delivered packets were not claimable", lost.load());
  CHECK(leaked == 0, "%d packets sent to a closed port stayed claimable",
        leaked.load());
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  testResolve();
  testSend();
  testThreads();
  MixerOutput::discardPackets();
  return robustness::finish("dart_port_test");
}
