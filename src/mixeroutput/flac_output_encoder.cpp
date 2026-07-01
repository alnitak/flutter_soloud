#include "flac_output_encoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef NO_XIPH_LIBS
#include <FLAC/format.h>
#include <FLAC/stream_encoder.h>
#endif

namespace {
void flacLog(const char *msg) {
  const char *dir = getenv("TMPDIR");
  if (dir == nullptr) {
    dir = getenv("HOME");
  }
  if (dir == nullptr) {
    dir = "/tmp";
  }
  char path[1024];
  snprintf(path, sizeof(path), "%s/flac_debug.log", dir);
  FILE *fp = fopen(path, "a");
  if (fp != nullptr) {
    fprintf(fp, "%s\n", msg);
    fclose(fp);
  }
}
}  // namespace

FlacOutputEncoder::FlacOutputEncoder() = default;

FlacOutputEncoder::~FlacOutputEncoder() {
#ifndef NO_XIPH_LIBS
  if (m_encoder != nullptr) {
    FLAC__stream_encoder_finish(static_cast<FLAC__StreamEncoder *>(m_encoder));
    FLAC__stream_encoder_delete(static_cast<FLAC__StreamEncoder *>(m_encoder));
    m_encoder = nullptr;
  }
#endif
}

bool FlacOutputEncoder::initialize(int sampleRate, int channels) {
#ifndef NO_XIPH_LIBS
  if (m_initialized) {
    return true;
  }

  m_sampleRate = sampleRate;
  m_channels = channels;

  FLAC__StreamEncoder *encoder = FLAC__stream_encoder_new();
  if (encoder == nullptr) {
    return false;
  }

  FLAC__bool ok = FLAC__stream_encoder_set_channels(encoder, channels);
  ok = ok && FLAC__stream_encoder_set_bits_per_sample(encoder, 16);
  ok = ok && FLAC__stream_encoder_set_sample_rate(encoder, sampleRate);

  if (!ok) {
    FLAC__stream_encoder_delete(encoder);
    return false;
  }

  FLAC__StreamEncoderInitStatus initStatus =
      FLAC__stream_encoder_init_stream(
          encoder, &FlacOutputEncoder::writeCallback, nullptr, nullptr, nullptr,
          this);

  if (initStatus != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
    FLAC__stream_encoder_delete(encoder);
    return false;
  }

  m_encoder = encoder;
  m_initialized = true;
  return true;
#else
  return false;
#endif
}

bool FlacOutputEncoder::encode(const float *input, size_t samples,
                               std::vector<uint8_t> &output) {
#ifndef NO_XIPH_LIBS
  flacLog("encode() called");
  if (!m_initialized || m_encoder == nullptr || input == nullptr ||
      samples == 0) {
    char buf[256];
    snprintf(buf, sizeof(buf), "encode() early return init=%d enc=%p samples=%zu",
             m_initialized, m_encoder, samples);
    flacLog(buf);
    return false;
  }

  // Convert float [-1, 1] to 16-bit PCM in int32 containers.
  m_inputBuffer.resize(samples);
  for (size_t i = 0; i < samples; ++i) {
    const float clamped = std::max(-1.0f, std::min(1.0f, input[i]));
    m_inputBuffer[i] = static_cast<FLAC__int32>(clamped * 32767.0f);
  }

  const size_t frames = samples / m_channels;
  if (frames == 0) {
    return true;
  }

  FLAC__bool ok = FLAC__stream_encoder_process_interleaved(
      static_cast<FLAC__StreamEncoder *>(m_encoder), m_inputBuffer.data(),
      frames);

  char buf[256];
  snprintf(buf, sizeof(buf), "encode() frames=%zu ok=%d output=%zu",
           frames, ok, m_outputAccumulator.size());
  flacLog(buf);

  if (!ok) {
    flacLog("encode() process_interleaved failed");
    return false;
  }

  output.swap(m_outputAccumulator);
  m_outputAccumulator.clear();
  return true;
#else
  return false;
#endif
}

bool FlacOutputEncoder::finalize(std::vector<uint8_t> &output) {
#ifndef NO_XIPH_LIBS
  flacLog("finalize() called");
  if (!m_initialized || m_encoder == nullptr) {
    flacLog("finalize() early return");
    return false;
  }

  FLAC__bool ok = FLAC__stream_encoder_finish(
      static_cast<FLAC__StreamEncoder *>(m_encoder));

  char buf[256];
  snprintf(buf, sizeof(buf), "finalize() ok=%d output=%zu", ok, m_outputAccumulator.size());
  flacLog(buf);

  output.swap(m_outputAccumulator);
  m_outputAccumulator.clear();

  FLAC__stream_encoder_delete(static_cast<FLAC__StreamEncoder *>(m_encoder));
  m_encoder = nullptr;
  m_initialized = false;

  return ok != 0;
#else
  return false;
#endif
}

#ifndef NO_XIPH_LIBS
FLAC__StreamEncoderWriteStatus FlacOutputEncoder::writeCallback(
    const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[],
    size_t bytes, unsigned int samples, unsigned int currentFrame,
    void *clientData) {
  auto *self = static_cast<FlacOutputEncoder *>(clientData);
  if (self == nullptr || buffer == nullptr || bytes == 0) {
    return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
  }

  self->m_outputAccumulator.insert(self->m_outputAccumulator.end(), buffer,
                                   buffer + bytes);
  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}
#endif
