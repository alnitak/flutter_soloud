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
// with a rebuild on failure, idle stop, explicit stop, rebuild) while posting
// reroute jobs the way miniaudio's own error callback does. It then checks
// that only one stream ever feeds the engine: every mix records the thread it
// runs on, and AAudio calls each stream's data callback from that stream's
// own thread. A crash or a hang is a failure too.
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
#include <sys/syscall.h>
#include <unistd.h>

namespace SoLoud
{
	extern ma_device gDevice;
	extern ma_context context;
	void miniaudio_setLowLatency(bool aLowLatency);
	result miniaudio_stopAudioDevice();
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

// What ma_stream_error_callback__aaudio() does when AAudio reports an error
// on the device's stream: unless the device is tearing down, queue a reroute
// on the context's job thread. Only posts while the device has a stream, since
// only a live stream can report one.
bool postRerouteLikeTheErrorCallback()
{
	ma_device *device = &SoLoud::gDevice;
	if (__atomic_load_n(&device->aaudio.pStreamPlayback, __ATOMIC_ACQUIRE) ==
		nullptr)
		return false;
	if (__atomic_load_n(&device->aaudio.isTearingDown.value, __ATOMIC_ACQUIRE))
		return false;

	ma_job job = ma_job_init(MA_JOB_TYPE_DEVICE_AAUDIO_REROUTE);
	job.data.device.aaudio.reroute.pDevice = device;
	job.data.device.aaudio.reroute.deviceType = ma_device_type_playback;
	return ma_device_job_thread_post(&SoLoud::context.aaudio.jobThread, &job) ==
		   MA_SUCCESS;
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
	std::atomic<long long> reroutesPosted{0};

	std::thread errors([&]
					   {
		std::mt19937 rng(1);
		while (running.load())
		{
			if (postRerouteLikeTheErrorCallback())
				reroutesPosted++;
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

	std::printf("  %lld reroutes posted, %lld seconds with concurrent "
				"streams\n",
				reroutesPosted.load(), concurrentSeconds);
	check(concurrentSeconds == 0,
		  "no two streams fed the device at the same time under stress");
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

	Probe probe;
	soloud.play(probe);

	checkOperationWaitsForQueueRoom(soloud);
	stress(soloud, seconds);
	checkSettledDevice(soloud);

	soloud.deinit();

	if (gFailures == 0)
	{
		std::printf("All AAudio reroute race tests passed.\n");
		return 0;
	}
	std::printf("%d AAudio reroute race test(s) failed.\n", gFailures);
	return 1;
}
