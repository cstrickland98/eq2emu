#pragma once

#include <string>
#include <string_view>

namespace eq2::core {

enum class LogLevel {
  trace,
  debug,
  info,
  warning,
  error,
  critical,
};

struct LogRecord {
  LogLevel level = LogLevel::info;
  std::string component;
  std::string message;
};

class LogSink {
 public:
  virtual ~LogSink() = default;

  virtual void write(const LogRecord& record) = 0;
};

class NullLogSink final : public LogSink {
 public:
  void write(const LogRecord& /*record*/) override {}
};

inline void log(LogSink& sink, LogLevel level, std::string_view component, std::string_view message) {
  sink.write(LogRecord{
      .level = level,
      .component = std::string(component),
      .message = std::string(message),
  });
}

}  // namespace eq2::core
