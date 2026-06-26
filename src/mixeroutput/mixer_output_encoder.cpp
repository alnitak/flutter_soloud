#include "mixer_output_encoder.h"

#include "flac_output_encoder.h"
#include "opus_output_encoder.h"
#include "vorbis_output_encoder.h"

MixerOutputEncoder *MixerOutputEncoder::create(MixerOutputFormat format) {
  switch (format) {
    case MIXER_OUTPUT_OPUS:
      return new OpusOutputEncoder();
    case MIXER_OUTPUT_VORBIS:
      return new VorbisOutputEncoder();
    case MIXER_OUTPUT_FLAC:
      return new FlacOutputEncoder();
    default:
      return nullptr;
  }
}
