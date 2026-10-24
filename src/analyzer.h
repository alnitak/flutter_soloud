#ifndef ANALYZER_H
#define ANALYZER_H

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "enums.h"
#include "pffft/pffft.h"
#include "soloud/include/soloud_bus.h"

class Analyzer;
class AnalyzerWorker;

/// Hands one analyzed window of [source] to Dart.
///
/// [data] holds [channelCount] wave windows of [waveSamples] floats, followed
/// by [channelCount] FFT windows of [fftSamples] floats. A count is 0 when its
/// kind is not computed. On native [data] is only valid during the call. On
/// the web it stays valid until the main thread has delivered it, see
/// [Analyzer::tryBeginDispatch].
typedef void (*AnalyzerDataCallback)(Analyzer &source,
                                     const float *data,
                                     int32_t channelCount,
                                     int32_t waveSamples,
                                     int32_t fftSamples);

/// Audio visualizer and frequency analyzer backed by PFFFT.
///
/// One instance analyzes the engine's mixed output ([instance]) and each mixing
/// bus with visualization enabled has its own. Captured audio goes into a
/// lock-free SPSC ring with one lane per channel. On native a single worker
/// thread shared by every instance takes the latest window of each one about
/// every 12 ms, downmixes or picks the selected channel, applies a Blackman
/// window, runs a real FFT, smooths it over time and hands the result to Dart.
/// On the web there are no threads, so the window is analyzed on the audio
/// thread right after it is captured.
///
/// Packets are only produced from new audio: when nothing is captured (a
/// paused or stopped bus, or a stopped device) nothing is sent, rather than
/// the last window again.
class Analyzer : public SoLoud::BusAudioTap {
 public:
  static constexpr int kMaxChannels = 8;

  /// The source id of the engine output. A bus uses its bus id, never 0.
  static constexpr int32_t kEngineSourceId = 0;

  /// The analyzer of the engine's mixed output.
  static Analyzer &instance();

  explicit Analyzer(int32_t sourceId);
  ~Analyzer() override;

  Analyzer(const Analyzer &) = delete;
  Analyzer &operator=(const Analyzer &) = delete;

  /// Start or stop visualization. Starting while running restarts it with
  /// the new settings.
  ///
  /// [enabled] whether visualization is enabled.
  /// [windowSize] power of two from 128 to 8192 (default 256).
  /// [kind] wave only, FFT only, or wave and FFT.
  /// [channel] VISUALIZATION_CHANNEL_MERGED (-1), VISUALIZATION_CHANNEL_ALL
  /// (-2), or a specific channel index below [channels].
  /// [channels] channel count of the analyzed audio.
  ///
  /// Invalid parameters are reported before anything changes.
  PlayerErrors setVisualizationEnabled(
      bool enabled,
      int windowSize = 256,
      VisualizationKind kind = VISUALIZATION_WAVE_AND_FFT,
      int channel = VISUALIZATION_CHANNEL_MERGED,
      int channels = 2);

  /// True if visualization is currently active.
  bool isVisualizationEnabled() const;

  /// The peak (largest absolute sample) of [channel] over the latest analyzed
  /// window, whatever the channel selection. 0 for an invalid channel or
  /// while disabled. Holds its value while nothing is captured.
  float getChannelPeak(int channel) const;

  int32_t sourceId() const { return m_sourceId; }
  int windowSize() const { return m_windowSize; }
  VisualizationKind kind() const { return m_kind; }
  int channelSelection() const { return m_channelSelection; }

  /// Interleaved frames from the audio thread: the engine output, from the
  /// device callback.
  void onAudioData(const float *data, unsigned int frames, int channels);

  /// SoLoud::BusAudioTap: planar frames a bus has just mixed, on the audio
  /// thread.
  void onBusAudio(const float *aBuffer,
                  unsigned int aSamples,
                  unsigned int aStride,
                  unsigned int aChannels) override;

  /// Set the FFT smoothing factor [0.0, 1.0] of every analyzer.
  static void setSmoothing(float smooth);

  /// Set the FFT decibel range [minDecibels, maxDecibels] for magnitude
  /// normalization, for every analyzer. Conforms to W3C Web Audio API
  /// (default: minDecibels = -100.0f, maxDecibels = -30.0f).
  static void setMinMaxDecibels(float minDecibels, float maxDecibels);

  /// Set the callback every analyzer hands its windows to.
  static void setDataCallback(AnalyzerDataCallback callback);

  /// Disable every analyzer, the engine output's and the buses', and stop the
  /// worker thread. The bus analyzers stay attached to their buses, disabled,
  /// until the buses release them.
  static void disableAll();

  /// Web (AudioWorklet) only: identifies this run of the analyzer across the
  /// hop to the main thread. Changes every time visualization is enabled.
  uint32_t serial() const { return m_serial; }

  /// Web (AudioWorklet) only: claim the in-flight dispatch before posting a
  /// window to the main thread. While claimed nothing new is analyzed, so the
  /// posted window stays untouched.
  bool tryBeginDispatch() {
    bool expected = false;
    return m_dispatchInFlight.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel);
  }

  /// Web (AudioWorklet) only, on the main thread: release the dispatch claimed
  /// by the analyzer [sourceId] at [serial]. Returns false when that analyzer
  /// has since been disabled, destroyed or restarted: the posted window may
  /// then be gone and must not be read.
  static bool finishWebDispatch(int32_t sourceId, uint32_t serial);

 private:
  friend class AnalyzerWorker;

  using Clock = std::chrono::steady_clock;

  PlayerErrors enableLocked(int windowSize,
                            VisualizationKind kind,
                            int channel,
                            int channels);
  void disableLocked(bool stopIdleWorker);
  void cleanupBuffers();

  void capture(const float *data,
               unsigned int frames,
               size_t frameStep,
               size_t channelStep,
               int channels);
  bool analyzeLatestWindow(float *packet);
  void computeFftMagnitudes(int channelIdx, float *magnitudes);
  void dispatch(const float *packet);
#ifdef __EMSCRIPTEN__
  void analyzeOnAudioThread();
#endif

  const int32_t m_sourceId;

  std::atomic<bool> m_running{false};
  std::atomic<int> m_inFlightAudioCallbacks{0};
  std::atomic<bool> m_dispatchInFlight{false};
  uint32_t m_serial{0};

  int m_windowSize{256};
  VisualizationKind m_kind{VISUALIZATION_WAVE_AND_FFT};
  int m_channelSelection{VISUALIZATION_CHANNEL_MERGED};
  // Channels stored in the ring: every channel of the analyzed audio.
  int m_lanes{0};
  // Channels in a packet: one, or every lane with VISUALIZATION_CHANNEL_ALL.
  int m_activeChannels{0};
  int m_waveSamples{0};
  int m_fftSamples{0};

  // Ring buffer, m_lanes lanes of m_ringFrames frames each, written by the
  // audio thread only. The analyzing thread reads frames the audio thread may
  // be overwriting (and then drops the torn copy, see m_writeReserve), so the
  // samples are relaxed atomics: plain loads and stores on every target.
  std::atomic<float> *m_ring{nullptr};
  size_t m_ringFrames{0};
  std::atomic<size_t> m_writeIndex{0};
  // Where the capture in progress will have written up to, announced before
  // it writes, so a torn copy of the window can be detected.
  std::atomic<size_t> m_writeReserve{0};
  // Channel count of the latest capture, for downmixing.
  std::atomic<int> m_capturedChannels{0};
  // End of the latest analyzed window. Only touched by the thread analyzing.
  size_t m_readIndex{0};

  // PFFFT setup, precomputed window table and SIMD aligned work buffers.
  PFFFT_Setup *m_pffftSetup{nullptr};
  float *m_windowTable{nullptr};
  float *m_windowedInput{nullptr};
  float *m_fftOutput{nullptr};
  float *m_fftWork{nullptr};
  // Time-domain window of each packet channel.
  float *m_window[kMaxChannels]{nullptr};
  // Smoothed linear FFT magnitudes of each packet channel.
  float *m_fftSmoothed[kMaxChannels]{nullptr};

  // Packets handed to the callback. The web alternates two of them so the
  // one in flight to the main thread is never rewritten.
#ifdef __EMSCRIPTEN__
  static constexpr int kPacketBuffers = 2;
#else
  static constexpr int kPacketBuffers = 1;
#endif
  float *m_packet[kPacketBuffers]{nullptr};
  int m_packetIndex{0};

  std::atomic<float> m_channelPeak[kMaxChannels];

  // When the worker may next send a packet. Only touched under its lock.
  Clock::time_point m_nextDispatch{};
};

#endif // ANALYZER_H
