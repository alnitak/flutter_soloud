// Regression test: seeking a disk-streamed FLAC file.
//
// dr_flac was built with DR_FLAC_NO_CRC, which also turns off its binary
// search. Without a SEEKTABLE, which encoders write only when asked and
// sample-FLAC.flac does not have, a seek then decoded the file from its first
// frame up to the target: a seek near the end of a file read almost all of
// it. A WavStream voice (LoadMode.disk) seeks under the audio mutex, so no
// voice played while it ran. dr_flac now checks the frames' checksums, and
// seeks by bisecting the file.
//
// For 19 targets from 5% to 95% of sample-FLAC.flac, each sought from near
// its end, the test counts the bytes the seek reads, and checks that the
// voice then plays exactly what a voice reading the file from its start
// plays there.
//
// Null backend, no audio hardware. Build and run from the flutter_soloud
// repository root with:
//
//   ./test/run_wavstream_seek_test.sh       (add --asan for ASan/UBSan)

#include "soloud.h"
#include "soloud_file.h"
#include "soloud_wavstream.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
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

const char *const kFile = "example/assets/audio/sample-FLAC.flac";
constexpr unsigned int kBlock = 1024;

// A file on disk that counts the bytes read from it.
class CountingFile : public SoLoud::File
{
public:
	SoLoud::DiskFile mFile;
	size_t mBytesRead = 0;

	int eof() override { return mFile.eof(); }
	unsigned int read(unsigned char *aDst, unsigned int aBytes) override
	{
		const unsigned int got = mFile.read(aDst, aBytes);
		mBytesRead += got;
		return got;
	}
	unsigned int length() override { return mFile.length(); }
	void seek(int aOffset) override { mFile.seek(aOffset); }
	unsigned int pos() override { return mFile.pos(); }
};

// The types of [path]'s FLAC metadata blocks, none when it is no FLAC file.
std::vector<int> metadataBlockTypes(const char *path)
{
	std::vector<int> types;
	std::ifstream in(path, std::ios::binary);
	char magic[4];
	if (!in.read(magic, 4) || std::memcmp(magic, "fLaC", 4) != 0)
		return types;
	for (bool last = false; !last;)
	{
		unsigned char header[4];
		if (!in.read((char *)header, 4))
			break;
		last = (header[0] & 0x80) != 0;
		types.push_back(header[0] & 0x7f);
		in.seekg((header[1] << 16) | (header[2] << 8) | header[3], std::ios::cur);
	}
	return types;
}

// Reads [voice] from where it is, [frames] frames into [planes].
void readFrames(SoLoud::AudioSourceInstance *voice, unsigned int channels, unsigned int frames,
				std::vector<std::vector<float>> &planes)
{
	std::vector<float> buffer(channels * kBlock);
	planes.assign(channels, {});
	for (unsigned int done = 0; done < frames;)
	{
		const unsigned int n = std::min(kBlock, frames - done);
		const unsigned int got = voice->getAudio(buffer.data(), n, kBlock);
		for (unsigned int ch = 0; ch < channels; ch++)
			planes[ch].insert(planes[ch].end(), buffer.begin() + ch * kBlock,
							  buffer.begin() + ch * kBlock + got);
		if (got < n)
			break;
		done += got;
	}
}

} // namespace

int main()
{
	const std::vector<int> types = metadataBlockTypes(kFile);
	check(!types.empty() && std::find(types.begin(), types.end(), 3) == types.end(),
		  std::string(kFile) + " is a FLAC file without a SEEKTABLE, which a seek would use");

	CountingFile file;
	if (file.mFile.open(kFile) != SoLoud::SO_NO_ERROR)
	{
		std::printf("FAIL: %s could not be opened\n", kFile);
		return 1;
	}
	SoLoud::WavStream ws;
	check(ws.loadFile(&file) == SoLoud::SO_NO_ERROR, std::string(kFile) + " loads");
	check(ws.mFiletype == SoLoud::WAVSTREAM_FLAC, std::string(kFile) + " streams through dr_flac");
	const unsigned int channels = ws.mChannels;
	const unsigned int total = ws.mSampleCount;
	const double rate = ws.mBaseSamplerate;

	// What a voice reading from the start plays.
	SoLoud::WavStream reference;
	check(reference.load(kFile) == SoLoud::SO_NO_ERROR, std::string(kFile) + " loads again");
	SoLoud::AudioSourceInstance *linear = reference.createInstance();
	linear->init(reference, 0);
	std::vector<std::vector<float>> expected;
	readFrames(linear, channels, total, expected);
	delete linear;
	check(expected.size() == channels && expected[0].size() == total,
		  "a voice reading from the start plays all " + std::to_string(total) + " frames");
	if (gFailures > 0)
	{
		std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
		return 1;
	}

	SoLoud::AudioSourceInstance *voice = ws.createInstance();
	voice->init(ws, 0);
	std::vector<float> scratch(SAMPLE_GRANULARITY * MAX_CHANNELS);
	size_t bytesRead = 0;
	size_t mostRead = 0;
	for (int i = 1; i < 20; i++)
	{
		const unsigned int target = (unsigned int)((double)total * i / 20) + 1234;
		voice->seek((total - 10000) / rate, scratch.data(), (unsigned int)scratch.size());
		file.mBytesRead = 0;
		voice->seek(target / rate, scratch.data(), (unsigned int)scratch.size());
		bytesRead += file.mBytesRead;
		mostRead = std::max(mostRead, file.mBytesRead);

		std::vector<std::vector<float>> played;
		readFrames(voice, channels, kBlock, played);
		bool same = played.size() == channels && played[0].size() == kBlock;
		for (unsigned int ch = 0; same && ch < channels; ch++)
			same = std::equal(played[ch].begin(), played[ch].end(), expected[ch].begin() + target);
		check(same, "after a seek to frame " + std::to_string(target) + " of " +
						std::to_string(total) + ", the voice plays what is there");
	}
	delete voice;

	// Decoding from the first frame, the 19 seeks read the file 9.5 times
	// over. Bisecting, each reads a few frames, unless dr_flac falls back
	// to decoding from the start, as it does for a few targets in a hundred.
	const double files = (double)bytesRead / file.length();
	std::printf("the 19 seeks read %.2f times the file, one seek %.1f%% of it at most\n", files,
				100.0 * mostRead / file.length());
	check(files < 4, "the 19 seeks read less than 4 times the file, not " + std::to_string(files));

	std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
	return gFailures == 0 ? 0 : 1;
}
