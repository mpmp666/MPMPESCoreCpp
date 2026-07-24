#include "mpmpes/util/Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>

namespace mpmpes::util {

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

const char* Logger::levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Notice: return "NOTICE";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Critical: return "CRITICAL";
  }
  return "?";
}

void Logger::log(LogLevel level, std::string_view msg) {
  if (level < min_) return;
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto t = clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::lock_guard lock(mu_);
  std::cerr << std::put_time(&tm, "%H:%M:%S") << " [" << levelName(level) << "] "
            << msg << '\n';
}

} // namespace mpmpes::util
