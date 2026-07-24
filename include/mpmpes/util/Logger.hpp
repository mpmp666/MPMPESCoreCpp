#pragma once

#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <iomanip>

namespace mpmpes::util {

enum class LogLevel { Debug, Info, Notice, Warning, Error, Critical };

class Logger {
public:
  static Logger& instance();

  void setMinLevel(LogLevel level) { min_ = level; }

  void log(LogLevel level, std::string_view msg);

  template <typename... Args>
  void info(Args&&... args) {
    log(LogLevel::Info, format(std::forward<Args>(args)...));
  }
  template <typename... Args>
  void notice(Args&&... args) {
    log(LogLevel::Notice, format(std::forward<Args>(args)...));
  }
  template <typename... Args>
  void warn(Args&&... args) {
    log(LogLevel::Warning, format(std::forward<Args>(args)...));
  }
  // alias used by plugin host / older call sites
  template <typename... Args>
  void warning(Args&&... args) {
    log(LogLevel::Warning, format(std::forward<Args>(args)...));
  }
  template <typename... Args>
  void error(Args&&... args) {
    log(LogLevel::Error, format(std::forward<Args>(args)...));
  }
  template <typename... Args>
  void critical(Args&&... args) {
    log(LogLevel::Critical, format(std::forward<Args>(args)...));
  }

private:
  Logger() = default;

  template <typename T>
  static void append(std::ostringstream& os, T&& v) {
    os << std::forward<T>(v);
  }
  template <typename T, typename... Rest>
  static void append(std::ostringstream& os, T&& v, Rest&&... rest) {
    os << std::forward<T>(v);
    append(os, std::forward<Rest>(rest)...);
  }
  template <typename... Args>
  static std::string format(Args&&... args) {
    std::ostringstream os;
    append(os, std::forward<Args>(args)...);
    return os.str();
  }

  static const char* levelName(LogLevel level);

  std::mutex mu_;
  LogLevel min_ = LogLevel::Info;
};

} // namespace mpmpes::util
