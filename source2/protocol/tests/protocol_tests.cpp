#include <eq2/core/endian.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/client_log.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/create_character.h>
#include <eq2/protocol/delete_character.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/login_response.h>
#include <eq2/protocol/login_world.h>
#include <eq2/protocol/opcode_table.h>
#include <eq2/protocol/opcode_version.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_fields.h>
#include <eq2/protocol/packet_header.h>
#include <eq2/protocol/packet_registry.h>
#include <eq2/protocol/packet_transform.h>
#include <eq2/protocol/play_character.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>
#include <eq2/protocol/stream_pipeline.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

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

auto hex_nibble(char value) -> std::uint8_t {
  if (value >= '0' && value <= '9') {
    return static_cast<std::uint8_t>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<std::uint8_t>(10 + value - 'a');
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<std::uint8_t>(10 + value - 'A');
  }
  return 0;
}

auto bytes_from_hex(std::string_view hex) -> std::vector<std::uint8_t> {
  auto bytes = std::vector<std::uint8_t>{};
  bytes.reserve(hex.size() / 2U);
  for (std::size_t index = 0; index + 1U < hex.size(); index += 2U) {
    bytes.push_back(static_cast<std::uint8_t>(
        (hex_nibble(hex[index]) << 4U) | hex_nibble(hex[index + 1U])));
  }
  return bytes;
}

auto find_ascii_offset(const std::vector<std::uint8_t>& bytes, std::string_view needle)
    -> std::size_t {
  const auto iter = std::search(
      bytes.begin(), bytes.end(), needle.begin(), needle.end(),
      [](std::uint8_t lhs, char rhs) { return lhs == static_cast<std::uint8_t>(rhs); });
  return iter == bytes.end() ? bytes.size()
                             : static_cast<std::size_t>(std::distance(bytes.begin(), iter));
}

class TestLegacyRc4 {
 public:
  explicit TestLegacyRc4(std::uint64_t key) {
    for (std::size_t i = 0; i < state_.size(); ++i) {
      state_[i] = static_cast<std::uint8_t>(i);
    }

    auto key_bytes = std::array<std::uint8_t, 8>{};
    for (std::size_t i = 0; i < key_bytes.size(); ++i) {
      key_bytes[i] = static_cast<std::uint8_t>((key >> (i * 8U)) & 0xffU);
    }

    std::size_t key_index = 0;
    std::size_t state_index = 0;
    for (std::size_t i = 0; i < state_.size(); ++i) {
      const auto temp = state_[i];
      state_index = (state_index + key_bytes[key_index] + temp) & 0xffU;
      state_[i] = state_[state_index];
      state_[state_index] = temp;
      key_index = (key_index + 1U) & 7U;
    }
  }

  void cypher(std::span<std::uint8_t> bytes) {
    for (auto& byte : bytes) {
      ++x_;
      const auto key_val_1 = state_[x_];
      y_ = static_cast<std::uint8_t>(y_ + key_val_1);
      const auto key_val_2 = state_[y_];
      state_[x_] = key_val_2;
      state_[y_] = key_val_1;
      byte ^= state_[(key_val_1 + key_val_2) & 0xffU];
    }
  }

 private:
  std::array<std::uint8_t, 256> state_{};
  std::uint8_t x_ = 0;
  std::uint8_t y_ = 0;
};

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

void packet_field_helpers_handle_eq2_length_prefixed_strings() {
  eq2::protocol::PacketWriter writer;
  eq2::protocol::append_eq2_8bit_string(writer, "world");
  eq2::protocol::append_eq2_16bit_string(writer, "character");

  const std::vector<std::uint8_t> expected{
      0x05,
      'w', 'o', 'r', 'l', 'd',
      0x09, 0x00,
      'c', 'h', 'a', 'r', 'a', 'c', 't', 'e', 'r',
  };
  require(span_to_vector(writer.bytes()) == expected,
          "EQ2 string helpers write one-byte and two-byte length prefixes");

  eq2::protocol::PacketReader reader(writer.bytes());
  const auto small = eq2::protocol::read_eq2_8bit_string(reader);
  const auto medium = eq2::protocol::read_eq2_16bit_string(reader);
  require(small.has_value(), "EQ2 8-bit string reads");
  require(medium.has_value(), "EQ2 16-bit string reads");
  require_eq(*small, std::string("world"), "EQ2 8-bit string round trips");
  require_eq(*medium, std::string("character"), "EQ2 16-bit string round trips");
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
      .unknown_d = 2,
  };

  const auto bytes = eq2::protocol::encode_session_response(response);
  const std::array<std::uint8_t, eq2::protocol::kSessionResponseSize> expected{
      0x10, 0x20, 0x30, 0x40,
      0x33, 0x62, 0x47, 0x02,
      0x02,
      0x01,
      0x00,
      0x00, 0x00, 0x02, 0x00,
      0x00, 0x00, 0x00, 0x02,
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

  const auto packed_small =
      eq2::protocol::encode_application_packet(0x04, payload, eq2::protocol::ApplicationOpcodeWidth::packed_u16);
  const std::vector<std::uint8_t> expected_packed_small{0x04, 0xde, 0xad};
  require(packed_small == expected_packed_small,
          "packed application opcodes below 0xff serialize as one byte");

  const auto packed_large =
      eq2::protocol::encode_application_packet(0x019a, payload, eq2::protocol::ApplicationOpcodeWidth::packed_u16);
  const std::vector<std::uint8_t> expected_packed_large{0xff, 0x9a, 0x01, 0xde, 0xad};
  require(packed_large == expected_packed_large,
          "packed application opcodes at 0xff or above serialize with marker and u16le");
  const auto decoded_packed_large =
      eq2::protocol::decode_application_packet(packed_large,
                                               eq2::protocol::ApplicationOpcodeWidth::packed_u16);
  require(decoded_packed_large.has_value(), "packed large application opcode decodes");
  if (decoded_packed_large.has_value()) {
    require_eq(decoded_packed_large->opcode, static_cast<std::uint16_t>(0x019a),
               "packed large application opcode round trips");
    require_eq(decoded_packed_large->header_size, static_cast<std::size_t>(3),
               "packed large application header reports marker size");
  }

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

  const std::vector<std::uint8_t> boundary_payload(0xfe, 0xcd);
  const std::array<std::span<const std::uint8_t>, 1> boundary_subpackets{
      std::span<const std::uint8_t>(boundary_payload),
  };
  const auto boundary = eq2::protocol::encode_combined_packet(boundary_subpackets);
  require(boundary.has_value(), "combined packet encodes 0xfe-byte subpacket");
  require_eq((*boundary)[0], eq2::protocol::kExtendedCombinedPacketLength,
             "0xfe-byte subpacket uses extended marker");
  require_eq((*boundary)[1], static_cast<std::uint8_t>(0x00),
             "0xfe-byte subpacket stores high length byte");
  require_eq((*boundary)[2], static_cast<std::uint8_t>(0xfe),
             "0xfe-byte subpacket stores low length byte");
  const auto decoded_boundary = boundary.has_value()
                                    ? eq2::protocol::decode_combined_packet(*boundary)
                                    : std::nullopt;
  require(decoded_boundary.has_value(), "0xfe-byte combined subpacket decodes");
  if (decoded_boundary.has_value()) {
    require_eq((*decoded_boundary)[0].length_prefix_size, static_cast<std::size_t>(3),
               "0xfe-byte combined subpacket reports three-byte prefix");
    require(span_to_vector((*decoded_boundary)[0].bytes) == boundary_payload,
            "0xfe-byte combined subpacket payload round trips");
  }

  const std::vector<std::uint8_t> long_payload(0xffff, 0xef);
  const std::array<std::span<const std::uint8_t>, 1> long_subpackets{
      std::span<const std::uint8_t>(long_payload),
  };
  const auto long_combined = eq2::protocol::encode_combined_packet(long_subpackets);
  require(long_combined.has_value(), "combined packet encodes 0xffff-byte subpacket");
  require_eq((*long_combined)[0], eq2::protocol::kExtendedCombinedPacketLength,
             "0xffff-byte subpacket uses extended marker byte 0");
  require_eq((*long_combined)[1], eq2::protocol::kExtendedCombinedPacketLength,
             "0xffff-byte subpacket uses extended marker byte 1");
  require_eq((*long_combined)[2], eq2::protocol::kExtendedCombinedPacketLength,
             "0xffff-byte subpacket uses extended marker byte 2");
  require_eq((*long_combined)[5], static_cast<std::uint8_t>(0xff),
             "0xffff-byte subpacket stores u32 high payload byte");
  require_eq((*long_combined)[6], static_cast<std::uint8_t>(0xff),
             "0xffff-byte subpacket stores u32 low payload byte");
  const auto decoded_long = long_combined.has_value()
                                ? eq2::protocol::decode_combined_packet(*long_combined)
                                : std::nullopt;
  require(decoded_long.has_value(), "0xffff-byte combined subpacket decodes");
  if (decoded_long.has_value()) {
    require_eq((*decoded_long)[0].length_prefix_size, static_cast<std::size_t>(7),
               "0xffff-byte combined subpacket reports seven-byte prefix");
    require(span_to_vector((*decoded_long)[0].bytes) == long_payload,
            "0xffff-byte combined subpacket payload round trips");
  }

  const std::array<std::uint8_t, 6> truncated_long{0xff, 0xff, 0xff, 0x00, 0x00, 0x01};
  require(!eq2::protocol::decode_combined_packet(truncated_long).has_value(),
          "combined packet rejects truncated seven-byte length");
}

void outbound_protocol_datagrams_can_coalesce_like_legacy_stream_writer() {
  const auto ack = eq2::protocol::append_legacy_crc(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpAck,
                                            std::array<std::uint8_t, 2>{0x00, 0x02}),
      eq2::protocol::kDefaultSessionKey);
  const auto app = eq2::protocol::encode_application_packet(
      0x1235, std::array<std::uint8_t, 5>{0x01, 0x00, 0x00, 0x00, 0x2a});
  eq2::protocol::PacketWriter sequenced;
  sequenced.append_u16_be(0);
  sequenced.append_bytes(app);
  const auto login_reply = eq2::protocol::append_legacy_crc(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, sequenced.bytes()),
      eq2::protocol::kDefaultSessionKey);
  const std::array<std::vector<std::uint8_t>, 2> packets{ack, login_reply};

  const auto combined = eq2::protocol::coalesce_legacy_protocol_datagrams(packets);
  require(combined.has_value(), "small CRC-protected datagrams coalesce");

  const auto stripped =
      eq2::protocol::strip_legacy_crc_if_present(*combined, eq2::protocol::kDefaultSessionKey);
  const auto protocol = eq2::protocol::decode_protocol_packet(stripped);
  require(protocol.has_value() && protocol->opcode == eq2::protocol::kOpCombined,
          "coalesced datagram uses OP_Combined");

  const auto subpackets = protocol.has_value()
                              ? eq2::protocol::decode_combined_packet(protocol->payload)
                              : std::nullopt;
  require(subpackets.has_value(), "coalesced datagram decodes subpackets");
  if (subpackets.has_value()) {
    const auto expected_ack = std::vector<std::uint8_t>(ack.begin(), ack.end() - 2);
    const auto expected_login_reply =
        std::vector<std::uint8_t>(login_reply.begin(), login_reply.end() - 2);
    require_eq(subpackets->size(), static_cast<std::size_t>(2),
               "coalesced datagram preserves packet count");
    require(span_to_vector((*subpackets)[0].bytes) == expected_ack,
            "coalesced datagram strips first packet CRC");
    require(span_to_vector((*subpackets)[1].bytes) == expected_login_reply,
            "coalesced datagram strips second packet CRC");
  }

  std::vector<std::uint8_t> large_payload(250, 0xee);
  const auto large = eq2::protocol::append_legacy_crc(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, large_payload),
      eq2::protocol::kDefaultSessionKey);
  const std::array<std::vector<std::uint8_t>, 2> oversized{login_reply, large};
  require(!eq2::protocol::coalesce_legacy_protocol_datagrams(oversized).has_value(),
          "oversized datagrams remain separate like the legacy writer");
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

void interserver_packet_framing_matches_legacy_tcp_server_packets() {
  const std::array<std::uint8_t, 3> payload{0xaa, 0xbb, 0xcc};
  const auto bytes = eq2::protocol::encode_interserver_packet(eq2::protocol::kServerOpLsInfo, payload);
  const std::vector<std::uint8_t> expected{
      0x0a, 0x00, 0x00, 0x00,
      0x00,
      0x00, 0x10,
      0xaa, 0xbb, 0xcc,
  };

  require(bytes.has_value(), "interserver packet encodes");
  require(*bytes == expected, "interserver packet stores size/opcode in legacy little-endian layout");

  const auto decoded = eq2::protocol::decode_interserver_packet(*bytes);
  require(decoded.has_value(), "interserver packet decodes");
  require_eq(decoded->opcode, eq2::protocol::kServerOpLsInfo, "interserver opcode round trip");
  require(!decoded->compressed, "interserver packet defaults to uncompressed");
  require(!decoded->destination.has_value(), "interserver packet defaults to no relay destination");
  require(span_to_vector(decoded->payload) == span_to_vector(payload), "interserver payload round trip");

  const eq2::protocol::InterserverPacketEncodeOptions options{
      .compressed = true,
      .inflated_size = 512,
      .destination = 77,
  };
  const auto relayed = eq2::protocol::encode_interserver_packet(0x2000, payload, options);
  require(relayed.has_value(), "interserver packet encodes compressed relay metadata");

  const auto decoded_relay = eq2::protocol::decode_interserver_packet(*relayed);
  require(decoded_relay.has_value(), "interserver packet decodes compressed relay metadata");
  require(decoded_relay->compressed, "interserver compressed flag round trips");
  require_eq(decoded_relay->inflated_size, static_cast<std::uint32_t>(512),
             "interserver inflated size round trips");
  require(decoded_relay->destination.has_value(), "interserver destination flag round trips");
  require_eq(*decoded_relay->destination, static_cast<std::int32_t>(77),
             "interserver destination id round trips");

  const std::array<std::uint8_t, 5> inflated_payload{1, 2, 3, 4, 5};
  auto compressed_payload = std::vector<std::uint8_t>(
      compressBound(static_cast<uLong>(inflated_payload.size())));
  auto compressed_size = static_cast<uLongf>(compressed_payload.size());
  require(compress2(compressed_payload.data(),
                    &compressed_size,
                    inflated_payload.data(),
                    static_cast<uLong>(inflated_payload.size()),
                    Z_BEST_SPEED) == Z_OK,
          "interserver test compresses a payload");
  compressed_payload.resize(static_cast<std::size_t>(compressed_size));
  const auto compressed_frame = eq2::protocol::encode_interserver_packet(
      0x2000,
      compressed_payload,
      eq2::protocol::InterserverPacketEncodeOptions{
          .compressed = true,
          .inflated_size = static_cast<std::uint32_t>(inflated_payload.size()),
      });
  require(compressed_frame.has_value(), "interserver compressed payload frame encodes");
  const auto decoded_compressed = eq2::protocol::decode_interserver_packet(*compressed_frame);
  const auto reinflated = decoded_compressed.has_value()
                              ? eq2::protocol::inflate_interserver_payload(*decoded_compressed)
                              : std::nullopt;
  require(reinflated.has_value() &&
              *reinflated == std::vector<std::uint8_t>(inflated_payload.begin(),
                                                       inflated_payload.end()),
          "interserver compressed payload inflates like the legacy TCP layer");

  auto truncated = *bytes;
  truncated.pop_back();
  require(!eq2::protocol::decode_interserver_packet(truncated).has_value(),
          "interserver packet rejects size mismatch");
}

void server_ls_info_payload_decodes_fixed_legacy_fields() {
  eq2::protocol::PacketWriter writer;
  const auto append_fixed = [&writer](std::string_view value, std::size_t size) {
    std::vector<std::uint8_t> bytes(size, 0);
    const auto copy_size = std::min(value.size(), size);
    std::copy_n(reinterpret_cast<const std::uint8_t*>(value.data()), copy_size, bytes.data());
    writer.append_bytes(bytes);
  };

  append_fixed("Public World", 201);
  append_fixed("127.0.0.1", 250);
  append_fixed("world-account", 31);
  append_fixed("secret", 256);
  append_fixed("0.5.0", 25);
  append_fixed("2026.05.14", 64);
  writer.append_u8(4);
  writer.append_u32_le(1234);

  const auto info = eq2::protocol::decode_server_ls_info_payload(writer.bytes());
  require(info.has_value(), "server LSInfo payload decodes");
  require_eq(info->world_name, std::string_view("Public World"), "LSInfo world name decodes");
  require_eq(info->address, std::string_view("127.0.0.1"), "LSInfo address decodes");
  require_eq(info->account, std::string_view("world-account"), "LSInfo account decodes");
  require_eq(info->password, std::string_view("secret"), "LSInfo password decodes");
  require_eq(info->protocol_version, std::string_view("0.5.0"), "LSInfo protocol version decodes");
  require_eq(info->server_version, std::string_view("2026.05.14"), "LSInfo server version decodes");
  require_eq(info->server_type, static_cast<std::uint8_t>(4), "LSInfo server type decodes");
  require_eq(info->database_version, static_cast<std::uint32_t>(1234), "LSInfo DB version decodes");

  const auto encoded = eq2::protocol::encode_server_ls_info_payload(eq2::protocol::ServerLsInfo{
      .world_name = "Public World",
      .address = "127.0.0.1",
      .account = "world-account",
      .password = "secret",
      .protocol_version = "0.5.0",
      .server_version = "2026.05.14",
      .server_type = 4,
      .database_version = 1234,
  });
  require_eq(encoded.size(), eq2::protocol::kServerLsInfoPayloadSize,
             "server LSInfo payload encodes to the packed legacy size");
  const auto encoded_info = eq2::protocol::decode_server_ls_info_payload(encoded);
  require(encoded_info.has_value(), "encoded LSInfo payload decodes");
  require_eq(encoded_info->world_name, std::string_view("Public World"),
             "encoded LSInfo world name round trips");

  const auto status_payload = eq2::protocol::encode_server_ls_status_payload(
      eq2::protocol::ServerLsStatus{
          .status = -2,
          .player_count = 123,
          .zone_count = 45,
          .world_max_level = 90,
      });
  require_eq(status_payload.size(), eq2::protocol::kServerLsStatusPayloadSize,
             "server LSStatus payload encodes to the packed legacy size");
  const auto status = eq2::protocol::decode_server_ls_status_payload(status_payload);
  require(status.has_value(), "server LSStatus payload decodes");
  require_eq(status->status, static_cast<std::int32_t>(-2), "LSStatus status decodes");
  require_eq(status->player_count, static_cast<std::int32_t>(123),
             "LSStatus player count decodes");
  require_eq(status->zone_count, static_cast<std::int32_t>(45),
             "LSStatus zone count decodes");
  require_eq(status->world_max_level, static_cast<std::uint8_t>(90),
             "LSStatus max level decodes");
}

void user_to_world_payloads_preserve_login_world_handoff_fields() {
  const eq2::protocol::CharDataUpdate delete_update{
      .account_id = 42,
      .character_id = 2002,
      .update_field = eq2::protocol::kCharUpdateDeleteFlag,
      .update_data = 1,
  };
  const auto delete_payload = eq2::protocol::encode_char_data_update_payload(delete_update);
  require_eq(delete_payload.size(), eq2::protocol::kCharDataUpdatePayloadSize,
             "CharDataUpdate payload has the packed legacy size");
  const auto decoded_delete = eq2::protocol::decode_char_data_update_payload(delete_payload);
  require(decoded_delete.has_value(), "CharDataUpdate payload decodes");
  if (decoded_delete.has_value()) {
    require_eq(decoded_delete->account_id, delete_update.account_id,
               "CharDataUpdate account id decodes");
    require_eq(decoded_delete->character_id, delete_update.character_id,
               "CharDataUpdate character id decodes");
    require_eq(decoded_delete->update_field, delete_update.update_field,
               "CharDataUpdate update field decodes");
    require_eq(decoded_delete->update_data, delete_update.update_data,
               "CharDataUpdate update data decodes");
  }

  const auto timestamp_payload = eq2::protocol::encode_character_timestamp_payload(
      eq2::protocol::CharacterTimeStamp{
          .character_id = 2002,
          .account_id = 42,
          .unix_timestamp = 123456,
      });
  require_eq(timestamp_payload.size(), eq2::protocol::kCharacterTimeStampPayloadSize,
             "CharacterTimeStamp payload has the packed legacy size");
  const auto timestamp = eq2::protocol::decode_character_timestamp_payload(timestamp_payload);
  require(timestamp.has_value(), "CharacterTimeStamp payload decodes");
  if (timestamp.has_value()) {
    require_eq(timestamp->character_id, std::int32_t{2002},
               "CharacterTimeStamp character id decodes");
    require_eq(timestamp->account_id, std::int32_t{42},
               "CharacterTimeStamp account id decodes");
    require_eq(timestamp->unix_timestamp, std::int32_t{123456},
               "CharacterTimeStamp value decodes");
  }

  const auto race_payload = eq2::protocol::encode_race_update_payload(
      eq2::protocol::RaceUpdate{
          .account_id = 42,
          .character_id = 2002,
          .model_type = 55,
          .race = 9,
      });
  require_eq(race_payload.size(), eq2::protocol::kRaceUpdatePayloadSize,
             "RaceUpdate payload has the packed legacy size");
  const auto race_update = eq2::protocol::decode_race_update_payload(race_payload);
  require(race_update.has_value(), "RaceUpdate payload decodes");
  if (race_update.has_value()) {
    require_eq(race_update->model_type, std::int16_t{55},
               "RaceUpdate model type decodes");
    require_eq(race_update->race, std::uint8_t{9}, "RaceUpdate race decodes");
  }

  const auto name_payload = eq2::protocol::encode_char_name_update_payload(
      eq2::protocol::CharNameUpdate{
          .account_id = 42,
          .character_id = 2002,
          .name = "Renamed",
      });
  const auto name_update = eq2::protocol::decode_char_name_update_payload(name_payload);
  require(name_update.has_value(), "CharNameUpdate payload decodes");
  if (name_update.has_value()) {
    require_eq(name_update->name, std::string_view("Renamed"),
               "CharNameUpdate name decodes");
  }

  const auto zone_payload = eq2::protocol::encode_char_zone_update_payload(
      eq2::protocol::CharZoneUpdate{
          .account_id = 42,
          .character_id = 2002,
          .zone_id = 12,
          .zone_name = "qeynos",
      });
  const auto zone_update = eq2::protocol::decode_char_zone_update_payload(zone_payload);
  require(zone_update.has_value(), "CharZoneUpdate payload decodes");
  if (zone_update.has_value()) {
    require_eq(zone_update->zone_id, std::int32_t{12},
               "CharZoneUpdate zone id decodes");
    require_eq(zone_update->zone_name, std::string_view("qeynos"),
               "CharZoneUpdate zone name decodes");
  }

  const eq2::protocol::UserToWorldRequest request{
      .login_account_id = 1001,
      .character_id = 2002,
      .world_id = 77,
      .from_id = 10,
      .to_id = 20,
      .ip_address = "192.0.2.10",
  };

  const auto request_payload = eq2::protocol::encode_user_to_world_request_payload(request);
  require_eq(request_payload.size(), eq2::protocol::kUserToWorldRequestPayloadSize,
             "UserToWorld request payload has the packed legacy size");

  const auto request_frame =
      eq2::protocol::encode_interserver_packet(eq2::protocol::kServerOpUserToWorldRequest,
                                               request_payload);
  require(request_frame.has_value(), "UserToWorld request frame encodes");
  const auto decoded_request_frame = eq2::protocol::decode_interserver_packet(*request_frame);
  require(decoded_request_frame.has_value(), "UserToWorld request frame decodes");
  require_eq(decoded_request_frame->opcode, eq2::protocol::kServerOpUserToWorldRequest,
             "UserToWorld request opcode round trips");

  const auto decoded_request =
      eq2::protocol::decode_user_to_world_request_payload(decoded_request_frame->payload);
  require(decoded_request.has_value(), "UserToWorld request payload decodes");
  require_eq(decoded_request->login_account_id, request.login_account_id,
             "UserToWorld request login account id decodes");
  require_eq(decoded_request->character_id, request.character_id,
             "UserToWorld request character id decodes");
  require_eq(decoded_request->world_id, request.world_id, "UserToWorld request world id decodes");
  require_eq(decoded_request->from_id, request.from_id, "UserToWorld request FromID decodes");
  require_eq(decoded_request->to_id, request.to_id, "UserToWorld request ToID decodes");
  require_eq(decoded_request->ip_address, std::string_view("192.0.2.10"),
             "UserToWorld request IP address decodes");

  const eq2::protocol::UserToWorldResponse response{
      .login_account_id = 1001,
      .character_id = 2002,
      .world_id = 77,
      .access_key = 0x10203040,
      .response = 1,
      .ip_address = "198.51.100.20",
      .port = 9100,
      .from_id = 20,
      .to_id = 10,
  };

  const auto response_payload = eq2::protocol::encode_user_to_world_response_payload(response);
  require_eq(response_payload.size(), eq2::protocol::kUserToWorldResponsePayloadSize,
             "UserToWorld response payload has the packed legacy size");

  const auto decoded_response = eq2::protocol::decode_user_to_world_response_payload(response_payload);
  require(decoded_response.has_value(), "UserToWorld response payload decodes");
  require_eq(decoded_response->login_account_id, response.login_account_id,
             "UserToWorld response login account id decodes");
  require_eq(decoded_response->character_id, response.character_id,
             "UserToWorld response character id decodes");
  require_eq(decoded_response->world_id, response.world_id, "UserToWorld response world id decodes");
  require_eq(decoded_response->access_key, response.access_key,
             "UserToWorld response access key decodes");
  require_eq(decoded_response->response, response.response, "UserToWorld response code decodes");
  require_eq(decoded_response->ip_address, std::string_view("198.51.100.20"),
             "UserToWorld response IP address decodes");
  require_eq(decoded_response->port, response.port, "UserToWorld response port decodes");
  require_eq(decoded_response->from_id, response.from_id, "UserToWorld response FromID decodes");
  require_eq(decoded_response->to_id, response.to_id, "UserToWorld response ToID decodes");

  auto truncated = response_payload;
  truncated.pop_back();
  require(!eq2::protocol::decode_user_to_world_response_payload(truncated).has_value(),
          "truncated UserToWorld response payload is rejected");
}

void world_update_payloads_match_legacy_login_support_structs() {
  const auto zone_request = eq2::protocol::encode_zone_update_request_payload();
  require_eq(zone_request.size(), eq2::protocol::kZoneUpdateRequestPayloadSize,
             "zone update request uses the legacy max-per-batch size");

  const auto equipment_request = eq2::protocol::encode_equipment_update_request_payload();
  require_eq(equipment_request.size(), eq2::protocol::kEquipmentUpdateRequestPayloadSize,
             "equipment update request uses the legacy max-per-batch size");

  const std::vector<eq2::protocol::WorldZoneUpdate> zone_updates{
      eq2::protocol::WorldZoneUpdate{
          .zone_id = 12,
          .name = "qeynos",
          .description = "Qeynos",
      },
  };
  const auto zone_payload = eq2::protocol::encode_world_zone_updates_payload(zone_updates);
  const auto decoded_zones = eq2::protocol::decode_world_zone_updates_payload(zone_payload);
  require(decoded_zones.has_value(), "world zone update list decodes");
  if (decoded_zones.has_value()) {
    require_eq(decoded_zones->size(), static_cast<std::size_t>(1),
               "world zone update list preserves count");
    require_eq(decoded_zones->front().zone_id, std::int32_t{12},
               "world zone update preserves zone id");
    require_eq(decoded_zones->front().name, std::string_view("qeynos"),
               "world zone update preserves name");
    require_eq(decoded_zones->front().description, std::string_view("Qeynos"),
               "world zone update preserves description");
  }

  const std::vector<eq2::protocol::EquipmentUpdate> equipment_updates{
      eq2::protocol::EquipmentUpdate{
          .id = 900,
          .world_character_id = 2002,
          .equip_type = 44,
          .red = 1,
          .green = 2,
          .blue = 3,
          .highlight_red = 4,
          .highlight_green = 5,
          .highlight_blue = 6,
          .slot = 7,
      },
  };
  auto equipment_payload =
      eq2::protocol::encode_login_equipment_update_payload(equipment_updates);
  equipment_payload.resize(equipment_payload.size() + 5U);
  const auto decoded_equipment =
      eq2::protocol::decode_login_equipment_update_payload(equipment_payload);
  require(decoded_equipment.has_value(), "login equipment update list decodes");
  if (decoded_equipment.has_value()) {
    require_eq(decoded_equipment->front().id, std::int32_t{900},
               "login equipment update preserves record id");
    require_eq(decoded_equipment->front().world_character_id, std::int32_t{2002},
               "login equipment update preserves world character id");
    require_eq(decoded_equipment->front().equip_type, std::int16_t{44},
               "login equipment update preserves equipment type");
    require_eq(decoded_equipment->front().highlight_blue, std::uint8_t{6},
               "login equipment update preserves highlight colors");
    require_eq(decoded_equipment->front().slot, std::int32_t{7},
               "login equipment update preserves slot");
  }

  const std::array<std::uint8_t, 4> picture_bytes{0xde, 0xad, 0xbe, 0xef};
  const auto picture_payload = eq2::protocol::encode_character_picture_update_payload(
      eq2::protocol::CharacterPictureUpdate{
          .account_id = 42,
          .character_id = 2002,
          .picture = picture_bytes,
      });
  const auto decoded_picture =
      eq2::protocol::decode_character_picture_update_payload(picture_payload);
  require(decoded_picture.has_value(), "character picture update decodes");
  if (decoded_picture.has_value()) {
    require_eq(decoded_picture->account_id, std::int32_t{42},
               "character picture update preserves account id");
    require_eq(decoded_picture->character_id, std::int32_t{2002},
               "character picture update preserves character id");
    require(span_to_vector(decoded_picture->picture) ==
                std::vector<std::uint8_t>(picture_bytes.begin(), picture_bytes.end()),
            "character picture update preserves image bytes");
  }
}

void create_character_payloads_match_legacy_login_world_exchange() {
  const auto request_payload = eq2::protocol::encode_create_character_request_fixture(
      eq2::protocol::CreateCharacterRequest{
          .account_id = 0,
          .server_id = 77,
          .character_name = "Newchar",
          .race = 1,
          .gender = 2,
          .deity = 3,
          .character_class = 4,
          .level = 1,
          .appearance_data_present = true,
          .body_size = 0.75,
          .body_age = 0.25,
          .appearance_files =
              eq2::protocol::CreateCharacterAppearanceFiles{
                  .race_file = "model/human_male",
                  .hair_file = "hair/short",
                  .face_file = "face/clean",
                  .chest_file = "armor/chest",
                  .legs_file = "armor/legs",
              },
          .appearance_values =
              {
                  eq2::protocol::CreateCharacterAppearanceValue{
                      .type = "skin_color",
                      .signed_value = true,
                      .red = 25,
                      .green = 50,
                      .blue = 75,
                  },
                  eq2::protocol::CreateCharacterAppearanceValue{
                      .type = "eye_type",
                      .signed_value = true,
                      .red = -50,
                      .green = 25,
                      .blue = 0,
                  },
              },
      },
      546);

  const auto race_file_text_offset = find_ascii_offset(request_payload, "model/human_male");
  require(race_file_text_offset != request_payload.size(),
          "create-character fixture contains race file");
  if (race_file_text_offset >= 3U && race_file_text_offset != request_payload.size()) {
    require_eq(request_payload[race_file_text_offset - 3U], std::uint8_t{4},
               "create-character fixture writes DoF customization version 4");
    const auto hair_file_length_offset =
        race_file_text_offset + std::string_view("model/human_male").size() +
        (5U * 3U * sizeof(float));
    require(hair_file_length_offset + 1U < request_payload.size(),
            "create-character fixture has room for first hair-file length");
    if (hair_file_length_offset + 1U < request_payload.size()) {
      require_eq(request_payload[hair_file_length_offset], std::uint8_t{10},
                 "create-character fixture has no 26-byte gap before hair_file");
      require_eq(request_payload[hair_file_length_offset + 1U], std::uint8_t{0},
                 "create-character fixture writes hair_file length little-endian");
    }
  }

  const auto request = eq2::protocol::parse_create_character_request(request_payload, 546);
  require(request.has_value(), "create-character request payload decodes");
  if (request.has_value()) {
    require_eq(request->account_id, std::int32_t{0},
               "create-character request account id decodes");
    require_eq(request->server_id, std::int32_t{77},
               "create-character request server id decodes");
    require_eq(request->character_name, std::string_view("Newchar"),
               "create-character request name decodes");
    require_eq(request->race, std::uint8_t{1}, "create-character race decodes");
    require_eq(request->gender, std::uint8_t{2}, "create-character gender decodes");
    require_eq(request->deity, std::uint8_t{3}, "create-character deity decodes");
    require_eq(request->character_class, std::uint8_t{4},
               "create-character class decodes");
    require(request->appearance_data_present,
            "create-character legacy appearance block decodes");
    require_eq(request->body_size, 0.75, "create-character body size decodes");
    require_eq(request->body_age, 0.25, "create-character body age decodes");
    require_eq(request->appearance_files.race_file, std::string_view("model/human_male"),
               "create-character race appearance file decodes");
    require_eq(request->appearance_files.hair_file, std::string_view("hair/short"),
               "create-character hair appearance file decodes");
    const auto skin = std::find_if(request->appearance_values.begin(),
                                   request->appearance_values.end(),
                                   [](const auto& value) { return value.type == "skin_color"; });
    require(skin != request->appearance_values.end(),
            "create-character skin color is saved from legacy floats");
    if (skin != request->appearance_values.end()) {
      require_eq(skin->red, 25, "create-character skin red scales like legacy SaveCharacter");
      require(skin->signed_value, "create-character skin color remains signed");
    }
    const auto eye_type = std::find_if(
        request->appearance_values.begin(),
        request->appearance_values.end(),
        [](const auto& value) { return value.type == "eye_type"; });
    require(eye_type != request->appearance_values.end(),
            "create-character shape floats are mapped to login_char_colors names");
    if (eye_type != request->appearance_values.end()) {
      require_eq(eye_type->red, -50, "create-character shape values use the 100x multiplier");
    }
  }

  const auto forwarded =
      eq2::protocol::encode_create_character_forward_payload(request_payload, 546, 42);
  require(forwarded.has_value(), "create-character forward payload encodes");
  if (forwarded.has_value()) {
    require_eq(forwarded->bytes.size(), request_payload.size() + sizeof(std::uint16_t),
               "create-character forward payload prepends client version");
    require_eq(forwarded->bytes[0], std::uint8_t{0x22},
               "create-character forward payload version byte 0");
    require_eq(forwarded->bytes[1], std::uint8_t{0x02},
               "create-character forward payload version byte 1");
    const auto patched_account =
        eq2::core::read_u32_le(std::span<const std::uint8_t>(forwarded->bytes), 7);
    require_eq(patched_account, std::uint32_t{42},
               "create-character forward payload patches account id at legacy offset");
  }

  const auto world_response_payload = eq2::protocol::encode_create_character_response_payload(
      eq2::protocol::CreateCharacterResponse{
          .account_id = 42,
          .character_id = 9001,
          .response = 1,
      });
  require_eq(world_response_payload.size(), eq2::protocol::kCharacterCreateResponsePayloadSize,
             "create-character world response has packed legacy size");
  const auto world_response =
      eq2::protocol::decode_create_character_response_payload(world_response_payload);
  require(world_response.has_value(), "create-character world response decodes");
  if (world_response.has_value()) {
    require_eq(world_response->account_id, std::int32_t{42},
               "create-character world response account id decodes");
    require_eq(world_response->character_id, std::int32_t{9001},
               "create-character world response character id decodes");
    require_eq(world_response->response, std::uint8_t{1},
               "create-character world response code decodes");
  }

  const auto client_reply_payload = eq2::protocol::encode_create_character_reply_payload(
      eq2::protocol::CreateCharacterReply{
          .account_id = 42,
          .response = 1,
          .character_name = "Newchar",
      },
      546);
  const auto client_reply =
      eq2::protocol::decode_create_character_reply_payload(client_reply_payload, 546);
  require(client_reply.has_value(), "create-character client reply decodes");
  if (client_reply.has_value()) {
    require_eq(client_reply->account_id, std::int32_t{42},
               "create-character client reply account id decodes");
    require_eq(client_reply->response, std::uint8_t{1},
               "create-character client reply response decodes");
    require_eq(client_reply->character_name, std::string_view("Newchar"),
               "create-character client reply name decodes");
  }
}

void login_response_payloads_serialize_reply_and_world_list() {
  const auto reply_payload = eq2::protocol::encode_login_reply_payload(
      eq2::protocol::LoginReplyPayload{
          .reply_code = 1,
          .account_id = 42,
      });
  const auto reply = eq2::protocol::decode_login_reply_payload(reply_payload);
  require(reply.has_value(), "login reply payload decodes");
  require_eq(reply->reply_code, static_cast<std::uint8_t>(1),
             "login reply payload preserves reply code");
  require_eq(reply->account_id, static_cast<std::uint32_t>(42),
             "login reply payload preserves account id");
  require_eq(eq2::protocol::login_reply_protocol_frame_size(0x1235),
             static_cast<std::size_t>(9),
             "login reply protocol frame size includes protocol and application headers");
  require_eq(eq2::protocol::login_reply_protocol_frame_size(0x1235,
                                                            eq2::protocol::ApplicationOpcodeWidth::two_bytes,
                                                            true),
             static_cast<std::size_t>(11),
             "sequenced login reply protocol frame size includes sequence header");

  const auto legacy_reply_payload = eq2::protocol::encode_login_reply_payload(
      eq2::protocol::LoginReplyPayload{
          .reply_code = 0,
          .account_id = 42,
      },
      546);
  const std::vector<std::uint8_t> expected_legacy_reply{
      0x00,
      0x00, 0x00,
      0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x2a, 0x00, 0x00, 0x00,
      0x00, 0x00,
      0x00,
      0x01,
  };
  require(legacy_reply_payload == expected_legacy_reply,
          "546 login reply uses the client-primary DoF layout without PacketParser tail");
  const auto legacy_reply =
      eq2::protocol::decode_login_reply_payload(legacy_reply_payload, 546);
  require(legacy_reply.has_value(), "546 login reply payload decodes");
  require_eq(legacy_reply->account_id, static_cast<std::uint32_t>(42),
             "546 login reply payload preserves account id");

  const auto world_list_payload = eq2::protocol::encode_login_world_list_payload(
      eq2::protocol::LoginWorldListPayload{
          .worlds =
              {
                  eq2::protocol::LoginWorldEntry{
                      .world_id = 77,
                      .display_name = "Public World",
                      .address = "127.0.0.1",
                  },
                  eq2::protocol::LoginWorldEntry{
                      .world_id = 78,
                      .display_name = "Dev World",
                      .address = "192.0.2.10",
                      .development_server = true,
                  },
              },
      });
  const auto world_list = eq2::protocol::decode_login_world_list_payload(world_list_payload);
  require(world_list.has_value(), "login world list payload decodes");
  require_eq(world_list->worlds.size(), static_cast<std::size_t>(2),
             "login world list preserves entry count");
  require_eq(world_list->worlds.front().world_id, 77,
             "login world list preserves world id");
  require_eq(world_list->worlds.front().display_name, std::string_view("Public World"),
             "login world list preserves display name");
  require_eq(world_list->worlds.back().address, std::string_view("192.0.2.10"),
             "login world list preserves advertised address");
  require(world_list->worlds.back().development_server,
          "login world list preserves development flag");

  const auto legacy_world_list_payload = eq2::protocol::encode_login_world_list_payload(
      eq2::protocol::LoginWorldListPayload{
          .worlds =
              {
                  eq2::protocol::LoginWorldEntry{
                      .world_id = 77,
                      .display_name = "Public World",
                  },
                  eq2::protocol::LoginWorldEntry{
                      .world_id = 78,
                      .display_name = "Dev World",
                      .development_server = true,
                  },
              },
      },
      546);
  const std::vector<std::uint8_t> expected_legacy_world_list{
      0x02,
      0x4d, 0x00, 0x00, 0x00,
      0x0c, 0x00,
      'P', 'u', 'b', 'l', 'i', 'c', ' ', 'W', 'o', 'r', 'l', 'd',
      0x0c, 0x00,
      'P', 'u', 'b', 'l', 'i', 'c', ' ', 'W', 'o', 'r', 'l', 'd',
      0x01,
      0x00,
      0x00,
      0x01,
      0x00, 0x00,
      0x00,
      0x01,
      0x00,
      0xff, 0xff, 0xff, 0xff,
      0x4e, 0x00, 0x00, 0x00,
      0x09, 0x00,
      'D', 'e', 'v', ' ', 'W', 'o', 'r', 'l', 'd',
      0x09, 0x00,
      'D', 'e', 'v', ' ', 'W', 'o', 'r', 'l', 'd',
      0x01,
      0x00,
      0x00,
      0x02,
      0x00, 0x00,
      0x01,
      0x01,
      0x00,
      0xff, 0xff, 0xff, 0xff,
  };
  require(legacy_world_list_payload == expected_legacy_world_list,
          "546 world list uses the legacy LoginStructs.xml world-list layout");
  const auto legacy_world_list =
      eq2::protocol::decode_login_world_list_payload(legacy_world_list_payload, 546);
  require(legacy_world_list.has_value(), "546 world-list payload decodes");
  require_eq(legacy_world_list->worlds.size(), static_cast<std::size_t>(2),
             "546 world-list payload preserves entry count");
  require_eq(legacy_world_list->worlds.back().display_name, std::string_view("Dev World"),
             "546 world-list payload preserves display name");
  require(legacy_world_list->worlds.back().development_server,
          "546 world-list payload preserves development load flag");

  const auto empty_character_list =
      eq2::protocol::encode_empty_character_list_payload(42, 546);
  const std::vector<std::uint8_t> expected_empty_character_list{
      0x00,
      0x2a, 0x00, 0x00, 0x00,
      0xff, 0xff, 0xff, 0xff,
      0x00, 0x00,
      0x07, 0x00, 0x00, 0x00,
      0x00,
  };
  require(empty_character_list == expected_empty_character_list,
          "546 empty character-list payload writes legacy account info");

  const auto login_reply_packet = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpPacket,
      eq2::protocol::encode_application_packet(0x1235, reply_payload));
  const auto world_list_packet = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpPacket,
      eq2::protocol::encode_application_packet(0x1236, world_list_payload));
  auto coalesced_response = login_reply_packet;
  coalesced_response.insert(coalesced_response.end(),
                            world_list_packet.begin(),
                            world_list_packet.end());
  const auto decoded_coalesced_reply =
      eq2::protocol::decode_login_reply_protocol_frame(coalesced_response, 0x1235);
  require(decoded_coalesced_reply.has_value(),
          "login reply protocol frame decodes when followed by another TCP packet");
  require_eq(decoded_coalesced_reply->account_id, static_cast<std::uint32_t>(42),
             "coalesced login reply preserves account id");

  eq2::protocol::PacketWriter sequenced_writer;
  sequenced_writer.append_u16_be(0);
  const auto sequenced_reply_app =
      eq2::protocol::encode_application_packet(0x1235, reply_payload);
  sequenced_writer.append_bytes(sequenced_reply_app);
  const auto sequenced_reply_packet = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpPacket, sequenced_writer.bytes());
  const auto decoded_sequenced_reply =
      eq2::protocol::decode_login_reply_protocol_frame(sequenced_reply_packet, 0x1235);
  require(decoded_sequenced_reply.has_value(), "sequenced login reply protocol frame decodes");
  require_eq(decoded_sequenced_reply->account_id, static_cast<std::uint32_t>(42),
             "sequenced login reply preserves account id");
}

void legacy_login_request_decodes_length_prefixed_credentials_and_version() {
  const auto fixture = eq2::protocol::encode_legacy_login_request_fixture(eq2::protocol::LoginRequest{
      .access_code = "station",
      .username = "tester",
      .password = "secret",
      .version = 546,
  });

  const auto parsed = eq2::protocol::parse_legacy_login_request(fixture);

  require(parsed.has_value(), "legacy login request parses in protocol");
  require_eq(parsed->access_code, std::string("station"), "access code field follows legacy order");
  require_eq(parsed->username, std::string("tester"), "username field follows legacy order");
  require_eq(parsed->password, std::string("secret"), "password field follows legacy order");
  require_eq(parsed->version, static_cast<std::int16_t>(546),
             "client version field follows the trailing account/passcode fields");

  auto truncated = fixture;
  truncated.pop_back();
  require(eq2::protocol::parse_legacy_login_request(truncated).has_value(),
          "classic login request parser accepts missing unused fixture tail");

  const auto captured_payload = bytes_from_hex(
      "000000000800746573746c6162730800746573747061737300000000000000002202");
  const auto captured = eq2::protocol::parse_legacy_login_request(captured_payload);
  require(captured.has_value(), "captured 546 real-client login request parses");
  if (captured.has_value()) {
    require_eq(captured->username, std::string("testlabs"),
               "captured 546 login request preserves username");
    require_eq(captured->password, std::string("testpass"),
               "captured 546 login request preserves password");
    require_eq(captured->version, static_cast<std::int16_t>(546),
               "captured 546 login request preserves client version");
  }

  auto truncated_captured = captured_payload;
  truncated_captured.pop_back();
  require(!eq2::protocol::parse_legacy_login_request(truncated_captured).has_value(),
          "truncated captured login request is rejected in protocol");
}

void login_by_num_request_decodes_legacy_and_extended_world_entry_layouts() {
  const eq2::protocol::LoginByNumRequest request{
      .account_id = 42,
      .access_code = 0x12345678,
      .version = 546,
  };

  const auto legacy_bytes = eq2::protocol::encode_legacy_login_by_num_request_fixture(request);
  const std::vector<std::uint8_t> expected_legacy{
      0x2a, 0x00, 0x00, 0x00,
      0x78, 0x56, 0x34, 0x12,
      0x22, 0x02,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
  };
  require(legacy_bytes == expected_legacy,
          "legacy LoginByNumRequest keeps account, access key, and version at the original offsets");

  const auto legacy = eq2::protocol::parse_legacy_login_by_num_request(legacy_bytes);
  require(legacy.has_value(), "legacy LoginByNumRequest parses");
  require_eq(legacy->account_id, request.account_id, "legacy LoginByNumRequest account id parses");
  require_eq(legacy->access_code, request.access_code, "legacy LoginByNumRequest access key parses");
  require_eq(legacy->version, request.version, "legacy LoginByNumRequest version parses");

  const eq2::protocol::LoginByNumRequest extended_request{
      .account_id = 77,
      .access_code = 0x01020304,
      .version = 1208,
  };
  const auto extended_bytes =
      eq2::protocol::encode_extended_login_by_num_request_fixture(extended_request);
  const auto extended = eq2::protocol::parse_extended_login_by_num_request(extended_bytes);
  require(extended.has_value(), "extended LoginByNumRequest parses");
  require_eq(extended->account_id, extended_request.account_id,
             "extended LoginByNumRequest account id parses");
  require_eq(extended->access_code, extended_request.access_code,
             "extended LoginByNumRequest access key parses");
  require_eq(extended->version, extended_request.version,
             "extended LoginByNumRequest version parses after the new unknown fields");

  auto truncated = extended_bytes;
  truncated.pop_back();
  require(!eq2::protocol::parse_extended_login_by_num_request(truncated).has_value(),
          "truncated extended LoginByNumRequest is rejected");
}

void login_request_version_fallbacks_match_phase1_inventory() {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  ranges.add_range(1208, 1208);

  require_eq(eq2::protocol::login_request_struct_version(546, ranges),
             eq2::protocol::kLegacyLoginRequestStructVersion,
             "login keeps legacy request struct for known classic version");
  require_eq(eq2::protocol::login_request_struct_version(0, ranges),
             eq2::protocol::kExtendedLoginRequestStructVersion,
             "login retries extended request struct for zero classic version");
  require_eq(eq2::protocol::login_request_struct_version(999, ranges),
             eq2::protocol::kExtendedLoginRequestStructVersion,
             "login retries extended request struct for unknown classic version");

  require_eq(eq2::protocol::world_login_request_struct_version(546, ranges),
             eq2::protocol::kLegacyLoginRequestStructVersion,
             "world keeps legacy request struct for known pre-1208 version");
  require_eq(eq2::protocol::world_login_request_struct_version(1208, ranges),
             eq2::protocol::kExtendedLoginRequestStructVersion,
             "world retries extended request struct for 1208 and newer");
  require_eq(eq2::protocol::world_login_request_struct_version(999, ranges),
             eq2::protocol::kExtendedLoginRequestStructVersion,
             "world retries extended request struct for unknown classic version");

  const auto extended_bytes =
      eq2::protocol::encode_extended_login_by_num_request_fixture(eq2::protocol::LoginByNumRequest{
          .account_id = 77,
          .access_code = 0x01020304,
          .version = 1208,
      });

  const auto parsed = eq2::protocol::parse_world_login_by_num_request(extended_bytes, ranges);
  require(parsed.has_value(), "world LoginByNumRequest parser retries the extended layout");
  require_eq(parsed->version, static_cast<std::int16_t>(1208),
             "world LoginByNumRequest parser returns the extended version");
}

void play_character_request_and_response_packets_match_legacy_login_layouts() {
  const auto old_request =
      eq2::protocol::encode_legacy_play_character_request_fixture(eq2::protocol::PlayCharacterRequest{
          .character_id = 1001,
          .character_name = "Alys",
      });
  const std::vector<std::uint8_t> expected_old_request{
      0xe9, 0x03, 0x00, 0x00,
      0x04, 0x00,
      'A', 'l', 'y', 's',
  };
  require(old_request == expected_old_request,
          "old play request stores character id followed by a 16-bit character name");

  const auto parsed_old = eq2::protocol::parse_play_character_request(old_request, 283);
  require(parsed_old.has_value(), "old play request parses");
  require_eq(parsed_old->character_id, 1001, "old play request character id decodes");
  require_eq(parsed_old->character_name, std::string("Alys"), "old play request name decodes");
  require_eq(parsed_old->server_id, 0, "old play request leaves server id for login DB lookup");

  const auto modern_request =
      eq2::protocol::encode_modern_play_character_request_fixture(eq2::protocol::PlayCharacterRequest{
          .character_id = 1001,
          .server_id = 77,
          .character_name = "Alys",
      });
  const std::vector<std::uint8_t> expected_modern_request{
      0xe9, 0x03, 0x00, 0x00,
      0x4d, 0x00, 0x00, 0x00,
      0x00,
      0x04, 0x00,
      'A', 'l', 'y', 's',
  };
  require(modern_request == expected_modern_request,
          "modern play request stores character id, server id, one byte, and a 16-bit name");

  const auto parsed_modern = eq2::protocol::parse_play_character_request(modern_request, 284);
  require(parsed_modern.has_value(), "modern play request parses");
  require_eq(parsed_modern->character_id, 1001, "modern play request character id decodes");
  require_eq(parsed_modern->server_id, 77, "modern play request server id decodes");
  require_eq(parsed_modern->character_name, std::string("Alys"),
             "modern play request character name decodes");

  const eq2::protocol::PlayCharacterResponse response{
      .response = 1,
      .server = "127.0.0.1",
      .port = 9100,
      .account_id = 42,
      .access_code = 0x12345678,
  };
  const auto classic_response = eq2::protocol::encode_play_character_response_payload(response, 546);
  const std::vector<std::uint8_t> expected_classic_response{
      0x01,
      0x09,
      '1', '2', '7', '.', '0', '.', '0', '.', '1',
      0x8c, 0x23,
      0x2a, 0x00, 0x00, 0x00,
      0x78, 0x56, 0x34, 0x12,
  };
  require(classic_response == expected_classic_response,
          "classic play response writes response, 8-bit server string, port, account, and access code");

  const auto parsed_classic =
      eq2::protocol::decode_play_character_response_payload(classic_response, 546);
  require(parsed_classic.has_value(), "classic play response decodes");
  require_eq(parsed_classic->server, std::string("127.0.0.1"), "classic play response server decodes");
  require_eq(parsed_classic->port, static_cast<std::uint16_t>(9100),
             "classic play response port decodes");
  require_eq(parsed_classic->access_code, 0x12345678, "classic play response access code decodes");

  const eq2::protocol::PlayCharacterResponse failure_response{
      .response = 2,
      .server = "127.0.0.1",
      .port = 9100,
      .account_id = 42,
      .access_code = 0x12345678,
  };
  const auto failed_classic_response =
      eq2::protocol::encode_play_character_response_payload(failure_response, 546);
  require(failed_classic_response == std::vector<std::uint8_t>{0x02},
          "failed classic play response is response-only like the client vtable");
  const auto parsed_failure =
      eq2::protocol::decode_play_character_response_payload(failed_classic_response, 546);
  require(parsed_failure.has_value(), "response-only failed classic play response decodes");
  if (parsed_failure.has_value()) {
    require_eq(parsed_failure->response, std::uint8_t{2},
               "response-only failed classic play response preserves failure code");
    require(parsed_failure->server.empty(),
            "response-only failed classic play response leaves server empty");
  }

  const auto version1096_response = eq2::protocol::encode_play_character_response_payload(response, 1096);
  require_eq(version1096_response[1], static_cast<std::uint8_t>(0),
             "1096 play response includes one zero int16 before server");
  require_eq(version1096_response[2], static_cast<std::uint8_t>(0),
             "1096 play response includes the second zero byte before server");
  require_eq(version1096_response[3], static_cast<std::uint8_t>(9),
             "1096 play response server string follows the one-int16 unknown");

  const auto version60085_response =
      eq2::protocol::encode_play_character_response_payload(response, 60085);
  require_eq(version60085_response[7], static_cast<std::uint8_t>(9),
             "60085 play response server string follows three int16 unknowns");

  auto truncated = modern_request;
  truncated.pop_back();
  require(!eq2::protocol::parse_play_character_request(truncated, 284).has_value(),
          "truncated modern play request is rejected");
}

void delete_character_request_and_response_packets_match_legacy_login_layouts() {
  const eq2::protocol::DeleteCharacterRequest request{
      .character_id = 1001,
      .server_id = 77,
      .unknown = 0,
      .character_name = "Alys",
  };

  const auto request_payload = eq2::protocol::encode_delete_character_request_fixture(request);
  const std::vector<std::uint8_t> expected_request{
      0xe9, 0x03, 0x00, 0x00,
      0x4d, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      0x04, 0x00,
      'A', 'l', 'y', 's',
  };
  require(request_payload == expected_request,
          "delete request stores character id, server id, unknown int32, and character name");

  const auto parsed_request = eq2::protocol::parse_delete_character_request(request_payload);
  require(parsed_request.has_value(), "delete request parses");
  require_eq(parsed_request->character_id, request.character_id,
             "delete request character id decodes");
  require_eq(parsed_request->server_id, request.server_id, "delete request server id decodes");
  require_eq(parsed_request->character_name, request.character_name,
             "delete request character name decodes");

  const eq2::protocol::DeleteCharacterResponse response{
      .response = 1,
      .server_id = 77,
      .character_id = 1001,
      .character_name = "Alys",
  };

  const auto response_payload = eq2::protocol::encode_delete_character_response_payload(response);
  const std::vector<std::uint8_t> expected_response{
      0x01,
      0x4d, 0x00, 0x00, 0x00,
      0xe9, 0x03, 0x00, 0x00,
      0x04, 0x00,
      'A', 'l', 'y', 's',
  };
  require(response_payload == expected_response,
          "delete response stores response code, server id, character id, and character name");

  const auto parsed_response =
      eq2::protocol::decode_delete_character_response_payload(response_payload);
  require(parsed_response.has_value(), "delete response decodes");
  require_eq(parsed_response->response, response.response, "delete response code decodes");
  require_eq(parsed_response->server_id, response.server_id, "delete response server id decodes");
  require_eq(parsed_response->character_id, response.character_id,
             "delete response character id decodes");

  auto truncated = request_payload;
  truncated.pop_back();
  require(!eq2::protocol::parse_delete_character_request(truncated).has_value(),
          "truncated delete request is rejected");
}

void client_log_payloads_inflate_legacy_compressed_messages() {
  const auto modern = eq2::protocol::encode_client_log_payload_fixture("client stack trace", 546);
  require(modern.size() > 8, "modern client log fixture includes compressed bytes");
  require_eq(eq2::core::read_u32_le(modern, 0),
             static_cast<std::uint32_t>(modern.size() - 8U),
             "modern client log payload stores compressed byte count");
  const auto decoded_modern = eq2::protocol::decode_client_log_payload(modern, 546);
  require(decoded_modern.has_value(), "modern client log payload inflates");
  if (decoded_modern.has_value()) {
    require_eq(decoded_modern->message, std::string_view("client stack trace"),
               "modern client log payload preserves message text");
    require_eq(decoded_modern->inflated_size, static_cast<std::uint32_t>(18),
               "modern client log payload stores original inflated size");
  }

  const auto legacy = eq2::protocol::encode_client_log_payload_fixture("box log", 283);
  const auto decoded_legacy = eq2::protocol::decode_client_log_payload(legacy, 283);
  require(decoded_legacy.has_value(), "box client log payload inflates");
  if (decoded_legacy.has_value()) {
    require_eq(decoded_legacy->message, std::string_view("box log"),
               "box client log payload preserves message text");
  }

  auto malformed = modern;
  malformed.pop_back();
  require(!eq2::protocol::decode_client_log_payload(malformed, 546).has_value(),
          "truncated client log payload is rejected");
  malformed = modern;
  malformed[0] = 0;
  malformed[1] = 0;
  malformed[2] = 0;
  malformed[3] = 0;
  require(!eq2::protocol::decode_client_log_payload(malformed, 546).has_value(),
          "zero compressed client log size is rejected");
}

void stream_pipeline_handles_session_handshake_and_app_dispatch() {
  eq2::protocol::StreamPipeline pipeline(eq2::protocol::StreamPipelineOptions{
      .session_key = 0x33624702,
  });
  const auto session_request =
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpSessionRequest,
                                            eq2::protocol::encode_session_request(
                                                eq2::protocol::SessionRequest{
                                                    .unknown_a = 0,
                                                    .session = 0x01020304,
                                                    .max_length = 512,
                                                }));

  const auto handshake = pipeline.receive_datagram(session_request);
  require_eq(handshake.events.size(), static_cast<std::size_t>(1),
             "stream pipeline emits one session event");
  require_eq(handshake.events.front().type, eq2::protocol::StreamEventType::session_requested,
             "stream pipeline identifies session request");
  require(pipeline.established(), "stream pipeline records established session");
  require_eq(pipeline.session_id(), static_cast<std::uint32_t>(0x01020304),
             "stream pipeline records session id");
  require_eq(handshake.outbound.size(), static_cast<std::size_t>(1),
             "stream pipeline emits session response bytes");

  const auto response = eq2::protocol::decode_protocol_packet(handshake.outbound.front());
  require(response.has_value(), "stream pipeline response is a protocol packet");
  require_eq(response->opcode, eq2::protocol::kOpSessionResponse,
             "stream pipeline response uses session response opcode");
  const auto decoded_response = eq2::protocol::decode_session_response(response->payload);
  require(decoded_response.has_value(), "stream pipeline response payload decodes");
  require_eq(decoded_response->session, static_cast<std::uint32_t>(0x01020304),
             "stream pipeline response keeps session id");
  require_eq(decoded_response->unknown_d, static_cast<std::uint32_t>(2),
             "stream pipeline response uses the recovered session trailer value");

  const auto app_payload =
      eq2::protocol::encode_application_packet(0x1234, std::array<std::uint8_t, 2>{0xaa, 0xbb});
  eq2::protocol::PacketWriter sequenced_app_writer;
  sequenced_app_writer.append_u16_be(0);
  sequenced_app_writer.append_bytes(app_payload);
  const auto app_datagram =
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket,
                                            sequenced_app_writer.bytes());
  const auto app_result = pipeline.receive_datagram(app_datagram);
  require_eq(app_result.events.size(), static_cast<std::size_t>(1),
             "stream pipeline emits one app packet event");
  require_eq(app_result.outbound.size(), static_cast<std::size_t>(1),
             "stream pipeline emits one ACK for sequenced app packet");
  const auto ack = eq2::protocol::decode_protocol_packet(app_result.outbound.front());
  require(ack.has_value() && ack->opcode == eq2::protocol::kOpAck,
          "stream pipeline ACK uses ACK protocol opcode");
  require_eq(app_result.events.front().type, eq2::protocol::StreamEventType::app_packet,
             "stream pipeline identifies app packet");
  require(app_result.events.front().app_packet.has_value(),
          "stream pipeline carries decoded app packet");
  require_eq(app_result.events.front().app_packet->opcode, static_cast<std::uint16_t>(0x1234),
             "stream pipeline carries app opcode");

  auto one_byte_pipeline = eq2::protocol::StreamPipeline(eq2::protocol::StreamPipelineOptions{
      .application_opcode_width = eq2::protocol::ApplicationOpcodeWidth::one_byte});
  const auto one_byte_handshake = one_byte_pipeline.receive_datagram(session_request);
  require_eq(one_byte_handshake.events.size(), static_cast<std::size_t>(1),
             "one-byte stream pipeline accepts session request");
  const auto one_byte_app = eq2::protocol::encode_application_packet(
      0x07, std::span<const std::uint8_t>{},
      eq2::protocol::ApplicationOpcodeWidth::one_byte);
  eq2::protocol::PacketWriter one_byte_sequenced_writer;
  one_byte_sequenced_writer.append_u16_be(0);
  one_byte_sequenced_writer.append_bytes(one_byte_app);
  const auto one_byte_datagram = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpPacket, one_byte_sequenced_writer.bytes());
  const auto one_byte_result = one_byte_pipeline.receive_datagram(one_byte_datagram);
  require_eq(one_byte_result.events.size(), static_cast<std::size_t>(1),
             "one-byte stream pipeline emits app event for empty sequenced packet");
  require_eq(one_byte_result.outbound.size(), static_cast<std::size_t>(1),
             "one-byte stream pipeline ACKs empty sequenced packet");
  require(one_byte_result.events.front().app_packet.has_value(),
          "one-byte stream pipeline decodes empty sequenced app packet");
  require_eq(one_byte_result.events.front().app_packet->opcode, static_cast<std::uint16_t>(0x07),
             "one-byte stream pipeline preserves empty sequenced app opcode");

  auto packed_pipeline = eq2::protocol::StreamPipeline(eq2::protocol::StreamPipelineOptions{
      .application_opcode_width = eq2::protocol::ApplicationOpcodeWidth::packed_u16});
  const auto packed_handshake = packed_pipeline.receive_datagram(session_request);
  require_eq(packed_handshake.events.size(), static_cast<std::size_t>(1),
             "packed stream pipeline accepts session request");
  const auto packed_app = eq2::protocol::encode_application_packet(
      0x019a, std::array<std::uint8_t, 1>{0x5a},
      eq2::protocol::ApplicationOpcodeWidth::packed_u16);
  eq2::protocol::PacketWriter packed_sequenced_writer;
  packed_sequenced_writer.append_u16_be(0);
  packed_sequenced_writer.append_bytes(packed_app);
  const auto packed_datagram = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpPacket, packed_sequenced_writer.bytes());
  const auto packed_result = packed_pipeline.receive_datagram(packed_datagram);
  require_eq(packed_result.events.size(), static_cast<std::size_t>(1),
             "packed stream pipeline emits app event for large packed opcode");
  require_eq(packed_result.outbound.size(), static_cast<std::size_t>(1),
             "packed stream pipeline ACKs large packed opcode");
  require(packed_result.events.front().app_packet.has_value(),
          "packed stream pipeline decodes large packed app packet");
  if (packed_result.events.front().app_packet.has_value()) {
    require_eq(packed_result.events.front().app_packet->opcode,
               static_cast<std::uint16_t>(0x019a),
               "packed stream pipeline preserves large packed opcode");
    require_eq(packed_result.events.front().app_packet->payload.front(),
               static_cast<std::uint8_t>(0x5a),
               "packed stream pipeline preserves large packed payload");
  }

  auto key_pipeline = eq2::protocol::StreamPipeline(eq2::protocol::StreamPipelineOptions{
      .application_opcode_width = eq2::protocol::ApplicationOpcodeWidth::one_byte});
  const auto key_handshake = key_pipeline.receive_datagram(session_request);
  require_eq(key_handshake.events.size(), static_cast<std::size_t>(1),
             "key stream pipeline accepts session request");

  auto client_stats = std::vector<std::uint8_t>(38, 0);
  client_stats[0] = 0x34;
  client_stats[1] = 0x12;
  const auto server_key_request =
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpServerKeyRequest,
                                            client_stats);
  const auto key_request_result = key_pipeline.receive_datagram(server_key_request);
  require_eq(key_request_result.events.size(), static_cast<std::size_t>(1),
             "stream pipeline emits key-request event");
  require_eq(key_request_result.events.front().type,
             eq2::protocol::StreamEventType::server_key_requested,
             "stream pipeline identifies legacy server key request");
  require_eq(key_request_result.outbound.size(), static_cast<std::size_t>(1),
             "stream pipeline replies to server key request with session stats");
  const auto stats_response = eq2::protocol::decode_protocol_packet(
      eq2::protocol::strip_legacy_crc_if_present(key_request_result.outbound.front(),
                                                 0x33624702));
  require(stats_response.has_value() &&
              stats_response->opcode == eq2::protocol::kOpSessionStatResponse,
          "stream pipeline session stats response uses the legacy opcode");
  require(stats_response.has_value() && stats_response->payload.size() == client_stats.size(),
          "stream pipeline preserves session stats response size");
  if (stats_response.has_value() && stats_response->payload.size() >= 2) {
    require_eq(stats_response->payload[0], static_cast<std::uint8_t>(0x34),
               "stream pipeline preserves session stats request id byte 0");
    require_eq(stats_response->payload[1], static_cast<std::uint8_t>(0x12),
               "stream pipeline preserves session stats request id byte 1");
  }

  constexpr auto rc4_key = std::uint64_t{0x0102030405060708ULL};
  auto rsa_packet_payload = std::vector<std::uint8_t>(69, 0);
  for (std::size_t i = 0; i < 8; ++i) {
    rsa_packet_payload[rsa_packet_payload.size() - 8 + i] =
        static_cast<std::uint8_t>((rc4_key >> ((7U - i) * 8U)) & 0xffU);
  }
  const auto rsa_result = key_pipeline.receive_datagram(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, rsa_packet_payload));
  require(rsa_result.events.empty(), "stream pipeline consumes RSA key packet internally");
  require_eq(rsa_result.outbound.size(), static_cast<std::size_t>(1),
             "stream pipeline ACKs RSA key packet");

  auto encrypted_login = std::vector<std::uint8_t>{0x07, 0xaa};
  TestLegacyRc4 client_cipher(~rc4_key);
  auto warmup = std::array<std::uint8_t, 20>{};
  client_cipher.cypher(warmup);
  client_cipher.cypher(encrypted_login);
  eq2::protocol::PacketWriter encrypted_writer;
  encrypted_writer.append_u16_be(1);
  encrypted_writer.append_bytes(encrypted_login);
  const auto encrypted_result = key_pipeline.receive_datagram(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket,
                                            encrypted_writer.bytes()));
  require_eq(encrypted_result.events.size(), static_cast<std::size_t>(1),
             "stream pipeline emits decrypted encrypted app packet");
  require(encrypted_result.events.front().app_packet.has_value(),
          "stream pipeline carries decrypted encrypted app packet");
  if (encrypted_result.events.front().app_packet.has_value()) {
    require_eq(encrypted_result.events.front().app_packet->opcode, static_cast<std::uint16_t>(0x07),
               "stream pipeline decrypts encrypted app opcode");
    require_eq(encrypted_result.events.front().app_packet->payload.front(),
               static_cast<std::uint8_t>(0xaa),
               "stream pipeline decrypts encrypted app payload");
  }

  auto captured_pipeline = eq2::protocol::StreamPipeline(eq2::protocol::StreamPipelineOptions{
      .application_opcode_width = eq2::protocol::ApplicationOpcodeWidth::one_byte});
  const auto captured_handshake = captured_pipeline.receive_datagram(session_request);
  require_eq(captured_handshake.events.size(), static_cast<std::size_t>(1),
             "captured app-combined stream accepts session request");
  const auto captured_key_request = captured_pipeline.receive_datagram(server_key_request);
  require_eq(captured_key_request.events.size(), static_cast<std::size_t>(1),
             "captured app-combined stream accepts key request");
  const auto captured_combined_key_and_login = bytes_from_hex(
      "00090000001944ffffffff3c00000067b8043cf0bcfb3e4ab2985ac4370681fee7cc37e71d3e62446c5855df8e2291510eaf140ce15d96e3e350eedfd77b56b0c82ea61c2e9b505da1df0623a9b42488e295e19129292da490099b8b4a1e86c1270ed026b6d721e3b71307e8dfcd04ecdb");
  const auto captured_result =
      captured_pipeline.receive_datagram(captured_combined_key_and_login);
  require_eq(captured_result.outbound.size(), static_cast<std::size_t>(1),
             "captured embedded key/login packet is ACKed once");
  require_eq(captured_result.events.size(), static_cast<std::size_t>(1),
             "captured embedded key/login packet emits decrypted login app event");
  require(captured_result.events.front().app_packet.has_value(),
          "captured embedded key/login packet carries app packet");
  if (captured_result.events.front().app_packet.has_value()) {
    require_eq(captured_result.events.front().app_packet->opcode, static_cast<std::uint16_t>(0),
               "captured embedded key/login packet decrypts login request opcode");
    const auto payload = captured_result.events.front().app_packet->payload;
    const auto payload_bytes = span_to_vector(payload);
    const auto username = std::string_view{"testlabs"};
    const auto password = std::string_view{"testpass"};
    const auto contains_username = std::search(payload_bytes.begin(),
                                               payload_bytes.end(),
                                               username.begin(),
                                               username.end()) != payload_bytes.end();
    const auto contains_password = std::search(payload_bytes.begin(),
                                               payload_bytes.end(),
                                               password.begin(),
                                               password.end()) != payload_bytes.end();
    require(contains_username, "captured embedded key/login packet decrypts username bytes");
    require(contains_password, "captured embedded key/login packet decrypts password bytes");
  }
  const auto captured_outbound_app = eq2::protocol::encode_application_packet(
      0x04, std::array<std::uint8_t, 1>{0xaa},
      eq2::protocol::ApplicationOpcodeWidth::one_byte);
  const auto captured_outbound =
      captured_pipeline.encode_application_protocol_packet(captured_outbound_app);
  const auto captured_outbound_protocol = eq2::protocol::decode_protocol_packet(
      eq2::protocol::strip_legacy_crc_if_present(captured_outbound, 0x33624702));
  require(captured_outbound_protocol.has_value(),
          "captured encrypted outbound app remains a protocol packet");
  if (captured_outbound_protocol.has_value() && captured_outbound_protocol->payload.size() >= 5) {
    auto encrypted_payload =
        span_to_vector(captured_outbound_protocol->payload.subspan(2));
    TestLegacyRc4 captured_server_cipher(0x1c2e9b505da1df06ULL);
    auto captured_warmup = std::array<std::uint8_t, 20>{};
    captured_server_cipher.cypher(captured_warmup);
    captured_server_cipher.cypher(encrypted_payload);
    require(encrypted_payload.size() >= 3, "captured encrypted outbound decrypts");
    if (encrypted_payload.size() >= 3) {
      require_eq(encrypted_payload[0], static_cast<std::uint8_t>(0),
                 "captured encrypted outbound preserves legacy zero opcode prefix");
      require_eq(encrypted_payload[1], static_cast<std::uint8_t>(0x04),
                 "captured encrypted outbound preserves legacy one-byte opcode");
      require_eq(encrypted_payload[2], static_cast<std::uint8_t>(0xaa),
                 "captured encrypted outbound preserves payload after legacy header");
    }
  }
  auto packed_captured_pipeline = eq2::protocol::StreamPipeline(eq2::protocol::StreamPipelineOptions{
      .application_opcode_width = eq2::protocol::ApplicationOpcodeWidth::packed_u16});
  const auto packed_captured_handshake =
      packed_captured_pipeline.receive_datagram(session_request);
  require_eq(packed_captured_handshake.events.size(), static_cast<std::size_t>(1),
             "packed captured stream accepts session request");
  const auto packed_captured_key_request =
      packed_captured_pipeline.receive_datagram(server_key_request);
  require_eq(packed_captured_key_request.events.size(), static_cast<std::size_t>(1),
             "packed captured stream accepts key request");
  const auto packed_captured_result =
      packed_captured_pipeline.receive_datagram(captured_combined_key_and_login);
  require_eq(packed_captured_result.events.size(), static_cast<std::size_t>(1),
             "packed captured stream accepts embedded key/login packet");
  const auto packed_captured_outbound_app = eq2::protocol::encode_application_packet(
      0x019a, std::array<std::uint8_t, 1>{0xaa},
      eq2::protocol::ApplicationOpcodeWidth::packed_u16);
  const auto packed_captured_outbound =
      packed_captured_pipeline.encode_application_protocol_packet(packed_captured_outbound_app);
  const auto packed_captured_outbound_protocol = eq2::protocol::decode_protocol_packet(
      eq2::protocol::strip_legacy_crc_if_present(packed_captured_outbound, 0x33624702));
  require(packed_captured_outbound_protocol.has_value(),
          "packed encrypted outbound app remains a protocol packet");
  if (packed_captured_outbound_protocol.has_value() &&
      packed_captured_outbound_protocol->payload.size() >= 7) {
    auto encrypted_payload =
        span_to_vector(packed_captured_outbound_protocol->payload.subspan(2));
    TestLegacyRc4 captured_server_cipher(0x1c2e9b505da1df06ULL);
    auto captured_warmup = std::array<std::uint8_t, 20>{};
    captured_server_cipher.cypher(captured_warmup);
    captured_server_cipher.cypher(encrypted_payload);
    require(encrypted_payload.size() >= 5, "packed encrypted outbound decrypts");
    if (encrypted_payload.size() >= 5) {
      require_eq(encrypted_payload[0], static_cast<std::uint8_t>(0),
                 "packed encrypted outbound preserves normal ClientNet control byte");
      require_eq(encrypted_payload[1], static_cast<std::uint8_t>(0xff),
                 "packed encrypted outbound preserves packed marker");
      require_eq(encrypted_payload[2], static_cast<std::uint8_t>(0x9a),
                 "packed encrypted outbound preserves packed opcode low byte");
      require_eq(encrypted_payload[3], static_cast<std::uint8_t>(0x01),
                 "packed encrypted outbound preserves packed opcode high byte");
      require_eq(encrypted_payload[4], static_cast<std::uint8_t>(0xaa),
                 "packed encrypted outbound preserves payload after packed header");
    }
  }

  const auto outbound_app =
      eq2::protocol::encode_application_packet(0x1235, std::array<std::uint8_t, 1>{0x01});
  const auto outbound = pipeline.encode_application_protocol_packet(outbound_app);
  const auto outbound_protocol = eq2::protocol::decode_protocol_packet(outbound);
  require(outbound_protocol.has_value() &&
              outbound_protocol->payload.size() >= outbound_app.size() + 2,
          "stream pipeline wraps outbound app packets with a sequence");
}

void stream_pipeline_replies_to_legacy_session_disconnect() {
  auto pipeline = eq2::protocol::StreamPipeline(eq2::protocol::StreamPipelineOptions{
      .session_key = 0x33624702,
  });
  const auto session_request =
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpSessionRequest,
                                            eq2::protocol::encode_session_request(
                                                eq2::protocol::SessionRequest{
                                                    .unknown_a = 0,
                                                    .session = 0x26ff3f0a,
                                                    .max_length = 512,
                                                }));
  const auto handshake = pipeline.receive_datagram(session_request);
  require_eq(handshake.events.size(), static_cast<std::size_t>(1),
             "disconnect parity stream accepts session request");
  require(pipeline.established(), "disconnect parity stream is established before disconnect");

  auto disconnect_payload = std::vector<std::uint8_t>(6, 0);
  eq2::core::write_u32_be(std::span<std::uint8_t>(disconnect_payload), 0, 0x26ff3f0a);
  disconnect_payload[5] = 0x0e;
  const auto client_disconnect = eq2::protocol::append_legacy_crc(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpSessionDisconnect,
                                            disconnect_payload),
      0x33624702);
  const auto disconnect = pipeline.receive_datagram(client_disconnect);
  require_eq(disconnect.events.size(), static_cast<std::size_t>(1),
             "disconnect parity stream emits one disconnect event");
  require_eq(disconnect.events.front().type, eq2::protocol::StreamEventType::disconnected,
             "disconnect parity stream records protocol disconnect");
  require_eq(disconnect.outbound.size(), static_cast<std::size_t>(1),
             "disconnect parity stream replies to legacy session disconnect");
  require(!pipeline.established(), "disconnect parity stream clears established state");

  const auto server_disconnect = eq2::protocol::decode_protocol_packet(
      eq2::protocol::strip_legacy_crc_if_present(disconnect.outbound.front(), 0x33624702));
  require(server_disconnect.has_value(), "disconnect parity reply decodes");
  require(server_disconnect.has_value() &&
              server_disconnect->opcode == eq2::protocol::kOpSessionDisconnect,
          "disconnect parity reply uses session disconnect opcode");
  require(server_disconnect.has_value() && server_disconnect->payload.size() == 6,
          "disconnect parity reply preserves legacy payload size");
  if (server_disconnect.has_value() && server_disconnect->payload.size() == 6) {
    require_eq(eq2::core::read_u32_be(server_disconnect->payload, 0),
               static_cast<std::uint32_t>(0x26ff3f0a),
               "disconnect parity reply keeps client session id");
    require_eq(server_disconnect->payload[4], static_cast<std::uint8_t>(0),
               "disconnect parity reply keeps zero reason high byte");
    require_eq(server_disconnect->payload[5], static_cast<std::uint8_t>(0x06),
               "disconnect parity reply uses legacy server disconnect reason");
  }
}

void stream_pipeline_handles_reliable_ordering_and_fragments() {
  const auto session_request =
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpSessionRequest,
                                            eq2::protocol::encode_session_request(
                                                eq2::protocol::SessionRequest{
                                                    .unknown_a = 0,
                                                    .session = 0x11121314,
                                                    .max_length = 512,
                                                }));
  const auto make_sequenced_packet =
      [](std::uint16_t protocol_opcode,
         std::uint16_t sequence,
         std::span<const std::uint8_t> payload) {
        eq2::protocol::PacketWriter writer;
        writer.append_u16_be(sequence);
        writer.append_bytes(payload);
        return eq2::protocol::encode_protocol_packet(protocol_opcode, writer.bytes());
      };

  auto ordered_pipeline = eq2::protocol::StreamPipeline{};
  const auto ordered_handshake = ordered_pipeline.receive_datagram(session_request);
  require_eq(ordered_handshake.events.size(), static_cast<std::size_t>(1),
             "ordering stream accepts session request");

  const auto app_zero =
      eq2::protocol::encode_application_packet(0x2100, std::array<std::uint8_t, 1>{0xa0});
  const auto app_one =
      eq2::protocol::encode_application_packet(0x2101, std::array<std::uint8_t, 1>{0xa1});

  const auto future_result = ordered_pipeline.receive_datagram(
      make_sequenced_packet(eq2::protocol::kOpPacket, 1, app_one));
  require(future_result.events.empty(), "future sequenced packet is queued without dispatch");
  require(future_result.outbound.empty(), "future sequenced packet is not ACKed before the gap closes");

  const auto ordered_result = ordered_pipeline.receive_datagram(
      make_sequenced_packet(eq2::protocol::kOpPacket, 0, app_zero));
  require_eq(ordered_result.events.size(), static_cast<std::size_t>(2),
             "current packet drains queued future packet in order");
  require_eq(ordered_result.outbound.size(), static_cast<std::size_t>(2),
             "current and queued future packets are ACKed");
  if (ordered_result.events.size() == 2 && ordered_result.events[0].app_packet.has_value() &&
      ordered_result.events[1].app_packet.has_value()) {
    require_eq(ordered_result.events[0].app_packet->opcode, static_cast<std::uint16_t>(0x2100),
               "current packet is dispatched before queued future packet");
    require_eq(ordered_result.events[1].app_packet->opcode, static_cast<std::uint16_t>(0x2101),
               "queued future packet dispatches after the gap closes");
  }

  const auto duplicate_result = ordered_pipeline.receive_datagram(
      make_sequenced_packet(eq2::protocol::kOpPacket, 0, app_zero));
  require(duplicate_result.events.empty(), "duplicate sequenced packet is not dispatched twice");
  require_eq(duplicate_result.outbound.size(), static_cast<std::size_t>(1),
             "duplicate sequenced packet emits an out-of-order ACK");
  if (!duplicate_result.outbound.empty()) {
    const auto duplicate_ack = eq2::protocol::decode_protocol_packet(
        eq2::protocol::strip_legacy_crc_if_present(duplicate_result.outbound.front(), 0x33624702));
    require(duplicate_ack.has_value() &&
                duplicate_ack->opcode == eq2::protocol::kOpOutOfOrderAck,
            "duplicate sequenced packet uses OP_OutOfOrderAck");
  }

  auto fragment_pipeline = eq2::protocol::StreamPipeline{};
  const auto fragment_handshake = fragment_pipeline.receive_datagram(session_request);
  require_eq(fragment_handshake.events.size(), static_cast<std::size_t>(1),
             "fragment stream accepts session request");

  const auto fragment_app =
      eq2::protocol::encode_application_packet(0x3456,
                                               std::array<std::uint8_t, 4>{0xaa, 0xbb, 0xcc, 0xdd});
  eq2::protocol::PacketWriter first_fragment;
  first_fragment.append_u16_be(0);
  first_fragment.append_u32_be(static_cast<std::uint32_t>(fragment_app.size()));
  first_fragment.append_bytes(std::span<const std::uint8_t>(fragment_app).first(3));
  const auto first_fragment_result = fragment_pipeline.receive_datagram(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpFragment,
                                            first_fragment.bytes()));
  require(first_fragment_result.events.empty(), "first fragment waits for reassembly");
  require_eq(first_fragment_result.outbound.size(), static_cast<std::size_t>(1),
             "first fragment is ACKed");

  eq2::protocol::PacketWriter second_fragment;
  second_fragment.append_u16_be(1);
  second_fragment.append_bytes(std::span<const std::uint8_t>(fragment_app).subspan(3));
  const auto second_fragment_result = fragment_pipeline.receive_datagram(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpFragment,
                                            second_fragment.bytes()));
  require_eq(second_fragment_result.outbound.size(), static_cast<std::size_t>(1),
             "final fragment is ACKed");
  require_eq(second_fragment_result.events.size(), static_cast<std::size_t>(1),
             "final fragment emits the reassembled app packet");
  if (!second_fragment_result.events.empty() &&
      second_fragment_result.events.front().app_packet.has_value()) {
    require_eq(second_fragment_result.events.front().app_packet->opcode,
               static_cast<std::uint16_t>(0x3456),
               "fragment reassembly preserves app opcode");
    require_eq(second_fragment_result.events.front().app_packet->payload.size(),
               static_cast<std::size_t>(4),
               "fragment reassembly preserves app payload");
  }
}

void stream_pipeline_handles_resend_and_reconnect_paths() {
  const auto make_session_request = [](std::uint32_t session) {
    return eq2::protocol::encode_protocol_packet(
        eq2::protocol::kOpSessionRequest,
        eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
            .unknown_a = 0,
            .session = session,
            .max_length = 512,
        }));
  };
  const auto make_control_packet = [](std::uint16_t protocol_opcode,
                                      std::uint16_t sequence) {
    eq2::protocol::PacketWriter writer;
    writer.append_u16_be(sequence);
    return eq2::protocol::encode_protocol_packet(protocol_opcode, writer.bytes());
  };
  const auto make_sequenced_packet = [](std::uint16_t sequence,
                                        std::span<const std::uint8_t> payload) {
    eq2::protocol::PacketWriter writer;
    writer.append_u16_be(sequence);
    writer.append_bytes(payload);
    return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket,
                                                 writer.bytes());
  };

  auto resend_pipeline = eq2::protocol::StreamPipeline{};
  const auto handshake = resend_pipeline.receive_datagram(make_session_request(0x11121314));
  require_eq(handshake.events.size(), static_cast<std::size_t>(1),
             "resend stream accepts session request");

  const auto outbound_app =
      eq2::protocol::encode_application_packet(0x4500,
                                               std::array<std::uint8_t, 1>{0xab});
  const auto outbound = resend_pipeline.encode_application_protocol_packet(outbound_app);
  const auto outbound_protocol = eq2::protocol::decode_protocol_packet(
      eq2::protocol::strip_legacy_crc_if_present(outbound, 0x33624702));
  require(outbound_protocol.has_value() && outbound_protocol->payload.size() >= 2,
          "resend stream emits a sequenced outbound packet");
  if (outbound_protocol.has_value() && outbound_protocol->payload.size() >= 2) {
    require_eq(eq2::core::read_u16_be(outbound_protocol->payload, 0),
               static_cast<std::uint16_t>(0),
               "first outbound reliable packet uses sequence zero");
  }

  const auto resend = resend_pipeline.receive_datagram(
      make_control_packet(eq2::protocol::kOpOutOfOrderAck, 0));
  require_eq(resend.outbound.size(), static_cast<std::size_t>(1),
             "out-of-order ACK resends a retained outbound packet");
  if (!resend.outbound.empty()) {
    require(resend.outbound.front() == outbound,
            "out-of-order ACK resends the same encoded reliable packet bytes");
  }
  require(resend.events.empty(), "out-of-order ACK does not emit an application event");

  const auto ack = resend_pipeline.receive_datagram(
      make_control_packet(eq2::protocol::kOpAck, 0));
  require(ack.events.empty(), "ACK consumes without application events");
  const auto after_ack_resend = resend_pipeline.receive_datagram(
      make_control_packet(eq2::protocol::kOpOutOfOrderAck, 0));
  require(after_ack_resend.outbound.empty(),
          "ACK removes retained outbound packet from resend history");

  auto reconnect_pipeline = eq2::protocol::StreamPipeline{};
  const auto first_handshake =
      reconnect_pipeline.receive_datagram(make_session_request(0x22222222));
  require_eq(first_handshake.events.size(), static_cast<std::size_t>(1),
             "reconnect stream accepts first session request");
  const auto app_zero =
      eq2::protocol::encode_application_packet(0x5100,
                                               std::array<std::uint8_t, 1>{0x10});
  const auto app_one =
      eq2::protocol::encode_application_packet(0x5101,
                                               std::array<std::uint8_t, 1>{0x11});
  const auto app_two =
      eq2::protocol::encode_application_packet(0x5102,
                                               std::array<std::uint8_t, 1>{0x12});
  const auto future_before_reconnect =
      reconnect_pipeline.receive_datagram(make_sequenced_packet(2, app_two));
  require(future_before_reconnect.events.empty(),
          "future packet before reconnect is queued");

  const auto reconnect =
      reconnect_pipeline.receive_datagram(make_session_request(0x33333333));
  require_eq(reconnect.events.size(), static_cast<std::size_t>(1),
             "reconnect stream accepts a fresh session request");
  require_eq(reconnect_pipeline.session_id(), static_cast<std::uint32_t>(0x33333333),
             "reconnect stream updates the active session id");

  const auto after_reconnect_zero =
      reconnect_pipeline.receive_datagram(make_sequenced_packet(0, app_zero));
  require_eq(after_reconnect_zero.events.size(), static_cast<std::size_t>(1),
             "reconnect resets inbound sequence to zero");
  const auto after_reconnect_one =
      reconnect_pipeline.receive_datagram(make_sequenced_packet(1, app_one));
  require_eq(after_reconnect_one.events.size(), static_cast<std::size_t>(1),
             "reconnect clears queued packets from the prior session");
  if (after_reconnect_one.events.size() == 1 &&
      after_reconnect_one.events.front().app_packet.has_value()) {
    require_eq(after_reconnect_one.events.front().app_packet->opcode,
               static_cast<std::uint16_t>(0x5101),
               "reconnect does not drain stale queued future packets");
  }
}

}  // namespace

int main() {
  packet_reader_and_writer_handle_bounds();
  packet_field_helpers_handle_eq2_length_prefixed_strings();
  session_request_round_trips_legacy_wire_order();
  session_response_round_trips_legacy_wire_order();
  protocol_packet_headers_match_legacy_wire_order();
  application_packet_headers_preserve_legacy_opcode_forms();
  opcode_tables_and_version_ranges_match_legacy_lookup();
  versioned_packet_registry_resolves_client_versions();
  combined_packet_framing_preserves_legacy_length_prefixes();
  outbound_protocol_datagrams_can_coalesce_like_legacy_stream_writer();
  transform_policy_marks_compression_encryption_boundaries();
  interserver_packet_framing_matches_legacy_tcp_server_packets();
  server_ls_info_payload_decodes_fixed_legacy_fields();
  user_to_world_payloads_preserve_login_world_handoff_fields();
  world_update_payloads_match_legacy_login_support_structs();
  create_character_payloads_match_legacy_login_world_exchange();
  login_response_payloads_serialize_reply_and_world_list();
  legacy_login_request_decodes_length_prefixed_credentials_and_version();
  login_by_num_request_decodes_legacy_and_extended_world_entry_layouts();
  login_request_version_fallbacks_match_phase1_inventory();
  play_character_request_and_response_packets_match_legacy_login_layouts();
  delete_character_request_and_response_packets_match_legacy_login_layouts();
  client_log_payloads_inflate_legacy_compressed_messages();
  stream_pipeline_handles_session_handshake_and_app_dispatch();
  stream_pipeline_replies_to_legacy_session_disconnect();
  stream_pipeline_handles_reliable_ordering_and_fragments();
  stream_pipeline_handles_resend_and_reconnect_paths();

  if (failures != 0) {
    std::cerr << failures << " protocol assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
