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
// vorbis-channels: reads copied the stream's channel count of planes out of
// what libvorbis returned for the link being read. A mono link after a stereo
// one has one plane: the second copy read a pointer past libvorbis' array and
// copied from wherever it pointed. A link with fewer channels than the stream
// now repeats them, and a mono stream plays the mean of a stereo link's two
// channels (it played the first), as it does for Opus.
//
// opus-length: each link counts its granule positions from 0, and the length
// came from the last page's: the last link's alone. Wav decodes only that
// many frames, so a 2 s link and a 1 s link loaded as 1 s. The length is now
// the sum of the links that play (a link without OpusTags does not), or
// unknown, 0, when one of them gives no granule position past its pre-skip:
// Wav then decodes to the end of the file.
//
// implausible-length: the length was whatever the last granule position, or
// an Ogg FLAC file's STREAMINFO, said. Wav read it into an int and sized its
// buffer from it, so a damaged or crafted file that said 2^40 frames asked
// for an allocation of nearly 2^64 bytes, and 2^50 overflowed the conversion
// to the output rate. A length of more frames than the file's bytes can
// decode to, or than 32 bits hold, is now unknown, and Wav decodes to the
// end; Wav does so too for a length past INT_MAX, which WavStream keeps.
//
// The test builds its files with libopus, libvorbisenc, libFLAC and libogg,
// and reads them through a WavStream voice instance the way the mixer does,
// through a voice on the null backend, and through Wav (what LoadMode.memory
// uses).
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_ogg_chain_test.sh [--asan] [case or group...]
//
// Groups: opus-channels, vorbis-channels, opus-length, implausible-length.
// With none, all cases run.

#include "soloud.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"

#include "native_audio_decoder.h"

#include <FLAC/stream_encoder.h>
#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>
#if __has_include(<opus/opus.h>)
#include <opus/opus.h>
#else
#include <opus.h>
#endif

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
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
constexpr unsigned int kVorbisRate = 44100;
constexpr unsigned int kFlacRate = 44100;
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
	bool tags = true;          // an OpusTags packet follows the OpusHead
	bool audioGranules = true; // audio pages carry granule positions
	std::optional<ogg_int64_t> lastGranule; // the last page's, if not as encoded
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
	if (link.tags)
	{
		op = {};
		op.packet = tags.data();
		op.bytes = (long)tags.size();
		op.packetno = 1;
		ogg_stream_packetin(&os, &op);
		takePages(os, out, true);
	}

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
		op.granulepos = link.audioGranules ? std::min(done, total) : -1;
		op.e_o_s = done >= total ? 1 : 0;
		if (op.e_o_s && link.lastGranule)
			op.granulepos = *link.lastGranule;
		ogg_stream_packetin(&os, &op);
		takePages(os, out, false);
	}
	takePages(os, out, true);
	ogg_stream_clear(&os);
	opus_encoder_destroy(enc);
	return out;
}

// One Ogg Vorbis link of a tone at 44.1 kHz.
Bytes makeVorbisLink(int channels, int serial, double seconds)
{
	vorbis_info vi;
	vorbis_info_init(&vi);
	if (vorbis_encode_init_vbr(&vi, channels, kVorbisRate, 0.4f) != 0)
	{
		std::printf("vorbis_encode_init_vbr failed\n");
		std::exit(1);
	}
	vorbis_comment vc;
	vorbis_comment_init(&vc);
	vorbis_dsp_state vd;
	vorbis_analysis_init(&vd, &vi);
	vorbis_block vb;
	vorbis_block_init(&vd, &vb);

	Bytes out;
	ogg_stream_state os;
	ogg_stream_init(&os, serial);
	ogg_packet header, comments, codebooks;
	vorbis_analysis_headerout(&vd, &vc, &header, &comments, &codebooks);
	ogg_stream_packetin(&os, &header);
	ogg_stream_packetin(&os, &comments);
	ogg_stream_packetin(&os, &codebooks);
	takePages(os, out, true);

	const long frames = std::lround(seconds * kVorbisRate);
	long done = 0;
	bool ended = false;
	while (!ended)
	{
		const long n = std::min(1024L, frames - done);
		if (n > 0)
		{
			float **buffer = vorbis_analysis_buffer(&vd, (int)n);
			for (long i = 0; i < n; i++)
			{
				for (int ch = 0; ch < channels; ch++)
					buffer[ch][i] = tone(serial, ch, done + i, kVorbisRate);
			}
			vorbis_analysis_wrote(&vd, (int)n);
			done += n;
		}
		else
		{
			vorbis_analysis_wrote(&vd, 0); // end of the link
			ended = true;
		}
		while (vorbis_analysis_blockout(&vd, &vb) == 1)
		{
			vorbis_analysis(&vb, nullptr);
			vorbis_bitrate_addblock(&vb);
			ogg_packet op;
			while (vorbis_bitrate_flushpacket(&vd, &op))
			{
				ogg_stream_packetin(&os, &op);
				// A page per packet: libvorbisfile skips a link of a
				// chained file whose audio is a single page.
				takePages(os, out, true);
			}
		}
	}
	takePages(os, out, true);
	ogg_stream_clear(&os);
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	return out;
}

// One Ogg FLAC link of a 16-bit tone at 44.1 kHz whose STREAMINFO says
// [totalSamples] frames, 0 for unknown: libFLAC writes the real count there
// only when it can seek back to the start, and it gets no seek callback.
Bytes makeOggFlacLink(int channels, int serial, double seconds, FLAC__uint64 totalSamples)
{
	FLAC__StreamEncoder *enc = FLAC__stream_encoder_new();
	if (enc == nullptr)
	{
		std::printf("FLAC__stream_encoder_new failed\n");
		std::exit(1);
	}
	FLAC__stream_encoder_set_channels(enc, (uint32_t)channels);
	FLAC__stream_encoder_set_bits_per_sample(enc, 16);
	FLAC__stream_encoder_set_sample_rate(enc, kFlacRate);
	FLAC__stream_encoder_set_ogg_serial_number(enc, serial);
	FLAC__stream_encoder_set_total_samples_estimate(enc, totalSamples);
	Bytes out;
	const FLAC__StreamEncoderWriteCallback write =
		[](const FLAC__StreamEncoder *, const FLAC__byte buffer[], size_t bytes, uint32_t,
		   uint32_t, void *data) {
			Bytes *file = static_cast<Bytes *>(data);
			file->insert(file->end(), buffer, buffer + bytes);
			return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
		};
	const FLAC__StreamEncoderInitStatus status =
		FLAC__stream_encoder_init_ogg_stream(enc, nullptr, write, nullptr, nullptr, nullptr, &out);
	if (status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
	{
		std::printf("FLAC__stream_encoder_init_ogg_stream failed: %s\n",
					FLAC__StreamEncoderInitStatusString[status]);
		std::exit(1);
	}

	const long frames = std::lround(seconds * kFlacRate);
	std::vector<FLAC__int32> pcm(1024 * channels);
	for (long done = 0; done < frames;)
	{
		const long n = std::min(1024L, frames - done);
		for (long i = 0; i < n; i++)
		{
			for (int ch = 0; ch < channels; ch++)
				pcm[i * channels + ch] =
					(FLAC__int32)std::lround(32767 * tone(serial, ch, done + i, kFlacRate));
		}
		if (!FLAC__stream_encoder_process_interleaved(enc, pcm.data(), (uint32_t)n))
		{
			std::printf("FLAC__stream_encoder_process_interleaved failed\n");
			std::exit(1);
		}
		done += n;
	}
	FLAC__stream_encoder_finish(enc);
	FLAC__stream_encoder_delete(enc);
	return out;
}

// Sets the granule position of [file]'s last page to [granule], as a damaged
// or crafted file can have it, and the page's checksum to match.
void setLastGranule(Bytes &file, ogg_int64_t granule)
{
	size_t last = 0;
	long headerLen = 0;
	long bodyLen = 0;
	for (size_t at = 0; at + 27 <= file.size();)
	{
		const int segments = file[at + 26];
		long body = 0;
		for (int i = 0; i < segments; i++)
			body += file[at + 27 + i];
		last = at;
		headerLen = 27 + segments;
		bodyLen = body;
		at += headerLen + body;
	}
	for (int i = 0; i < 8; i++)
		file[last + 6 + i] = (unsigned char)((uint64_t)granule >> (8 * i));
	ogg_page og;
	og.header = file.data() + last;
	og.header_len = headerLen;
	og.body = file.data() + last + headerLen;
	og.body_len = bodyLen;
	ogg_page_checksum_set(&og);
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

// Largest difference between channel 0 of [mono] from frame [start] on and
// the mean of [stereo]'s two channels, or -1 when [mono] has no such frames.
double downmixDifference(const Planar &mono, unsigned int start, const Planar &stereo)
{
	const unsigned int frames = stereo.frames();
	if (mono.channels < 1 || stereo.channels < 2 || frames == 0 ||
		start + frames > mono.frames())
		return -1;
	double most = 0;
	for (unsigned int i = 0; i < frames; i++)
	{
		const float mix = (stereo.planes[0][i] + stereo.planes[1][i]) / 2.0f;
		most = std::max(most, (double)std::fabs(mono.planes[0][start + i] - mix));
	}
	return most;
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

void testVorbisStereoThenMono()
{
	const std::string what = "stereo Vorbis link, then a mono link";
	std::printf("%s\n", what.c_str());
	const Bytes file = chain({makeVorbisLink(2, 41, 0.5), makeVorbisLink(1, 42, 1.5)});

	SoLoud::WavStream ws;
	if (!loadStream(ws, file, what))
		return;
	check(ws.mChannels == 2, what + ": the stream has the first link's 2 channels");
	const Planar streamed = streamAll(ws, what, 5.0);
	for (unsigned int ch = 0; ch < 2; ch++)
	{
		check(rms(streamed, ch, 0.1, 0.4, kVorbisRate) > kHeard,
			  what + ": channel " + std::to_string(ch) + " plays the first link");
		check(rms(streamed, ch, 0.6, 1.9, kVorbisRate) > kHeard,
			  what + ": channel " + std::to_string(ch) + " plays the second link");
	}
	const double difference = channelDifference(streamed, 0.6, 1.9, kVorbisRate);
	check(difference == 0,
		  what + ": the mono link plays the same on both channels (they differ by " +
			  std::to_string(difference) + ")");

	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	const Planar loaded = wavAll(wav);
	for (unsigned int ch = 0; ch < loaded.channels; ch++)
		check(rms(loaded, ch, 0.6, 1.9, kVorbisRate) > kHeard,
			  what + ": Wav channel " + std::to_string(ch) + " holds the second link");
	const double wavDifference = channelDifference(loaded, 0.6, 1.9, kVorbisRate);
	check(wavDifference == 0,
		  what + ": Wav holds the mono link on both channels (they differ by " +
			  std::to_string(wavDifference) + ")");
}

void testVorbisMonoThenStereo()
{
	const std::string what = "mono Vorbis link, then a stereo link";
	std::printf("%s\n", what.c_str());
	const Bytes first = makeVorbisLink(1, 51, 0.5);
	const Bytes second = makeVorbisLink(2, 52, 1.5);
	const Bytes file = chain({first, second});

	SoLoud::WavStream ws;
	if (!loadStream(ws, file, what))
		return;
	check(ws.mChannels == 1, what + ": the stream has the first link's 1 channel");
	const Planar streamed = streamAll(ws, what, 5.0);
	check(rms(streamed, 0, 0.1, 0.4, kVorbisRate) > kHeard, what + ": the first link plays");
	check(rms(streamed, 0, 0.6, 1.9, kVorbisRate) > kHeard, what + ": the second link plays");

	// The second link, its channels two tones, plays as their mean, as Opus
	// gives it: what each link decodes to on its own shows where it starts
	// and what its channels are.
	SoLoud::WavStream firstAlone;
	SoLoud::WavStream secondAlone;
	if (!loadStream(firstAlone, first, what + " (first link alone)") ||
		!loadStream(secondAlone, second, what + " (second link alone)"))
		return;
	const unsigned int start = streamAll(firstAlone, what + " (first link alone)", 5.0).frames();
	const Planar stereo = streamAll(secondAlone, what + " (second link alone)", 5.0);
	const double mixDifference = downmixDifference(streamed, start, stereo);
	check(mixDifference >= 0 && mixDifference < 1e-6,
		  what + ": the second link plays as the mean of its channels (off by " +
			  std::to_string(mixDifference) + ")");

	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	check(wav.mChannels == 1, what + ": Wav has the first link's 1 channel");
	const Planar loaded = wavAll(wav);
	check(rms(loaded, 0, 0.6, 1.9, kVorbisRate) > kHeard, what + ": Wav holds the second link");
	const double wavMixDifference = downmixDifference(loaded, start, stereo);
	check(wavMixDifference >= 0 && wavMixDifference < 1e-6,
		  what + ": Wav holds the second link as the mean of its channels (off by " +
			  std::to_string(wavMixDifference) + ")");
}

// Checks the length WavStream and Wav report for [file], and that Wav holds
// every link: [links] are the seconds of each link that plays, at [rate].
void checkLength(const std::string &what, const Bytes &file, const std::vector<double> &links,
				 unsigned int rate = kOpusRate)
{
	double seconds = 0;
	for (double s : links)
		seconds += s;
	const unsigned int frames = (unsigned int)std::lround(seconds * rate);

	SoLoud::WavStream ws;
	if (loadStream(ws, file, what))
	{
		check(ws.mSampleCount == frames,
			  what + ": WavStream is " + std::to_string(frames) + " frames long, not " +
				  std::to_string(ws.mSampleCount));
		check(std::fabs(ws.getLength() - seconds) < 1e-9,
			  what + ": WavStream::getLength() is " + std::to_string(seconds) + " s, not " +
				  std::to_string(ws.getLength()));
	}

	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	check(wav.mSampleCount == frames, what + ": Wav is " + std::to_string(frames) +
										  " frames long, not " +
										  std::to_string(wav.mSampleCount));
	check(std::fabs(wav.getLength() - seconds) < 1e-9,
		  what + ": Wav::getLength() is " + std::to_string(seconds) + " s, not " +
			  std::to_string(wav.getLength()));
	const Planar loaded = wavAll(wav);
	double start = 0;
	for (size_t i = 0; i < links.size(); i++)
	{
		for (unsigned int ch = 0; ch < loaded.channels; ch++)
			check(rms(loaded, ch, start + 0.1, start + links[i] - 0.1, rate) > kHeard,
				  what + ": Wav channel " + std::to_string(ch) + " holds link " +
					  std::to_string(i + 1) + " of those that play");
		start += links[i];
	}
}

void testOpusLengthTwoLinks()
{
	const std::string what = "2 s stereo Opus link, then a 1 s one";
	std::printf("%s\n", what.c_str());
	checkLength(what, chain({makeOpusLink({2, 2, 61, 2.0}), makeOpusLink({2, 2, 62, 1.0})}),
				{2.0, 1.0});
}

void testOpusLengthThreeLinks()
{
	const std::string what = "1 s mono Opus link, a 0.5 s stereo one and a 1.5 s mono one";
	std::printf("%s\n", what.c_str());
	checkLength(what,
				chain({makeOpusLink({1, 1, 71, 1.0}), makeOpusLink({2, 2, 72, 0.5}),
					   makeOpusLink({1, 1, 73, 1.5})}),
				{1.0, 0.5, 1.5});
}

void testOpusLengthSkippedLink()
{
	const std::string what =
		"1.5 s stereo Opus link, then a 0.5 s link whose OpusHead says 0 channels";
	std::printf("%s\n", what.c_str());
	// The second link does not play, so it adds nothing.
	checkLength(what, chain({makeOpusLink({2, 2, 81, 1.5}), makeOpusLink({2, 0, 82, 0.5})}),
				{1.5});
}

void testOpusLengthSingleLink()
{
	const std::string what = "a single 2 s stereo Opus link";
	std::printf("%s\n", what.c_str());
	checkLength(what, makeOpusLink({2, 2, 91, 2.0}), {2.0});
}

// Checks that WavStream and Wav report [file]'s length as unknown, 0, and
// that Wav, which then decodes the file to its end, holds every link:
// [links] are the seconds of each link that plays, at [rate].
void checkUnknownLength(const std::string &what, const Bytes &file,
						const std::vector<double> &links, unsigned int rate = kOpusRate)
{
	double seconds = 0;
	for (double s : links)
		seconds += s;
	const unsigned int frames = (unsigned int)std::lround(seconds * rate);

	SoLoud::WavStream ws;
	if (loadStream(ws, file, what))
	{
		check(ws.mSampleCount == 0, what + ": WavStream's length is unknown, 0 frames, not " +
										std::to_string(ws.mSampleCount));
		const Planar streamed = streamAll(ws, what, seconds + 1.0);
		check(streamed.frames() >= frames, what + ": the stream plays " +
											   std::to_string(frames) + " frames or more, not " +
											   std::to_string(streamed.frames()));
	}

	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	check(wav.mSampleCount >= frames, what + ": Wav holds " + std::to_string(frames) +
										  " frames or more, not " +
										  std::to_string(wav.mSampleCount));
	const Planar loaded = wavAll(wav);
	double start = 0;
	for (size_t i = 0; i < links.size(); i++)
	{
		for (unsigned int ch = 0; ch < loaded.channels; ch++)
			check(rms(loaded, ch, start + 0.1, start + links[i] - 0.1, rate) > kHeard,
				  what + ": Wav channel " + std::to_string(ch) + " holds link " +
					  std::to_string(i + 1) + " of those that play");
		start += links[i];
	}
}

void testOpusLengthUntaggedLink()
{
	const std::string what = "2 s stereo Opus link, a 1 s one without OpusTags, a 0.5 s one";
	std::printf("%s\n", what.c_str());
	// The decoder takes the second link's packets for headers until the next
	// link, so it does not play, and adds nothing.
	OpusLink untagged{2, 2, 102, 1.0};
	untagged.tags = false;
	checkLength(what,
				chain({makeOpusLink({2, 2, 101, 2.0}), makeOpusLink(untagged),
					   makeOpusLink({2, 2, 103, 0.5})}),
				{2.0, 0.5});
}

void testOpusLengthNoGranule()
{
	const std::string what =
		"1 s stereo Opus link, then a 1 s one whose pages give no granule position";
	std::printf("%s\n", what.c_str());
	OpusLink second{2, 2, 112, 1.0};
	second.audioGranules = false;
	checkUnknownLength(what, chain({makeOpusLink({2, 2, 111, 1.0}), makeOpusLink(second)}),
					   {1.0, 1.0});
}

void testOpusLengthGranuleBelowPreSkip()
{
	const std::string what =
		"1 s stereo Opus link, then a 1 s one whose only granule position, 100, is below "
		"its pre-skip";
	std::printf("%s\n", what.c_str());
	OpusLink second{2, 2, 122, 1.0};
	second.audioGranules = false;
	second.lastGranule = 100;
	checkUnknownLength(what, chain({makeOpusLink({2, 2, 121, 1.0}), makeOpusLink(second)}),
					   {1.0, 1.0});
}

void testOpusLengthSkippedLinkNoGranule()
{
	const std::string what = "1.5 s stereo Opus link, then a 0.5 s link whose OpusHead says 0 "
							 "channels and whose pages give no granule position";
	std::printf("%s\n", what.c_str());
	// A link that does not play cannot make the length unknown.
	OpusLink skipped{2, 0, 132, 0.5};
	skipped.audioGranules = false;
	checkLength(what, chain({makeOpusLink({2, 2, 131, 1.5}), makeOpusLink(skipped)}), {1.5});
}

void testImplausibleOpusPast32Bits()
{
	const std::string what = "1 s stereo Opus link whose last granule position is 2^40";
	std::printf("%s\n", what.c_str());
	// Cut to 32 bits, the length was 2^32 - 312: a negative int to Wav.
	Bytes file = makeOpusLink({2, 2, 141, 1.0});
	setLastGranule(file, (ogg_int64_t)1 << 40);
	checkUnknownLength(what, file, {1.0});
}

void testImplausibleOpusOverflow()
{
	const std::string what = "1 s stereo Opus link whose last granule position is 2^50";
	std::printf("%s\n", what.c_str());
	// Times the output rate, this many frames overflowed an int64_t.
	Bytes file = makeOpusLink({2, 2, 142, 1.0});
	setLastGranule(file, (ogg_int64_t)1 << 50);
	checkUnknownLength(what, file, {1.0});
}

void testImplausibleOpusPastFileSize()
{
	const std::string what = "1 s mono Opus link whose last granule position is past its size";
	std::printf("%s\n", what.c_str());
	// An int holds this many frames, but the file's bytes cannot decode to
	// them: an Opus packet takes a byte at least and decodes to 120 ms at most,
	// 5760 frames.
	Bytes file = makeOpusLink({1, 1, 143, 1.0});
	setLastGranule(file, (ogg_int64_t)file.size() * 5760 + kOpusRate);
	checkUnknownLength(what, file, {1.0});
}

void testImplausibleOpusChain()
{
	const std::string what =
		"two 1 s stereo Opus links, the second's last granule position INT64_MAX - 10";
	std::printf("%s\n", what.c_str());
	// The links' lengths add up past INT64_MAX.
	Bytes file = chain({makeOpusLink({2, 2, 144, 1.0}), makeOpusLink({2, 2, 145, 1.0})});
	setLastGranule(file, INT64_MAX - 10);
	checkUnknownLength(what, file, {1.0, 1.0});
}

void testImplausibleVorbis()
{
	const std::string what = "1 s stereo Vorbis link whose last granule position is 2^63 - 16";
	std::printf("%s\n", what.c_str());
	Bytes file = makeVorbisLink(2, 146, 1.0);
	setLastGranule(file, INT64_MAX - 15);
	checkUnknownLength(what, file, {1.0}, kVorbisRate);
}

void testImplausibleFlacGranule()
{
	const std::string what = "1 s stereo Ogg FLAC link of unknown length whose last granule "
							 "position is 2^63 - 16";
	std::printf("%s\n", what.c_str());
	Bytes file = makeOggFlacLink(2, 147, 1.0, 0);
	setLastGranule(file, INT64_MAX - 15);
	checkUnknownLength(what, file, {1.0}, kFlacRate);
}

void testImplausibleFlacStreamInfo()
{
	const std::string what = "1 s stereo Ogg FLAC link whose STREAMINFO says 2^36 - 2^31 frames";
	std::printf("%s\n", what.c_str());
	// Cut to 32 bits, the count was 2^31, a negative int to Wav. The last
	// granule position, which is right, gives the length instead.
	checkLength(what, makeOggFlacLink(2, 148, 1.0, 0xF80000000), {1.0}, kFlacRate);
}

void testImplausibleWavPastInt()
{
	const std::string what = "40 s stereo Opus link whose last granule position is INT_MAX + 48000";
	std::printf("%s\n", what.c_str());
	// As a 12-hour file's would, the length fits in 32 bits and in what the
	// file's bytes can decode to, so WavStream keeps it. Wav, which counts
	// frames in an int, decodes to the end of the file instead.
	Bytes file = makeOpusLink({2, 2, 149, 40.0});
	const ogg_int64_t granule = (ogg_int64_t)INT_MAX + kOpusRate;
	check((ogg_int64_t)file.size() * 5760 > granule,
		  what + ": the file's " + std::to_string(file.size()) +
			  " bytes can decode to that many frames");
	setLastGranule(file, granule);

	SoLoud::WavStream ws;
	if (loadStream(ws, file, what))
		check(ws.mSampleCount > (unsigned int)INT_MAX,
			  what + ": WavStream keeps the length past INT_MAX, not " +
				  std::to_string(ws.mSampleCount) + " frames");

	SoLoud::Wav wav;
	if (!loadWav(wav, file, what))
		return;
	const unsigned int frames = 40 * kOpusRate;
	check(wav.mSampleCount >= frames && wav.mSampleCount < frames + kOpusRate,
		  what + ": Wav holds the link's " + std::to_string(frames) + " frames, not " +
			  std::to_string(wav.mSampleCount));
	const Planar loaded = wavAll(wav);
	for (unsigned int ch = 0; ch < loaded.channels; ch++)
		check(rms(loaded, ch, 0.1, 39.9, kOpusRate) > kHeard,
			  what + ": Wav channel " + std::to_string(ch) + " holds the link");
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
	{"vorbis-channels/stereo-mono", testVorbisStereoThenMono},
	{"vorbis-channels/mono-stereo", testVorbisMonoThenStereo},
	{"opus-length/two-links", testOpusLengthTwoLinks},
	{"opus-length/three-links", testOpusLengthThreeLinks},
	{"opus-length/skipped-link", testOpusLengthSkippedLink},
	{"opus-length/single-link", testOpusLengthSingleLink},
	{"opus-length/untagged-link", testOpusLengthUntaggedLink},
	{"opus-length/no-granule", testOpusLengthNoGranule},
	{"opus-length/granule-below-pre-skip", testOpusLengthGranuleBelowPreSkip},
	{"opus-length/skipped-link-no-granule", testOpusLengthSkippedLinkNoGranule},
	{"implausible-length/opus-past-32-bits", testImplausibleOpusPast32Bits},
	{"implausible-length/opus-overflow", testImplausibleOpusOverflow},
	{"implausible-length/opus-past-file-size", testImplausibleOpusPastFileSize},
	{"implausible-length/opus-chain", testImplausibleOpusChain},
	{"implausible-length/vorbis", testImplausibleVorbis},
	{"implausible-length/flac-granule", testImplausibleFlacGranule},
	{"implausible-length/flac-streaminfo", testImplausibleFlacStreamInfo},
	{"implausible-length/wav-past-int", testImplausibleWavPastInt},
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
