#pragma once

#include <algorithm>
#include <chrono>

#include <eq2/core/clock.h>

namespace eq2::core {

class DeadlineTimer {
 public:
  DeadlineTimer(const Clock& clock, SteadyClock::duration duration)
      : clock_(clock), deadline_(clock.now() + duration) {}

  [[nodiscard]] auto expired() const -> bool {
    return clock_.now() >= deadline_;
  }

  [[nodiscard]] auto remaining() const -> SteadyClock::duration {
    return std::max(SteadyClock::duration::zero(), deadline_ - clock_.now());
  }

  void reset(SteadyClock::duration duration) {
    deadline_ = clock_.now() + duration;
  }

 private:
  const Clock& clock_;
  SteadyTimePoint deadline_;
};

class IntervalTimer {
 public:
  IntervalTimer(const Clock& clock, SteadyClock::duration interval)
      : clock_(clock), interval_(interval), next_tick_(clock.now() + interval) {}

  [[nodiscard]] auto ready() const -> bool {
    return clock_.now() >= next_tick_;
  }

  auto consume_tick() -> bool {
    if (!ready()) {
      return false;
    }

    next_tick_ += interval_;
    return true;
  }

 private:
  const Clock& clock_;
  SteadyClock::duration interval_;
  SteadyTimePoint next_tick_;
};

}  // namespace eq2::core
