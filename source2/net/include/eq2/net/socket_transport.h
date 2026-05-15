#pragma once

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
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
#include <sys/time.h>
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

inline auto make_ipv4_addr(std::string_view host, std::uint16_t port) -> std::optional<sockaddr_in> {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);

  const auto normalized = host.empty() ? std::string{"0.0.0.0"} : std::string(host);
  if (inet_pton(AF_INET, normalized.c_str(), &address.sin_addr) != 1) {
    return std::nullopt;
  }

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

inline auto sockaddr_key(const sockaddr_in& address) -> std::uint64_t {
  return (static_cast<std::uint64_t>(ntohl(address.sin_addr.s_addr)) << 16U) |
         static_cast<std::uint64_t>(ntohs(address.sin_port));
}

inline auto send_socket_bytes(NativeSocket socket, std::span<const std::uint8_t> bytes) -> bool {
  auto written = std::size_t{0};
  while (written < bytes.size()) {
    const auto remaining = bytes.size() - written;
    const auto chunk = remaining > static_cast<std::size_t>(std::numeric_limits<int>::max())
                           ? static_cast<std::size_t>(std::numeric_limits<int>::max())
                           : remaining;
    const auto sent = send(socket,
                           reinterpret_cast<const char*>(bytes.data() + written),
                           static_cast<int>(chunk),
                           0);
    if (sent <= 0) {
      return false;
    }
    written += static_cast<std::size_t>(sent);
  }

  return true;
}

inline void set_receive_timeout(NativeSocket socket, int milliseconds) {
#ifdef _WIN32
  const auto timeout = static_cast<DWORD>(milliseconds);
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));
#else
  timeval timeout{};
  timeout.tv_sec = milliseconds / 1000;
  timeout.tv_usec = (milliseconds % 1000) * 1000;
  setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
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
    return start(SocketEndpoint{.address = "127.0.0.1", .port = requested_port});
  }

  auto start(SocketEndpoint endpoint) -> bool {
    if (!runtime_.ok()) {
      return false;
    }

    listen_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == kInvalidSocket) {
      return false;
    }

    const auto address = make_ipv4_addr(endpoint.address, endpoint.port);
    if (!address.has_value()) {
      stop();
      return false;
    }

    if (bind(listen_socket_, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
      stop();
      return false;
    }

    port_ = bound_port(listen_socket_);
    if (port_ == 0 || listen(listen_socket_, 16) != 0) {
      stop();
      return false;
    }

    running_ = true;
    worker_ = std::thread([this] { accept_loop(); });
    return true;
  }

  void stop() {
    running_ = false;

    close_socket(listen_socket_);
    listen_socket_ = kInvalidSocket;
    if (worker_.joinable()) {
      worker_.join();
    }

    std::vector<NativeSocket> client_sockets;
    {
      std::lock_guard lock(sockets_mutex_);
      client_sockets.reserve(client_sockets_.size());
      for (const auto& [_, socket] : client_sockets_) {
        client_sockets.push_back(socket);
      }
      client_sockets_.clear();
    }
    for (const auto socket : client_sockets) {
      close_socket(socket);
    }

    for (auto& client_worker : client_workers_) {
      if (client_worker.joinable()) {
        client_worker.join();
      }
    }
    client_workers_.clear();
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return port_;
  }

  auto send(SessionId session, std::span<const std::uint8_t> bytes) -> bool {
    std::lock_guard lock(sockets_mutex_);
    const auto iter = client_sockets_.find(session.value);
    if (iter == client_sockets_.end()) {
      return false;
    }

    return send_socket_bytes(iter->second, bytes);
  }

 private:
  void emit(SessionEvent event) {
    if (sink_) {
      sink_(std::move(event));
    }
  }

  void register_client_socket(SessionId session, NativeSocket socket) {
    std::lock_guard lock(sockets_mutex_);
    client_sockets_[session.value] = socket;
  }

  [[nodiscard]] auto unregister_client_socket(SessionId session, NativeSocket socket) -> bool {
    std::lock_guard lock(sockets_mutex_);
    const auto iter = client_sockets_.find(session.value);
    if (iter == client_sockets_.end() || iter->second != socket) {
      return false;
    }

    client_sockets_.erase(iter);
    return true;
  }

  void accept_loop() {
    auto next_session = std::uint64_t{1};
    while (running_) {
      const auto client_socket = accept(listen_socket_, nullptr, nullptr);
      if (client_socket == kInvalidSocket) {
        break;
      }

      const auto id = SessionId{next_session++};
      register_client_socket(id, client_socket);
      emit(SessionEvent{
          .type = SessionEventType::accepted,
          .session = id,
          .transport = TransportKind::tcp,
      });

      client_workers_.emplace_back([this, id, client_socket] {
        client_loop(id, client_socket);
      });
    }
  }

  void client_loop(SessionId id, NativeSocket client_socket) {
    std::vector<std::uint8_t> buffer(4096);
    while (running_) {
      const auto received =
          recv(client_socket, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
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
    if (unregister_client_socket(id, client_socket)) {
      close_socket(client_socket);
    }
  }

  SocketRuntime runtime_;
  Session::EventSink sink_;
  NativeSocket listen_socket_ = kInvalidSocket;
  std::mutex sockets_mutex_;
  std::unordered_map<std::uint64_t, NativeSocket> client_sockets_;
  std::thread worker_;
  std::vector<std::thread> client_workers_;
  std::atomic_bool running_ = false;
  std::uint16_t port_ = 0;
};

class TcpSocketClient {
 public:
  TcpSocketClient() = default;

  TcpSocketClient(const TcpSocketClient&) = delete;
  auto operator=(const TcpSocketClient&) -> TcpSocketClient& = delete;

  ~TcpSocketClient() {
    close();
  }

  auto connect_to(SocketEndpoint endpoint) -> bool {
    close();
    if (!runtime_.ok()) {
      return false;
    }

    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == kInvalidSocket) {
      return false;
    }

    const auto address = make_ipv4_addr(endpoint.address, endpoint.port);
    if (!address.has_value()) {
      close();
      return false;
    }

    if (connect(socket_, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
      close();
      return false;
    }

    set_receive_timeout(socket_, 1000);
    return true;
  }

  auto send(std::span<const std::uint8_t> bytes) -> bool {
    return socket_ != kInvalidSocket && send_socket_bytes(socket_, bytes);
  }

  auto receive(std::size_t max_response_bytes = 4096) -> std::optional<std::vector<std::uint8_t>> {
    if (socket_ == kInvalidSocket) {
      return std::nullopt;
    }

    std::vector<std::uint8_t> buffer(max_response_bytes);
    const auto received =
        recv(socket_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (received <= 0) {
      return std::nullopt;
    }

    buffer.resize(static_cast<std::size_t>(received));
    return buffer;
  }

  void close() {
    close_socket(socket_);
    socket_ = kInvalidSocket;
  }

 private:
  SocketRuntime runtime_;
  NativeSocket socket_ = kInvalidSocket;
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
    return start(SocketEndpoint{.address = "127.0.0.1", .port = requested_port});
  }

  auto start(SocketEndpoint endpoint) -> bool {
    if (!runtime_.ok()) {
      return false;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
      return false;
    }

    const auto address = make_ipv4_addr(endpoint.address, endpoint.port);
    if (!address.has_value()) {
      stop();
      return false;
    }

    if (bind(socket_, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
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

    std::vector<SessionId> sessions;
    {
      std::lock_guard lock(remotes_mutex_);
      sessions.reserve(remotes_by_session_.size());
      for (const auto& [session, _] : remotes_by_session_) {
        sessions.push_back(SessionId{session});
      }
      remotes_by_session_.clear();
      sessions_by_remote_.clear();
    }

    for (const auto session : sessions) {
      emit(SessionEvent{
          .type = SessionEventType::disconnected,
          .session = session,
          .transport = TransportKind::udp,
          .reason = "UDP socket stopped",
      });
    }
  }

  [[nodiscard]] auto port() const -> std::uint16_t {
    return port_;
  }

  auto send(SessionId session, std::span<const std::uint8_t> bytes) -> bool {
    sockaddr_in remote{};
    {
      std::lock_guard lock(remotes_mutex_);
      const auto iter = remotes_by_session_.find(session.value);
      if (iter == remotes_by_session_.end()) {
        return false;
      }
      remote = iter->second;
    }

    const auto sent = sendto(socket_,
                             reinterpret_cast<const char*>(bytes.data()),
                             static_cast<int>(bytes.size()),
                             0,
                             reinterpret_cast<const sockaddr*>(&remote),
                             sizeof(remote));
    return sent == static_cast<int>(bytes.size());
  }

 private:
  void emit(SessionEvent event) {
    if (sink_) {
      sink_(std::move(event));
    }
  }

  auto session_for_remote(const sockaddr_in& remote) -> std::pair<SessionId, bool> {
    std::lock_guard lock(remotes_mutex_);
    const auto remote_key = sockaddr_key(remote);
    const auto existing = sessions_by_remote_.find(remote_key);
    if (existing != sessions_by_remote_.end()) {
      return {existing->second, false};
    }

    const auto session = SessionId{next_session_++};
    sessions_by_remote_[remote_key] = session;
    remotes_by_session_[session.value] = remote;
    return {session, true};
  }

  void receive_loop() {
    std::vector<std::uint8_t> buffer(4096);
    while (running_) {
      sockaddr_in remote{};
#ifdef _WIN32
      auto remote_length = static_cast<int>(sizeof(remote));
#else
      auto remote_length = static_cast<socklen_t>(sizeof(remote));
#endif
      const auto received =
          recvfrom(socket_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                   reinterpret_cast<sockaddr*>(&remote), &remote_length);
      if (received <= 0) {
        break;
      }

      const auto [id, is_new] = session_for_remote(remote);
      if (is_new) {
        emit(SessionEvent{
            .type = SessionEventType::connected,
            .session = id,
            .transport = TransportKind::udp,
        });
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
  std::mutex remotes_mutex_;
  std::unordered_map<std::uint64_t, SessionId> sessions_by_remote_;
  std::unordered_map<std::uint64_t, sockaddr_in> remotes_by_session_;
  std::uint64_t next_session_ = 1;
  std::atomic_bool running_ = false;
  std::uint16_t port_ = 0;
};

class UdpSocketClient {
 public:
  UdpSocketClient() = default;

  UdpSocketClient(const UdpSocketClient&) = delete;
  auto operator=(const UdpSocketClient&) -> UdpSocketClient& = delete;

  ~UdpSocketClient() {
    close();
  }

  auto connect_to(SocketEndpoint endpoint) -> bool {
    close();
    if (!runtime_.ok()) {
      return false;
    }

    socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
      return false;
    }

    const auto address = make_ipv4_addr(endpoint.address, endpoint.port);
    if (!address.has_value()) {
      close();
      return false;
    }

    if (connect(socket_, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
      close();
      return false;
    }

    set_receive_timeout(socket_, 1000);
    return true;
  }

  auto send(std::span<const std::uint8_t> bytes) -> bool {
    return socket_ != kInvalidSocket && send_socket_bytes(socket_, bytes);
  }

  auto receive(std::size_t max_response_bytes = 4096) -> std::optional<std::vector<std::uint8_t>> {
    if (socket_ == kInvalidSocket) {
      return std::nullopt;
    }

    std::vector<std::uint8_t> buffer(max_response_bytes);
    const auto received =
        recv(socket_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (received <= 0) {
      return std::nullopt;
    }

    buffer.resize(static_cast<std::size_t>(received));
    return buffer;
  }

  void close() {
    close_socket(socket_);
    socket_ = kInvalidSocket;
  }

 private:
  SocketRuntime runtime_;
  NativeSocket socket_ = kInvalidSocket;
};

inline auto send_tcp(SocketEndpoint endpoint, std::span<const std::uint8_t> bytes) -> bool {
  SocketRuntime runtime;
  if (!runtime.ok()) {
    return false;
  }

  auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == kInvalidSocket) {
    return false;
  }

  const auto address = make_ipv4_addr(endpoint.address, endpoint.port);
  if (!address.has_value()) {
    close_socket(socket);
    return false;
  }

  if (connect(socket, reinterpret_cast<const sockaddr*>(&*address), sizeof(*address)) != 0) {
    close_socket(socket);
    return false;
  }

  const auto sent = send_socket_bytes(socket, bytes);
  close_socket(socket);
  return sent;
}

inline auto send_tcp_loopback(std::uint16_t port, std::span<const std::uint8_t> bytes) -> bool {
  return send_tcp(SocketEndpoint{.address = "127.0.0.1", .port = port}, bytes);
}

inline auto exchange_tcp(SocketEndpoint endpoint,
                         std::span<const std::uint8_t> bytes,
                         std::size_t max_response_bytes = 4096)
    -> std::optional<std::vector<std::uint8_t>> {
  TcpSocketClient client;
  if (!client.connect_to(std::move(endpoint))) {
    return std::nullopt;
  }

  if (!client.send(bytes)) {
    return std::nullopt;
  }

  return client.receive(max_response_bytes);
}

inline auto exchange_tcp_loopback(std::uint16_t port,
                                  std::span<const std::uint8_t> bytes,
                                  std::size_t max_response_bytes = 4096)
    -> std::optional<std::vector<std::uint8_t>> {
  return exchange_tcp(SocketEndpoint{.address = "127.0.0.1", .port = port},
                      bytes,
                      max_response_bytes);
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

inline auto exchange_udp(SocketEndpoint endpoint,
                         std::span<const std::uint8_t> bytes,
                         std::size_t max_response_bytes = 4096)
    -> std::optional<std::vector<std::uint8_t>> {
  UdpSocketClient client;
  if (!client.connect_to(std::move(endpoint))) {
    return std::nullopt;
  }

  if (!client.send(bytes)) {
    return std::nullopt;
  }

  return client.receive(max_response_bytes);
}

inline auto exchange_udp_loopback(std::uint16_t port,
                                  std::span<const std::uint8_t> bytes,
                                  std::size_t max_response_bytes = 4096)
    -> std::optional<std::vector<std::uint8_t>> {
  return exchange_udp(SocketEndpoint{.address = "127.0.0.1", .port = port},
                      bytes,
                      max_response_bytes);
}

}  // namespace eq2::net
