#pragma once

#include <iostream>
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

class ConsoleLogSink final : public LogSink {
 public:
  explicit ConsoleLogSink(std::ostream& output = std::cout) : output_(output) {}

  void write(const LogRecord& record) override {
    output_ << '[' << level_name(record.level) << "] " << record.component << ": "
            << record.message << '\n';
  }

 private:
  static auto level_name(LogLevel level) -> std::string_view {
    switch (level) {
      case LogLevel::trace:
        return "trace";
      case LogLevel::debug:
        return "debug";
      case LogLevel::info:
        return "info";
      case LogLevel::warning:
        return "warning";
      case LogLevel::error:
        return "error";
      case LogLevel::critical:
        return "critical";
    }

    return "unknown";
  }

  std::ostream& output_;
};

inline void log(LogSink& sink, LogLevel level, std::string_view component, std::string_view message) {
  sink.write(LogRecord{
      .level = level,
      .component = std::string(component),
      .message = std::string(message),
  });
}

}  // namespace eq2::core
