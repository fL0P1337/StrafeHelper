// Logger.h
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>


class Logger {
public:
  static Logger &GetInstance() {
    static Logger instance;
    return instance;
  }

  // Prohibit copy/move
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  void Log(const std::string &message);
  std::vector<std::string> GetRecentLogs(size_t maxCount = 0);
  void Clear();
  [[nodiscard]] uint64_t GetGeneration() const noexcept;

private:
  Logger() = default;
  ~Logger() = default;

  std::mutex m_mutex;
  std::deque<std::string> m_logs;
  std::atomic<uint64_t> m_generation{0};
  const size_t MAX_LINES = 1000;
};
