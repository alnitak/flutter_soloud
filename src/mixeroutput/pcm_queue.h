#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

/// A thread-safe queue of raw PCM chunks used to feed the encoder thread.
class PcmChunkQueue {
 public:
  ~PcmChunkQueue();

  /// Push a chunk of interleaved float PCM samples.
  /// Returns false if the queue has been stopped.
  bool push(std::vector<float> chunk);

  /// Pop a chunk. Blocks until a chunk is available or the queue is stopped.
  /// Returns true if a chunk was popped, false if stopped and empty.
  bool pop(std::vector<float> &chunk);

  /// Signal the consumer to stop waiting. Further pushes return false.
  void stop();

  /// True if the queue has been stopped.
  bool isStopped() const;

 private:
  std::mutex m_mutex;
  std::condition_variable m_cond;
  std::queue<std::vector<float>> m_queue;
  std::atomic<bool> m_stopped{false};
};
