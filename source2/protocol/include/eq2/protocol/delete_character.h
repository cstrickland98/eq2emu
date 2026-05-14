#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_fields.h>

namespace eq2::protocol {

struct DeleteCharacterRequest {
  std::int32_t character_id = 0;
  std::int32_t server_id = 0;
  std::int32_t unknown = 0;
  std::string character_name;
};

struct DeleteCharacterResponse {
  std::uint8_t response = 0;
  std::int32_t server_id = 0;
  std::int32_t character_id = 0;
  std::int32_t account_id = 0;
  std::string character_name;
  std::int32_t max_characters = 0;
};

inline auto parse_delete_character_request(std::span<const std::uint8_t> bytes)
    -> std::optional<DeleteCharacterRequest> {
  PacketReader reader(bytes);

  auto character_id = read_i32_le(reader);
  auto server_id = read_i32_le(reader);
  auto unknown = read_i32_le(reader);
  auto character_name = read_eq2_16bit_string(reader);
  if (!character_id || !server_id || !unknown || !character_name) {
    return std::nullopt;
  }

  return DeleteCharacterRequest{
      .character_id = *character_id,
      .server_id = *server_id,
      .unknown = *unknown,
      .character_name = std::move(*character_name),
  };
}

inline auto encode_delete_character_request_fixture(const DeleteCharacterRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  append_i32_le(writer, request.character_id);
  append_i32_le(writer, request.server_id);
  append_i32_le(writer, request.unknown);
  append_eq2_16bit_string(writer, request.character_name);
  return std::move(writer).into_bytes();
}

inline auto encode_delete_character_response_payload(const DeleteCharacterResponse& response)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u8(response.response);
  append_i32_le(writer, response.server_id);
  append_i32_le(writer, response.character_id);
  append_i32_le(writer, response.account_id);
  append_eq2_16bit_string(writer, response.character_name);
  append_i32_le(writer, response.max_characters);
  return std::move(writer).into_bytes();
}

inline auto decode_delete_character_response_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<DeleteCharacterResponse> {
  PacketReader reader(bytes);

  auto response = reader.read_u8();
  auto server_id = read_i32_le(reader);
  auto character_id = read_i32_le(reader);
  auto account_id = read_i32_le(reader);
  auto character_name = read_eq2_16bit_string(reader);
  auto max_characters = read_i32_le(reader);
  if (!response || !server_id || !character_id || !account_id || !character_name ||
      !max_characters) {
    return std::nullopt;
  }

  return DeleteCharacterResponse{
      .response = *response,
      .server_id = *server_id,
      .character_id = *character_id,
      .account_id = *account_id,
      .character_name = std::move(*character_name),
      .max_characters = *max_characters,
  };
}

}  // namespace eq2::protocol
