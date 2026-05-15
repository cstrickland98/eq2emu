#include <eq2/net/session.h>
#include <eq2/net/tcp_server.h>
#include <eq2/net/udp_stream.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
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

}  // namespace

int main() {
  tcp_server_emits_lifecycle_and_receive_events();
  write_queue_applies_backpressure_and_drains_fifo();
  tcp_server_stop_disconnects_sessions_and_rejects_new_accepts();
  udp_stream_server_handles_datagram_sessions();

  if (failures != 0) {
    std::cerr << failures << " net assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
