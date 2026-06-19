#include "core/Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>

namespace video_engine {

namespace {

std::mutex& logMutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace

void Logger::info(const std::string& message) {
  log("INFO", message);
}

void Logger::warn(const std::string& message) {
  log("WARN", message);
}

void Logger::error(const std::string& message) {
  log("ERROR", message);
}

void Logger::log(const char* level, const std::string& message) {
  std::lock_guard<std::mutex> guard(logMutex());
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm tm_snapshot{};
#if defined(_WIN32)
  localtime_s(&tm_snapshot, &now_time);
#else
  localtime_r(&now_time, &tm_snapshot);
#endif
  std::cerr << "[" << std::put_time(&tm_snapshot, "%F %T") << "] "
            << "[" << level << "] " << message << std::endl;
}

}  // namespace video_engine
