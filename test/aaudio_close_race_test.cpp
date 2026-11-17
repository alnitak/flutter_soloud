// Android-only stress test: closing AAudio streams while the output they play
// on is being rerouted.
//
// On Android 11 and earlier a legacy (non-MMAP) AAudio stream is an AudioTrack
// whose callbacks hold a raw pointer to the stream. AAudioStream_close()
// deletes the stream at once, so a callback already being delivered -- audio
// data, or a route change -- can run on a freed stream and crash the process.
// Oboe closes a stream by stopping it, waiting about a burst, then closing it.
// miniaudio closes AAudio streams directly, so flutter_soloud does Oboe's stop
// and wait in the AAudioStream_close it installs in miniaudio's context
// (closeAAudioStream() in soloud_miniaudio.cpp).
//
// The engine is brought up first, so that the context is flutter_soloud's own.
// Then several threads open, start and close devices on that context, another
// lists the playback devices (which opens and closes probe streams), and
// another keeps rerouting the output by opening short-lived raw AAudio
// streams with alternating explicit devices and usages. A crash is a failure.
// On an Android 11 x86 emulator, closing streams without the wait crashed on
// an AudioTrack thread, in AudioStreamLegacy_callback() or in
// AudioTrack::restoreTrack_l(), within minutes.
//
// The devices the test opens itself are kept out of miniaudio's rerouting.
// Where the output really switches (API 36 does), their streams disconnect,
// and miniaudio's reroute job, unserialized with ma_device_uninit(), can
// reopen a stream on a device that has since been uninitialized and zeroed:
// a crash in miniaudio, with or without the wait. flutter_soloud serializes
// reroutes against its own device (ScopedRerouteHold in soloud_miniaudio.cpp);
// here each device is marked as tearing down as soon as it is initialized,
// which stops miniaudio queueing reroutes for it, and the job thread is
// drained before the device is uninitialized, for a reroute queued just
// before that.
//
// Run from the flutter_soloud repository root with a device or emulator
// attached:
//
//   ./test/run_aaudio_close_race_test.sh [seconds] [threads] [device] [device]
//
// The two devices are the AAudio device IDs the rerouting thread alternates
// between: 2 (speaker) and 5 (telephony) on the stock emulator image. See
// `adb shell dumpsys media.audio_policy` for a device's own.

#include "soloud.h"
#include "backend/miniaudio/miniaudio.h"

#include <aaudio/AAudio.h>
#include <android/api-level.h>
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace SoLoud
{
	extern ma_context context;
	void miniaudio_setLowLatency(bool aLowLatency);
	ma_result miniaudio_listPlaybackDevices(std::vector<ma_device_info> &aDevices);
#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
	unsigned int miniaudio_testEnumerationProbeCloses();
	unsigned int miniaudio_testEnumerationProbeWaits();
	int miniaudio_testEnumerationBackend();
#endif
}

namespace
{

aaudio_data_callback_result_t silence(AAudioStream *, void *, void *audio,
									  int32_t frames)
{
	std::memset(audio, 0, frames * 2 * sizeof(float));
	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void ignoreError(AAudioStream *, void *, aaudio_result_t) {}

struct Drain
{
	std::mutex mutex;
	std::condition_variable cv;
	bool done = false;
};

ma_result markDrained(ma_job *pJob)
{
	auto *drain = reinterpret_cast<Drain *>(pJob->data.custom.data0);
	// Notified under the lock: the waiter owns `drain`, and returns as soon
	// as it can take the lock.
	std::lock_guard<std::mutex> lock(drain->mutex);
	drain->done = true;
	drain->cv.notify_all();
	return MA_SUCCESS;
}

/// Wait until the context's job thread has run every job queued before now.
void drainJobThread()
{
	Drain drain;
	ma_job job = ma_job_init(MA_JOB_TYPE_CUSTOM);
	job.data.custom.proc = markDrained;
	job.data.custom.data0 = (ma_uintptr)&drain;
	while (ma_device_job_thread_post(&SoLoud::context.aaudio.jobThread, &job) !=
		   MA_SUCCESS)
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	std::unique_lock<std::mutex> lock(drain.mutex);
	drain.cv.wait(lock, [&drain] { return drain.done; });
}

void deviceSilence(ma_device *, void *output, const void *, ma_uint32 frames)
{
	std::memset(output, 0, frames * 2 * sizeof(float));
}

/// Opens, briefly runs and closes a raw AAudio stream routed to [deviceId],
/// which moves the shared output and sends every stream on it a route change.
/// Closed Oboe's way, so that only the streams under test can crash.
void reroute(int32_t deviceId, bool voiceCommunication)
{
	AAudioStreamBuilder *builder = nullptr;
	if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
		return;
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
	AAudioStreamBuilder_setChannelCount(builder, 2);
	AAudioStreamBuilder_setDeviceId(builder, deviceId);
	if (voiceCommunication)
	{
		// The close checks also build for API 26, before usage selection was
		// added. Resolve it at runtime so newer devices still exercise this
		// callback path while an older device can run the pre-init probe check.
		using SetUsage = void (*)(AAudioStreamBuilder *, aaudio_usage_t);
		const auto setUsage = reinterpret_cast<SetUsage>(
			dlsym(RTLD_DEFAULT, "AAudioStreamBuilder_setUsage"));
		if (setUsage != nullptr)
			setUsage(builder, AAUDIO_USAGE_VOICE_COMMUNICATION);
	}
	AAudioStreamBuilder_setDataCallback(builder, silence, nullptr);
	AAudioStreamBuilder_setErrorCallback(builder, ignoreError, nullptr);
	AAudioStream *stream = nullptr;
	if (AAudioStreamBuilder_openStream(builder, &stream) == AAUDIO_OK)
	{
		AAudioStream_requestStart(stream);
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
		AAudioStream_requestStop(stream);
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		AAudioStream_close(stream);
	}
	AAudioStreamBuilder_delete(builder);
}

} // namespace

int main(int argc, char **argv)
{
	const int seconds = argc > 1 ? std::atoi(argv[1]) : 120;
	// On an Android 11 emulator 6 closer threads did not bring out the crash
	// in two-minute runs; 10 did, in each of four three-minute runs.
	const int threads = argc > 2 ? std::atoi(argv[2]) : 10;
	const int32_t devices[2] = {argc > 3 ? std::atoi(argv[3]) : 2,
								argc > 4 ? std::atoi(argv[4]) : 5};

	// The legacy (AudioTrack) path, where the raw-pointer callbacks are.
	SoLoud::miniaudio_setLowLatency(false);

	// An app process inherits libaaudio.so, and the libraries it loads, from
	// zygote. Here miniaudio's dlopen() is the only reference, so the
	// dlclose() in deinit() would unmap libaudioclient and libbinder while
	// the binder thread pool still runs in them. Keep it loaded, as zygote
	// does.
	dlopen("libaaudio.so", RTLD_NOW);

#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
	// Before engine init the global context has no function table. Enumeration
	// creates its own context, whose probe closes need the same protection.
	// Count the intercepted closes even on newer SDKs, where probes should
	// bypass the delay: both the coverage and the SDK policy are asserted.
	const auto closesBefore = SoLoud::miniaudio_testEnumerationProbeCloses();
	const auto waitsBefore = SoLoud::miniaudio_testEnumerationProbeWaits();
	std::vector<ma_device_info> beforeInitDevices;
	if (SoLoud::miniaudio_listPlaybackDevices(beforeInitDevices) != MA_SUCCESS) {
		std::fprintf(stderr, "FAIL: pre-init enumeration failed\n");
		return 1;
	}
	if (SoLoud::miniaudio_testEnumerationBackend() == ma_backend_aaudio) {
		const auto probeCloses = SoLoud::miniaudio_testEnumerationProbeCloses() - closesBefore;
		const auto probeWaits = SoLoud::miniaudio_testEnumerationProbeWaits() - waitsBefore;
		const auto expectedWaits = android_get_device_api_level() <= 30 ? probeCloses : 0;
		if (probeCloses == 0 || probeWaits != expectedWaits) {
			std::fprintf(stderr, "FAIL: pre-init probes: %u protected closes, %u waits (expected %u)\n",
				probeCloses, probeWaits, expectedWaits);
			return 1;
		}
		std::printf("Pre-init probes: %u protected closes, %u waits\n", probeCloses, probeWaits);
	} else {
		std::printf("Pre-init AAudio check skipped: enumeration selected another backend\n");
	}
#endif

	SoLoud::Soloud soloud;
	if (soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::MINIAUDIO,
					48000, 2048, 2) != SoLoud::SO_NO_ERROR)
	{
		std::printf("SKIPPED: miniaudio could not open a device\n");
		return 0;
	}
	if (SoLoud::context.backend != ma_backend_aaudio)
	{
		std::printf("SKIPPED: the device is not using AAudio\n");
		soloud.deinit();
		return 0;
	}

	std::atomic<bool> stop{false};
	std::atomic<long> closes{0};
	std::atomic<long> reroutes{0};
	std::atomic<long> enumerations{0};

	std::thread rerouter([&]
						 {
							 for (long i = 0; !stop.load(); ++i)
							 {
								 reroute(devices[i % 2], i % 3 == 0);
								 ++reroutes;
							 } });

	std::thread enumerator([&]
						   {
							   while (!stop.load())
							   {
								   std::vector<ma_device_info> found;
								   SoLoud::miniaudio_listPlaybackDevices(found);
								   ++enumerations;
								   std::this_thread::sleep_for(
									   std::chrono::milliseconds(50));
							   } });

	std::vector<std::thread> closers;
	for (int t = 0; t < threads; ++t)
		closers.emplace_back([&, t]
							 {
								 std::minstd_rand random(t + 1);
								 while (!stop.load())
								 {
									 ma_device_config config =
										 ma_device_config_init(ma_device_type_playback);
									 config.playback.format = ma_format_f32;
									 config.playback.channels = 2;
									 config.sampleRate = 48000;
									 config.dataCallback = deviceSilence;
									 ma_device device;
									 // Too many streams at once are refused;
									 // that is not what this tests.
									 if (ma_device_init(&SoLoud::context, &config,
														&device) != MA_SUCCESS)
										 continue;
									 // Keep miniaudio from rerouting it (see
									 // the top of this file).
									 __atomic_store_n(&device.aaudio.isTearingDown.value,
													  MA_TRUE, __ATOMIC_SEQ_CST);
									 ma_device_start(&device);
									 std::this_thread::sleep_for(
										 std::chrono::microseconds(random() % 8000));
									 drainJobThread();
									 ma_device_uninit(&device);
									 ++closes;
								 } });

	std::this_thread::sleep_for(std::chrono::seconds(seconds));
	stop = true;
	rerouter.join();
	enumerator.join();
	for (std::thread &closer : closers)
		closer.join();
	soloud.deinit();

	std::printf("%ld devices closed and %ld enumerations during %ld reroutes "
				"in %ds, no crash\n",
				closes.load(), enumerations.load(), reroutes.load(), seconds);
	if (closes.load() == 0 || reroutes.load() == 0)
	{
		std::fprintf(stderr, "FAIL: the stress did not run\n");
		return 1;
	}
	std::printf("AAudio close race test passed.\n");
	return 0;
}
