#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "../enums.h"
#include "mixer_output_encoder.h"
#include "pcm_queue.h"

/// Callback used to notify Dart that new mixer output data is available.
/// [data] points to the start of the contiguous unread region in the
/// circular buffer. [length] is the number of valid bytes, which may be
/// smaller than the notification threshold when the buffer wraps.
using MixerOutputDataCallback = std::function<void(uint8_t *data, size_t length)>;

/// Singleton that captures the SoLoud master mix output into a circular
/// buffer and notifies Dart when enough data is available.
class MixerOutput {
 public:
  static MixerOutput &instance();

  /// True after [start] has been called until [stop] is called.
  bool isRunning() const;

  /// Start capturing mixer output.
  ///
  /// [format] the desired output format.
  /// [sampleRate] the sample rate. Use -1 to follow the engine sample rate.
  /// [channels] the channel count. Use -1 to follow the engine channels.
  /// [bufferSizeBytes] the total size of the circular buffer.
  /// [notificationThresholdBytes] how many bytes must be available before
  /// the Dart callback is triggered. Used for compressed formats.
  /// [chunkPCMFrames] fixed number of PCM frames per emitted chunk. Used for
  /// PCM formats; use -1 to disable and fall back to the threshold behavior.
  PlayerErrors start(MixerOutputFormat format, int sampleRate, int channels,
                     size_t bufferSizeBytes,
                     size_t notificationThresholdBytes,
                     int chunkPCMFrames = -1);

  /// Stop capturing and release resources.
  void stop();

  /// Called from the audio thread for every mixed block.
  void onAudioData(const float *data, unsigned int frames);

  /// Direct pointer to the circular buffer storage.
  uint8_t *getBufferPointer();

  /// Total size of the circular buffer in bytes.
  size_t getBufferSize() const;

  /// Number of unread bytes currently in the buffer.
  size_t getAvailableBytes() const;

  /// Current read offset in the circular buffer.
  size_t getReadOffset() const;

  /// Advance the read position by [bytes]. Must not exceed available bytes.
  void advanceReadPosition(size_t bytes);

  /// Set the Dart notification callback.
  void setDataCallback(MixerOutputDataCallback callback);

  /// Monotonic identifier for the current capture session. Changes every
  /// time [start] is called, so callers can ignore stale messages from a
  /// previous capture (especially important on the web where callbacks are
  /// proxied asynchronously to the main thread).
  uint32_t captureId() const { return m_captureId.load(); }

  /// Encoder currently in use, or nullptr for PCM formats.
  MixerOutputEncoder *encoder() const { return m_encoder.get(); }

  /// Returns the cached WAV header, or an empty vector if no WAV capture has
  /// been started or if the encoder did not produce a header.
  const std::vector<uint8_t> &getWavHeader() const { return m_wavHeader; }

 private:
  MixerOutput() = default;
  ~MixerOutput();

  MixerOutput(const MixerOutput &) = delete;
  MixerOutput &operator=(const MixerOutput &) = delete;

  void notificationThreadFunc();
  void encoderThreadFunc();
  size_t contiguousAvailable() const;
  bool isCompressedFormat() const;
  void writeToBuffer(const uint8_t *data, size_t bytes);
#ifdef __EMSCRIPTEN__
  /// Single notification pass used on web, where there are no worker
  /// threads: called from [onAudioData] (the main/browser thread, which is
  /// also the audio thread there) after new data has been written.
  void processNotifications();
#endif

  std::atomic<bool> m_running{false};
  std::atomic<bool> m_shouldStop{false};
  std::atomic<uint32_t> m_captureId{0};

  MixerOutputFormat m_format = MIXER_OUTPUT_PCM_F32LE;
  int m_sampleRate = 44100;
  int m_channels = 2;
  size_t m_bytesPerSample = 4;
  size_t m_bytesPerFrame = 8;

  size_t m_chunkPCMFrames = 0;
  size_t m_chunkBytes = 0;
  std::vector<uint8_t> m_chunkBuffer;

  std::vector<uint8_t> m_buffer;
  size_t m_bufferSize = 0;
  std::atomic<size_t> m_writeOffset{0};
  std::atomic<size_t> m_readOffset{0};

  size_t m_notificationThreshold = 4096;
  std::atomic<bool> m_notified{false};

  MixerOutputDataCallback m_callback;

  std::thread m_notificationThread;

  // Compressed format state.
  std::unique_ptr<PcmChunkQueue> m_pcmQueue;
  std::unique_ptr<MixerOutputEncoder> m_encoder;
  std::thread m_encoderThread;

#ifdef __EMSCRIPTEN__
  // Scratch buffer for inline (thread-free) encoding on web. Reused across
  // audio callbacks to avoid allocating on the audio thread.
  std::vector<uint8_t> m_encodeScratch;
#endif

  // WAV header cached on stop so callers can retrieve it after the encoder
  // has been released.
  std::vector<uint8_t> m_wavHeader;
};
