#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <eq2/net/session.h>

namespace eq2::net {

struct TcpEndpoint {
  std::string address = "0.0.0.0";
  std::uint16_t port = 0;
};

struct TcpServerConfig {
  TcpEndpoint listen;
  BackpressurePolicy backpressure;
};

class TcpServer {
 public:
  explicit TcpServer(TcpServerConfig config, Session::EventSink sink)
      : config_(std::move(config)), sink_(std::move(sink)) {}

  TcpServer(const TcpServer&) = delete;
  auto operator=(const TcpServer&) -> TcpServer& = delete;

  void start() {
    stopped_ = false;
  }

  void stop() {
    if (stopped_) {
      return;
    }

    stopped_ = true;
    for (auto& [_, session] : sessions_) {
      session->disconnect("server stopped");
    }

    sessions_.clear();
    emit(SessionEvent{
        .type = SessionEventType::stopped,
        .transport = TransportKind::tcp,
        .reason = "server stopped",
    });
  }

  [[nodiscard]] auto stopped() const -> bool {
    return stopped_;
  }

  [[nodiscard]] auto endpoint() const -> const TcpEndpoint& {
    return config_.listen;
  }

  auto accept() -> std::optional<SessionId> {
    if (stopped_) {
      return std::nullopt;
    }

    const auto id = SessionId{next_session_id_++};
    auto session = std::make_unique<Session>(
        id,
        TransportKind::tcp,
        [this](SessionEvent event) { emit(std::move(event)); },
        config_.backpressure);
    sessions_.emplace(id.value, std::move(session));

    emit(SessionEvent{
        .type = SessionEventType::accepted,
        .session = id,
        .transport = TransportKind::tcp,
    });
    return id;
  }

  auto receive(SessionId id, std::span<const std::uint8_t> bytes) -> bool {
    auto* session = find_session(id);
    if (session == nullptr) {
      return false;
    }

    session->receive(bytes);
    return true;
  }

  auto send(SessionId id, std::span<const std::uint8_t> bytes) -> bool {
    auto* session = find_session(id);
    return session != nullptr && session->send(bytes);
  }

  auto drain_one_write(SessionId id) -> std::optional<std::vector<std::uint8_t>> {
    auto* session = find_session(id);
    if (session == nullptr) {
      return std::nullopt;
    }

    return session->drain_one_write();
  }

  auto disconnect(SessionId id, std::string reason = {}) -> bool {
    auto iter = sessions_.find(id.value);
    if (iter == sessions_.end()) {
      return false;
    }

    iter->second->disconnect(std::move(reason));
    sessions_.erase(iter);
    return true;
  }

  [[nodiscard]] auto session_count() const -> std::size_t {
    return sessions_.size();
  }

 private:
  auto find_session(SessionId id) -> Session* {
    const auto iter = sessions_.find(id.value);
    if (iter == sessions_.end()) {
      return nullptr;
    }

    return iter->second.get();
  }

  void emit(SessionEvent event) {
    if (sink_) {
      sink_(std::move(event));
    }
  }

  TcpServerConfig config_;
  Session::EventSink sink_;
  std::unordered_map<std::uint64_t, std::unique_ptr<Session>> sessions_;
  std::uint64_t next_session_id_ = 1;
  bool stopped_ = true;
};

}  // namespace eq2::net
