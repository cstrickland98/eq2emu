#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

inline constexpr std::uint16_t kServerOpKeepAlive = 0x0001;
inline constexpr std::uint16_t kServerOpLsInfo = 0x1000;
inline constexpr std::uint16_t kServerOpLsStatus = 0x1001;
inline constexpr std::size_t kServerLsInfoPayloadSize = 832;

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

}  // namespace eq2::protocol
