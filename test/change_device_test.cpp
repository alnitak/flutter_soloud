// Standalone native regression tests for the output-device swap.
//
// miniaudio_changeDevice_impl() replaces the engine's ma_device in place:
// uninit the old one, init the new one, start it. Two things about that were
// wrong, and both are only reachable once a device change actually reaches the
// backend (before #532 it did not, so nothing exercised this).
//
// 1. The swap ran with SoLoud's audio-thread mutex held. That mutex does not
//    protect gDevice at all -- the data callback reaches the engine through
//    pDevice->pUserData and takes the mutex itself inside Soloud::mix() -- so
//    holding it across the swap only starves the audio thread. On Android that
//    is fatal: on AAudio's legacy (non-MMAP) path a stream reports STARTED only
//    once its first data callback has run, so the blocked callback makes
//    ma_device_start() time out after 5s, and the cleanup ma_device_uninit()
//    then waits forever on that same blocked callback. Permanent freeze (ANR).
//
// 2. Nothing serialized the swap against the other operations on the same
//    gDevice: soloud_miniaudio_pause(), soloud_miniaudio_resume() and
//    soloud_miniaudio_deinit(). Between the uninit and the init there is no
//    device at all, so a concurrent start/stop runs against a torn struct.
//    This is not hypothetical or lifecycle-only: flutter_soloud's Player runs a
//    pause scheduler on its own thread that calls Soloud::pause() ~500ms after
//    the last voice ends. Without the device-ops lock, kConcurrentSwaps below
//    segfaults or hangs; with it, it is stable.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_change_device_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr int kSerialSwaps = 30;
constexpr int kConcurrentThreads = 4;
constexpr int kSwapsPerThread = 25;

// A single swap closes one stream and opens another: tens of milliseconds.
// The number that matters is the one this excludes -- the 5s AAudio
// state-transition timeout that the held audio mutex used to produce.
constexpr long long kSwapBudgetMs = 2000;

int gFailures = 0;

void check(bool condition, const char *what)
{
	if (condition)
	{
		std::printf("  ok: %s\n", what);
		return;
	}
	std::printf("  FAILED: %s\n", what);
	gFailures++;
}

long long nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

// A never-ending silent source, so the mixer is actually running while the
// device underneath it is replaced.
class SilenceInstance : public SoLoud::AudioSourceInstance
{
public:
	unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
						  unsigned int aBufferSize) override
	{
		for (unsigned int c = 0; c < mChannels; c++)
			for (unsigned int i = 0; i < aSamplesToRead; i++)
				aBuffer[i + c * aBufferSize] = 0.0f;
		return aSamplesToRead;
	}

	bool hasEnded() override { return false; }
};

class Silence : public SoLoud::AudioSource
{
public:
	Silence() { mChannels = 2; }

	SoLoud::AudioSourceInstance *createInstance() override
	{
		return new SilenceInstance();
	}
};

// Swapping to the null device id asks miniaudio for the current OS default,
// which is what SoLoud.changeDevice() does when called without an argument.
SoLoud::result swapToDefault(SoLoud::Soloud &soloud)
{
	return soloud.miniaudio_changeDevice(nullptr);
}

// A swap that never finishes cannot be caught from inside the process, so this
// pins down the stall that precedes the deadlock instead: with the audio mutex
// held across ma_device_start(), each swap costs the full AAudio timeout.
void testSwapsAreFastAndSurviveMixing(SoLoud::Soloud &soloud)
{
	std::printf("swaps under a live voice\n");

	Silence silence;
	const SoLoud::handle handle = soloud.play(silence);
	check(soloud.isValidVoiceHandle(handle), "the test voice is playing");

	long long worstMs = 0;
	int errors = 0;
	for (int i = 0; i < kSerialSwaps; i++)
	{
		const long long start = nowMs();
		if (swapToDefault(soloud) != SoLoud::SO_NO_ERROR)
			errors++;
		const long long elapsed = nowMs() - start;
		if (elapsed > worstMs)
			worstMs = elapsed;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	std::printf("  %d swaps, slowest %lldms\n", kSerialSwaps, worstMs);
	check(errors == 0, "every swap reported success");
	check(worstMs < kSwapBudgetMs,
		  "no swap blocked for the AAudio state-transition timeout");
	check(soloud.isValidVoiceHandle(handle),
		  "the voice survived: a device change replaces the output device, "
		  "it does not touch voices");

	soloud.stop(handle);
}

// The realistic collision is a swap against the pause scheduler's
// Soloud::pause() on its own thread. Racing swaps against each other reaches
// the same shared gDevice through the same lock, and is far easier to aim.
void testConcurrentDeviceOps(SoLoud::Soloud &soloud)
{
	std::printf("concurrent device operations\n");

	Silence silence;
	const SoLoud::handle handle = soloud.play(silence);

	std::atomic<int> errors{0};
	std::vector<std::thread> threads;
	threads.reserve(kConcurrentThreads);
	for (int t = 0; t < kConcurrentThreads; t++)
	{
		threads.emplace_back([&soloud, &errors]
							 {
			for (int i = 0; i < kSwapsPerThread; i++)
			{
				if (swapToDefault(soloud) != SoLoud::SO_NO_ERROR)
					errors++;
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			} });
	}

	// Pause/resume are the operations flutter_soloud itself drives concurrently
	// with a swap, so run them against it too rather than only swap-vs-swap.
	std::thread pauser([&soloud]
					   {
		for (int i = 0; i < kSwapsPerThread; i++)
		{
			soloud.pause();
			soloud.resume();
			std::this_thread::sleep_for(std::chrono::milliseconds(3));
		} });

	for (auto &thread : threads)
		thread.join();
	pauser.join();

	std::printf("  %d swaps across %d threads, interleaved with pause/resume\n",
				kConcurrentThreads * kSwapsPerThread, kConcurrentThreads);
	check(errors.load() == 0, "every concurrent swap reported success");
	check(soloud.isValidVoiceHandle(handle),
		  "the voice survived the concurrent swaps");

	soloud.stop(handle);
}

} // namespace

int main()
{
	SoLoud::Soloud soloud;
	const SoLoud::result init = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF,
											SoLoud::Soloud::MINIAUDIO);
	if (init != SoLoud::SO_NO_ERROR)
	{
		// No usable audio backend (headless CI, no /dev/snd). There is nothing
		// to swap, and reporting a failure here would only be noise.
		std::printf("SKIPPED: miniaudio could not open a device (error %d)\n",
					init);
		return 0;
	}

	testSwapsAreFastAndSurviveMixing(soloud);
	testConcurrentDeviceOps(soloud);

	soloud.deinit();

	if (gFailures == 0)
	{
		std::printf("All change-device tests passed.\n");
		return 0;
	}
	std::printf("%d change-device test(s) failed.\n", gFailures);
	return 1;
}
