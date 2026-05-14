#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_fields.h>

namespace eq2::protocol {

inline constexpr std::uint16_t kModernPlayCharacterRequestVersion = 284;
inline constexpr std::uint16_t kPlayCharacterResponseUnknown1Version = 1096;
inline constexpr std::uint16_t kPlayCharacterResponseExpandedUnknownsVersion = 60085;

struct PlayCharacterRequest {
  std::int32_t character_id = 0;
  std::int32_t server_id = 0;
  std::string character_name;
};

struct PlayCharacterResponse {
  std::uint8_t response = 0;
  std::string server;
  std::uint16_t port = 0;
  std::int32_t account_id = 0;
  std::int32_t access_code = 0;
};

inline auto parse_legacy_play_character_request(std::span<const std::uint8_t> bytes)
    -> std::optional<PlayCharacterRequest> {
  PacketReader reader(bytes);

  auto character_id = read_i32_le(reader);
  auto character_name = read_eq2_16bit_string(reader);
  if (!character_id || !character_name) {
    return std::nullopt;
  }

  return PlayCharacterRequest{
      .character_id = *character_id,
      .character_name = std::move(*character_name),
  };
}

inline auto parse_modern_play_character_request(std::span<const std::uint8_t> bytes)
    -> std::optional<PlayCharacterRequest> {
  PacketReader reader(bytes);

  auto character_id = read_i32_le(reader);
  auto server_id = read_i32_le(reader);
  auto unknown = reader.read_bytes(3);
  if (!character_id || !server_id || !unknown) {
    return std::nullopt;
  }

  return PlayCharacterRequest{
      .character_id = *character_id,
      .server_id = *server_id,
  };
}

inline auto parse_play_character_request(std::span<const std::uint8_t> bytes,
                                         std::uint16_t client_version)
    -> std::optional<PlayCharacterRequest> {
  if (client_version < kModernPlayCharacterRequestVersion) {
    return parse_legacy_play_character_request(bytes);
  }

  return parse_modern_play_character_request(bytes);
}

inline auto encode_legacy_play_character_request_fixture(const PlayCharacterRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  append_i32_le(writer, request.character_id);
  append_eq2_16bit_string(writer, request.character_name);
  return std::move(writer).into_bytes();
}

inline auto encode_modern_play_character_request_fixture(const PlayCharacterRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  append_i32_le(writer, request.character_id);
  append_i32_le(writer, request.server_id);
  writer.append_u8(0);
  writer.append_u8(0);
  writer.append_u8(0);
  return std::move(writer).into_bytes();
}

inline auto encode_play_character_response_payload(const PlayCharacterResponse& response,
                                                   std::uint16_t client_version)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u8(response.response);

  if (client_version >= kPlayCharacterResponseExpandedUnknownsVersion) {
    writer.append_u16_le(0);
    writer.append_u16_le(0);
    writer.append_u16_le(0);
  } else if (client_version >= kPlayCharacterResponseUnknown1Version) {
    writer.append_u16_le(0);
  }

  append_eq2_8bit_string(writer, response.server);
  writer.append_u16_le(response.port);
  append_i32_le(writer, response.account_id);
  append_i32_le(writer, response.access_code);
  return std::move(writer).into_bytes();
}

inline auto decode_play_character_response_payload(std::span<const std::uint8_t> bytes,
                                                   std::uint16_t client_version)
    -> std::optional<PlayCharacterResponse> {
  PacketReader reader(bytes);
  auto response = reader.read_u8();
  if (!response.has_value()) {
    return std::nullopt;
  }

  if (client_version >= kPlayCharacterResponseExpandedUnknownsVersion) {
    if (!reader.read_u16_le() || !reader.read_u16_le() || !reader.read_u16_le()) {
      return std::nullopt;
    }
  } else if (client_version >= kPlayCharacterResponseUnknown1Version) {
    if (!reader.read_u16_le()) {
      return std::nullopt;
    }
  }

  auto server = read_eq2_8bit_string(reader);
  auto port = reader.read_u16_le();
  auto account_id = read_i32_le(reader);
  auto access_code = read_i32_le(reader);
  if (!server || !port || !account_id || !access_code) {
    return std::nullopt;
  }

  return PlayCharacterResponse{
      .response = *response,
      .server = std::move(*server),
      .port = *port,
      .account_id = *account_id,
      .access_code = *access_code,
  };
}

}  // namespace eq2::protocol
