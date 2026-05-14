#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/delete_character.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/login_request.h>
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

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <span>
#include <string>
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
}

void user_to_world_payloads_preserve_login_world_handoff_fields() {
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
             "client version field follows the four trailing unknown strings");

  auto truncated = fixture;
  truncated.pop_back();
  require(!eq2::protocol::parse_legacy_login_request(truncated).has_value(),
          "truncated login request is rejected in protocol");
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
      });
  const std::vector<std::uint8_t> expected_modern_request{
      0xe9, 0x03, 0x00, 0x00,
      0x4d, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00,
  };
  require(modern_request == expected_modern_request,
          "modern play request stores character id, server id, and three legacy unknown bytes");

  const auto parsed_modern = eq2::protocol::parse_play_character_request(modern_request, 284);
  require(parsed_modern.has_value(), "modern play request parses");
  require_eq(parsed_modern->character_id, 1001, "modern play request character id decodes");
  require_eq(parsed_modern->server_id, 77, "modern play request server id decodes");

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
      .account_id = 42,
      .character_name = "Alys",
      .max_characters = 10,
  };

  const auto response_payload = eq2::protocol::encode_delete_character_response_payload(response);
  const std::vector<std::uint8_t> expected_response{
      0x01,
      0x4d, 0x00, 0x00, 0x00,
      0xe9, 0x03, 0x00, 0x00,
      0x2a, 0x00, 0x00, 0x00,
      0x04, 0x00,
      'A', 'l', 'y', 's',
      0x0a, 0x00, 0x00, 0x00,
  };
  require(response_payload == expected_response,
          "delete response stores response code, ids, character name, and max character count");

  const auto parsed_response =
      eq2::protocol::decode_delete_character_response_payload(response_payload);
  require(parsed_response.has_value(), "delete response decodes");
  require_eq(parsed_response->response, response.response, "delete response code decodes");
  require_eq(parsed_response->account_id, response.account_id, "delete response account id decodes");
  require_eq(parsed_response->max_characters, response.max_characters,
             "delete response max character count decodes");

  auto truncated = request_payload;
  truncated.pop_back();
  require(!eq2::protocol::parse_delete_character_request(truncated).has_value(),
          "truncated delete request is rejected");
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
  transform_policy_marks_compression_encryption_boundaries();
  interserver_packet_framing_matches_legacy_tcp_server_packets();
  server_ls_info_payload_decodes_fixed_legacy_fields();
  user_to_world_payloads_preserve_login_world_handoff_fields();
  legacy_login_request_decodes_length_prefixed_credentials_and_version();
  login_by_num_request_decodes_legacy_and_extended_world_entry_layouts();
  login_request_version_fallbacks_match_phase1_inventory();
  play_character_request_and_response_packets_match_legacy_login_layouts();
  delete_character_request_and_response_packets_match_legacy_login_layouts();

  if (failures != 0) {
    std::cerr << failures << " protocol assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
