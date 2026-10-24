#include "analyzer.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

// Shared by every analyzer.
std::atomic<AnalyzerDataCallback> gDataCallback{nullptr};
std::atomic<float> gFftSmoothing{0.8f};
std::atomic<float> gMinDecibels{-100.0f};
std::atomic<float> gMaxDecibels{-30.0f};
std::atomic<uint32_t> gNextSerial{0};
// Advanced by Analyzer::abandonDartDispatches().
std::atomic<uint32_t> gDartDispatchEpoch{0};

// The ring keeps four windows, and at least this many frames: a device
// callback can deliver thousands of frames at once.
constexpr size_t kMinRingFrames = 2048;

size_t nextPowerOfTwo(size_t value) {
  size_t power = 1;
  while (power < value) {
    power <<= 1;
  }
  return power;
}

float *allocFloats(size_t count) {
  auto *buffer =
      static_cast<float *>(pffft_aligned_malloc(count * sizeof(float)));
  if (buffer != nullptr) {
    std::memset(buffer, 0, count * sizeof(float));
  }
  return buffer;
}

void freeFloats(float *&buffer) {
  if (buffer != nullptr) {
    pffft_aligned_free(buffer);
    buffer = nullptr;
  }
}

static_assert(std::atomic<float>::is_always_lock_free,
              "the audio thread writes the ring, so it must not lock");

}  // namespace

/// The enabled analyzers and, on native, the one thread that analyzes and
/// dispatches all of them. A thread per analyzer would mean one thread per
/// visualized bus waking every few milliseconds.
///
/// Lock order: lifecycleMutex -> mutex -> the Dart callback gate, which the
/// worker takes inside the data callback. Neither lock is ever taken on the
/// audio thread, and nobody may wait for lifecycleMutex while holding the gate
/// exclusively, or a dispatch in progress could never finish.
class AnalyzerWorker {
 public:
  static AnalyzerWorker &get() {
    // Deliberately never destroyed, like the engine output's analyzer: the
    // audio thread and the worker can outlive static destruction.
    static AnalyzerWorker *worker = new AnalyzerWorker();
    return *worker;
  }

  /// Serializes enabling, disabling and destroying analyzers, and starting
  /// and stopping the thread. Every analyzer in [analyzers] stays alive while
  /// it is held: an analyzer removes itself under it before it is freed.
  std::mutex lifecycleMutex;

  /// Guards [analyzers]. The worker holds it while it analyzes them, so once
  /// remove() returns the worker no longer touches the analyzer removed.
  /// add() and remove() hold lifecycleMutex too, so either lock is enough to
  /// read the list.
  std::mutex mutex;
  std::vector<Analyzer *> analyzers;

  /// The caller holds lifecycleMutex.
  void add(Analyzer *analyzer) {
    std::lock_guard<std::mutex> lock(mutex);
    analyzer->m_nextDispatch = Analyzer::Clock::time_point{};
    analyzers.push_back(analyzer);
#ifndef __EMSCRIPTEN__
    if (!m_thread.joinable()) {
      m_stop = false;
      m_thread = std::thread(&AnalyzerWorker::run, this);
    }
#endif
  }

  /// The caller holds lifecycleMutex.
  void remove(Analyzer *analyzer) {
    std::lock_guard<std::mutex> lock(mutex);
    analyzers.erase(std::remove(analyzers.begin(), analyzers.end(), analyzer),
                    analyzers.end());
  }

  /// Join the thread if no analyzer is left. The caller holds lifecycleMutex.
  void stopIfIdle() {
#ifndef __EMSCRIPTEN__
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!analyzers.empty() || !m_thread.joinable()) {
        return;
      }
      m_stop = true;
    }
    m_wake.notify_all();
    m_thread.join();
#endif
  }

 private:
  AnalyzerWorker() = default;

#ifndef __EMSCRIPTEN__
  void run() {
    // Each analyzer sends at most one packet every kDispatchInterval (about
    // 80 per second, to match display refresh without flooding the Dart event
    // queue), and is checked for a new window every kPollInterval.
    constexpr auto kPollInterval = std::chrono::milliseconds(3);
    constexpr auto kDispatchInterval = std::chrono::milliseconds(12);

    std::unique_lock<std::mutex> lock(mutex);
    while (!m_stop) {
      const auto now = Analyzer::Clock::now();
      for (Analyzer *analyzer : analyzers) {
        if (now < analyzer->m_nextDispatch ||
            analyzer->isDartDispatchInFlight()) {
          continue;
        }
        float *packet = analyzer->m_packet[0];
        if (analyzer->analyzeLatestWindow(packet)) {
          analyzer->dispatch(packet);
          analyzer->m_nextDispatch = now + kDispatchInterval;
        }
      }
      m_wake.wait_for(lock, kPollInterval, [this] { return m_stop; });
    }
  }

  std::condition_variable m_wake;
  // Guarded by mutex.
  bool m_stop = false;
  // Guarded by lifecycleMutex.
  std::thread m_thread;
#endif
};

Analyzer &Analyzer::instance() {
  // Deliberately never destroyed: the audio thread can outlive static
  // destruction.
  static Analyzer *engineOutput = new Analyzer(kEngineSourceId);
  return *engineOutput;
}

Analyzer::Analyzer(int32_t sourceId) : m_sourceId(sourceId) {
  for (auto &peak : m_channelPeak) {
    peak.store(0.0f, std::memory_order_relaxed);
  }
}

Analyzer::~Analyzer() {
  setVisualizationEnabled(false);
}

bool Analyzer::isVisualizationEnabled() const {
  return m_running.load(std::memory_order_acquire);
}

float Analyzer::getChannelPeak(int channel) const {
  if (channel < 0 || channel >= kMaxChannels) {
    return 0.0f;
  }
  return m_channelPeak[channel].load(std::memory_order_relaxed);
}

void Analyzer::setSmoothing(float smooth) {
  if (smooth < 0.0f) smooth = 0.0f;
  if (smooth > 1.0f) smooth = 1.0f;
  gFftSmoothing.store(smooth, std::memory_order_relaxed);
}

void Analyzer::setMinMaxDecibels(float minDecibels, float maxDecibels) {
  if (minDecibels < maxDecibels) {
    gMinDecibels.store(minDecibels, std::memory_order_relaxed);
    gMaxDecibels.store(maxDecibels, std::memory_order_relaxed);
  }
}

void Analyzer::setDataCallback(AnalyzerDataCallback callback) {
  gDataCallback.store(callback, std::memory_order_release);
}

void Analyzer::disableAll(const std::function<bool()> &shouldDisable) {
  AnalyzerWorker &worker = AnalyzerWorker::get();
  std::lock_guard<std::mutex> lifecycle(worker.lifecycleMutex);
  if (shouldDisable && !shouldDisable()) {
    return;
  }
  std::vector<Analyzer *> enabled;
  {
    std::lock_guard<std::mutex> lock(worker.mutex);
    enabled = worker.analyzers;
  }
  for (Analyzer *analyzer : enabled) {
    analyzer->disableLocked(/*stopIdleWorker=*/false);
  }
  worker.stopIfIdle();
}

void Analyzer::beginDartDispatch(const float *copy) {
  // The epoch first: until the packet is published the analyzer is not in
  // flight, whatever the epoch says.
  m_dartDispatchEpoch.store(gDartDispatchEpoch.load(std::memory_order_acquire),
                            std::memory_order_relaxed);
  m_dartPacket.store(copy, std::memory_order_release);
}

bool Analyzer::isDartDispatchInFlight() const {
  return m_dartPacket.load(std::memory_order_acquire) != nullptr &&
         m_dartDispatchEpoch.load(std::memory_order_relaxed) ==
             gDartDispatchEpoch.load(std::memory_order_acquire);
}

void Analyzer::finishDartDispatch(const float *copy) {
  if (copy == nullptr) {
    return;
  }
  // lifecycleMutex rather than mutex, which the worker holds across a whole
  // pass: Dart reports from its event loop and should not wait for one. It
  // keeps every listed analyzer alive and the list unchanged.
  AnalyzerWorker &worker = AnalyzerWorker::get();
  std::lock_guard<std::mutex> lifecycle(worker.lifecycleMutex);
  for (Analyzer *analyzer : worker.analyzers) {
    const float *expected = copy;
    if (analyzer->m_dartPacket.compare_exchange_strong(
            expected, nullptr, std::memory_order_acq_rel)) {
      return;
    }
  }
}

void Analyzer::abandonDartDispatches() {
  gDartDispatchEpoch.fetch_add(1, std::memory_order_acq_rel);
}

bool Analyzer::finishWebDispatch(int32_t sourceId, uint32_t serial) {
  AnalyzerWorker &worker = AnalyzerWorker::get();
  std::lock_guard<std::mutex> lock(worker.mutex);
  for (Analyzer *analyzer : worker.analyzers) {
    if (analyzer->m_sourceId == sourceId && analyzer->m_serial == serial) {
      analyzer->m_dispatchInFlight.store(false, std::memory_order_release);
      return true;
    }
  }
  return false;
}

PlayerErrors Analyzer::setVisualizationEnabled(
    bool enabled,
    int windowSize,
    VisualizationKind kind,
    int channel,
    int channels) {
  if (enabled) {
    // Validate before touching what is running.
    if (windowSize < 128 || windowSize > 8192 ||
        (windowSize & (windowSize - 1)) != 0) {
      return invalidParameter;
    }
    if (kind < VISUALIZATION_WAVE || kind > VISUALIZATION_WAVE_AND_FFT) {
      return invalidParameter;
    }
    if (channels <= 0 || channels > kMaxChannels) {
      return invalidParameter;
    }
    // Merged, all, or one of the analyzed audio's channels.
    if (channel < VISUALIZATION_CHANNEL_ALL || channel >= channels) {
      return invalidParameter;
    }
  }

  AnalyzerWorker &worker = AnalyzerWorker::get();
  std::lock_guard<std::mutex> lifecycle(worker.lifecycleMutex);
  if (!enabled) {
    disableLocked(/*stopIdleWorker=*/true);
    return noError;
  }

  // Restart with the new settings, keeping the worker up meanwhile.
  disableLocked(/*stopIdleWorker=*/false);
  const PlayerErrors error = enableLocked(windowSize, kind, channel, channels);
  if (error != noError) {
    worker.stopIfIdle();
  }
  return error;
}

PlayerErrors Analyzer::enableLocked(int windowSize,
                                    VisualizationKind kind,
                                    int channel,
                                    int channels) {
  m_windowSize = windowSize;
  m_kind = kind;
  m_channelSelection = channel;
  m_lanes = channels;
  m_activeChannels = (channel == VISUALIZATION_CHANNEL_ALL) ? channels : 1;
  m_waveSamples = (kind != VISUALIZATION_FFT) ? windowSize : 0;
  m_fftSamples = (kind != VISUALIZATION_WAVE) ? windowSize / 2 : 0;
  m_ringFrames = nextPowerOfTwo(
      std::max(static_cast<size_t>(windowSize) * 4, kMinRingFrames));

  // Value-initialized, so the ring starts out silent.
  m_ring = new (std::nothrow)
      std::atomic<float>[static_cast<size_t>(m_lanes) * m_ringFrames]();
  if (m_ring == nullptr) {
    cleanupBuffers();
    return outOfMemory;
  }

  for (int c = 0; c < m_activeChannels; c++) {
    m_window[c] = allocFloats(m_windowSize);
    if (m_window[c] == nullptr) {
      cleanupBuffers();
      return outOfMemory;
    }
  }

  if (m_fftSamples > 0) {
    m_pffftSetup = pffft_new_setup(m_windowSize, PFFFT_REAL);
    m_windowTable = allocFloats(m_windowSize);
    m_windowedInput = allocFloats(m_windowSize);
    m_fftOutput = allocFloats(m_windowSize);
    m_fftWork = allocFloats(m_windowSize);
    if (m_pffftSetup == nullptr || m_windowTable == nullptr ||
        m_windowedInput == nullptr || m_fftOutput == nullptr ||
        m_fftWork == nullptr) {
      cleanupBuffers();
      return outOfMemory;
    }
    for (int c = 0; c < m_activeChannels; c++) {
      m_fftSmoothed[c] = allocFloats(m_fftSamples);
      if (m_fftSmoothed[c] == nullptr) {
        cleanupBuffers();
        return outOfMemory;
      }
    }

    // Precompute the Blackman window.
    const float alpha = 0.16f;
    const float a0 = 0.5f * (1.0f - alpha);
    const float a1 = 0.5f;
    const float a2 = 0.5f * alpha;
    const float denom = static_cast<float>(m_windowSize - 1);
    for (int i = 0; i < m_windowSize; i++) {
      m_windowTable[i] = a0 -
                         a1 * cosf(2.0f * static_cast<float>(M_PI) * static_cast<float>(i) / denom) +
                         a2 * cosf(4.0f * static_cast<float>(M_PI) * static_cast<float>(i) / denom);
    }
  }

  const size_t packetFloats = static_cast<size_t>(m_activeChannels) *
                              (m_waveSamples + m_fftSamples);
  for (auto &packet : m_packet) {
    packet = allocFloats(packetFloats);
    if (packet == nullptr) {
      cleanupBuffers();
      return outOfMemory;
    }
  }

  m_writeIndex.store(0, std::memory_order_relaxed);
  m_writeReserve.store(0, std::memory_order_relaxed);
  m_capturedChannels.store(channels, std::memory_order_relaxed);
  m_readIndex = 0;
  m_packetIndex = 0;
  m_dispatchInFlight.store(false, std::memory_order_relaxed);
  m_dartPacket.store(nullptr, std::memory_order_relaxed);
  m_serial = gNextSerial.fetch_add(1, std::memory_order_relaxed) + 1;
  m_running.store(true, std::memory_order_release);
  AnalyzerWorker::get().add(this);
  return noError;
}

void Analyzer::disableLocked(bool stopIdleWorker) {
  AnalyzerWorker &worker = AnalyzerWorker::get();
  if (m_running.load(std::memory_order_acquire)) {
    // seq_cst pairs with capture(): either a capture sees m_running false, or
    // the loop below sees it in flight. With weaker orderings both could miss
    // the other, and the buffers would be freed under the audio thread.
    m_running.store(false, std::memory_order_seq_cst);
    // Waits out the worker's pass over this analyzer.
    worker.remove(this);
    while (m_inFlightAudioCallbacks.load(std::memory_order_seq_cst) > 0) {
      std::this_thread::yield();
    }
    cleanupBuffers();
    for (auto &peak : m_channelPeak) {
      peak.store(0.0f, std::memory_order_relaxed);
    }
  }
  if (stopIdleWorker) {
    worker.stopIfIdle();
  }
}

void Analyzer::cleanupBuffers() {
  if (m_pffftSetup != nullptr) {
    pffft_destroy_setup(m_pffftSetup);
    m_pffftSetup = nullptr;
  }
  freeFloats(m_windowTable);
  freeFloats(m_windowedInput);
  freeFloats(m_fftOutput);
  freeFloats(m_fftWork);
  delete[] m_ring;
  m_ring = nullptr;
  for (int c = 0; c < kMaxChannels; c++) {
    freeFloats(m_window[c]);
    freeFloats(m_fftSmoothed[c]);
  }
  for (auto &packet : m_packet) {
    freeFloats(packet);
  }
}

void Analyzer::onAudioData(const float *data, unsigned int frames, int channels) {
  capture(data, frames, static_cast<size_t>(channels > 0 ? channels : 1), 1,
          channels);
}

void Analyzer::onBusAudio(const float *aBuffer,
                          unsigned int aSamples,
                          unsigned int aStride,
                          unsigned int aChannels) {
  capture(aBuffer, aSamples, 1, aStride, static_cast<int>(aChannels));
}

void Analyzer::capture(const float *data,
                       unsigned int frames,
                       size_t frameStep,
                       size_t channelStep,
                       int channels) {
  if (data == nullptr || frames == 0 || channels <= 0 ||
      !m_running.load(std::memory_order_acquire)) {
    return;
  }

  m_inFlightAudioCallbacks.fetch_add(1, std::memory_order_seq_cst);
  if (!m_running.load(std::memory_order_seq_cst)) {
    m_inFlightAudioCallbacks.fetch_sub(1, std::memory_order_release);
    return;
  }

  const size_t mask = m_ringFrames - 1;
  const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
  // Announce the frames about to be overwritten before writing them, so the
  // analyzing thread can tell when the window it copied was torn.
  m_writeReserve.store(writeIdx + frames, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);

  // Only the newest m_ringFrames frames can ever be analyzed.
  const unsigned int first =
      frames > m_ringFrames ? frames - static_cast<unsigned int>(m_ringFrames)
                            : 0;
  const int captured = std::min(channels, m_lanes);
  for (int c = 0; c < m_lanes; c++) {
    std::atomic<float> *lane = m_ring + static_cast<size_t>(c) * m_ringFrames;
    if (c < captured) {
      const float *src = data + static_cast<size_t>(c) * channelStep;
      for (unsigned int i = first; i < frames; i++) {
        lane[(writeIdx + i) & mask].store(src[i * frameStep],
                                          std::memory_order_relaxed);
      }
    } else {
      for (unsigned int i = first; i < frames; i++) {
        lane[(writeIdx + i) & mask].store(0.0f, std::memory_order_relaxed);
      }
    }
  }
  m_capturedChannels.store(captured, std::memory_order_relaxed);
  m_writeIndex.store(writeIdx + frames, std::memory_order_release);

#ifdef __EMSCRIPTEN__
  analyzeOnAudioThread();
#endif

  m_inFlightAudioCallbacks.fetch_sub(1, std::memory_order_release);
}

#ifdef __EMSCRIPTEN__
void Analyzer::analyzeOnAudioThread() {
  if (m_dispatchInFlight.load(std::memory_order_acquire)) {
    return;
  }
  const int next = (m_packetIndex + 1) % kPacketBuffers;
  float *packet = m_packet[next];
  if (!analyzeLatestWindow(packet)) {
    return;
  }
  m_packetIndex = next;
  dispatch(packet);
}
#endif

bool Analyzer::analyzeLatestWindow(float *packet) {
  const size_t windowSize = static_cast<size_t>(m_windowSize);
  const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
  // Only analyze a full window of audio captured since the previous one.
  if (writeIdx - m_readIndex < windowSize) {
    return false;
  }

  const size_t mask = m_ringFrames - 1;
  const size_t start = writeIdx - windowSize;
  auto ringLane = [this](int lane) -> const std::atomic<float> * {
    return m_ring + static_cast<size_t>(lane) * m_ringFrames;
  };
  auto sample = [mask, start](const std::atomic<float> *lane, size_t i) {
    return lane[(start + i) & mask].load(std::memory_order_relaxed);
  };

  // The latest window of each packet channel.
  if (m_channelSelection == VISUALIZATION_CHANNEL_MERGED) {
    // The average of the captured channels, which is how miniaudio downmixes
    // to mono.
    const int count = std::max(
        1, std::min(m_capturedChannels.load(std::memory_order_relaxed), m_lanes));
    float *out = m_window[0];
    const std::atomic<float> *first = ringLane(0);
    for (size_t i = 0; i < windowSize; i++) {
      out[i] = sample(first, i);
    }
    for (int c = 1; c < count; c++) {
      const std::atomic<float> *lane = ringLane(c);
      for (size_t i = 0; i < windowSize; i++) {
        out[i] += sample(lane, i);
      }
    }
    if (count > 1) {
      const float scale = 1.0f / static_cast<float>(count);
      for (size_t i = 0; i < windowSize; i++) {
        out[i] *= scale;
      }
    }
  } else {
    for (int c = 0; c < m_activeChannels; c++) {
      // The selected channel, which enabling checked is below m_lanes, or
      // each channel in turn.
      const int lane = m_channelSelection >= 0 ? m_channelSelection : c;
      const std::atomic<float> *src = ringLane(lane);
      float *out = m_window[c];
      for (size_t i = 0; i < windowSize; i++) {
        out[i] = sample(src, i);
      }
    }
  }

  float peaks[kMaxChannels] = {0};
  for (int c = 0; c < m_lanes; c++) {
    const std::atomic<float> *lane = ringLane(c);
    float peak = 0.0f;
    for (size_t i = 0; i < windowSize; i++) {
      const float magnitude = fabsf(sample(lane, i));
      if (magnitude > peak) peak = magnitude;
    }
    peaks[c] = peak;
  }

  // The audio thread kept writing during the copy. If it got to the frames
  // of this window, the copy may be torn: drop it, a newer one comes next.
  std::atomic_thread_fence(std::memory_order_acquire);
  if (m_writeReserve.load(std::memory_order_relaxed) - start > m_ringFrames) {
    return false;
  }

  m_readIndex = writeIdx;
  for (int c = 0; c < m_lanes; c++) {
    m_channelPeak[c].store(peaks[c], std::memory_order_relaxed);
  }

  if (m_waveSamples > 0) {
    for (int c = 0; c < m_activeChannels; c++) {
      std::memcpy(packet + static_cast<size_t>(c) * m_waveSamples, m_window[c],
                  windowSize * sizeof(float));
    }
  }

  if (m_fftSamples > 0) {
    float *fft = packet + static_cast<size_t>(m_activeChannels) * m_waveSamples;
    for (int c = 0; c < m_activeChannels; c++) {
      for (size_t i = 0; i < windowSize; i++) {
        m_windowedInput[i] = m_window[c][i] * m_windowTable[i];
      }
      pffft_transform_ordered(
          m_pffftSetup,
          m_windowedInput,
          m_fftOutput,
          m_fftWork,
          PFFFT_FORWARD);
      computeFftMagnitudes(c, fft + static_cast<size_t>(c) * m_fftSamples);
    }
  }
  return true;
}

/// Computes FFT magnitudes normalized to [0.0, 1.0] conforming to the
/// W3C Web Audio API specification for AnalyserNode:
/// - FFT Windowing and Smoothing over Time:
///   https://www.w3.org/TR/webaudio/#fft-windowing-and-smoothing-over-time
/// - AnalyserNode.getByteFrequencyData:
///   https://www.w3.org/TR/webaudio/#dom-analysernode-getbytefrequencydata
///
/// Algorithm:
/// 1. Normalized linear DFT magnitude: |X[k]| = sqrt(real^2 + imag^2) / N
///    (for DC bin 0: |X[0]| = |real[0]| / N).
/// 2. Temporal smoothing on linear magnitude:
///    X_smoothed[k] = tau * X_smoothed_prev[k] + (1 - tau) * |X[k]|
/// 3. Conversion to decibels:
///    dB[k] = 20 * log10(X_smoothed[k])
/// 4. Linear scaling from [minDecibels, maxDecibels] to [0.0, 1.0]:
///    val = (dB[k] - minDecibels) / (maxDecibels - minDecibels), clamped to [0.0, 1.0].
void Analyzer::computeFftMagnitudes(int channelIdx, float *magnitudes) {
  const int numBins = m_fftSamples;
  const float norm = 1.0f / static_cast<float>(m_windowSize);
  const float smooth = gFftSmoothing.load(std::memory_order_relaxed);
  const float minDb = gMinDecibels.load(std::memory_order_relaxed);
  const float maxDb = gMaxDecibels.load(std::memory_order_relaxed);
  const float range = maxDb - minDb;
  const float invRange = (range > 0.0f) ? (1.0f / range) : 1.0f;
  float *smoothed = m_fftSmoothed[channelIdx];

  // Magnitude threshold corresponding to minDecibels (e.g. 10^(-100/20) = 1e-5)
  // Any magnitude below or equal to minMag yields val = 0.0f.
  const float minMag = powf(10.0f, minDb / 20.0f);

  auto normalize = [&](float smoothedMag) {
    if (smoothedMag <= minMag) {
      return 0.0f;
    }
    const float db = 20.0f * log10f(smoothedMag);
    float val = (db - minDb) * invRange;
    if (val < 0.0f) val = 0.0f;
    else if (val > 1.0f) val = 1.0f;
    return val;
  };

  // DC component (bin 0)
  const float dcMag = fabsf(m_fftOutput[0]) * norm;
  smoothed[0] = smooth * smoothed[0] + (1.0f - smooth) * dcMag;
  magnitudes[0] = normalize(smoothed[0]);

  // Frequency bins 1 to numBins - 1
  for (int k = 1; k < numBins; k++) {
    const float real = m_fftOutput[2 * k];
    const float imag = m_fftOutput[2 * k + 1];
    const float mag = sqrtf(real * real + imag * imag) * norm;

    // Temporal smoothing on linear magnitude (per W3C spec):
    smoothed[k] = smooth * smoothed[k] + (1.0f - smooth) * mag;
    magnitudes[k] = normalize(smoothed[k]);
  }
}

void Analyzer::dispatch(const float *packet) {
  const auto callback = gDataCallback.load(std::memory_order_acquire);
  if (callback == nullptr) {
    return;
  }
  callback(*this, packet, m_activeChannels, m_waveSamples, m_fftSamples);
}
