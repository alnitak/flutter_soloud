// this source is used only on Mac and iOS to reference all the others

/// SoLoud sources
#define WITH_NULL
#define WITH_NOSOUND
#define WITH_MINIAUDIO

#include <AudioToolbox/AudioSession.h>
#include <AudioToolbox/AudioToolbox.h>

// #define WITH_COREAUDIO
// Core
#include "soloud/src/core/soloud.cpp"
#include "soloud/src/core/soloud_audiosource.cpp"
#include "soloud/src/core/soloud_bus.cpp"
#include "soloud/src/core/soloud_core_3d.cpp"
#include "soloud/src/core/soloud_core_basicops.cpp"
#include "soloud/src/core/soloud_core_faderops.cpp"
#include "soloud/src/core/soloud_core_filterops.cpp"
#include "soloud/src/core/soloud_core_getters.cpp"
#include "soloud/src/core/soloud_core_setters.cpp"
#include "soloud/src/core/soloud_core_voicegroup.cpp"
#include "soloud/src/core/soloud_core_voiceops.cpp"
#include "soloud/src/core/soloud_fader.cpp"
#include "soloud/src/core/soloud_fft.cpp"
#include "soloud/src/core/soloud_fft_lut.cpp"
#include "soloud/src/core/soloud_file.cpp"
#include "soloud/src/core/soloud_filter.cpp"
#include "soloud/src/core/soloud_misc.cpp"
#include "soloud/src/core/soloud_queue.cpp"
#include "soloud/src/core/soloud_thread.cpp"


// Audiosources
// 	ay
#include "soloud/src/audiosource/ay/chipplayer.cpp"
#include "soloud/src/audiosource/ay/sndbuffer.cpp"
#include "soloud/src/audiosource/ay/sndchip.cpp"
#include "soloud/src/audiosource/ay/sndrender.cpp"
#include "soloud/src/audiosource/ay/soloud_ay.cpp"

// monotone
#include "soloud/src/audiosource/monotone/soloud_monotone.cpp"

// noise
#include "soloud/src/audiosource/noise/soloud_noise.cpp"

// sfxr
#include "soloud/src/audiosource/sfxr/soloud_sfxr.cpp"

// 	speech
// #include "soloud/src/audiosource/speech/Elements.def"
#include "soloud/src/audiosource/speech/darray.cpp"
#include "soloud/src/audiosource/speech/klatt.cpp"
#include "soloud/src/audiosource/speech/resonator.cpp"
#include "soloud/src/audiosource/speech/soloud_speech.cpp"
#include "soloud/src/audiosource/speech/tts.cpp"

// 	tedsid
#include "soloud/src/audiosource/tedsid/sid.cpp"
#include "soloud/src/audiosource/tedsid/soloud_tedsid.cpp"
#include "soloud/src/audiosource/tedsid/ted.cpp"

// 	vic
#include "soloud/src/audiosource/vic/soloud_vic.cpp"

// 	# wav
#include "soloud/src/audiosource/wav/stb_vorbis.c"
#include "soloud/src/audiosource/wav/dr_impl.cpp"
#include "soloud/src/audiosource/wav/soloud_wav.cpp"
#include "soloud/src/audiosource/wav/soloud_wavstream.cpp"


// Backends
#include "soloud/src/backend/null/soloud_null.cpp"
#include "soloud/src/backend/nosound/soloud_nosound.cpp"
#include "soloud/src/backend/miniaudio/soloud_miniaudio.cpp"

// Filters
#include "soloud/src/filter/soloud_bassboostfilter.cpp"
#include "soloud/src/filter/soloud_biquadresonantfilter.cpp"
#include "soloud/src/filter/soloud_dcremovalfilter.cpp"
#include "soloud/src/filter/soloud_duckfilter.cpp"
#include "soloud/src/filter/soloud_echofilter.cpp"
#include "soloud/src/filter/soloud_eqfilter.cpp"
#include "soloud/src/filter/soloud_fftfilter.cpp"
#include "soloud/src/filter/soloud_flangerfilter.cpp"
#include "soloud/src/filter/soloud_freeverbfilter.cpp"
#include "soloud/src/filter/soloud_lofifilter.cpp"
#include "soloud/src/filter/soloud_robotizefilter.cpp"
#include "soloud/src/filter/soloud_waveshaperfilter.cpp"



// 	vizsn
// #include "soloud/src/audiosource/vizsn/soloud_vizsn.cpp"


#include "common.cpp"
#include "bindings.cpp"
#include "player.cpp"
#include "analyzer.cpp"
#include "synth/basic_wave.cpp"
#include "waveform/waveform.cpp"
#include "waveform/miniaudio_libvorbis.cpp"
#include "audiobuffer/audiobuffer.cpp"
#include "audiobuffer/stream_decoder.cpp"
#include "audiobuffer/flac_stream_decoder.cpp"
#include "audiobuffer/opus_stream_decoder.cpp"
#include "audiobuffer/vorbis_stream_decoder.cpp"
#include "audiobuffer/mp3_stream_decoder.cpp"
#include "filters/filters.cpp"
#include "filters/pitch_shift_filter.cpp"
#include "filters/smbPitchShift.cpp"
#include "filters/limiter.cpp"
#include "filters/compressor.cpp"
