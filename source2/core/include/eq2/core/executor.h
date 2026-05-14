#pragma once

#include <functional>
#include <thread>

#include <eq2/core/blocking_queue.h>

namespace eq2::core {

class SerialExecutor {
 public:
  SerialExecutor() : worker_([this] { run(); }) {}

  SerialExecutor(const SerialExecutor&) = delete;
  auto operator=(const SerialExecutor&) -> SerialExecutor& = delete;

  ~SerialExecutor() {
    stop();
  }

  auto post(std::function<void()> task) -> bool {
    return tasks_.push(std::move(task));
  }

  void stop() {
    tasks_.close();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  void run() {
    while (auto task = tasks_.pop()) {
      (*task)();
    }
  }

  BlockingQueue<std::function<void()>> tasks_;
  std::thread worker_;
};

}  // namespace eq2::core
