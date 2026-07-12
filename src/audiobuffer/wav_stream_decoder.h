#ifndef WAV_STREAM_DECODER_H
#define WAV_STREAM_DECODER_H

#include "stream_decoder.h"
#include <vector>

#include "../soloud/src/audiosource/wav/dr_wav.h"

/// Wrapper class for WAV stream decoder using dr_wav
class WavDecoderWrapper : public IDecoderWrapper {
public:
  WavDecoderWrapper();

  ~WavDecoderWrapper() override;

  bool initializeDecoder(int engineSamplerate, int engineChannels) override;

  // Call this when no more data will be added to signal end-of-stream
  void setDataEnded() override;

  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &buffer, int *sampleRate,
         int *channels, size_t maxOutputSamples = 0) override;

  bool canSeekToTime(double seconds) const override;
  uint64_t timeToByteOffset(double seconds) override;
  double getDuration() const override;

  static bool checkForValidWavHeader(const std::vector<unsigned char> &buffer);

private:
  void cleanup();

  static size_t on_read(void *pUserData, void *pBufferOut, size_t bytesToRead);
  static drwav_bool32 on_seek(void *pUserData, int offset,
                              drwav_seek_origin origin);

  drwav decoder;
  bool isInitialized;
  std::vector<unsigned char> audioData;
  size_t m_read_pos;
  bool mDataEnded; // Signals that no more data will be added
};

#endif // WAV_STREAM_DECODER_H
