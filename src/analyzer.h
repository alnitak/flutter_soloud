#ifndef ANALYZER_H
#define ANALYZER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "enums.h"
#include "pffft/pffft.h"
#include "soloud/src/backend/miniaudio/miniaudio.h"

/// Audio visualizer and frequency analyzer backed by PFFFT and miniaudio.
///
/// Captures mixed audio from the audio thread into a lock-free SPSC ring buffer,
/// applies windowing (Blackman), runs real FFT transforms via PFFFT on a dedicated
/// worker thread, applies frequency smoothing, and delivers zero-copy pointers
/// to Dart.
class Analyzer {
 public:
  static constexpr int kMaxChannels = 8;
  static constexpr size_t kRingBufferSize = 65536;

  static Analyzer &instance();

  Analyzer();
  ~Analyzer();

  Analyzer(const Analyzer &) = delete;
  Analyzer &operator=(const Analyzer &) = delete;

  /// Start or stop visualization.
  ///
  /// [enabled] whether visualization is enabled.
  /// [windowSize] power of two from 128 to 8192 (default 256).
  /// [kind] wave only, FFT only, or wave and FFT.
  /// [channel] VISUALIZATION_CHANNEL_MERGED (-1), VISUALIZATION_CHANNEL_ALL (-2), or specific channel index.
  /// [engineChannels] active playback channel count of the engine.
  PlayerErrors setVisualizationEnabled(
      bool enabled,
      int windowSize = 256,
      VisualizationKind kind = VISUALIZATION_WAVE_AND_FFT,
      int channel = VISUALIZATION_CHANNEL_MERGED,
      int engineChannels = 2);

  /// True if visualization is currently active.
  bool isVisualizationEnabled() const;

  /// Set FFT smoothing factor [0.0, 1.0].
  void setSmoothing(float smooth);

  /// Set the Dart notification callback.
  void setDataCallback(dartVisualizationCallback_t callback);

  /// Called from the audio thread for each rendered mixed block.
  void onAudioData(const float *data, unsigned int frames, int channels);

  int windowSize() const { return m_windowSize; }
  VisualizationKind kind() const { return m_kind; }
  int channelSelection() const { return m_channelSelection; }

 private:
  void workerThreadFunc();
  void processWindow(size_t readIdx, int nextPingPong);
  void computeFftMagnitudes(int channelIdx, int pingPong);
  void dispatchToDart(int pingPong);
  void cleanupBuffers();

  std::atomic<bool> m_running{false};
  std::atomic<bool> m_shouldStop{false};
  std::atomic<int> m_inFlightAudioCallbacks{0};

  int m_windowSize{256};
  VisualizationKind m_kind{VISUALIZATION_WAVE_AND_FFT};
  int m_channelSelection{VISUALIZATION_CHANNEL_MERGED};
  int m_engineChannels{2};
  int m_activeChannels{1};
  std::atomic<float> m_fftSmoothing{0.8f};

  // PFFFT setup and precomputed window table
  PFFFT_Setup *m_pffftSetup{nullptr};
  float *m_windowTable{nullptr};

  // Miniaudio channel converter for downmixing multi-channel to mono
  bool m_hasChannelConverter{false};
  ma_channel_converter m_channelConverter;
  std::vector<float> m_scratchConverted;
  std::vector<float> m_scratchExtract;

  // SPSC Lock-free Ring Buffer per channel
  std::vector<float> m_ringBuffer[kMaxChannels];
  std::atomic<size_t> m_writeIndex{0};
  std::atomic<size_t> m_readIndex{0};

  // SIMD Aligned processing buffers (per active channel)
  float *m_windowedInput[kMaxChannels]{nullptr};
  float *m_fftOutput[kMaxChannels]{nullptr};
  float *m_fftWork[kMaxChannels]{nullptr};

  // Double-buffered export buffers to prevent tearing during asynchronous Dart delivery
  float *m_waveBuffers[kMaxChannels][2]{{nullptr, nullptr}};
  float *m_fftBuffers[kMaxChannels][2]{{nullptr, nullptr}};
  float *m_fftSmoothed[kMaxChannels]{nullptr};
  std::atomic<int> m_pingPongIndex{0};

  // Pointers arrays passed to Dart callback
  const float *m_wavePtrsExport[kMaxChannels]{nullptr};
  const float *m_fftPtrsExport[kMaxChannels]{nullptr};

  std::atomic<dartVisualizationCallback_t> m_callback{nullptr};
  std::thread m_workerThread;
};

#endif // ANALYZER_H
