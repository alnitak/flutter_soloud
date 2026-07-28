#include "pcm_queue.h"

PcmChunkQueue::~PcmChunkQueue() { stop(); }

bool PcmChunkQueue::push(std::vector<float> chunk) {
  if (m_stopped.load()) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopped.load()) {
      return false;
    }
    m_queue.push(std::move(chunk));
  }

  m_cond.notify_one();
  return true;
}

bool PcmChunkQueue::pop(std::vector<float> &chunk) {
  std::unique_lock<std::mutex> lock(m_mutex);
  m_cond.wait(lock, [this] { return !m_queue.empty() || m_stopped.load(); });

  if (m_queue.empty()) {
    return false;
  }

  chunk = std::move(m_queue.front());
  m_queue.pop();
  return true;
}

void PcmChunkQueue::stop() {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stopped.store(true);
  }
  m_cond.notify_all();
}

bool PcmChunkQueue::isStopped() const { return m_stopped.load(); }
