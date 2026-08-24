#include "analyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Analyzer &Analyzer::instance() {
  static Analyzer instance;
  return instance;
}

Analyzer::Analyzer() {
  for (int c = 0; c < kMaxChannels; c++) {
    m_ringBuffer[c].assign(kRingBufferSize, 0.0f);
  }
}

Analyzer::~Analyzer() {
  setVisualizationEnabled(false);
}

bool Analyzer::isVisualizationEnabled() const {
  return m_running.load(std::memory_order_acquire);
}

void Analyzer::setSmoothing(float smooth) {
  if (smooth < 0.0f) smooth = 0.0f;
  if (smooth > 1.0f) smooth = 1.0f;
  m_fftSmoothing.store(smooth, std::memory_order_relaxed);
}

void Analyzer::setDataCallback(dartVisualizationCallback_t callback) {
  m_callback.store(callback, std::memory_order_release);
}

PlayerErrors Analyzer::setVisualizationEnabled(
    bool enabled,
    int windowSize,
    VisualizationKind kind,
    int channel,
    int engineChannels) {
  if (!enabled) {
    if (!m_running.load(std::memory_order_acquire)) {
      return noError;
    }
    m_running.store(false, std::memory_order_release);
    m_shouldStop.store(true, std::memory_order_release);
    m_dispatchInFlight.store(false, std::memory_order_release);

    if (m_workerThread.joinable()) {
      m_workerThread.join();
    }

    while (m_inFlightAudioCallbacks.load(std::memory_order_acquire) > 0) {
      std::this_thread::yield();
    }

    cleanupBuffers();
    return noError;
  }

  // Validate windowSize: must be power of two between 128 and 8192
  if (windowSize < 128 || windowSize > 8192 ||
      (windowSize & (windowSize - 1)) != 0) {
    return invalidParameter;
  }

  if (engineChannels <= 0 || engineChannels > kMaxChannels) {
    return invalidParameter;
  }

  // Stop previous instance if running
  if (m_running.load(std::memory_order_acquire)) {
    setVisualizationEnabled(false);
  }

  m_windowSize = windowSize;
  m_kind = kind;
  m_channelSelection = channel;
  m_engineChannels = engineChannels;

  // Determine active channels count
  if (m_channelSelection == VISUALIZATION_CHANNEL_ALL) {
    m_activeChannels = m_engineChannels;
  } else {
    m_activeChannels = 1;
  }

  // Initialize miniaudio channel converter if needed (for downmixing multi-channel to mono)
  if (m_channelSelection == VISUALIZATION_CHANNEL_MERGED && m_engineChannels > 1) {
    ma_channel_converter_config config = ma_channel_converter_config_init(
        ma_format_f32,
        m_engineChannels,
        NULL,
        1,
        NULL,
        ma_channel_mix_mode_default);
    ma_result res = ma_channel_converter_init(&config, NULL, &m_channelConverter);
    if (res != MA_SUCCESS) {
      return unknownError;
    }
    m_hasChannelConverter = true;
  } else {
    m_hasChannelConverter = false;
  }

  // Initialize PFFFT setup
  if (m_kind != VISUALIZATION_WAVE) {
    m_pffftSetup = pffft_new_setup(m_windowSize, PFFFT_REAL);
    if (m_pffftSetup == nullptr) {
      cleanupBuffers();
      return outOfMemory;
    }

    // Precompute Blackman window table
    m_windowTable = static_cast<float *>(
        pffft_aligned_malloc(m_windowSize * sizeof(float)));
    if (m_windowTable == nullptr) {
      cleanupBuffers();
      return outOfMemory;
    }

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

  // Allocate per-channel processing and double-buffered export memory
  const size_t fftBins = static_cast<size_t>(m_windowSize / 2);
  for (int c = 0; c < m_activeChannels; c++) {
    m_ringBuffer[c].assign(kRingBufferSize, 0.0f);

    for (int p = 0; p < 2; p++) {
      m_waveBuffers[c][p] = static_cast<float *>(
          pffft_aligned_malloc(m_windowSize * sizeof(float)));
      if (m_waveBuffers[c][p] == nullptr) {
        cleanupBuffers();
        return outOfMemory;
      }
      std::memset(m_waveBuffers[c][p], 0, m_windowSize * sizeof(float));

      if (m_kind != VISUALIZATION_WAVE) {
        m_fftBuffers[c][p] = static_cast<float *>(
            pffft_aligned_malloc(fftBins * sizeof(float)));
        if (m_fftBuffers[c][p] == nullptr) {
          cleanupBuffers();
          return outOfMemory;
        }
        std::memset(m_fftBuffers[c][p], 0, fftBins * sizeof(float));
      }
    }

    if (m_kind != VISUALIZATION_WAVE) {
      m_windowedInput[c] = static_cast<float *>(
          pffft_aligned_malloc(m_windowSize * sizeof(float)));
      m_fftOutput[c] = static_cast<float *>(
          pffft_aligned_malloc(m_windowSize * sizeof(float)));
      m_fftWork[c] = static_cast<float *>(
          pffft_aligned_malloc(m_windowSize * sizeof(float)));
      m_fftSmoothed[c] = static_cast<float *>(
          pffft_aligned_malloc(fftBins * sizeof(float)));

      if (!m_windowedInput[c] || !m_fftOutput[c] || !m_fftWork[c] || !m_fftSmoothed[c]) {
        cleanupBuffers();
        return outOfMemory;
      }
      std::memset(m_fftSmoothed[c], 0, fftBins * sizeof(float));
    }
  }

  m_scratchConverted.reserve(8192);
  m_scratchExtract.reserve(8192);

  m_writeIndex.store(0, std::memory_order_relaxed);
  m_readIndex.store(0, std::memory_order_relaxed);
  m_pingPongIndex.store(0, std::memory_order_relaxed);
  m_shouldStop.store(false, std::memory_order_relaxed);
  m_running.store(true, std::memory_order_release);

#ifndef __EMSCRIPTEN__
  m_workerThread = std::thread(&Analyzer::workerThreadFunc, this);
#endif

  return noError;
}

void Analyzer::cleanupBuffers() {
  if (m_pffftSetup != nullptr) {
    pffft_destroy_setup(m_pffftSetup);
    m_pffftSetup = nullptr;
  }

  if (m_windowTable != nullptr) {
    pffft_aligned_free(m_windowTable);
    m_windowTable = nullptr;
  }

  if (m_hasChannelConverter) {
    ma_channel_converter_uninit(&m_channelConverter, NULL);
    m_hasChannelConverter = false;
  }

  for (int c = 0; c < kMaxChannels; c++) {
    for (int p = 0; p < 2; p++) {
      if (m_waveBuffers[c][p] != nullptr) {
        pffft_aligned_free(m_waveBuffers[c][p]);
        m_waveBuffers[c][p] = nullptr;
      }
      if (m_fftBuffers[c][p] != nullptr) {
        pffft_aligned_free(m_fftBuffers[c][p]);
        m_fftBuffers[c][p] = nullptr;
      }
    }

    if (m_windowedInput[c] != nullptr) {
      pffft_aligned_free(m_windowedInput[c]);
      m_windowedInput[c] = nullptr;
    }
    if (m_fftOutput[c] != nullptr) {
      pffft_aligned_free(m_fftOutput[c]);
      m_fftOutput[c] = nullptr;
    }
    if (m_fftWork[c] != nullptr) {
      pffft_aligned_free(m_fftWork[c]);
      m_fftWork[c] = nullptr;
    }
    if (m_fftSmoothed[c] != nullptr) {
      pffft_aligned_free(m_fftSmoothed[c]);
      m_fftSmoothed[c] = nullptr;
    }
  }
}

void Analyzer::onAudioData(const float *data, unsigned int frames, int channels) {
  if (!m_running.load(std::memory_order_acquire) || data == nullptr || frames == 0) {
    return;
  }

  m_inFlightAudioCallbacks.fetch_add(1, std::memory_order_acquire);
  if (!m_running.load(std::memory_order_acquire)) {
    m_inFlightAudioCallbacks.fetch_sub(1, std::memory_order_release);
    return;
  }

  const size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);

  if (m_channelSelection == VISUALIZATION_CHANNEL_MERGED) {
    if (channels > 1 && m_hasChannelConverter) {
      if (m_scratchConverted.size() < frames) {
        m_scratchConverted.resize(frames);
      }
      ma_channel_converter_process_pcm_frames(
          &m_channelConverter, m_scratchConverted.data(), data, frames);
      for (unsigned int i = 0; i < frames; i++) {
        m_ringBuffer[0][(writeIdx + i) & (kRingBufferSize - 1)] =
            m_scratchConverted[i];
      }
    } else {
      for (unsigned int i = 0; i < frames; i++) {
        m_ringBuffer[0][(writeIdx + i) & (kRingBufferSize - 1)] =
            data[i * channels];
      }
    }
  } else if (m_channelSelection == VISUALIZATION_CHANNEL_ALL) {
    const int count = (channels < m_activeChannels) ? channels : m_activeChannels;
    for (int c = 0; c < count; c++) {
      for (unsigned int i = 0; i < frames; i++) {
        m_ringBuffer[c][(writeIdx + i) & (kRingBufferSize - 1)] =
            data[i * channels + c];
      }
    }
  } else if (m_channelSelection >= 0) {
    const int targetCh = (m_channelSelection < channels) ? m_channelSelection : 0;
    for (unsigned int i = 0; i < frames; i++) {
      m_ringBuffer[0][(writeIdx + i) & (kRingBufferSize - 1)] =
          data[i * channels + targetCh];
    }
  }

  m_writeIndex.store(writeIdx + frames, std::memory_order_release);

#ifdef __EMSCRIPTEN__
  const size_t totalWritten = writeIdx + frames;
  if (totalWritten >= static_cast<size_t>(m_windowSize)) {
    if (!m_dispatchInFlight.load(std::memory_order_relaxed)) {
      const size_t targetReadIdx = totalWritten - m_windowSize;
      const int pingPong = m_pingPongIndex.load(std::memory_order_relaxed);
      const int nextPingPong = 1 - pingPong;
      processWindow(targetReadIdx, nextPingPong);
      m_pingPongIndex.store(nextPingPong, std::memory_order_release);
      m_readIndex.store(totalWritten, std::memory_order_release);
      dispatchToDart(nextPingPong);
    }
  }
#endif

  m_inFlightAudioCallbacks.fetch_sub(1, std::memory_order_release);
}

void Analyzer::workerThreadFunc() {
  while (!m_shouldStop.load(std::memory_order_relaxed)) {
    const size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
    const size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
    const size_t available = writeIdx - readIdx;

    if (available < static_cast<size_t>(m_windowSize)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(3));
      continue;
    }

    // Always inspect the latest window so visualization stays in sync with real-time audio
    const size_t targetReadIdx = writeIdx - m_windowSize;

    const int pingPong = m_pingPongIndex.load(std::memory_order_relaxed);
    const int nextPingPong = 1 - pingPong;

    processWindow(targetReadIdx, nextPingPong);

    m_pingPongIndex.store(nextPingPong, std::memory_order_release);
    m_readIndex.store(writeIdx, std::memory_order_release);

    dispatchToDart(nextPingPong);

    // Throttle worker thread to ~60-100 fps (12 ms) to match display refresh and avoid Dart event queue pressure
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
  }
}

void Analyzer::processWindow(size_t readIdx, int nextPingPong) {
  for (int c = 0; c < m_activeChannels; c++) {
    // Copy time-domain wave samples from ring buffer
    for (int i = 0; i < m_windowSize; i++) {
      m_waveBuffers[c][nextPingPong][i] =
          m_ringBuffer[c][(readIdx + i) & (kRingBufferSize - 1)];
    }

    // If FFT is enabled, compute real forward transform and magnitudes
    if (m_kind != VISUALIZATION_WAVE && m_pffftSetup != nullptr && m_windowTable != nullptr) {
      for (int i = 0; i < m_windowSize; i++) {
        m_windowedInput[c][i] =
            m_waveBuffers[c][nextPingPong][i] * m_windowTable[i];
      }

      pffft_transform_ordered(
          m_pffftSetup,
          m_windowedInput[c],
          m_fftOutput[c],
          m_fftWork[c],
          PFFFT_FORWARD);

      computeFftMagnitudes(c, nextPingPong);
    }
  }
}

void Analyzer::computeFftMagnitudes(int channelIdx, int pingPong) {
  const int numBins = m_windowSize / 2;
  const float norm = 2.0f / static_cast<float>(m_windowSize);
  const float smooth = m_fftSmoothing.load(std::memory_order_relaxed);

  // DC component (bin 0)
  float dcMag = fabsf(m_fftOutput[channelIdx][0]) / static_cast<float>(m_windowSize);
  float dcVal = 2.0f * log10f(dcMag * 4.0f + 1.0f);
  if (dcVal > 1.0f) dcVal = 1.0f;
  else if (dcVal < 0.001f) dcVal = 0.0f;

  if (dcVal >= m_fftSmoothed[channelIdx][0]) {
    m_fftSmoothed[channelIdx][0] = dcVal;
  } else {
    m_fftSmoothed[channelIdx][0] =
        smooth * m_fftSmoothed[channelIdx][0] + (1.0f - smooth) * dcVal;
  }
  m_fftBuffers[channelIdx][pingPong][0] = m_fftSmoothed[channelIdx][0];

  // Frequency bins 1 to numBins - 1
  for (int k = 1; k < numBins; k++) {
    const float real = m_fftOutput[channelIdx][2 * k];
    const float imag = m_fftOutput[channelIdx][2 * k + 1];
    const float mag = sqrtf(real * real + imag * imag) * norm;

    // Frequency-dependent boost for high frequencies and logarithmic scaling
    const float freqScaling = sqrtf(static_cast<float>(k + 1));
    const float scaledMag = mag * freqScaling;
    float val = 2.0f * log10f(scaledMag * 4.0f + 1.0f);

    if (val > 1.0f) val = 1.0f;
    else if (val < 0.001f) val = 0.0f;

    if (val >= m_fftSmoothed[channelIdx][k]) {
      m_fftSmoothed[channelIdx][k] = val;
    } else {
      m_fftSmoothed[channelIdx][k] =
          smooth * m_fftSmoothed[channelIdx][k] + (1.0f - smooth) * val;
    }
    m_fftBuffers[channelIdx][pingPong][k] = m_fftSmoothed[channelIdx][k];
  }
}

void Analyzer::dispatchToDart(int pingPong) {
  const auto cb = m_callback.load(std::memory_order_acquire);
  if (cb == nullptr) {
    return;
  }

  for (int c = 0; c < m_activeChannels; c++) {
    m_wavePtrsExport[c] =
        (m_kind != VISUALIZATION_FFT) ? m_waveBuffers[c][pingPong] : nullptr;
    m_fftPtrsExport[c] =
        (m_kind != VISUALIZATION_WAVE) ? m_fftBuffers[c][pingPong] : nullptr;
  }

  const int waveSamples = (m_kind != VISUALIZATION_FFT) ? m_windowSize : 0;
  const int fftSamples = (m_kind != VISUALIZATION_WAVE) ? (m_windowSize / 2) : 0;

  cb(m_activeChannels, m_wavePtrsExport, waveSamples, m_fftPtrsExport, fftSamples);
}