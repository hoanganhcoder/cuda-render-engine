#pragma once

#include <string>

namespace video_engine {

class Logger {
public:
  static void info(const std::string& message);
  static void warn(const std::string& message);
  static void error(const std::string& message);

private:
  static void log(const char* level, const std::string& message);
};

}  // namespace video_engine
