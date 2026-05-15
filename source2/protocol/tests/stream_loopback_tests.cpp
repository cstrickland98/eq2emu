#include <eq2/net/socket_transport.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>
#include <eq2/protocol/stream_pipeline.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
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

void udp_loopback_bytes_flow_through_stream_pipeline() {
  std::mutex mutex;
  std::vector<eq2::net::SessionEvent> net_events;
  eq2::net::UdpSocketServer server([&](eq2::net::SessionEvent event) {
    std::lock_guard lock(mutex);
    net_events.push_back(std::move(event));
  });
  require(server.start(), "UDP loopback server starts");

  const auto request =
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpSessionRequest,
                                            eq2::protocol::encode_session_request(
                                                eq2::protocol::SessionRequest{
                                                    .session = 0x11223344,
                                                    .max_length = 512,
                                                }));
  require(eq2::net::send_udp_loopback(server.port(), request),
          "session request bytes send over UDP loopback");

  std::vector<std::uint8_t> received;
  for (auto attempt = 0; attempt < 50 && received.empty(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::lock_guard lock(mutex);
    for (const auto& event : net_events) {
      if (event.type == eq2::net::SessionEventType::received) {
        received = event.bytes;
      }
    }
  }
  server.stop();

  require(!received.empty(), "UDP loopback server receives session request bytes");

  eq2::protocol::StreamPipeline pipeline;
  const auto result = pipeline.receive_datagram(received);
  require_eq(result.events.size(), static_cast<std::size_t>(1),
             "stream pipeline handles net-received bytes");
  require_eq(result.events.front().type, eq2::protocol::StreamEventType::session_requested,
             "stream pipeline decodes UDP loopback session request");
  require_eq(result.outbound.size(), static_cast<std::size_t>(1),
             "stream pipeline creates outbound session response from loopback request");
}

}  // namespace

int main() {
  udp_loopback_bytes_flow_through_stream_pipeline();

  if (failures != 0) {
    std::cerr << failures << " stream loopback assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
