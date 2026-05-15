#pragma once

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <eq2/core/blocking_queue.h>
#include <eq2/core/result.h>

namespace eq2::db {

using QueryParameter = std::string;

struct QueryRequest {
  std::string sql;
  std::vector<QueryParameter> parameters;
};

struct QueryRow {
  std::map<std::string, std::string> columns;

  [[nodiscard]] auto get(std::string_view name) const -> std::optional<std::string> {
    const auto iter = columns.find(std::string(name));
    if (iter == columns.end()) {
      return std::nullopt;
    }

    return iter->second;
  }
};

struct QueryResult {
  std::vector<QueryRow> rows;
  std::uint64_t affected_rows = 0;
};

class QueryConnection {
 public:
  QueryConnection() = default;
  QueryConnection(const QueryConnection&) = delete;
  auto operator=(const QueryConnection&) -> QueryConnection& = delete;
  virtual ~QueryConnection() = default;

  virtual auto execute(const QueryRequest& request) -> eq2::core::Result<QueryResult> = 0;
};

class ConnectionPool {
 public:
  explicit ConnectionPool(std::vector<std::shared_ptr<QueryConnection>> connections)
      : connections_(std::move(connections)) {}

  [[nodiscard]] auto size() const -> std::size_t {
    std::lock_guard lock(mutex_);
    return connections_.size();
  }

  auto acquire() -> std::shared_ptr<QueryConnection> {
    std::lock_guard lock(mutex_);
    if (connections_.empty()) {
      return {};
    }

    auto connection = connections_[next_];
    next_ = (next_ + 1) % connections_.size();
    return connection;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<QueryConnection>> connections_;
  std::size_t next_ = 0;
};

class AsyncQueryExecutor {
 public:
  explicit AsyncQueryExecutor(std::size_t worker_count = 1) {
    if (worker_count == 0) {
      worker_count = 1;
    }

    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
      workers_.emplace_back([this] { run(); });
    }
  }

  AsyncQueryExecutor(const AsyncQueryExecutor&) = delete;
  auto operator=(const AsyncQueryExecutor&) -> AsyncQueryExecutor& = delete;

  ~AsyncQueryExecutor() {
    stop();
  }

  auto execute(std::shared_ptr<QueryConnection> connection, QueryRequest request)
      -> std::future<eq2::core::Result<QueryResult>> {
    std::packaged_task<eq2::core::Result<QueryResult>()> task(
        [connection = std::move(connection), request = std::move(request)] {
          if (!connection) {
            return eq2::core::Result<QueryResult>::failure(eq2::core::Error{
                .code = eq2::core::ErrorCode::unavailable,
                .message = "no database connection available",
            });
          }

          return connection->execute(request);
        });

    auto future = task.get_future();
    if (!tasks_.push(std::move(task))) {
      std::promise<eq2::core::Result<QueryResult>> promise;
      promise.set_value(eq2::core::Result<QueryResult>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::cancelled,
          .message = "database executor stopped",
      }));
      return promise.get_future();
    }

    return future;
  }

  void stop() {
    tasks_.close();
    for (auto& worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

 private:
  void run() {
    while (auto task = tasks_.pop()) {
      (*task)();
    }
  }

  eq2::core::BlockingQueue<std::packaged_task<eq2::core::Result<QueryResult>()>> tasks_;
  std::vector<std::thread> workers_;
};

}  // namespace eq2::db
