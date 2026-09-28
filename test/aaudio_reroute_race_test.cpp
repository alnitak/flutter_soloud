// Android-only stress test: miniaudio's AAudio reroute job racing
// flutter_soloud's own operations on the same ma_device.
//
// When an AAudio stream reports an error (a disconnect, a route change),
// miniaudio does not reroute inline. The stream's error callback posts a
// reroute job to the context's job thread, and that job later closes the
// device's stream and opens a replacement -- on its own thread, while
// flutter_soloud starts, stops and replaces the same gDevice from its own.
//
// Unserialized, the two break gDevice in several ways, and this test has seen
// all of them: a second stream left running, so two streams deliver data
// callbacks into the one ma_device (whose fixed-size callback buffer is not
// thread safe); a data callback outliving the buffers it reads, which crashes
// in memcpy on the AudioTrack thread; and a stop issued on a stream the
// reroute already closed, which hangs in AAudioStream_requestStop().
//
// The test drives the device operations flutter_soloud's Player does (start
// with a rebuild on failure, idle stop, explicit stop, rebuild) while
// reporting stream errors the way AAudio does, through the error callback
// flutter_soloud registers: mostly for the open stream, and sometimes late,
// for one that has since been closed. It then checks that only one stream
// ever feeds the engine: every mix records the thread it runs on, and AAudio
// calls each stream's data callback from that stream's own thread. A crash or
// a hang is a failure too.
//
// Run from the flutter_soloud repository root with a device or emulator
// attached:
//
//   ./test/run_aaudio_reroute_race_test.sh [seconds] [operations]
//
// [operations] narrows the device operations the stress drives to a subset of
// S (start), P (idle stop), X (explicit stop) and R (rebuild). All by default.

#include "soloud.h"
#include "soloud_audiosource.h"
#include "backend/miniaudio/miniaudio.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <dlfcn.h>
#include <sys/syscall.h>
#include <unistd.h>

// miniaudio's AAudio types, which only its implementation declares.
struct ma_AAudioStream_t;
struct ma_AAudioStreamBuilder_t;
typedef ma_AAudioStream_t *ma_AAudioStream;
typedef ma_AAudioStreamBuilder_t *ma_AAudioStreamBuilder;
typedef void (*SetPerformanceMode)(ma_AAudioStreamBuilder *pBuilder,
								   int32_t mode);

namespace SoLoud
{
	extern ma_device gDevice;
	extern ma_context context;
	void miniaudio_setLowLatency(bool aLowLatency);
	result miniaudio_stopAudioDevice();
	void gateAAudioStreamError(ma_AAudioStream *pStream, void *pUserData,
							   int32_t error);
	extern SetPerformanceMode gSetAAudioPerformanceMode;
}

namespace
{

int gFailures = 0;

void check(bool condition, const char *what)
{
	std::printf("  %s: %s\n", condition ? "ok" : "FAILED", what);
	std::fflush(stdout);
	if (!condition)
		gFailures++;
}

long long nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::steady_clock::now().time_since_epoch())
		.count();
}

// Which threads the engine is being mixed on. AAudio runs each stream's data
// callback on that stream's own thread, and a replaced stream's thread exits,
// so a device fed by one stream at a time only ever moves forward to a new
// thread: A, B, C... Mixing going back to the thread before the current one
// (A, B, A) means two streams are delivering callbacks at once.
//
// Only touched from mix(), which holds the audio mutex.
long gMixThread = 0;
long gPreviousMixThread = 0;
std::atomic<long long> gInterleavedMixes{0};
std::atomic<long long> gMixCount{0};

class ProbeInstance : public SoLoud::AudioSourceInstance
{
public:
	unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
						  unsigned int aBufferSize) override
	{
		const long tid = syscall(SYS_gettid);
		if (tid != gMixThread)
		{
			if (tid == gPreviousMixThread)
				gInterleavedMixes.fetch_add(1, std::memory_order_relaxed);
			gPreviousMixThread = gMixThread;
			gMixThread = tid;
		}
		gMixCount.fetch_add(1, std::memory_order_relaxed);

		for (unsigned int c = 0; c < mChannels; c++)
			for (unsigned int i = 0; i < aSamplesToRead; i++)
				aBuffer[i + c * aBufferSize] = 0.0f;
		return aSamplesToRead;
	}

	bool hasEnded() override { return false; }
};

class Probe : public SoLoud::AudioSource
{
public:
	Probe() { mChannels = 2; }

	SoLoud::AudioSourceInstance *createInstance() override
	{
		return new ProbeInstance();
	}
};

bool usingAAudio()
{
	return SoLoud::context.backend == ma_backend_aaudio;
}

const int32_t kAAudioErrorDisconnected = -899; // AAUDIO_ERROR_DISCONNECTED

ma_AAudioStream *currentStream()
{
	return static_cast<ma_AAudioStream *>(__atomic_load_n(
		&SoLoud::gDevice.aaudio.pStreamPlayback, __ATOMIC_ACQUIRE));
}

// AAudio reporting a disconnect on [stream]: it calls the error callback
// registered for the stream, with the ma_device miniaudio registered as its
// user data. The callback compares the stream pointer but, for a closed
// stream, never dereferences it.
void reportDisconnect(ma_AAudioStream *stream)
{
	SoLoud::gateAAudioStreamError(stream, &SoLoud::gDevice,
								  kAAudioErrorDisconnected);
}

std::atomic<int> gReroutes{0};

void countReroutes(unsigned int state)
{
	if (state == 2) // rerouted
		gReroutes++;
}

bool waitForReroutes(int atLeast, int ms)
{
	for (int waited = 0; waited < ms; waited += 10)
	{
		if (gReroutes.load() >= atLeast)
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return gReroutes.load() >= atLeast;
}

// Player::performAudioDeviceStart(): resume, and if that fails rebuild the
// device and resume once more.
void startLikePlayer(SoLoud::Soloud &soloud)
{
	if (soloud.resume() == SoLoud::SO_NO_ERROR)
		return;
	if (soloud.miniaudio_changeDevice(nullptr) != SoLoud::SO_NO_ERROR)
		return;
	soloud.resume();
}

// Mixes that went back to the previous stream's thread over [ms].
long long interleavedMixesOver(int ms)
{
	const long long before = gInterleavedMixes.load();
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	return gInterleavedMixes.load() - before;
}

long long mixesOver(int ms)
{
	const long long before = gMixCount.load();
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	return gMixCount.load() - before;
}

// Which device operations the stress loop drives: S(tart like Player),
// P(ause), X (explicit stop), R(ebuild). All of them unless narrowed.
const char *gOperations = "SPXR";

void stress(SoLoud::Soloud &soloud, int seconds)
{
	std::printf("device operations racing reroutes for %ds\n", seconds);
	std::fflush(stdout);

	std::atomic<bool> running{true};
	std::atomic<long long> errorsReported{0};
	std::atomic<long long> lateErrorsReported{0};

	std::thread errors([&]
					   {
		std::mt19937 rng(1);
		ma_AAudioStream *open = nullptr;
		ma_AAudioStream *closed = nullptr;
		while (running.load())
		{
			ma_AAudioStream *stream = currentStream();
			if (stream != open)
			{
				closed = open;
				open = stream;
			}
			// One error in four arrives late, from the stream replaced last.
			if (closed != nullptr && rng() % 4 == 0)
			{
				reportDisconnect(closed);
				lateErrorsReported++;
			}
			else if (open != nullptr)
			{
				reportDisconnect(open);
				errorsReported++;
			}
			std::this_thread::sleep_for(
				std::chrono::microseconds(rng() % 20000));
		} });

	std::thread operations([&]
						   {
		std::mt19937 rng(2);
		const std::string operations = gOperations;
		while (running.load())
		{
			switch (operations[rng() % operations.size()])
			{
			case 'S':
				startLikePlayer(soloud);
				break;
			case 'P':
				soloud.pause();
				break;
			case 'X':
				SoLoud::miniaudio_stopAudioDevice();
				break;
			case 'R':
				// The rebuild performAudioDeviceStart() does after a failed
				// start. Driven directly: an injected reroute does not make
				// the next start fail the way a real disconnect can.
				soloud.miniaudio_changeDevice(nullptr);
				soloud.resume();
				break;
			}
			std::this_thread::sleep_for(
				std::chrono::microseconds(rng() % 20000));
		} });

	const long long end = nowMs() + seconds * 1000LL;
	long long concurrentSeconds = 0;
	while (nowMs() < end)
	{
		const long long interleaved = interleavedMixesOver(1000);
		if (interleaved > 0)
		{
			concurrentSeconds++;
			std::printf("  %lld mixes interleaved between two streams' "
						"threads in 1s\n",
						interleaved);
			std::fflush(stdout);
		}
	}

	running.store(false);
	errors.join();
	operations.join();

	std::printf("  %lld errors reported (%lld of them late), %lld reroutes, "
				"%lld seconds with concurrent streams\n",
				errorsReported.load() + lateErrorsReported.load(),
				lateErrorsReported.load(), (long long)gReroutes.load(),
				concurrentSeconds);
	check(concurrentSeconds == 0,
		  "no two streams fed the device at the same time under stress");
}

// Occupies memory of every small size, once, right after a stream is closed,
// so that the stream replacing it cannot be allocated where it was.
std::vector<void *> gPlugs;
ma_proc gCloseBeneath = nullptr;

int32_t closeAndPlug(ma_AAudioStream *stream)
{
	const int32_t result =
		reinterpret_cast<int32_t (*)(ma_AAudioStream *)>(gCloseBeneath)(stream);
	if (gPlugs.empty())
		for (size_t size = 16; size <= 8192; size += 16)
			gPlugs.push_back(std::malloc(size));
	return result;
}

// AAudio can report an error for a stream after it has been closed: an idle
// legacy stream reports a route change from a binder thread that closing the
// stream does not wait for. That error must not reroute the device that
// replaced the stream. The open stream's errors still have to get through.
void checkClosedStreamErrorsIgnored(SoLoud::Soloud &soloud)
{
	std::printf("errors from a closed stream\n");

	startLikePlayer(soloud);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	// A new stream tends to be allocated where the one just closed was, which
	// would make the two indistinguishable here. Never in AAudio, which keeps
	// a stream alive while it reports an error; so keep the replacement off
	// the closed stream's memory for the one rebuild.
	ma_AAudioStream *closed = currentStream();
	gCloseBeneath = SoLoud::context.aaudio.AAudioStream_close;
	SoLoud::context.aaudio.AAudioStream_close = (ma_proc)closeAndPlug;
	soloud.miniaudio_changeDevice(nullptr);
	SoLoud::context.aaudio.AAudioStream_close = gCloseBeneath;
	for (void *plug : gPlugs)
		std::free(plug);
	gPlugs.clear();
	soloud.resume();
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	if (closed == nullptr || closed == currentStream())
	{
		std::printf("  SKIPPED: the replacement stream took the closed one's "
					"address\n");
		return;
	}

	const int before = gReroutes.load();
	reportDisconnect(closed);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	std::printf("  %d reroutes after a closed stream's error\n",
				gReroutes.load() - before);
	check(gReroutes.load() == before,
		  "an error from a closed stream does not reroute the device that "
		  "replaced it");

	reportDisconnect(currentStream());
	check(waitForReroutes(before + 1, 3000),
		  "an error from the open stream reroutes the device");
}

// The performance mode every stream the device opens asks AAudio for,
// recorded where it reaches AAudio.
const int32_t kAAudioPerformanceModeNone = 10; // AAUDIO_PERFORMANCE_MODE_NONE
SetPerformanceMode gSetPerformanceModeBeneath = nullptr;
std::atomic<int> gStreamsBuilt{0};
std::atomic<int> gLowLatencyStreamsBuilt{0};

void recordPerformanceMode(ma_AAudioStreamBuilder *pBuilder, int32_t mode)
{
	gStreamsBuilt++;
	if (mode != kAAudioPerformanceModeNone)
		gLowLatencyStreamsBuilt++;
	gSetPerformanceModeBeneath(pBuilder, mode);
}

// With low latency off, the device opens its streams in AAudio's default
// performance mode, the legacy path, and every stream a reroute reopens has
// to as well: miniaudio builds those from a fresh config whose performance
// profile defaults to low latency.
void checkReopenedStreamsKeepPerformanceMode()
{
	std::printf("performance mode of reopened streams\n");
	std::printf("  %d streams built, %d of them asking for low latency\n",
				gStreamsBuilt.load(), gLowLatencyStreamsBuilt.load());
	check(gStreamsBuilt.load() > 0 && gLowLatencyStreamsBuilt.load() == 0,
		  "every stream the device opens, rerouted ones included, keeps the "
		  "configured performance mode");
}

// miniaudio gives the bare streams it opens to probe the default device its
// own low-latency hint, and that is none of a reroute's business.
void checkProbesKeepTheirPerformanceMode()
{
	std::printf("performance mode of a probe stream\n");
	const int built = gStreamsBuilt.load();
	const int lowLatency = gLowLatencyStreamsBuilt.load();
	ma_device_info info;
	const ma_result result = ma_context_get_device_info(
		&SoLoud::context, ma_device_type_playback, nullptr, &info);
	const int probes = gStreamsBuilt.load() - built;
	const int lowLatencyProbes = gLowLatencyStreamsBuilt.load() - lowLatency;
	std::printf("  %d probe streams, %d of them asking for low latency\n",
				probes, lowLatencyProbes);
	check(result == MA_SUCCESS && probes > 0 && lowLatencyProbes == probes,
		  "probing the default device keeps miniaudio's low-latency hint");
}

void *failToAllocate(size_t, void *)
{
	return nullptr;
}

void *failToReallocate(void *, size_t, void *)
{
	return nullptr;
}

// ma_device_init() leaves the stream the backend opened open when a later
// step fails, and nothing in miniaudio ever closes it. Make one fail that way
// -- every allocation miniaudio makes through the context fails, and the
// first comes after the stream is open -- then rebuild the device properly.
// An error from the stream the failed attempt left behind must not reroute
// the new device.
void checkFailedInitLeavesNoStream(SoLoud::Soloud &soloud)
{
	std::printf("device initialization failing after its stream opened\n");

	startLikePlayer(soloud);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	const ma_allocation_callbacks allocator =
		SoLoud::context.allocationCallbacks;
	SoLoud::context.allocationCallbacks.onMalloc = failToAllocate;
	SoLoud::context.allocationCallbacks.onRealloc = failToReallocate;
	const SoLoud::result failed = soloud.miniaudio_changeDevice(nullptr);
	SoLoud::context.allocationCallbacks = allocator;
	ma_AAudioStream *orphan = currentStream();
	check(failed != SoLoud::SO_NO_ERROR && orphan != nullptr,
		  "the device fails to initialize after opening its stream");

	// Keep the next stream off the orphan's memory, should it have been freed.
	std::vector<void *> plugs;
	for (size_t size = 16; size <= 8192; size += 16)
		plugs.push_back(std::malloc(size));
	soloud.miniaudio_changeDevice(nullptr);
	for (void *plug : plugs)
		std::free(plug);
	soloud.resume();
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	if (orphan == nullptr || orphan == currentStream())
	{
		std::printf("  SKIPPED: the new stream took the orphan's address\n");
		return;
	}

	const int before = gReroutes.load();
	reportDisconnect(orphan);
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	std::printf("  %d reroutes after the orphaned stream's error\n",
				gReroutes.load() - before);
	check(gReroutes.load() == before,
		  "the stream a failed initialization opened is closed, and its "
		  "errors do not reroute the next device");
}

// A job that parks the job thread until released.
struct ParkedJobThread
{
	std::mutex mutex;
	std::condition_variable cv;
	bool parked = false;
	bool released = false;
};

ma_result parkJobThread(ma_job *pJob)
{
	auto *parking =
		reinterpret_cast<ParkedJobThread *>(pJob->data.custom.data0);
	std::unique_lock<std::mutex> lock(parking->mutex);
	parking->parked = true;
	parking->cv.notify_all();
	parking->cv.wait(lock, [parking] { return parking->released; });
	return MA_SUCCESS;
}

ma_result doNothing(ma_job *)
{
	return MA_SUCCESS;
}

// A device operation holds the job thread before it touches the device, and
// with the job thread busy and its queue full it cannot even queue that hold.
// It has to wait for room, not give up and operate on the device while a
// reroute could run.
void checkOperationWaitsForQueueRoom(SoLoud::Soloud &soloud)
{
	std::printf("device operation with the job queue full\n");

	startLikePlayer(soloud);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	check(ma_device_get_state(&SoLoud::gDevice) == ma_device_state_started,
		  "the device is started before the queue fills");

	ParkedJobThread parking;
	ma_job park = ma_job_init(MA_JOB_TYPE_CUSTOM);
	park.data.custom.proc = parkJobThread;
	park.data.custom.data0 = (ma_uintptr)&parking;
	ma_device_job_thread_post(&SoLoud::context.aaudio.jobThread, &park);
	{
		std::unique_lock<std::mutex> lock(parking.mutex);
		parking.cv.wait(lock, [&parking] { return parking.parked; });
	}

	ma_job filler = ma_job_init(MA_JOB_TYPE_CUSTOM);
	filler.data.custom.proc = doNothing;
	int queued = 0;
	while (ma_device_job_thread_post(&SoLoud::context.aaudio.jobThread,
									 &filler) == MA_SUCCESS)
		queued++;

	std::atomic<bool> pauseReturned{false};
	std::thread pausing([&]
						{
		soloud.pause();
		pauseReturned = true; });

	// Well past the point where giving up on the hold used to let the
	// operation through: 1000 one-millisecond retries, which take over 2s on
	// an emulator.
	std::this_thread::sleep_for(std::chrono::milliseconds(5000));
	const bool wentAhead =
		pauseReturned.load() ||
		ma_device_get_state(&SoLoud::gDevice) != ma_device_state_started;
	std::printf("  %d jobs filled the queue; after 5s the pause %s\n", queued,
				wentAhead ? "had gone ahead" : "was still waiting");
	check(!wentAhead, "an operation waits for room in the queue rather than "
					  "going ahead without holding reroutes");

	{
		std::lock_guard<std::mutex> lock(parking.mutex);
		parking.released = true;
		parking.cv.notify_all();
	}
	// The pause only gets its hold in once the job thread is past the parking
	// job, so `parking` is no longer in use when this returns.
	pausing.join();
	check(ma_device_get_state(&SoLoud::gDevice) == ma_device_state_stopped,
		  "the operation completes once the queue drains");
}

void checkSettledDevice(SoLoud::Soloud &soloud)
{
	std::printf("settled device\n");

	// Let queued reroutes drain, then leave the device running.
	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	startLikePlayer(soloud);
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	const long long interleaved = interleavedMixesOver(2000);
	std::printf("  %lld interleaved mixes in 2s while started\n", interleaved);
	check(interleaved == 0, "a started device is fed by one stream at a time");

	soloud.pause();
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	const long long mixes = mixesOver(1000);
	std::printf("  %lld mixes in 1s while stopped\n", mixes);
	check(mixes == 0, "a stopped device is fed by no stream");
}

} // namespace

int main(int argc, char **argv)
{
	const int seconds = argc > 1 ? std::atoi(argv[1]) : 30;
	if (argc > 2)
		gOperations = argv[2];

	// flutter_soloud apps that pass lowLatency: false get AAudio's legacy
	// (AudioTrack) path, where this race was first seen crashing an app.
	SoLoud::miniaudio_setLowLatency(false);

	// An app process inherits libaaudio.so, and the libraries it loads, from
	// zygote. Here miniaudio's dlopen() is the only reference, so the
	// dlclose() in deinit() would unmap libaudioclient and libbinder while
	// the binder thread pool still runs in them, and the next binder wakeup
	// would crash the test. Keep it loaded, as zygote does.
	dlopen("libaaudio.so", RTLD_NOW);

	SoLoud::Soloud soloud;
	const SoLoud::result init = soloud.init(
		SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::MINIAUDIO, 48000, 2048, 2);
	if (init != SoLoud::SO_NO_ERROR)
	{
		std::printf("SKIPPED: miniaudio could not open a device (error %d)\n",
					init);
		return 0;
	}
	if (!usingAAudio())
	{
		std::printf("SKIPPED: the device is not using AAudio\n");
		soloud.deinit();
		return 0;
	}

	soloud.setStateChangedCallback(countReroutes);
	gSetPerformanceModeBeneath = SoLoud::gSetAAudioPerformanceMode;
	SoLoud::gSetAAudioPerformanceMode = recordPerformanceMode;

	Probe probe;
	soloud.play(probe);

	checkClosedStreamErrorsIgnored(soloud);
	checkFailedInitLeavesNoStream(soloud);
	checkOperationWaitsForQueueRoom(soloud);
	stress(soloud, seconds);
	checkSettledDevice(soloud);
	checkReopenedStreamsKeepPerformanceMode();
	checkProbesKeepTheirPerformanceMode();

	// A late error for the device's last stream, once deinit() has closed it
	// and zeroed the device. miniaudio's own callback would dereference the
	// zeroed device's context before checking anything.
	std::printf("error after deinit\n");
	ma_AAudioStream *last = currentStream();
	soloud.deinit();
	if (last != nullptr)
		reportDisconnect(last);
	check(last != nullptr, "an error from a stream closed by deinit() is "
						   "ignored");

	if (gFailures == 0)
	{
		std::printf("All AAudio reroute race tests passed.\n");
		return 0;
	}
	std::printf("%d AAudio reroute race test(s) failed.\n", gFailures);
	return 1;
}
