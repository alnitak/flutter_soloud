// Standalone native tests for the mixer-output PCM chunk queue
// (src/mixeroutput/pcm_queue.cpp, class PcmChunkQueue), the hand-off between
// the audio callback and the encoder thread.
//
//   * fifo: chunks come out in push order with their contents intact, and are
//     moved (no copy of the sample storage);
//   * overflow: the queue is unbounded by design, so 200k pushes with no
//     consumer all succeed and drain in order (documents that there is no
//     capacity limit and no dropping);
//   * underflow: pop() on an empty queue blocks until a push arrives, and
//     until stop() when nothing arrives; pop() after stop on an empty queue
//     returns false immediately and leaves the out-parameter alone;
//   * stop_drains: stop() refuses further pushes but the consumer still gets
//     every chunk queued before it;
//   * spsc / mpmc: producer/consumer threads; per-producer order is preserved
//     with one consumer, and with several consumers every chunk is popped
//     exactly once;
//   * stop_race: producers push while another thread calls stop(); every push
//     that returned true is popped exactly once, none that returned false is.
//
// No audio device and no assets are needed. Meant to be run under --tsan too.
//
//   ./test/common/pcm_queue_test.sh [--asan|--tsan] [scenario]
//
// --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
// with ThreadSanitizer. A scenario name runs only that scenario.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "mixeroutput/pcm_queue.h"

namespace {

std::atomic<int> gFailures{0};
std::atomic<int> gAssertions{0};

#define CHECK(condition, format, ...)                                     \
    do                                                                    \
    {                                                                     \
        ++gAssertions;                                                    \
        if (!(condition))                                                 \
        {                                                                 \
            ++gFailures;                                                  \
            std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", __FILE__, \
                         __LINE__, ##__VA_ARGS__);                        \
        }                                                                 \
    } while (0)

using namespace std::chrono_literals;

// A chunk encodes (producer, sequence) in its first two samples and has a
// size derived from them, so a corrupted or mixed-up chunk is detectable.
std::vector<float> makeChunk(int producer, int seq)
{
    const size_t n = 2 + static_cast<size_t>((seq * 7 + producer) % 61);
    std::vector<float> v(n);
    v[0] = static_cast<float>(producer);
    v[1] = static_cast<float>(seq);
    for (size_t i = 2; i < n; ++i)
        v[i] = static_cast<float>(producer * 1000 + seq % 1000) + static_cast<float>(i);
    return v;
}

bool chunkValid(const std::vector<float> &v, int *producer, int *seq)
{
    if (v.size() < 2) return false;
    *producer = static_cast<int>(v[0]);
    *seq = static_cast<int>(v[1]);
    std::vector<float> want = makeChunk(*producer, *seq);
    return v == want;
}

void testFifo()
{
    PcmChunkQueue q;
    CHECK(!q.isStopped(), "new queue reports stopped");
    std::vector<float> first = makeChunk(0, 0);
    const float *firstData = first.data();
    CHECK(q.push(std::move(first)), "push 0");
    for (int i = 1; i < 10; ++i) CHECK(q.push(makeChunk(0, i)), "push %d", i);
    CHECK(q.push(std::vector<float>()), "push of an empty chunk");

    std::vector<float> out;
    CHECK(q.pop(out), "pop 0");
    CHECK(out.data() == firstData, "chunk storage was copied, not moved");
    int p, s;
    CHECK(chunkValid(out, &p, &s) && s == 0, "chunk 0 content");
    for (int i = 1; i < 10; ++i)
    {
        CHECK(q.pop(out), "pop %d", i);
        CHECK(chunkValid(out, &p, &s) && p == 0 && s == i, "chunk %d order/content", i);
    }
    CHECK(q.pop(out), "pop empty chunk");
    CHECK(out.empty(), "empty chunk came back non-empty (%zu)", out.size());
}

void testOverflow()
{
    PcmChunkQueue q;
    const int kCount = 200000;
    for (int i = 0; i < kCount; ++i)
    {
        if (!q.push(std::vector<float>{static_cast<float>(i)}))
        {
            CHECK(false, "push %d refused: queue is expected to be unbounded", i);
            return;
        }
    }
    q.stop();
    std::vector<float> out;
    int n = 0;
    bool ordered = true;
    while (q.pop(out))
    {
        if (out.size() != 1 || out[0] != static_cast<float>(n)) ordered = false;
        ++n;
    }
    CHECK(n == kCount, "drained %d of %d", n, kCount);
    CHECK(ordered, "order lost after mass push");
}

void testUnderflow()
{
    // 1) Blocks until a push arrives.
    {
        PcmChunkQueue q;
        std::atomic<bool> returned{false};
        std::atomic<bool> popResult{false};
        std::vector<float> got;
        std::thread consumer([&] {
            popResult = q.pop(got);
            returned = true;
        });
        std::this_thread::sleep_for(100ms);
        CHECK(!returned.load(), "pop() returned on an empty, running queue");
        CHECK(q.push(makeChunk(3, 4)), "push");
        consumer.join();
        int p, s;
        CHECK(popResult.load(), "pop() returned false after a push");
        CHECK(chunkValid(got, &p, &s) && p == 3 && s == 4, "wrong chunk popped");
    }
    // 2) Blocks until stop() when nothing arrives; out param untouched.
    {
        PcmChunkQueue q;
        std::atomic<bool> returned{false};
        std::atomic<bool> popResult{true};
        std::vector<float> got{42.0f};
        std::thread consumer([&] {
            popResult = q.pop(got);
            returned = true;
        });
        std::this_thread::sleep_for(100ms);
        CHECK(!returned.load(), "pop() returned before stop()");
        q.stop();
        consumer.join();
        CHECK(!popResult.load(), "pop() returned true on stopped empty queue");
        CHECK(got.size() == 1 && got[0] == 42.0f, "out param modified on false");
        CHECK(q.isStopped(), "isStopped() false after stop()");
    }
    // 3) Several blocked consumers are all released by stop().
    {
        PcmChunkQueue q;
        std::atomic<int> released{0};
        std::vector<std::thread> ts;
        for (int i = 0; i < 4; ++i)
            ts.emplace_back([&] {
                std::vector<float> v;
                if (!q.pop(v)) ++released;
            });
        std::this_thread::sleep_for(50ms);
        q.stop();
        for (auto &t : ts) t.join();
        CHECK(released.load() == 4, "only %d of 4 blocked consumers released", released.load());
    }
    // 4) Pop after stop on empty queue does not block.
    {
        PcmChunkQueue q;
        q.stop();
        q.stop();  // idempotent
        std::vector<float> v;
        auto t0 = std::chrono::steady_clock::now();
        CHECK(!q.pop(v), "pop() true on stopped empty queue");
        CHECK(std::chrono::steady_clock::now() - t0 < 1s, "pop() blocked after stop()");
    }
}

void testStopDrains()
{
    PcmChunkQueue q;
    for (int i = 0; i < 5; ++i) q.push(makeChunk(1, i));
    q.stop();
    CHECK(!q.push(makeChunk(1, 99)), "push accepted after stop()");
    std::vector<float> out;
    int n = 0, p, s;
    while (q.pop(out))
    {
        CHECK(chunkValid(out, &p, &s) && s == n, "drain order at %d", n);
        ++n;
    }
    CHECK(n == 5, "drained %d of 5 chunks queued before stop()", n);
}

void testSpsc()
{
    PcmChunkQueue q;
    const int kCount = 20000;
    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i)
        {
            q.push(makeChunk(0, i));
            if (i % 1000 == 0) std::this_thread::yield();
        }
        q.stop();
    });
    std::vector<float> out;
    int expected = 0;
    bool ok = true;
    while (q.pop(out))
    {
        int p, s;
        if (!chunkValid(out, &p, &s) || s != expected) ok = false;
        ++expected;
    }
    producer.join();
    CHECK(ok, "order or content broken");
    CHECK(expected == kCount, "popped %d of %d", expected, kCount);
}

void runMpmc(int producers, int consumers, int perProducer)
{
    PcmChunkQueue q;
    std::vector<std::vector<std::atomic<int>>> seen(producers);
    for (auto &v : seen) v = std::vector<std::atomic<int>>(perProducer);
    for (auto &v : seen)
        for (auto &a : v) a = 0;
    std::atomic<int> corrupt{0};
    std::atomic<int> orderViolations{0};

    std::vector<std::thread> cs;
    for (int c = 0; c < consumers; ++c)
    {
        cs.emplace_back([&] {
            std::vector<int> lastSeq(producers, -1);
            std::vector<float> out;
            while (q.pop(out))
            {
                int p, s;
                if (!chunkValid(out, &p, &s) || p < 0 || p >= producers ||
                    s < 0 || s >= perProducer)
                {
                    ++corrupt;
                    continue;
                }
                // Within one consumer, a producer's chunks are still seen
                // in increasing order (the queue is FIFO).
                if (s <= lastSeq[p]) ++orderViolations;
                lastSeq[p] = s;
                ++seen[p][s];
            }
        });
    }
    std::vector<std::thread> ps;
    for (int p = 0; p < producers; ++p)
    {
        ps.emplace_back([&, p] {
            for (int s = 0; s < perProducer; ++s)
                if (!q.push(makeChunk(p, s))) ++corrupt;
        });
    }
    for (auto &t : ps) t.join();
    q.stop();
    for (auto &t : cs) t.join();

    int missing = 0, dup = 0;
    for (auto &v : seen)
        for (auto &a : v)
        {
            if (a.load() == 0) ++missing;
            if (a.load() > 1) ++dup;
        }
    CHECK(corrupt.load() == 0, "%dP/%dC: %d corrupt chunks or refused pushes",
          producers, consumers, corrupt.load());
    CHECK(missing == 0, "%dP/%dC: %d chunks lost", producers, consumers, missing);
    CHECK(dup == 0, "%dP/%dC: %d chunks popped twice", producers, consumers, dup);
    CHECK(orderViolations.load() == 0, "%dP/%dC: %d FIFO violations",
          producers, consumers, orderViolations.load());
}

void testMpmc()
{
    runMpmc(4, 1, 5000);
    runMpmc(4, 4, 5000);
    runMpmc(1, 3, 10000);
}

void testStopRace()
{
    for (int round = 0; round < 30; ++round)
    {
        PcmChunkQueue q;
        const int kProducers = 3;
        std::atomic<long long> acceptedSum{0}, acceptedCount{0};
        std::atomic<long long> poppedSum{0}, poppedCount{0};
        std::atomic<bool> go{false};

        std::thread consumer([&] {
            std::vector<float> out;
            while (q.pop(out))
            {
                poppedSum += static_cast<long long>(out[0]);
                ++poppedCount;
            }
        });
        std::vector<std::thread> ps;
        for (int p = 0; p < kProducers; ++p)
        {
            ps.emplace_back([&, p] {
                while (!go.load()) std::this_thread::yield();
                for (int i = 1;; ++i)
                {
                    const long long id = p * 1000000LL + i;
                    if (!q.push(std::vector<float>{static_cast<float>(id)})) break;
                    acceptedSum += id;
                    ++acceptedCount;
                }
            });
        }
        go = true;
        std::this_thread::sleep_for(std::chrono::microseconds(200 + round * 50));
        q.stop();
        for (auto &t : ps) t.join();
        consumer.join();
        CHECK(acceptedCount.load() == poppedCount.load(),
              "round %d: accepted %lld pushes, popped %lld", round,
              acceptedCount.load(), poppedCount.load());
        CHECK(acceptedSum.load() == poppedSum.load(),
              "round %d: accepted/popped chunk ids differ", round);
        CHECK(!q.push(std::vector<float>{1.0f}), "round %d: push after stop", round);
    }
}

struct Scenario
{
    const char *name;
    void (*run)();
};

const Scenario kScenarios[] = {
    {"fifo", testFifo},
    {"overflow", testOverflow},
    {"underflow", testUnderflow},
    {"stop_drains", testStopDrains},
    {"spsc", testSpsc},
    {"mpmc", testMpmc},
    {"stop_race", testStopRace},
};

}  // namespace

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const Scenario &s : kScenarios)
    {
        if (only != nullptr && std::strcmp(only, s.name) != 0) continue;
        const int before = gFailures.load();
        s.run();
        ++ran;
        std::printf("%s %s\n", gFailures.load() == before ? "PASS" : "FAIL", s.name);
        std::fflush(stdout);
    }
    if (ran == 0)
    {
        std::fprintf(stderr, "unknown scenario: %s\n", only);
        return 2;
    }
    std::printf("%d assertions, %d failures\n", gAssertions.load(), gFailures.load());
    return gFailures.load() == 0 ? 0 : 1;
}
