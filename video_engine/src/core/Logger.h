#pragma once

#include <functional>
#include <string>

namespace video_engine {

class Logger {
public:
  using Sink = std::function<void(const std::string&)>;

  static void info(const std::string& message);
  static void warn(const std::string& message);
  static void error(const std::string& message);
  static Sink setSink(Sink sink);

private:
  static void log(const char* level, const std::string& message);
};

}  // namespace video_engine
