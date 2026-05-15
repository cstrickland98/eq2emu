#include <eq2/net/session.h>
#include <eq2/net/socket_transport.h>
#include <eq2/net/tcp_server.h>
#include <eq2/net/udp_stream.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

auto failures = 0;

void require(bool condition, std::string_view message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

template <typename T, typename U>
void require_eq(const T& actual, const U& expected, std::string_view message) {
  if (!(actual == expected)) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

void tcp_server_emits_lifecycle_and_receive_events() {
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::TcpServer server(
      eq2::net::TcpServerConfig{
          .listen = {.address = "127.0.0.1", .port = 9000},
      },
      [&](eq2::net::SessionEvent event) { events.push_back(std::move(event)); });

  server.start();
  const auto session = server.accept();
  require(session.has_value(), "started TCP server accepts sessions");

  const std::array<std::uint8_t, 3> bytes{0xaa, 0xbb, 0xcc};
  require(server.receive(*session, bytes), "server routes inbound bytes to a session");

  require_eq(events.size(), static_cast<std::size_t>(2), "accept and receive emit two events");
  require_eq(events[0].type, eq2::net::SessionEventType::accepted, "first event is accepted");
  require_eq(events[1].type, eq2::net::SessionEventType::received, "second event is received");
  require(events[1].bytes == std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
          "received event carries packet bytes");
}

void write_queue_applies_backpressure_and_drains_fifo() {
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::Session session(
      eq2::net::SessionId{1},
      eq2::net::TransportKind::tcp,
      [&](eq2::net::SessionEvent event) { events.push_back(std::move(event)); },
      eq2::net::BackpressurePolicy{.max_queued_bytes = 4});

  const std::array<std::uint8_t, 2> first{0x01, 0x02};
  const std::array<std::uint8_t, 2> second{0x03, 0x04};
  const std::array<std::uint8_t, 1> overflow{0x05};

  require(session.send(first), "first write fits backpressure budget");
  require(session.send(second), "second write exactly fills backpressure budget");
  require(!session.send(overflow), "overflow write is rejected");
  require_eq(session.queued_write_bytes(), static_cast<std::size_t>(4), "write queue tracks bytes");

  const auto drained_first = session.drain_one_write();
  const auto drained_second = session.drain_one_write();
  require(drained_first.has_value(), "first queued packet drains");
  require(drained_second.has_value(), "second queued packet drains");
  require(*drained_first == std::vector<std::uint8_t>(first.begin(), first.end()),
          "writes drain in FIFO order");
  require(*drained_second == std::vector<std::uint8_t>(second.begin(), second.end()),
          "second write drains after first");
  require(!session.drain_one_write().has_value(), "empty write queue reports no packet");

  require_eq(events.size(), static_cast<std::size_t>(3), "two writable events and one backpressure event emit");
  require_eq(events[2].type, eq2::net::SessionEventType::backpressure,
             "overflow write emits backpressure event");
}

void tcp_server_stop_disconnects_sessions_and_rejects_new_accepts() {
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::TcpServer server(
      eq2::net::TcpServerConfig{},
      [&](eq2::net::SessionEvent event) { events.push_back(std::move(event)); });

  server.start();
  const auto session = server.accept();
  require(session.has_value(), "server accepts before stop");
  require_eq(server.session_count(), static_cast<std::size_t>(1), "server tracks accepted session");

  server.stop();
  require(server.stopped(), "server records stopped state");
  require_eq(server.session_count(), static_cast<std::size_t>(0), "server clears sessions on stop");
  require(!server.accept().has_value(), "stopped server rejects accepts");

  require_eq(events.back().type, eq2::net::SessionEventType::stopped, "stop emits stopped event");
}

void udp_stream_server_handles_datagram_sessions() {
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::UdpStreamServer server(
      eq2::net::UdpStreamConfig{
          .bind = {.address = "127.0.0.1", .port = 9100},
          .backpressure = {.max_queued_bytes = 8},
      },
      [&](eq2::net::SessionEvent event) { events.push_back(std::move(event)); });

  server.start();
  const auto stream = server.open_stream();
  require(stream.has_value(), "started UDP stream server opens stream sessions");

  const std::array<std::uint8_t, 2> inbound{0x00, 0x09};
  require(server.receive_datagram(*stream, inbound), "UDP stream receives datagrams");
  require_eq(events.back().transport, eq2::net::TransportKind::udp,
             "UDP receive event keeps transport kind");
  require_eq(events.back().type, eq2::net::SessionEventType::received,
             "UDP stream receive emits received event");

  const std::array<std::uint8_t, 3> outbound{0xaa, 0xbb, 0xcc};
  require(server.send_datagram(*stream, outbound), "UDP stream queues outbound datagram");
  const auto drained = server.drain_one_datagram(*stream);
  require(drained.has_value(), "UDP stream drains outbound datagram");
  require(*drained == std::vector<std::uint8_t>(outbound.begin(), outbound.end()),
          "UDP stream preserves outbound datagram bytes");
}

void real_tcp_transport_exchanges_loopback_bytes() {
  std::mutex mutex;
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::TcpSocketServer server([&](eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex);
    events.push_back(std::move(event));
  });

  require(server.start(), "real TCP transport starts on loopback");
  require(server.port() != 0, "real TCP transport binds a local port");

  const std::array<std::uint8_t, 3> bytes{0x10, 0x20, 0x30};
  require(eq2::net::send_tcp_loopback(server.port(), bytes),
          "real TCP transport accepts loopback client bytes");

  auto observed = false;
  for (auto attempt = 0; attempt < 50 && !observed; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard lock(mutex);
    for (const auto& event : events) {
      observed = observed || (event.type == eq2::net::SessionEventType::received &&
                              event.transport == eq2::net::TransportKind::tcp &&
                              event.bytes == std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    }
  }

  server.stop();
  require(observed, "real TCP transport emits received event with loopback bytes");
}

void real_tcp_transport_writes_loopback_response() {
  std::mutex mutex;
  std::vector<eq2::net::SessionEvent> events;
  auto write_succeeded = false;
  const std::array<std::uint8_t, 3> request{0x44, 0x55, 0x66};
  const std::array<std::uint8_t, 4> response{0x10, 0x20, 0x30, 0x40};

  eq2::net::TcpSocketServer* server_ref = nullptr;
  eq2::net::TcpSocketServer server([&](eq2::net::SessionEvent event) {
    if (event.type == eq2::net::SessionEventType::received &&
        event.bytes == std::vector<std::uint8_t>(request.begin(), request.end()) &&
        server_ref != nullptr) {
      const auto sent = server_ref->send(event.session, response);
      std::lock_guard lock(mutex);
      write_succeeded = sent;
      events.push_back(std::move(event));
      return;
    }

    std::lock_guard lock(mutex);
    events.push_back(std::move(event));
  });
  server_ref = &server;

  require(server.start(), "real TCP transport response test starts on loopback");
  const auto received = eq2::net::exchange_tcp_loopback(server.port(), request);
  server.stop();

  require(received.has_value(), "real TCP transport client receives response bytes");
  require(received == std::optional<std::vector<std::uint8_t>>(
                          std::vector<std::uint8_t>(response.begin(), response.end())),
          "real TCP transport writes exact response bytes");
  require(write_succeeded, "real TCP transport send reports success for active session");
}

void real_tcp_transport_accepts_loopback_when_bound_to_any_address() {
  std::mutex mutex;
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::TcpSocketServer server([&](eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex);
    events.push_back(std::move(event));
  });

  require(server.start(eq2::net::SocketEndpoint{.address = "0.0.0.0", .port = 0}),
          "real TCP transport binds configured any-address endpoint");
  require(server.port() != 0, "real TCP transport any-address endpoint receives bound port");

  const std::array<std::uint8_t, 2> bytes{0x77, 0x88};
  require(eq2::net::send_tcp_loopback(server.port(), bytes),
          "real TCP transport any-address endpoint accepts loopback client");

  auto observed = false;
  for (auto attempt = 0; attempt < 50 && !observed; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard lock(mutex);
    for (const auto& event : events) {
      observed = observed || (event.type == eq2::net::SessionEventType::received &&
                              event.transport == eq2::net::TransportKind::tcp &&
                              event.bytes == std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    }
  }

  server.stop();
  require(observed, "real TCP transport any-address endpoint emits received event");
}

void real_tcp_transport_processes_simultaneous_clients() {
  std::mutex mutex;
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::TcpSocketServer server([&](eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex);
    events.push_back(std::move(event));
  });

  require(server.start(), "real TCP transport concurrent test starts on loopback");
  require(server.port() != 0, "real TCP transport concurrent test binds a local port");

  eq2::net::TcpSocketClient first_client;
  eq2::net::TcpSocketClient second_client;
  require(first_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = server.port()}),
          "first concurrent loopback client connects");
  require(second_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = server.port()}),
          "second concurrent loopback client connects");

  const std::array<std::uint8_t, 2> first_bytes{0x21, 0x22};
  const std::array<std::uint8_t, 2> second_bytes{0x31, 0x32};
  require(first_client.send(first_bytes), "first concurrent loopback client sends bytes");
  require(second_client.send(second_bytes), "second concurrent loopback client sends bytes");

  auto observed_first = false;
  auto observed_second = false;
  for (auto attempt = 0; attempt < 100 && (!observed_first || !observed_second); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard lock(mutex);
    for (const auto& event : events) {
      observed_first = observed_first ||
                       (event.type == eq2::net::SessionEventType::received &&
                        event.bytes == std::vector<std::uint8_t>(first_bytes.begin(), first_bytes.end()));
      observed_second = observed_second ||
                        (event.type == eq2::net::SessionEventType::received &&
                         event.bytes == std::vector<std::uint8_t>(second_bytes.begin(), second_bytes.end()));
    }
  }

  first_client.close();
  second_client.close();
  server.stop();

  require(observed_first, "real TCP transport processes first concurrent client");
  require(observed_second, "real TCP transport processes second concurrent client");
}

void real_udp_transport_exchanges_loopback_datagrams() {
  std::mutex mutex;
  std::vector<eq2::net::SessionEvent> events;
  eq2::net::UdpSocketServer server([&](eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex);
    events.push_back(std::move(event));
  });

  require(server.start(), "real UDP transport starts on loopback");
  require(server.port() != 0, "real UDP transport binds a local port");

  const std::array<std::uint8_t, 2> bytes{0xaa, 0xbb};
  require(eq2::net::send_udp_loopback(server.port(), bytes),
          "real UDP transport accepts loopback datagrams");

  auto observed = false;
  for (auto attempt = 0; attempt < 50 && !observed; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard lock(mutex);
    for (const auto& event : events) {
      observed = observed || (event.type == eq2::net::SessionEventType::received &&
                              event.transport == eq2::net::TransportKind::udp &&
                              event.bytes == std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    }
  }

  server.stop();
  require(observed, "real UDP transport emits received event with loopback datagram");
}

void real_udp_transport_writes_loopback_response() {
  std::mutex mutex;
  std::vector<eq2::net::SessionEvent> events;
  auto write_succeeded = false;
  const std::array<std::uint8_t, 3> request{0x34, 0x45, 0x56};
  const std::array<std::uint8_t, 4> response{0xa0, 0xb0, 0xc0, 0xd0};

  eq2::net::UdpSocketServer* server_ref = nullptr;
  eq2::net::UdpSocketServer server([&](eq2::net::SessionEvent event) {
    if (event.type == eq2::net::SessionEventType::received &&
        event.bytes == std::vector<std::uint8_t>(request.begin(), request.end()) &&
        server_ref != nullptr) {
      const auto sent = server_ref->send(event.session, response);
      std::lock_guard lock(mutex);
      write_succeeded = sent;
      events.push_back(std::move(event));
      return;
    }

    std::lock_guard lock(mutex);
    events.push_back(std::move(event));
  });
  server_ref = &server;

  require(server.start(), "real UDP transport response test starts on loopback");
  const auto received = eq2::net::exchange_udp_loopback(server.port(), request);
  server.stop();

  require(received.has_value(), "real UDP transport client receives response bytes");
  require(received == std::optional<std::vector<std::uint8_t>>(
                          std::vector<std::uint8_t>(response.begin(), response.end())),
          "real UDP transport writes exact response bytes");
  require(write_succeeded, "real UDP transport send reports success for active remote session");
}

}  // namespace

int main() {
  tcp_server_emits_lifecycle_and_receive_events();
  write_queue_applies_backpressure_and_drains_fifo();
  tcp_server_stop_disconnects_sessions_and_rejects_new_accepts();
  udp_stream_server_handles_datagram_sessions();
  real_tcp_transport_exchanges_loopback_bytes();
  real_tcp_transport_writes_loopback_response();
  real_tcp_transport_accepts_loopback_when_bound_to_any_address();
  real_tcp_transport_processes_simultaneous_clients();
  real_udp_transport_exchanges_loopback_datagrams();
  real_udp_transport_writes_loopback_response();

  if (failures != 0) {
    std::cerr << failures << " net assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
