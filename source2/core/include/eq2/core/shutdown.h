#pragma once

#include <condition_variable>
#include <mutex>

namespace eq2::core {

class ShutdownSignal {
 public:
  void request_stop() {
    {
      std::lock_guard lock(mutex_);
      stop_requested_ = true;
    }

    changed_.notify_all();
  }

  [[nodiscard]] auto stop_requested() const -> bool {
    std::lock_guard lock(mutex_);
    return stop_requested_;
  }

  void wait() const {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return stop_requested_; });
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable changed_;
  bool stop_requested_ = false;
};

}  // namespace eq2::core
