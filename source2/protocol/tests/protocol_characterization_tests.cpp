#include <eq2/login/login_request_version.h>
#include <eq2/protocol/opcode_table.h>
#include <eq2/protocol/opcode_version.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
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
    std::cerr << "FAIL: " << message << " expected=" << expected << " actual=" << actual << '\n';
  }
}

void session_request_round_trips_legacy_wire_order() {
  const eq2::protocol::SessionRequest request{
      .unknown_a = 0,
      .session = 0x01020304,
      .max_length = 512,
  };

  const auto bytes = eq2::protocol::encode_session_request(request);
  const std::array<std::uint8_t, eq2::protocol::kSessionRequestSize> expected{
      0x00, 0x00, 0x00, 0x00,
      0x01, 0x02, 0x03, 0x04,
      0x00, 0x00, 0x02, 0x00,
  };

  require(bytes == expected, "session request uses legacy big-endian fields");

  const auto decoded = eq2::protocol::decode_session_request(bytes);
  require(decoded.has_value(), "session request decodes from full buffer");
  require_eq(decoded->unknown_a, request.unknown_a, "session request unknown_a round trip");
  require_eq(decoded->session, request.session, "session request session round trip");
  require_eq(decoded->max_length, request.max_length, "session request max_length round trip");
}

void session_response_round_trips_legacy_wire_order() {
  const eq2::protocol::SessionResponse response{
      .session = 0x10203040,
      .key = 0x33624702,
      .unknown_a = 2,
      .format = eq2::protocol::session_response_format(true, false),
      .unknown_b = 0,
      .max_length = 512,
      .unknown_d = 0,
  };

  const auto bytes = eq2::protocol::encode_session_response(response);
  const std::array<std::uint8_t, eq2::protocol::kSessionResponseSize> expected{
      0x10, 0x20, 0x30, 0x40,
      0x33, 0x62, 0x47, 0x02,
      0x02,
      0x01,
      0x00,
      0x00, 0x00, 0x02, 0x00,
      0x00, 0x00, 0x00, 0x00,
  };

  require(bytes == expected, "session response uses legacy big-endian fields and packed size");

  const auto decoded = eq2::protocol::decode_session_response(bytes);
  require(decoded.has_value(), "session response decodes from full buffer");
  require_eq(decoded->session, response.session, "session response session round trip");
  require_eq(decoded->key, response.key, "session response key round trip");
  require_eq(decoded->unknown_a, response.unknown_a, "session response unknown_a round trip");
  require_eq(decoded->format, response.format, "session response format round trip");
  require_eq(decoded->max_length, response.max_length, "session response max_length round trip");
}

void protocol_packet_opcode_prefix_matches_legacy_protocol_packets() {
  const std::array<std::uint8_t, 3> payload{0xaa, 0xbb, 0xcc};
  const auto bytes = eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, payload);
  const std::vector<std::uint8_t> expected{0x00, 0x09, 0xaa, 0xbb, 0xcc};

  require(bytes == expected, "single-byte protocol opcode serializes with leading zero byte");

  const auto decoded = eq2::protocol::decode_protocol_packet(bytes);
  require(decoded.has_value(), "protocol packet decodes from full buffer");
  require_eq(decoded->opcode, eq2::protocol::kOpPacket, "protocol opcode round trip");
  require(decoded->payload.size() == payload.size(), "protocol payload size round trip");
  require(std::equal(decoded->payload.begin(), decoded->payload.end(), payload.begin()),
          "protocol payload bytes round trip");
  require(eq2::protocol::is_protocol_packet_opcode(eq2::protocol::kOpSessionRequest),
          "session request is classified as a protocol packet");
  require(eq2::protocol::is_protocol_packet_opcode(eq2::protocol::kOpSessionDisconnect),
          "session disconnect is classified as a protocol packet");
  require(eq2::protocol::is_protocol_packet_opcode(eq2::protocol::kOpSessionStatResponse),
          "session stats are classified as a protocol packet");
  require(eq2::protocol::is_protocol_packet_opcode(eq2::protocol::kOpOutOfSession),
          "out-of-session is classified as a protocol packet");
  require(!eq2::protocol::is_protocol_packet_opcode(eq2::protocol::kOpAppCombined),
          "app combined is not classified as a top-level protocol packet");
}

void opcode_version_ranges_match_legacy_lookup() {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  ranges.add_range(1096, 1199);

  require_eq(ranges.opcode_version_for(546), 546, "range lower bound maps to range key");
  require_eq(ranges.opcode_version_for(550), 546, "range middle maps to range key");
  require_eq(ranges.opcode_version_for(561), 546, "range upper bound maps to range key");
  require_eq(ranges.opcode_version_for(1208), 1208, "unknown version maps to itself");
  require(ranges.contains_client_version(1096), "explicit range key is available");
  require(!ranges.contains_client_version(1208), "missing range is unavailable");
}

void opcode_table_preserves_missing_and_replacement_behavior() {
  eq2::protocol::OpcodeTable table;
  table.define_emu_opcode(1, "OP_LoginRequestMsg");
  table.define_emu_opcode(2, "OP_PlayCharacterRequestMsg");

  table.load_mappings({
      {"OP_LoginRequestMsg", static_cast<std::uint16_t>(0x1234)},
  });

  require_eq(table.emu_to_eq(1), 0x1234, "defined opcode maps emu to eq");
  require_eq(table.eq_to_emu(0x1234), 1, "defined opcode maps eq to emu");
  require_eq(table.emu_to_eq(2), eq2::protocol::kMissingEqOpcode, "missing opcode is marked as 0xffff");
  require_eq(table.eq_to_emu(0xbeef), eq2::protocol::kUnknownEmuOpcode, "unknown eq opcode maps to unknown");

  table.set_opcode(1, 0x5678);
  require_eq(table.emu_to_eq(1), 0x5678, "set opcode replaces emu mapping");
  require_eq(table.eq_to_emu(0x5678), 1, "set opcode creates new reverse mapping");
  require_eq(table.eq_to_emu(0x1234), eq2::protocol::kUnknownEmuOpcode, "set opcode clears old reverse mapping");
}

void login_and_world_version_fallbacks_match_phase1_inventory() {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  ranges.add_range(1208, 1208);

  require_eq(eq2::login::login_request_struct_version(546, ranges), 1,
             "login keeps legacy request struct for known classic version");
  require_eq(eq2::login::login_request_struct_version(0, ranges), 1208,
             "login retries extended request struct for zero classic version");
  require_eq(eq2::login::login_request_struct_version(999, ranges), 1208,
             "login retries extended request struct for unknown classic version");

  require_eq(eq2::login::world_login_request_struct_version(546, ranges), 1,
             "world keeps legacy request struct for known pre-1208 version");
  require_eq(eq2::login::world_login_request_struct_version(1208, ranges), 1208,
             "world retries extended request struct for 1208 and newer");
  require_eq(eq2::login::world_login_request_struct_version(999, ranges), 1208,
             "world retries extended request struct for unknown classic version");
}

}  // namespace

int main() {
  session_request_round_trips_legacy_wire_order();
  session_response_round_trips_legacy_wire_order();
  protocol_packet_opcode_prefix_matches_legacy_protocol_packets();
  opcode_version_ranges_match_legacy_lookup();
  opcode_table_preserves_missing_and_replacement_behavior();
  login_and_world_version_fallbacks_match_phase1_inventory();

  if (failures != 0) {
    std::cerr << failures << " characterization assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
