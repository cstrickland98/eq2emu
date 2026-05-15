#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace eq2::net {

struct SessionId {
  std::uint64_t value = 0;

  friend auto operator==(SessionId lhs, SessionId rhs) -> bool = default;
};

enum class TransportKind {
  tcp,
  udp,
};

enum class SessionEventType {
  accepted,
  connected,
  received,
  writable,
  backpressure,
  disconnected,
  stopped,
};

struct SessionEvent {
  SessionEventType type = SessionEventType::received;
  SessionId session;
  TransportKind transport = TransportKind::tcp;
  std::vector<std::uint8_t> bytes;
  std::string reason;
};

struct BackpressurePolicy {
  std::size_t max_queued_bytes = 64 * 1024;
};

class WriteQueue {
 public:
  explicit WriteQueue(BackpressurePolicy policy = {}) : policy_(policy) {}

  [[nodiscard]] auto queued_bytes() const -> std::size_t {
    return queued_bytes_;
  }

  [[nodiscard]] auto queued_packets() const -> std::size_t {
    return packets_.size();
  }

  [[nodiscard]] auto empty() const -> bool {
    return packets_.empty();
  }

  auto push(std::span<const std::uint8_t> bytes) -> bool {
    if (closed_ || queued_bytes_ + bytes.size() > policy_.max_queued_bytes) {
      return false;
    }

    packets_.emplace_back(bytes.begin(), bytes.end());
    queued_bytes_ += bytes.size();
    return true;
  }

  auto pop() -> std::optional<std::vector<std::uint8_t>> {
    if (packets_.empty()) {
      return std::nullopt;
    }

    auto packet = std::move(packets_.front());
    packets_.pop_front();
    queued_bytes_ -= packet.size();
    return packet;
  }

  void close() {
    closed_ = true;
  }

  [[nodiscard]] auto closed() const -> bool {
    return closed_;
  }

 private:
  BackpressurePolicy policy_;
  std::deque<std::vector<std::uint8_t>> packets_;
  std::size_t queued_bytes_ = 0;
  bool closed_ = false;
};

class Session {
 public:
  using EventSink = std::function<void(SessionEvent)>;

  Session(SessionId id,
          TransportKind transport,
          EventSink sink,
          BackpressurePolicy backpressure = {})
      : id_(id), transport_(transport), sink_(std::move(sink)), writes_(backpressure) {}

  [[nodiscard]] auto id() const -> SessionId {
    return id_;
  }

  [[nodiscard]] auto transport() const -> TransportKind {
    return transport_;
  }

  [[nodiscard]] auto queued_write_bytes() const -> std::size_t {
    return writes_.queued_bytes();
  }

  [[nodiscard]] auto open() const -> bool {
    return open_;
  }

  void receive(std::span<const std::uint8_t> bytes) {
    if (!open_) {
      return;
    }

    emit(SessionEvent{
        .type = SessionEventType::received,
        .session = id_,
        .transport = transport_,
        .bytes = {bytes.begin(), bytes.end()},
    });
  }

  auto send(std::span<const std::uint8_t> bytes) -> bool {
    if (!open_) {
      return false;
    }

    if (!writes_.push(bytes)) {
      emit(SessionEvent{
          .type = SessionEventType::backpressure,
          .session = id_,
          .transport = transport_,
          .reason = "write queue capacity exceeded",
      });
      return false;
    }

    emit(SessionEvent{
        .type = SessionEventType::writable,
        .session = id_,
        .transport = transport_,
    });
    return true;
  }

  auto drain_one_write() -> std::optional<std::vector<std::uint8_t>> {
    return writes_.pop();
  }

  void disconnect(std::string reason = {}) {
    if (!open_) {
      return;
    }

    open_ = false;
    writes_.close();
    emit(SessionEvent{
        .type = SessionEventType::disconnected,
        .session = id_,
        .transport = transport_,
        .reason = std::move(reason),
    });
  }

 private:
  void emit(SessionEvent event) {
    if (sink_) {
      sink_(std::move(event));
    }
  }

  SessionId id_;
  TransportKind transport_ = TransportKind::tcp;
  EventSink sink_;
  WriteQueue writes_;
  bool open_ = true;
};

}  // namespace eq2::net
