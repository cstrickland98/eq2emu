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

struct UdpEndpoint {
  std::string address = "0.0.0.0";
  std::uint16_t port = 0;
};

struct UdpStreamConfig {
  UdpEndpoint bind;
  BackpressurePolicy backpressure;
};

class UdpStreamServer {
 public:
  explicit UdpStreamServer(UdpStreamConfig config, Session::EventSink sink)
      : config_(std::move(config)), sink_(std::move(sink)) {}

  UdpStreamServer(const UdpStreamServer&) = delete;
  auto operator=(const UdpStreamServer&) -> UdpStreamServer& = delete;

  void start() {
    stopped_ = false;
  }

  void stop() {
    if (stopped_) {
      return;
    }

    stopped_ = true;
    for (auto& [_, stream] : streams_) {
      stream->disconnect("UDP stream server stopped");
    }

    streams_.clear();
    emit(SessionEvent{
        .type = SessionEventType::stopped,
        .transport = TransportKind::udp,
        .reason = "UDP stream server stopped",
    });
  }

  [[nodiscard]] auto endpoint() const -> const UdpEndpoint& {
    return config_.bind;
  }

  auto open_stream() -> std::optional<SessionId> {
    if (stopped_) {
      return std::nullopt;
    }

    const auto id = SessionId{next_stream_id_++};
    auto stream = std::make_unique<Session>(
        id,
        TransportKind::udp,
        [this](SessionEvent event) { emit(std::move(event)); },
        config_.backpressure);
    streams_.emplace(id.value, std::move(stream));

    emit(SessionEvent{
        .type = SessionEventType::connected,
        .session = id,
        .transport = TransportKind::udp,
    });
    return id;
  }

  auto receive_datagram(SessionId id, std::span<const std::uint8_t> bytes) -> bool {
    auto* stream = find_stream(id);
    if (stream == nullptr) {
      return false;
    }

    stream->receive(bytes);
    return true;
  }

  auto send_datagram(SessionId id, std::span<const std::uint8_t> bytes) -> bool {
    auto* stream = find_stream(id);
    return stream != nullptr && stream->send(bytes);
  }

  auto drain_one_datagram(SessionId id) -> std::optional<std::vector<std::uint8_t>> {
    auto* stream = find_stream(id);
    if (stream == nullptr) {
      return std::nullopt;
    }

    return stream->drain_one_write();
  }

 private:
  auto find_stream(SessionId id) -> Session* {
    const auto iter = streams_.find(id.value);
    if (iter == streams_.end()) {
      return nullptr;
    }

    return iter->second.get();
  }

  void emit(SessionEvent event) {
    if (sink_) {
      sink_(std::move(event));
    }
  }

  UdpStreamConfig config_;
  Session::EventSink sink_;
  std::unordered_map<std::uint64_t, std::unique_ptr<Session>> streams_;
  std::uint64_t next_stream_id_ = 1;
  bool stopped_ = true;
};

}  // namespace eq2::net
