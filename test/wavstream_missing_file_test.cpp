// Regression test: a voice of a disk-streamed WavStream whose file is gone.
//
// A WavStream loaded from a path in disk mode keeps only the path, and every
// voice opens the file again. When the file has been deleted or moved since
// it was loaded, or overwritten with bytes no decoder accepts, the voice has
// no file to read: getAudio() plays nothing. hasEnded() compared the voice's
// position with the source's length, so such a voice never ended and never
// reported voice-ended: it played silence until it was stopped, holding a
// voice slot. It now ends at the first mix.
//
// For each repo sample (WAV, MP3, FLAC and, with the Xiph libraries, Ogg
// Vorbis), loaded from a temporary copy, the test checks that once the copy is
// deleted:
// - play(), playClocked(), playScheduled(), play3d(), play3dClocked() and
//   play3dScheduled() return a voice;
// - the voice ends at the first mix that reads it (a clocked start first
//   waits out its lead), is reported through the voice-ended callback, and
//   frees its slot;
// - a voice already playing from memory plays on exactly as it would alone;
// - the source plays again once the file is back.
// It checks the same for a copy overwritten after loading with zeros: the
// file opens, but no decoder accepts it. A DiskFile without an open file
// reads nothing, stays at 0 and is at its end, as on an empty file.
//
// Null backend, no audio hardware. Build and run from the flutter_soloud
// repository root with:
//
//   ./test/run_wavstream_missing_file_test.sh       (add --asan for ASan/UBSan)

#include "soloud.h"
#include "soloud_file.h"
#include "soloud_wavstream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unistd.h>
#include <vector>

namespace
{

int gAssertions = 0;
int gFailures = 0;

void check(bool ok, const std::string &what)
{
	++gAssertions;
	if (!ok)
	{
		++gFailures;
		std::printf("FAIL: %s\n", what.c_str());
	}
}

constexpr unsigned int kRate = 44100;
constexpr unsigned int kBlock = 512;
constexpr int kBlocks = 8;

// Handles the voice-ended callback reported. The callback runs on the thread
// that mixes, here the test's own, after the audio mutex is released.
std::mutex gEndedMutex;
std::vector<unsigned int> gEnded;

void onVoiceEnded(unsigned int *handle)
{
	std::lock_guard<std::mutex> lock(gEndedMutex);
	gEnded.push_back(*handle);
}

bool reportedEnded(unsigned int handle)
{
	std::lock_guard<std::mutex> lock(gEndedMutex);
	return std::find(gEnded.begin(), gEnded.end(), handle) != gEnded.end();
}

std::vector<char> readAll(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<char>(std::istreambuf_iterator<char>(in), {});
}

bool writeAll(const std::string &path, const std::vector<char> &bytes)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out.write(bytes.data(), (std::streamsize)bytes.size());
	return (bool)out;
}

enum class Start
{
	play,
	clocked,
	scheduled,
	play3d,
	play3dClocked,
	play3dScheduled,
};

constexpr Start kStarts[] = {Start::play,   Start::clocked,
							 Start::scheduled, Start::play3d,
							 Start::play3dClocked, Start::play3dScheduled};

const char *name(Start how)
{
	switch (how)
	{
	case Start::play:
		return "play()";
	case Start::clocked:
		return "playClocked()";
	case Start::scheduled:
		return "playScheduled()";
	case Start::play3d:
		return "play3d()";
	case Start::play3dClocked:
		return "play3dClocked()";
	case Start::play3dScheduled:
		return "play3dScheduled()";
	}
	return "?";
}

SoLoud::handle start(SoLoud::Soloud &soloud, SoLoud::AudioSource &source,
					 Start how)
{
	const double now = soloud.getEngineTime();
	switch (how)
	{
	case Start::play:
		return soloud.play(source);
	case Start::clocked:
		return soloud.playClocked(now, source);
	case Start::scheduled:
		return soloud.playScheduled(now, source);
	case Start::play3d:
		return soloud.play3d(source, 1.0f, 0.0f, 0.0f);
	case Start::play3dClocked:
		return soloud.play3dClocked(now, source, 1.0f, 0.0f, 0.0f);
	case Start::play3dScheduled:
		return soloud.play3dScheduled(now, source, 1.0f, 0.0f, 0.0f);
	}
	return 0;
}

/// An engine on the null backend with a looping voice playing [background]
/// from memory, mixed by hand.
struct Rig
{
	SoLoud::Soloud soloud;
	SoLoud::handle background = 0;
	std::vector<float> out = std::vector<float>(kBlock * 2);
	std::vector<float> mixed;

	explicit Rig(SoLoud::WavStream &backgroundSource)
	{
		{
			// Handles are reused from one engine to the next.
			std::lock_guard<std::mutex> lock(gEndedMutex);
			gEnded.clear();
		}
		soloud.init(0, SoLoud::Soloud::NULLDRIVER, kRate, kBlock, 2);
		soloud.setVoiceEndedCallback(onVoiceEnded);
		background = soloud.play(backgroundSource);
		soloud.setLooping(background, true);
	}

	~Rig()
	{
		soloud.setVoiceEndedCallback(nullptr);
		soloud.deinit();
	}

	void mix()
	{
		soloud.mix(out.data(), kBlock);
		mixed.insert(mixed.end(), out.begin(), out.end());
	}
};

/// What [kBlocks] mixes of the background voice alone sound like.
std::vector<float> backgroundAlone(SoLoud::WavStream &backgroundSource)
{
	Rig rig(backgroundSource);
	for (int i = 0; i < kBlocks; ++i)
		rig.mix();
	return rig.mixed;
}

/// The mix at which a voice started with [how] is read for the first time:
/// a clocked start leads the output by two buffers (see playClocked()).
int firstReadMix(Start how)
{
	return how == Start::clocked || how == Start::play3dClocked ? 3 : 1;
}

/// Plays [source], whose file has just been deleted or broken, next to the
/// background voice and checks the voice ends as soon as it is mixed and
/// leaves the background voice alone.
void checkVoiceEnds(SoLoud::WavStream &source, SoLoud::WavStream &backgroundSource,
					const std::vector<float> &reference, Start how,
					const std::string &what)
{
	Rig rig(backgroundSource);
	rig.mix();
	const SoLoud::handle voice = start(rig.soloud, source, how);
	check(voice != 0, what + ": " + name(how) + " returns a voice");
	int mixes = 0;
	while (mixes < firstReadMix(how) && rig.soloud.isValidVoiceHandle(voice))
	{
		rig.mix();
		++mixes;
	}
	check(!rig.soloud.isValidVoiceHandle(voice),
		  what + ": the " + name(how) + " voice ends at the first mix that reads it");
	check(reportedEnded(voice),
		  what + ": the " + name(how) + " voice is reported ended");
	check(rig.soloud.getActiveVoiceCount() == 1,
		  what + ": only the background voice is left after " + name(how));
	for (int i = 1 + mixes; i < kBlocks; ++i)
		rig.mix();
	check(rig.soloud.isValidVoiceHandle(rig.background),
		  what + ": the background voice plays on after " + name(how));
	check(rig.mixed == reference,
		  what + ": the background voice sounds as it does alone after " +
			  name(how));
}

/// The source plays (some non-silent output within a few blocks) and is
/// still playing at the end of them.
void checkPlays(SoLoud::WavStream &source, SoLoud::WavStream &backgroundSource,
				const std::string &what)
{
	Rig rig(backgroundSource);
	rig.soloud.setVolume(rig.background, 0.0f);
	const SoLoud::handle voice = rig.soloud.play(source);
	float peak = 0.0f;
	for (int i = 0; i < kBlocks; ++i)
	{
		rig.mix();
		for (float sample : rig.out)
			peak = std::fmax(peak, std::fabs(sample));
	}
	check(rig.soloud.isValidVoiceHandle(voice), what + ": the voice is playing");
	check(peak > 0.0f, what + ": the voice is audible");
}

void testFormat(const std::string &asset, const std::string &workDir,
				SoLoud::WavStream &backgroundSource,
				const std::vector<float> &reference)
{
	std::printf("%s\n", asset.c_str());
	const std::vector<char> bytes = readAll(asset);
	check(!bytes.empty(), asset + " can be read");
	if (bytes.empty())
		return;
	const std::string path =
		workDir + "/stream" + asset.substr(asset.rfind('.'));
	check(writeAll(path, bytes), "the copy of " + asset + " is written");

	SoLoud::WavStream source;
	check(source.load(path.c_str()) == SoLoud::SO_NO_ERROR,
		  asset + " loads from disk");
	checkPlays(source, backgroundSource, asset + " before it is deleted");

	// Deleted after loading.
	check(std::remove(path.c_str()) == 0, "the copy of " + asset + " is deleted");
	for (Start how : kStarts)
		checkVoiceEnds(source, backgroundSource, reference, how,
					   asset + " deleted");

	// Back again: the source opens it once more.
	check(writeAll(path, bytes), "the copy of " + asset + " is written back");
	checkPlays(source, backgroundSource, asset + " written back");

	// Overwritten with bytes no decoder accepts: the file opens, the decoder
	// does not.
	check(writeAll(path, std::vector<char>(bytes.size(), 0)),
		  "the copy of " + asset + " is overwritten");
	for (Start how : kStarts)
		checkVoiceEnds(source, backgroundSource, reference, how,
					   asset + " overwritten");
	std::remove(path.c_str());
}

void testDiskFileWithoutFile(const std::string &workDir)
{
	std::printf("DiskFile without a file\n");
	SoLoud::DiskFile file;
	const std::string path = workDir + "/does-not-exist.mp3";
	check(file.open(path.c_str()) == SoLoud::FILE_NOT_FOUND,
		  "DiskFile::open() reports a missing file");
	unsigned char buffer[16] = {};
	check(file.read(buffer, sizeof(buffer)) == 0,
		  "DiskFile::read() without a file reads nothing");
	file.seek(4);
	check(file.pos() == 0, "DiskFile::pos() without a file is 0");
	check(file.eof() != 0, "DiskFile::eof() without a file is at the end");
	check(file.length() == 0, "DiskFile::length() without a file is 0");
}

} // namespace

int main()
{
	// Keep the report up to a crash.
	std::setvbuf(stdout, nullptr, _IOLBF, 0);
	const char *tmp = std::getenv("TMPDIR");
	std::string workTemplate =
		std::string(tmp != nullptr && *tmp != 0 ? tmp : "/tmp") +
		"/wavstream_missing_file_XXXXXX";
	const char *workDir = mkdtemp(&workTemplate[0]);
	if (workDir == nullptr)
	{
		std::printf("FAIL: no temporary directory\n");
		return 1;
	}

	SoLoud::WavStream backgroundSource;
	if (backgroundSource.loadToMem("example/assets/audio/sample-WAV.wav") !=
		SoLoud::SO_NO_ERROR)
	{
		std::printf("FAIL: run from the flutter_soloud repository root\n");
		return 1;
	}
	const std::vector<float> reference = backgroundAlone(backgroundSource);

	std::vector<std::string> assets = {
		"example/assets/audio/sample-WAV.wav",
		"example/assets/audio/sample-MP3.mp3",
		"example/assets/audio/sample-FLAC.flac",
	};
#if !defined(NO_XIPH_LIBS)
	assets.push_back("example/assets/audio/sample-vorbis.ogg");
#endif
	for (const std::string &asset : assets)
		testFormat(asset, workDir, backgroundSource, reference);

	testDiskFileWithoutFile(workDir);
	rmdir(workDir);

	std::printf("%d assertions, %d failures\n", gAssertions, gFailures);
	return gFailures == 0 ? 0 : 1;
}
