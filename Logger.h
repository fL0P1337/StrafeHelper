// Logger.h
#pragma once

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

private:
  Logger() = default;
  ~Logger() = default;

  std::mutex m_mutex;
  std::deque<std::string> m_logs;
  const size_t MAX_LINES = 1000;
};
