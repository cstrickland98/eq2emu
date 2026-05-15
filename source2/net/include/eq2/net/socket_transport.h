#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <eq2/net/session.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace eq2::net {

#ifdef _WIN32
using NativeSocket = SOCKET;
inline constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
inline void close_socket(NativeSocket socket) {
  if (socket != kInvalidSocket) {
    closesocket(socket);
  }
}
inline auto last_socket_error() -> int {
  return WSAGetLastError();
}
#else
using NativeSocket = int;
inline constexpr NativeSocket kInvalidSocket = -1;
inline void close_socket(NativeSocket socket) {
  if (socket != kInvalidSocket) {
    close(socket);
  }
}
inline auto last_socket_error() -> int {
  return errno;
}
#endif

class SocketRuntime {
 public:
  SocketRuntime() {
#ifdef _WIN32
    WSADATA data{};
    ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    ok_ = true;
#endif
  }

  SocketRuntime(const SocketRuntime&) = delete;
  auto operator=(const SocketRuntime&) -> SocketRuntime& = delete;

  ~SocketRuntime() {
#ifdef _WIN32
    if (ok_) {
      WSACleanup();
    }
#endif
  }

  [[nodiscard]] auto ok() const -> bool {
    return ok_;
  }

 private:
  bool ok_ = false;
};

struct SocketEndpoint {
  std::string address = "127.0.0.1";
  std::uint16_t port = 0;
};

inline auto make_loopback_addr(std::uint16_t port) -> sockaddr_in {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  return address;
}

inline auto bound_port(NativeSocket socket) -> std::uint16_t {
  sockaddr_in address{};
#ifdef _WIN32
  auto length = static_cast<int>(sizeof(address));
#else
  auto length = static_cast<socklen_t>(sizeof(address));
#endif
  if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    return 0;
  }
  return ntohs(address.sin_port);
}

class TcpSocketServer {
 public:
  explicit TcpSocketServer(Session::EventSink sink) : sink_(std::move(sink)) {}

  TcpSocketServer(const TcpSocketServer&) = delete;
  auto operator=(const TcpSocketServer&) -> TcpSocketServer& = delete;

  ~TcpSocketServer() {
    stop();
  }

  auto start(std::uint16_t requested_port = 0) -> bool {
    if (!runtime_.ok()) {
      return false;
    }

    listen_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == kInvalidSocket) {
      return false;
    }

    const auto address = make_loopback_addr(requested_port);
    if (bind(listen_socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      stop();
      return false;
    }

    port_ = bound_port(listen_socket_);
    if (port_ == 0 || listen(listen_socket_, 1) != 0) {
      stop();
      return false;
    }

    running_ = true;
    worker_ = std::thread([this] { accept_loop(); });
    return true;
  }

  void stop() {
    running_ = false;
    close_socket(client_socket_);
    client_socket_ = kInvalidSocket;
    close_socket(listen_socket_);
    listen_socket_ = kInvalidSocket;
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return port_;
  }

 private:
  void emit(SessionEvent event) {
    if (sink_) {
      sink_(std::move(event));
    }
  }

  void accept_loop() {
    auto next_session = std::uint64_t{1};
    while (running_) {
      client_socket_ = accept(listen_socket_, nullptr, nullptr);
      if (client_socket_ == kInvalidSocket) {
        break;
      }

      const auto id = SessionId{next_session++};
      emit(SessionEvent{
          .type = SessionEventType::accepted,
          .session = id,
          .transport = TransportKind::tcp,
      });

      std::vector<std::uint8_t> buffer(4096);
      while (running_) {
        const auto received =
            recv(client_socket_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
        if (received <= 0) {
          break;
        }

        emit(SessionEvent{
            .type = SessionEventType::received,
            .session = id,
            .transport = TransportKind::tcp,
            .bytes = {buffer.begin(), buffer.begin() + received},
        });
      }

      emit(SessionEvent{
          .type = SessionEventType::disconnected,
          .session = id,
          .transport = TransportKind::tcp,
      });
      close_socket(client_socket_);
      client_socket_ = kInvalidSocket;
    }
  }

  SocketRuntime runtime_;
  Session::EventSink sink_;
  NativeSocket listen_socket_ = kInvalidSocket;
  NativeSocket client_socket_ = kInvalidSocket;
  std::thread worker_;
  std::atomic_bool running_ = false;
  std::uint16_t port_ = 0;
};

class UdpSocketServer {
 public:
  explicit UdpSocketServer(Session::EventSink sink) : sink_(std::move(sink)) {}

  UdpSocketServer(const UdpSocketServer&) = delete;
  auto operator=(const UdpSocketServer&) -> UdpSocketServer& = delete;

  ~UdpSocketServer() {
    stop();
  }

  auto start(std::uint16_t requested_port = 0) -> bool {
    if (!runtime_.ok()) {
      return false;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
      return false;
    }

    const auto address = make_loopback_addr(requested_port);
    if (bind(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
      stop();
      return false;
    }

    port_ = bound_port(socket_);
    if (port_ == 0) {
      stop();
      return false;
    }

    running_ = true;
    worker_ = std::thread([this] { receive_loop(); });
    return true;
  }

  void stop() {
    running_ = false;
    close_socket(socket_);
    socket_ = kInvalidSocket;
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return port_;
  }

 private:
  void emit(SessionEvent event) {
    if (sink_) {
      sink_(std::move(event));
    }
  }

  void receive_loop() {
    const auto id = SessionId{1};
    emit(SessionEvent{
        .type = SessionEventType::connected,
        .session = id,
        .transport = TransportKind::udp,
    });

    std::vector<std::uint8_t> buffer(4096);
    while (running_) {
      const auto received =
          recvfrom(socket_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                   nullptr, nullptr);
      if (received <= 0) {
        break;
      }

      emit(SessionEvent{
          .type = SessionEventType::received,
          .session = id,
          .transport = TransportKind::udp,
          .bytes = {buffer.begin(), buffer.begin() + received},
      });
    }
  }

  SocketRuntime runtime_;
  Session::EventSink sink_;
  NativeSocket socket_ = kInvalidSocket;
  std::thread worker_;
  std::atomic_bool running_ = false;
  std::uint16_t port_ = 0;
};

inline auto send_tcp_loopback(std::uint16_t port, std::span<const std::uint8_t> bytes) -> bool {
  SocketRuntime runtime;
  if (!runtime.ok()) {
    return false;
  }

  auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == kInvalidSocket) {
    return false;
  }

  const auto address = make_loopback_addr(port);
  if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    close_socket(socket);
    return false;
  }

  const auto sent = send(socket, reinterpret_cast<const char*>(bytes.data()),
                         static_cast<int>(bytes.size()), 0);
  close_socket(socket);
  return sent == static_cast<int>(bytes.size());
}

inline auto send_udp_loopback(std::uint16_t port, std::span<const std::uint8_t> bytes) -> bool {
  SocketRuntime runtime;
  if (!runtime.ok()) {
    return false;
  }

  auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket == kInvalidSocket) {
    return false;
  }

  const auto address = make_loopback_addr(port);
  const auto sent = sendto(socket, reinterpret_cast<const char*>(bytes.data()),
                           static_cast<int>(bytes.size()), 0,
                           reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  close_socket(socket);
  return sent == static_cast<int>(bytes.size());
}

}  // namespace eq2::net
