#pragma once

#include <chrono>

namespace eq2::core {

using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = SteadyClock::time_point;

class Clock {
 public:
  virtual ~Clock() = default;

  [[nodiscard]] virtual auto now() const -> SteadyTimePoint = 0;
};

class SystemClock final : public Clock {
 public:
  [[nodiscard]] auto now() const -> SteadyTimePoint override {
    return SteadyClock::now();
  }
};

class ManualClock final : public Clock {
 public:
  explicit ManualClock(SteadyTimePoint current = SteadyTimePoint{}) : current_(current) {}

  [[nodiscard]] auto now() const -> SteadyTimePoint override {
    return current_;
  }

  void advance(SteadyClock::duration elapsed) {
    current_ += elapsed;
  }

 private:
  SteadyTimePoint current_;
};

}  // namespace eq2::core
