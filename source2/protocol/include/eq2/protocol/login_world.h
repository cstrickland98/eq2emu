#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

inline constexpr std::uint16_t kServerOpKeepAlive = 0x0001;
inline constexpr std::uint16_t kServerOpLsInfo = 0x1000;
inline constexpr std::uint16_t kServerOpLsStatus = 0x1001;
inline constexpr std::uint16_t kServerOpUserToWorldRequest = 0xab00;
inline constexpr std::uint16_t kServerOpUserToWorldResponse = 0xab01;
inline constexpr std::size_t kServerLsInfoPayloadSize = 832;
inline constexpr std::size_t kUserToWorldRequestPayloadSize = 41;
inline constexpr std::size_t kUserToWorldResponsePayloadSize = 109;

struct ServerLsInfo {
  std::string_view world_name;
  std::string_view address;
  std::string_view account;
  std::string_view password;
  std::string_view protocol_version;
  std::string_view server_version;
  std::uint8_t server_type = 0;
  std::uint32_t database_version = 0;
};

struct UserToWorldRequest {
  std::int32_t login_account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t world_id = 0;
  std::int32_t from_id = 0;
  std::int32_t to_id = 0;
  std::string_view ip_address;
};

struct UserToWorldResponse {
  std::int32_t login_account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t world_id = 0;
  std::int32_t access_key = 0;
  std::uint8_t response = 0;
  std::string_view ip_address;
  std::int32_t port = 0;
  std::int32_t from_id = 0;
  std::int32_t to_id = 0;
};

namespace detail {

inline auto fixed_c_string_view(std::span<const std::uint8_t> bytes) -> std::string_view {
  const auto end = std::find(bytes.begin(), bytes.end(), std::uint8_t{0});
  const auto size = static_cast<std::size_t>(end - bytes.begin());
  return std::string_view(reinterpret_cast<const char*>(bytes.data()), size);
}

inline auto read_fixed_c_string(PacketReader& reader, std::size_t size)
    -> std::optional<std::string_view> {
  const auto bytes = reader.read_bytes(size);
  if (!bytes.has_value()) {
    return std::nullopt;
  }

  return fixed_c_string_view(*bytes);
}

inline auto read_interserver_i32(PacketReader& reader) -> std::optional<std::int32_t> {
  const auto value = reader.read_u32_le();
  if (!value.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::int32_t>(*value);
}

inline void append_interserver_i32(PacketWriter& writer, std::int32_t value) {
  writer.append_u32_le(static_cast<std::uint32_t>(value));
}

inline void append_fixed_c_string(PacketWriter& writer, std::string_view value, std::size_t size) {
  std::vector<std::uint8_t> bytes(size, 0);
  const auto copy_size = std::min(value.size(), size);
  std::copy_n(reinterpret_cast<const std::uint8_t*>(value.data()), copy_size, bytes.data());
  writer.append_bytes(bytes);
}

}  // namespace detail

inline auto decode_server_ls_info_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<ServerLsInfo> {
  if (bytes.size() != kServerLsInfoPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto world_name = detail::read_fixed_c_string(reader, 201);
  auto address = detail::read_fixed_c_string(reader, 250);
  auto account = detail::read_fixed_c_string(reader, 31);
  auto password = detail::read_fixed_c_string(reader, 256);
  auto protocol_version = detail::read_fixed_c_string(reader, 25);
  auto server_version = detail::read_fixed_c_string(reader, 64);
  auto server_type = reader.read_u8();
  auto database_version = reader.read_u32_le();
  if (!world_name || !address || !account || !password || !protocol_version || !server_version ||
      !server_type || !database_version) {
    return std::nullopt;
  }

  return ServerLsInfo{
      .world_name = *world_name,
      .address = *address,
      .account = *account,
      .password = *password,
      .protocol_version = *protocol_version,
      .server_version = *server_version,
      .server_type = *server_type,
      .database_version = *database_version,
  };
}

inline auto decode_user_to_world_request_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<UserToWorldRequest> {
  if (bytes.size() != kUserToWorldRequestPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto login_account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto world_id = detail::read_interserver_i32(reader);
  auto from_id = detail::read_interserver_i32(reader);
  auto to_id = detail::read_interserver_i32(reader);
  auto ip_address = detail::read_fixed_c_string(reader, 21);
  if (!login_account_id || !character_id || !world_id || !from_id || !to_id || !ip_address) {
    return std::nullopt;
  }

  return UserToWorldRequest{
      .login_account_id = *login_account_id,
      .character_id = *character_id,
      .world_id = *world_id,
      .from_id = *from_id,
      .to_id = *to_id,
      .ip_address = *ip_address,
  };
}

inline auto encode_user_to_world_request_payload(const UserToWorldRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, request.login_account_id);
  detail::append_interserver_i32(writer, request.character_id);
  detail::append_interserver_i32(writer, request.world_id);
  detail::append_interserver_i32(writer, request.from_id);
  detail::append_interserver_i32(writer, request.to_id);
  detail::append_fixed_c_string(writer, request.ip_address, 21);
  return std::move(writer).into_bytes();
}

inline auto decode_user_to_world_response_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<UserToWorldResponse> {
  if (bytes.size() != kUserToWorldResponsePayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto login_account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto world_id = detail::read_interserver_i32(reader);
  auto access_key = detail::read_interserver_i32(reader);
  auto response = reader.read_u8();
  auto ip_address = detail::read_fixed_c_string(reader, 80);
  auto port = detail::read_interserver_i32(reader);
  auto from_id = detail::read_interserver_i32(reader);
  auto to_id = detail::read_interserver_i32(reader);
  if (!login_account_id || !character_id || !world_id || !access_key || !response ||
      !ip_address || !port || !from_id || !to_id) {
    return std::nullopt;
  }

  return UserToWorldResponse{
      .login_account_id = *login_account_id,
      .character_id = *character_id,
      .world_id = *world_id,
      .access_key = *access_key,
      .response = *response,
      .ip_address = *ip_address,
      .port = *port,
      .from_id = *from_id,
      .to_id = *to_id,
  };
}

inline auto encode_user_to_world_response_payload(const UserToWorldResponse& response)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, response.login_account_id);
  detail::append_interserver_i32(writer, response.character_id);
  detail::append_interserver_i32(writer, response.world_id);
  detail::append_interserver_i32(writer, response.access_key);
  writer.append_u8(response.response);
  detail::append_fixed_c_string(writer, response.ip_address, 80);
  detail::append_interserver_i32(writer, response.port);
  detail::append_interserver_i32(writer, response.from_id);
  detail::append_interserver_i32(writer, response.to_id);
  return std::move(writer).into_bytes();
}

}  // namespace eq2::protocol
