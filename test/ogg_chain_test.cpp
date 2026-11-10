// Regression test: chained Ogg files whose links differ.
//
// An Ogg file can chain links, complete streams with their own headers, one
// after another: a radio recording that switches from stereo music to a mono
// announcement, or files joined with cat. MBOggDecoder decodes the links back
// to back. getChannels() reports the first link's channel count for the whole
// stream, and WavStream and Wav size their buffers for it.
//
// opus-channels: each link's OpusHead replaced that channel count. A link
// whose header declares 0 channels left it at 0, and the next read divided by
// it (SIGFPE on x86-64). A mono link after a stereo one left the second
// channel unwritten; a stereo link after a mono one wrote a second channel
// past the caller's buffer. Every link is now decoded at the stream's channel
// count, and a link with a header no Opus decoder takes is skipped.
//
// The test builds its files with libopus and libogg, and reads them through a
// WavStream voice instance the way the mixer does, through a voice on the null
// backend, and through Wav (what LoadMode.memory uses).
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_ogg_chain_test.sh [--asan] [case or group...]
//
// Groups: opus-channels. With none, all cases run.

#include "soloud.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"

#include "native_audio_decoder.h"

#include <ogg/ogg.h>
#if __has_include(<opus/opus.h>)
#include <opus/opus.h>
#else
#include <opus.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// soloud_wav.cpp hands formats it does not decode itself to the platform
// decoder. This test loads only Ogg, so it links this stub instead of the
// platform decoders.
DecodedAudioData NativeAudioDecoder::decodeFile(const char *)
{
	DecodedAudioData result;
	result.errorMessage = "not linked in this test";
	return result;
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *, size_t)
{
	DecodedAudioData result;
	result.errorMessage = "not linked in this test";
	return result;
}

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

constexpr unsigned int kOpusRate = 48000;
constexpr unsigned int kBlock = 512;
// Never a decoded sample: marks what a read did not write.
constexpr float kUnwritten = 1000.0f;

using Bytes = std::vector<unsigned char>;

void append(Bytes &out, const Bytes &more)
{
	out.insert(out.end(), more.begin(), more.end());
}

void putLe16(Bytes &out, unsigned int v)
{
	out.push_back((unsigned char)(v & 0xff));
	out.push_back((unsigned char)((v >> 8) & 0xff));
}

void putLe32(Bytes &out, unsigned int v)
{
	putLe16(out, v & 0xffff);
	putLe16(out, v >> 16);
}

// Moves the pages libogg has ready (all of them when [flush]) into [out].
void takePages(ogg_stream_state &os, Bytes &out, bool flush)
{
	ogg_page og;
	while (flush ? ogg_stream_flush(&os, &og) : ogg_stream_pageout(&os, &og))
	{
		out.insert(out.end(), og.header, og.header + og.header_len);
		out.insert(out.end(), og.body, og.body + og.body_len);
	}
}

// Tone of channel [ch] in a link: links and channels differ in pitch.
float tone(int serial, int ch, long frame, unsigned int rate)
{
	const double freq = 220.0 * (1 + serial % 5) + 110.0 * ch;
	return 0.5f * (float)std::sin(2 * M_PI * freq * (double)frame / rate);
}

struct OpusLink
{
	int channels;     // encoded
	int headChannels; // declared in the OpusHead
	int serial;
	double seconds;
};

// One Ogg Opus link: OpusHead and OpusTags pages, then 20 ms packets of a tone.
// The last granule position is pre-skip + the tone's length, so a decoder
// that trims the end gets exactly [seconds] of audio.
Bytes makeOpusLink(const OpusLink &link)
{
	int error = OPUS_OK;
	OpusEncoder *enc =
		opus_encoder_create(kOpusRate, link.channels, OPUS_APPLICATION_AUDIO, &error);
	if (error != OPUS_OK || enc == nullptr)
	{
		std::printf("opus_encoder_create failed: %d\n", error);
		std::exit(1);
	}
	opus_int32 preSkip = 0;
	opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&preSkip));

	Bytes out;
	ogg_stream_state os;
	ogg_stream_init(&os, link.serial);

	Bytes head = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1,
				  (unsigned char)link.headChannels};
	putLe16(head, (unsigned int)preSkip);
	putLe32(head, kOpusRate);
	putLe16(head, 0); // output gain
	head.push_back(0); // channel mapping family
	ogg_packet op = {};
	op.packet = head.data();
	op.bytes = (long)head.size();
	op.b_o_s = 1;
	op.packetno = 0;
	ogg_stream_packetin(&os, &op);
	takePages(os, out, true);

	const std::string vendor = "ogg_chain_test";
	Bytes tags = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'};
	putLe32(tags, (unsigned int)vendor.size());
	tags.insert(tags.end(), vendor.begin(), vendor.end());
	putLe32(tags, 0); // no comments
	op = {};
	op.packet = tags.data();
	op.bytes = (long)tags.size();
	op.packetno = 1;
	ogg_stream_packetin(&os, &op);
	takePages(os, out, true);

	// Encode the encoder's lookahead too, so all of the tone comes out.
	const long frames = std::lround(link.seconds * kOpusRate);
	const long total = frames + preSkip;
	std::vector<float> pcm(960 * link.channels);
	unsigned char packet[4000];
	long done = 0;
	ogg_int64_t packetNo = 2;
	while (done < total)
	{
		for (long i = 0; i < 960; i++)
		{
			for (int ch = 0; ch < link.channels; ch++)
			{
				const long frame = done + i;
				pcm[i * link.channels + ch] =
					frame < frames ? tone(link.serial, ch, frame, kOpusRate) : 0.0f;
			}
		}
		const int bytes = opus_encode_float(enc, pcm.data(), 960, packet, sizeof(packet));
		if (bytes < 0)
		{
			std::printf("opus_encode_float failed: %d\n", bytes);
			std::exit(1);
		}
		done += 960;
		op = {};
		op.packet = packet;
		op.bytes = bytes;
		op.packetno = packetNo++;
		op.granulepos = std::min(done, total);
		op.e_o_s = done >= total ? 1 : 0;
		ogg_stream_packetin(&os, &op);
		takePages(os, out, false);
	}
	takePages(os, out, true);
	ogg_stream_clear(&os);
	opus_encoder_destroy(enc);
	return out;
}

Bytes chain(const std::vector<Bytes> &links)
{
	Bytes out;
	for (const Bytes &link : links)
		append(out, link);
	return out;
}

struct Planar
{
	unsigned int channels = 0;
	std::vector<std::vector<float>> planes;

	unsigned int frames() const
	{
		return planes.empty() ? 0 : (unsigned int)planes[0].size();
	}
};

// Reads [ws] to its end through a voice instance, kBlock frames at a time like
// the mixer. Each read gets a buffer of the stream's channels plus one, all
// kUnwritten: a read must write every channel of the stream for every frame
// it returns, and nothing past them.
Planar streamAll(SoLoud::WavStream &ws, const std::string &what, double maxSeconds)
{
	Planar out;
	out.channels = ws.mChannels;
	out.planes.resize(out.channels);
	SoLoud::AudioSourceInstance *voice = ws.createInstance();
	voice->init(ws, 0);
	const unsigned int maxFrames = (unsigned int)(maxSeconds * ws.mBaseSamplerate);
	std::vector<float> buffer((out.channels + 1) * kBlock);
	bool wrotePast = false;
	bool leftUnwritten = false;
	while (!voice->hasEnded() && out.frames() < maxFrames)
	{
		std::fill(buffer.begin(), buffer.end(), kUnwritten);
		const unsigned int got = voice->getAudio(buffer.data(), kBlock, kBlock);
		for (unsigned int i = 0; i < kBlock; i++)
			wrotePast |= buffer[out.channels * kBlock + i] != kUnwritten;
		for (unsigned int ch = 0; ch < out.channels; ch++)
		{
			const float *plane = buffer.data() + ch * kBlock;
			for (unsigned int i = 0; i < got; i++)
				leftUnwritten |= plane[i] == kUnwritten;
			out.planes[ch].insert(out.planes[ch].end(), plane, plane + got);
		}
	}
	check(voice->hasEnded(), what + ": the stream ends");
	check(!wrotePast, what + ": reads write no channel past the stream's " +
						  std::to_string(out.channels));
	check(!leftUnwritten, what + ": reads write all " + std::to_string(out.channels) +
							  " channels of every frame");
	delete voice;
	return out;
}

Planar wavAll(const SoLoud::Wav &wav)
{
	Planar out;
	out.channels = wav.mChannels;
	out.planes.resize(out.channels);
	for (unsigned int ch = 0; ch < out.channels; ch++)
	{
		const float *plane = wav.mData + (size_t)ch * wav.mSampleCount;
		out.planes[ch].assign(plane, plane + wav.mSampleCount);
	}
	return out;
}

// RMS of channel [ch] over [from, to) seconds, or -1 when it has no such
// frames.
double rms(const Planar &p, unsigned int ch, double from, double to, unsigned int rate)
{
	const size_t a = (size_t)(from * rate);
	const size_t b = (size_t)(to * rate);
	if (ch >= p.channels || b > p.planes[ch].size() || a >= b)
		return -1;
	double sum = 0;
	for (size_t i = a; i < b; i++)
		sum += (double)p.planes[ch][i] * p.planes[ch][i];
	return std::sqrt(sum / (double)(b - a));
}

// Largest difference between channels 0 and 1 over [from, to) seconds, or
// -1 when they have no such frames.
double channelDifference(const Planar &p, double from, double to, unsigned int rate)
{
	const size_t a = (size_t)(from * rate);
	const size_t b = (size_t)(to * rate);
	if (p.channels < 2 || b > p.planes[1].size() || a >= b)
		return -1;
	double most = 0;
	for (size_t i = a; i < b; i++)
		most = std::max(most, (double)std::fabs(p.planes[0][i] - p.planes[1][i]));
	return most;
}

// A tone has an RMS of 0.35, or 0.25 down-mixed with another; silence and
// what a codec leaves of it are far below this.
constexpr double kHeard = 0.1;

bool loadStream(SoLoud::WavStream &ws, const Bytes &file, const std::string &what)
{
	const SoLoud::result r = ws.loadMem(file.data(), (unsigned int)file.size(), true, false);
	check(r == SoLoud::SO_NO_ERROR, what + ": WavStream::loadMem() returns " +
										std::to_string(r) + ", not 0");
	return r == SoLoud::SO_NO_ERROR;
}

bool loadWav(SoLoud::Wav &wav, const Bytes &file, const std::string &what)
{
	const SoLoud::result r = wav.loadMem(file.data(), (unsigned int)file.size(), true, false);
	check(r == SoLoud::SO_NO_ERROR, what + ": Wav::loadMem() returns " +
										std::to_string(r) + ", not 0");
	return r == SoLoud::SO_NO_ERROR;
}

// An engine on the null backend, mixed by hand. Declare it before the sources
// it plays, so that they are destroyed first.
struct Engine
{
	SoLoud::Soloud soloud;

	Engine()
	{
		soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, kOpusRate,
					kBlock, 2);
	}

	~Engine() { soloud.deinit(); }

	// Plays [ws] until its voice ends; returns the seconds it played, or -1
	// when it was still playing after [maxSeconds].
	double playToEnd(SoLoud::WavStream &ws, double maxSeconds)
	{
		const SoLoud::handle h = soloud.play(ws);
		std::vector<float> mix(kBlock * 2);
		unsigned int frames = 0;
		while (soloud.isValidVoiceHandle(h) && frames < maxSeconds * kOpusRate)
		{
			soloud.mix(mix.data(), kBlock);
			frames += kBlock;
		}
		return soloud.isValidVoiceHandle(h) ? -1 : (double)frames / kOpusRate;
	}
};

void testOpusZeroChannelLink()
{
	const std::string what =
		"stereo Opus link, a link whose OpusHead says 0 channels, a stereo link";
	std::printf("%s\n", what.c_str());
	// The last link is the longest, so that Wav, which decodes as many frames
	// as getLengthInSamples() says, gets past the first link too.
	const Bytes file = chain({makeOpusLink({2, 2, 11, 1.0}), makeOpusLink({2, 0, 12, 0.5}),
							  makeOpusLink({2, 2, 13, 1.5})});

	Engine engine;
	SoLoud::WavStream ws;
	if (!loadStream(ws, file, what))
		return;
	check(ws.mChannels == 2, what + ": the stream has the first link's 2 channels");
	const Planar streamed = streamAll(ws, what, 5.0);
	// The second link is skipped: 2.5 s of the other two, and their encoder
	// padding, under one 20 ms packet each.
	const unsigned int expected = (unsigned int)(2.5 * kOpusRate);
	check(streamed.frames() >= expected && streamed.frames() <= expected + 2 * 960,
		  what + ": the stream plays the first and the last link, " +
			  std::to_string(expected) + " to " + std::to_string(expected + 2 * 960) +
			  " frames, not " + std::to_string(streamed.frames()));
	for (unsigned int ch = 0; ch < 2; ch++)
	{
		check(rms(streamed, ch, 0.1, 0.9, kOpusRate) > kHeard,
			  what + ": channel " + std::to_string(ch) + " plays the first link");
		check(rms(streamed, ch, 1.1, 2.4, kOpusRate) > kHeard,
			  what + ": channel " + std::to_string(ch) + " plays the last link");
	}

	const double played = engine.playToEnd(ws, 5.0);
	check(played >= 2.5 && played < 2.7,
		  what + ": a voice plays the first and the last link, then ends (played " +
			  std::to_string(played) + " s)");

	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	const Planar loaded = wavAll(wav);
	for (unsigned int ch = 0; ch < loaded.channels; ch++)
	{
		check(rms(loaded, ch, 0.1, 0.9, kOpusRate) > kHeard,
			  what + ": Wav channel " + std::to_string(ch) + " holds the first link");
		check(rms(loaded, ch, 1.1, 1.4, kOpusRate) > kHeard,
			  what + ": Wav channel " + std::to_string(ch) + " holds the last link");
	}
}

void testOpusMonoThenStereo()
{
	const std::string what = "mono Opus link, then a stereo link";
	std::printf("%s\n", what.c_str());
	// Wav decodes only as many frames as getLengthInSamples() says, so the
	// second link is the longer one: it then decodes into Wav too.
	const Bytes file = chain({makeOpusLink({1, 1, 21, 0.5}), makeOpusLink({2, 2, 22, 1.5})});

	SoLoud::WavStream ws;
	if (!loadStream(ws, file, what))
		return;
	check(ws.mChannels == 1, what + ": the stream has the first link's 1 channel");
	const Planar streamed = streamAll(ws, what, 5.0);
	check(rms(streamed, 0, 0.1, 0.4, kOpusRate) > kHeard, what + ": the first link plays");
	check(rms(streamed, 0, 0.6, 1.4, kOpusRate) > kHeard,
		  what + ": the second link plays, down-mixed to mono");

	// Wav decodes into a buffer of exactly the stream's channels.
	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	check(wav.mChannels == 1, what + ": Wav has the first link's 1 channel");
	const Planar loaded = wavAll(wav);
	check(rms(loaded, 0, 0.1, 0.4, kOpusRate) > kHeard, what + ": Wav holds the first link");
	check(rms(loaded, 0, 0.6, 1.4, kOpusRate) > kHeard, what + ": Wav holds the second link");
}

void testOpusStereoThenMono()
{
	const std::string what = "stereo Opus link, then a mono link";
	std::printf("%s\n", what.c_str());
	const Bytes file = chain({makeOpusLink({2, 2, 31, 0.5}), makeOpusLink({1, 1, 32, 1.5})});

	SoLoud::WavStream ws;
	if (!loadStream(ws, file, what))
		return;
	check(ws.mChannels == 2, what + ": the stream has the first link's 2 channels");
	const Planar streamed = streamAll(ws, what, 5.0);
	for (unsigned int ch = 0; ch < 2; ch++)
	{
		check(rms(streamed, ch, 0.1, 0.4, kOpusRate) > kHeard,
			  what + ": channel " + std::to_string(ch) + " plays the first link");
		check(rms(streamed, ch, 0.6, 1.4, kOpusRate) > kHeard,
			  what + ": channel " + std::to_string(ch) + " plays the second link");
	}
	const double difference = channelDifference(streamed, 0.6, 1.4, kOpusRate);
	check(difference >= 0 && difference < 1e-6,
		  what + ": the mono link plays the same on both channels (they differ by " +
			  std::to_string(difference) + ")");

	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	const Planar loaded = wavAll(wav);
	const double wavDifference = channelDifference(loaded, 0.6, 1.4, kOpusRate);
	check(wavDifference >= 0 && wavDifference < 1e-6,
		  what + ": Wav holds the mono link on both channels (they differ by " +
			  std::to_string(wavDifference) + ")");
}

// A case runs when no argument is given, or when an argument names it or its
// group ("opus-channels" runs "opus-channels/zero").
bool wanted(int argc, char **argv, const std::string &name)
{
	if (argc < 2)
		return true;
	for (int i = 1; i < argc; i++)
	{
		const std::string arg = argv[i];
		if (name == arg || name.rfind(arg + "/", 0) == 0)
			return true;
	}
	return false;
}

struct Case
{
	const char *name;
	void (*run)();
};

const Case kCases[] = {
	{"opus-channels/zero", testOpusZeroChannelLink},
	{"opus-channels/mono-stereo", testOpusMonoThenStereo},
	{"opus-channels/stereo-mono", testOpusStereoThenMono},
};

} // namespace

int main(int argc, char **argv)
{
	// Keep the output in order with the decoder's own printf()s, and complete
	// up to a crash.
	setvbuf(stdout, nullptr, _IOLBF, 0);

	for (const Case &c : kCases)
	{
		if (wanted(argc, argv, c.name))
			c.run();
	}

	std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
	return gFailures == 0 ? 0 : 1;
}
