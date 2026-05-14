#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace eq2::core {

template <typename T>
class BlockingQueue {
 public:
  BlockingQueue() = default;
  BlockingQueue(const BlockingQueue&) = delete;
  auto operator=(const BlockingQueue&) -> BlockingQueue& = delete;

  auto push(T value) -> bool {
    {
      std::lock_guard lock(mutex_);
      if (closed_) {
        return false;
      }

      values_.push(std::move(value));
    }

    ready_.notify_one();
    return true;
  }

  auto pop() -> std::optional<T> {
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [this] { return closed_ || !values_.empty(); });

    if (values_.empty()) {
      return std::nullopt;
    }

    auto value = std::move(values_.front());
    values_.pop();
    return value;
  }

  auto try_pop() -> std::optional<T> {
    std::lock_guard lock(mutex_);
    if (values_.empty()) {
      return std::nullopt;
    }

    auto value = std::move(values_.front());
    values_.pop();
    return value;
  }

  void close() {
    {
      std::lock_guard lock(mutex_);
      closed_ = true;
    }

    ready_.notify_all();
  }

  [[nodiscard]] auto closed() const -> bool {
    std::lock_guard lock(mutex_);
    return closed_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable ready_;
  std::queue<T> values_;
  bool closed_ = false;
};

}  // namespace eq2::core
