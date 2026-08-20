// Smoke test for the render-ahead ring on the real miniaudio backend
// (Phase 4 of OPTION_B_RETROACTIVE_REMIX_PLAN.md). The null-backend harnesses
// (render_ring_test, retroactive_remix_test) drive the ring by hand; this one
// exercises the actual device callback path: small device period, callback
// top-up mixing, lock-free ring reads, playhead bookkeeping, and retroactive
// play/stop calls while the device is running.
//
// Needs a real output device; on a machine without one the test reports
// SKIPPED and exits 0 (same policy as change_device_test).
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_render_ring_device_test.sh

#include "soloud.h"
#include "soloud_wav.h"

#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int gFailures = 0;

void check(bool condition, const char *what)
{
    if (!condition)
    {
        ++gFailures;
        std::fprintf(stderr, "  FAIL: %s\n", what);
    }
}

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kQuantum = 2048;       // engine mix quantum
constexpr unsigned int kDevicePeriod = 512;   // small device period
constexpr unsigned int kRenderAhead = 8192;   // ~186 ms at 44.1 kHz

} // namespace

int main()
{
    SoLoud::Soloud soloud;
    soloud.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
    const SoLoud::result init =
        soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::MINIAUDIO,
                    kSampleRate, kQuantum, 2);
    if (init != SoLoud::SO_NO_ERROR)
    {
        std::printf("SKIPPED: miniaudio could not open a device (error %d)\n",
                    init);
        return 0;
    }

    check(soloud.isRenderAheadEnabled(), "ring should be enabled");
    // The engine quantum is the configured buffer size, decoupled from the
    // small device period.
    check(soloud.getBackendBufferSize() == kQuantum,
          "engine quantum should be the configured buffer size");

    // One second of a 440 Hz sine, mono.
    std::vector<short> pcm(kSampleRate, 0);
    for (unsigned int i = 0; i < kSampleRate; ++i)
        pcm[i] = (short)(12000 * sin(2.0 * 3.14159265 * 440.0 * i / kSampleRate));
    SoLoud::Wav wav;
    if (wav.loadRawWave16(pcm.data(), (unsigned int)pcm.size(),
                          (float)kSampleRate, 1) != SoLoud::SO_NO_ERROR)
    {
        std::printf("SKIPPED: could not build test tone\n");
        soloud.deinit();
        return 0;
    }

    const SoLoud::handle h = soloud.play(wav);
    check(h != 0, "play failed");

    // Let the device run: callback top-ups and ring reads in flight.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const SoLoud::time engine = soloud.getEngineTime();
    const SoLoud::time playhead = soloud.getPlayheadTime();
    const SoLoud::time latency = soloud.getOutputLatency();
    check(engine > 0.2, "engine time should advance while the device runs");
    check(playhead > 0.1, "playhead should advance while the device runs");
    check(playhead < engine, "playhead must trail the mix clock");
    // The gap is the ring depth: nominally renderAhead, oscillating by up to
    // one quantum plus a device period from the top-up policy.
    const double gap = engine - playhead;
    const double nominal = (double)kRenderAhead / kSampleRate;
    check(gap > nominal - 0.05 && gap < nominal + 0.1,
          "engine/playhead gap outside the ring depth envelope");
    check(latency > 0.1 && latency < 0.4,
          "output latency outside the expected envelope");

    // Retroactive calls while the device is running: must not crash, wedge,
    // or stall the clock.
    for (int i = 0; i < 20; ++i)
    {
        SoLoud::handle h2 = soloud.play(wav);
        soloud.stop(h2);
        soloud.setVolume(h, 0.5f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const SoLoud::time before = soloud.getEngineTime();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(soloud.getEngineTime() > before,
          "engine time stalled across retroactive calls");

    soloud.stopAll();
    soloud.deinit();

    if (gFailures == 0)
    {
        std::printf("All render-ring device tests passed.\n");
        return 0;
    }
    std::printf("%d render-ring device test(s) failed.\n", gFailures);
    return 1;
}
