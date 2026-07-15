// Logger.cpp
#include "Logger.h"
#include <iostream>

void Logger::Log(const std::string &message) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_logs.push_back(message);
    if (m_logs.size() > MAX_LINES) {
      m_logs.pop_front();
    }
    m_generation.fetch_add(1, std::memory_order_release);
  }

  std::cout << message << '\n';
}

std::vector<std::string> Logger::GetRecentLogs(size_t maxCount) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (maxCount == 0 || maxCount >= m_logs.size()) {
    return std::vector<std::string>(m_logs.begin(), m_logs.end());
  }
  return std::vector<std::string>(m_logs.end() - maxCount, m_logs.end());
}

void Logger::Clear() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_logs.clear();
  m_generation.fetch_add(1, std::memory_order_release);
}

uint64_t Logger::GetGeneration() const noexcept {
  return m_generation.load(std::memory_order_acquire);
}
