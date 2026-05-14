#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/opcode_table.h>
#include <eq2/protocol/opcode_version.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_header.h>
#include <eq2/protocol/packet_registry.h>
#include <eq2/protocol/packet_transform.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

#include <algorithm>
#include <array>
#include <cstdint>
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
    std::cerr << "FAIL: " << message << '\n';
  }
}

auto span_to_vector(std::span<const std::uint8_t> bytes) -> std::vector<std::uint8_t> {
  return {bytes.begin(), bytes.end()};
}

void packet_reader_and_writer_handle_bounds() {
  eq2::protocol::PacketWriter writer;
  writer.append_u8(0xaa);
  writer.append_u16_be(0x1234);
  writer.append_u16_le(0x5678);
  writer.append_u32_be(0x90abcdef);
  writer.append_u32_le(0x10203040);

  const std::vector<std::uint8_t> expected{
      0xaa,
      0x12, 0x34,
      0x78, 0x56,
      0x90, 0xab, 0xcd, 0xef,
      0x40, 0x30, 0x20, 0x10,
  };
  require(span_to_vector(writer.bytes()) == expected, "packet writer preserves explicit byte order");

  eq2::protocol::PacketReader reader(writer.bytes());
  require_eq(reader.read_u8().value_or(0), static_cast<std::uint8_t>(0xaa), "reader reads u8");
  require_eq(reader.read_u16_be().value_or(0), static_cast<std::uint16_t>(0x1234), "reader reads big-endian u16");
  require_eq(reader.read_u16_le().value_or(0), static_cast<std::uint16_t>(0x5678), "reader reads little-endian u16");
  require_eq(reader.read_u32_be().value_or(0), static_cast<std::uint32_t>(0x90abcdef),
             "reader reads big-endian u32");
  require_eq(reader.read_u32_le().value_or(0), static_cast<std::uint32_t>(0x10203040),
             "reader reads little-endian u32");
  require(!reader.read_u8().has_value(), "reader returns empty optional past end");
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
  require_eq(decoded->format, response.format, "session response format round trip");
  require_eq(decoded->max_length, response.max_length, "session response max_length round trip");
}

void protocol_packet_headers_match_legacy_wire_order() {
  const std::array<std::uint8_t, 3> payload{0xaa, 0xbb, 0xcc};
  const auto bytes = eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, payload);
  const std::vector<std::uint8_t> expected{0x00, 0x09, 0xaa, 0xbb, 0xcc};

  require(bytes == expected, "protocol packet prefixes the big-endian protocol opcode");

  eq2::protocol::PacketReader reader(bytes);
  const auto header = eq2::protocol::read_protocol_header(reader);
  require(header.has_value(), "protocol header reads from packet bytes");
  require_eq(header->opcode, eq2::protocol::kOpPacket, "protocol header opcode round trip");
  require_eq(reader.consumed(), eq2::protocol::kProtocolHeaderSize, "protocol reader consumes header size");

  const auto decoded = eq2::protocol::decode_protocol_packet(bytes);
  require(decoded.has_value(), "protocol packet decodes from full buffer");
  require_eq(decoded->opcode, eq2::protocol::kOpPacket, "protocol opcode round trip");
  require(span_to_vector(decoded->payload) == span_to_vector(payload), "protocol payload bytes round trip");
  require(eq2::protocol::is_protocol_packet_opcode(eq2::protocol::kOpSessionRequest),
          "session request is classified as a protocol packet");
  require(!eq2::protocol::is_protocol_packet_opcode(eq2::protocol::kOpAppCombined),
          "app combined is not classified as a top-level protocol packet");
}

void application_packet_headers_preserve_legacy_opcode_forms() {
  const std::array<std::uint8_t, 2> payload{0xde, 0xad};

  const auto two_byte = eq2::protocol::encode_application_packet(0x1234, payload);
  const std::vector<std::uint8_t> expected_two_byte{0x34, 0x12, 0xde, 0xad};
  require(two_byte == expected_two_byte, "two-byte application opcodes serialize little-endian");

  const auto padded = eq2::protocol::encode_application_packet(0x4700, payload);
  const std::vector<std::uint8_t> expected_padded{0x00, 0x00, 0x47, 0xde, 0xad};
  require(padded == expected_padded, "application opcodes ending in zero keep legacy padding byte");

  const auto one_byte =
      eq2::protocol::encode_application_packet(0x34, payload, eq2::protocol::ApplicationOpcodeWidth::one_byte);
  const std::vector<std::uint8_t> expected_one_byte{0x34, 0xde, 0xad};
  require(one_byte == expected_one_byte, "one-byte application opcodes serialize without padding");

  const auto decoded = eq2::protocol::decode_application_packet(padded);
  require(decoded.has_value(), "padded application packet decodes");
  require_eq(decoded->opcode, static_cast<std::uint16_t>(0x4700), "padded application opcode round trip");
  require_eq(decoded->header_size, static_cast<std::size_t>(3), "padded application header reports full size");
  require(span_to_vector(decoded->payload) == span_to_vector(payload), "padded application payload round trip");
}

void opcode_tables_and_version_ranges_match_legacy_lookup() {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  ranges.add_range(1096, 1199);

  require_eq(ranges.opcode_version_for(550), static_cast<std::int16_t>(546), "range middle maps to range key");
  require_eq(ranges.opcode_version_for(1208), static_cast<std::int16_t>(1208), "unknown version maps to itself");
  require(ranges.contains_client_version(1096), "explicit range key is available");
  require(!ranges.contains_client_version(1208), "missing range is unavailable");

  eq2::protocol::OpcodeTable table;
  table.define_emu_opcode(1, "OP_LoginRequestMsg");
  table.define_emu_opcode(2, "OP_PlayCharacterRequestMsg");

  table.load_mappings({
      {"OP_LoginRequestMsg", static_cast<std::uint16_t>(0x1234)},
  });

  require_eq(table.emu_to_eq(1), static_cast<std::uint16_t>(0x1234), "defined opcode maps emu to eq");
  require_eq(table.eq_to_emu(0x1234), static_cast<std::uint16_t>(1), "defined opcode maps eq to emu");
  require_eq(table.emu_to_eq(2), eq2::protocol::kMissingEqOpcode, "missing opcode is marked as 0xffff");

  table.set_opcode(1, 0x5678);
  require_eq(table.eq_to_emu(0x1234), eq2::protocol::kUnknownEmuOpcode, "set opcode clears old reverse mapping");
}

void versioned_packet_registry_resolves_client_versions() {
  eq2::protocol::VersionedPacketRegistry registry;
  registry.define_packet(1, "OP_LoginRequestMsg");
  registry.add_opcode_range(546, 561);
  registry.register_version(546, {
                                  {"OP_LoginRequestMsg", static_cast<std::uint16_t>(0x1234)},
                              });

  const auto eq_opcode = registry.emu_to_eq(550, 1);
  require(eq_opcode.has_value(), "registry resolves emu opcode through client-version range");
  require_eq(*eq_opcode, static_cast<std::uint16_t>(0x1234), "registry returns mapped eq opcode");

  const auto emu_opcode = registry.eq_to_emu(550, 0x1234);
  require(emu_opcode.has_value(), "registry resolves eq opcode through client-version range");
  require_eq(*emu_opcode, static_cast<std::uint16_t>(1), "registry returns mapped emu opcode");

  require(!registry.emu_to_eq(1208, 1).has_value(), "registry reports no table for unregistered version");

  const auto definition = registry.packet_definition(1, 1208);
  require(definition.has_value(), "registry returns packet definition metadata");
  require_eq(definition->name, std::string_view("OP_LoginRequestMsg"), "registry preserves packet name");
  require_eq(definition->struct_version, static_cast<std::int16_t>(1208), "registry carries struct version");
}

void combined_packet_framing_preserves_legacy_length_prefixes() {
  const auto first =
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpAck, std::array<std::uint8_t, 2>{0x10, 0x20});
  std::vector<std::uint8_t> large_payload(253, 0xab);
  const auto second = eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, large_payload);
  const std::array<std::span<const std::uint8_t>, 2> subpackets{
      std::span<const std::uint8_t>(first),
      std::span<const std::uint8_t>(second),
  };

  const auto combined = eq2::protocol::encode_combined_packet(subpackets);
  require(combined.has_value(), "combined packet encodes subpackets");
  require_eq((*combined)[0], static_cast<std::uint8_t>(first.size()), "small subpacket uses one-byte length");
  require_eq((*combined)[first.size() + 1], eq2::protocol::kExtendedCombinedPacketLength,
             "large subpacket uses extended length marker");
  require_eq((*combined)[first.size() + 2], static_cast<std::uint8_t>(0x00),
             "extended length stores high byte first");
  require_eq((*combined)[first.size() + 3], static_cast<std::uint8_t>(second.size()),
             "extended length stores low byte second");

  const auto decoded = eq2::protocol::decode_combined_packet(*combined);
  require(decoded.has_value(), "combined packet decodes");
  require_eq(decoded->size(), static_cast<std::size_t>(2), "combined packet exposes both subpackets");
  require_eq((*decoded)[0].length_prefix_size, static_cast<std::size_t>(1), "small prefix size is reported");
  require_eq((*decoded)[1].length_prefix_size, static_cast<std::size_t>(3), "extended prefix size is reported");
  require(span_to_vector((*decoded)[0].bytes) == first, "first subpacket bytes round trip");
  require(span_to_vector((*decoded)[1].bytes) == second, "second subpacket bytes round trip");

  const std::array<std::uint8_t, 4> truncated{0x04, 0x00, 0x09, 0xaa};
  require(!eq2::protocol::decode_combined_packet(truncated).has_value(),
          "combined packet rejects truncated subpacket data");
}

void transform_policy_marks_compression_encryption_boundaries() {
  const auto format = eq2::protocol::session_response_format(true, true);
  const auto policy = eq2::protocol::packet_transform_policy(format);

  require(policy.compressed, "compressed session format enables compression policy");
  require(policy.encrypted, "encoded session format enables encryption policy");
  require(policy.compression_boundary == eq2::protocol::PacketTransformBoundary::transport_payload,
          "compression boundary is the transport payload");
  require(policy.encryption_boundary == eq2::protocol::PacketTransformBoundary::transport_payload,
          "encryption boundary is the transport payload");
  require(!eq2::protocol::protocol_packet_uses_crc(eq2::protocol::kOpSessionRequest),
          "session request is outside the CRC boundary");
  require(eq2::protocol::protocol_packet_uses_crc(eq2::protocol::kOpPacket),
          "sequenced packet stays inside the CRC boundary");

  const auto session_request = eq2::protocol::encode_protocol_packet(eq2::protocol::kOpSessionRequest, {});
  const auto session_crc = eq2::protocol::packet_crc_policy(session_request);
  require(session_crc.has_value(), "session request has crc policy");
  require(!session_crc->requires_crc, "session request bypasses crc");

  const auto packet = eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, std::array<std::uint8_t, 1>{0});
  const auto packet_crc = eq2::protocol::packet_crc_policy(packet);
  require(packet_crc.has_value(), "sequenced packet has crc policy");
  require(packet_crc->requires_crc, "sequenced packet requires crc");

  const std::array<std::uint8_t, 5> legacy_app_combined{
      0x00,
      eq2::protocol::kOpAppCombined,
      0x00,
      eq2::protocol::kOpAppCombined,
      0x01,
  };
  const auto app_combined_crc = eq2::protocol::packet_crc_policy(legacy_app_combined);
  require(app_combined_crc.has_value(), "legacy app-combined packet has crc policy");
  require(app_combined_crc->legacy_app_combined_bypass, "legacy app-combined bypass is identified");
  require(!app_combined_crc->requires_crc, "legacy app-combined packet bypasses crc");
}

}  // namespace

int main() {
  packet_reader_and_writer_handle_bounds();
  session_request_round_trips_legacy_wire_order();
  session_response_round_trips_legacy_wire_order();
  protocol_packet_headers_match_legacy_wire_order();
  application_packet_headers_preserve_legacy_opcode_forms();
  opcode_tables_and_version_ranges_match_legacy_lookup();
  versioned_packet_registry_resolves_client_versions();
  combined_packet_framing_preserves_legacy_length_prefixes();
  transform_policy_marks_compression_encryption_boundaries();

  if (failures != 0) {
    std::cerr << failures << " protocol assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
