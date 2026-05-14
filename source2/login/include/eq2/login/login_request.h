#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::login {

struct ParsedLoginRequest {
  std::string access_code;
  std::string username;
  std::string password;
  std::int16_t version = 0;
};

namespace detail {

inline auto read_i16_le(eq2::protocol::PacketReader& reader) -> std::optional<std::int16_t> {
  const auto value = reader.read_u16_le();
  if (!value.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::int16_t>(*value);
}

inline auto read_i32_le(eq2::protocol::PacketReader& reader) -> std::optional<std::int32_t> {
  const auto value = reader.read_u32_le();
  if (!value.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::int32_t>(*value);
}

inline auto read_eq2_16bit_string(eq2::protocol::PacketReader& reader)
    -> std::optional<std::string> {
  const auto size = read_i16_le(reader);
  if (!size || *size < 0) {
    return std::nullopt;
  }

  const auto string_size = static_cast<std::size_t>(*size);
  const auto bytes = reader.read_bytes(string_size);
  if (!bytes.has_value()) {
    return std::nullopt;
  }

  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

}  // namespace detail

inline auto parse_legacy_login_request(std::span<const std::uint8_t> bytes)
    -> std::optional<ParsedLoginRequest> {
  eq2::protocol::PacketReader reader(bytes);

  auto access_code = detail::read_eq2_16bit_string(reader);
  if (!access_code) {
    return std::nullopt;
  }

  if (!detail::read_eq2_16bit_string(reader)) {
    return std::nullopt;
  }

  auto username = detail::read_eq2_16bit_string(reader);
  auto password = detail::read_eq2_16bit_string(reader);
  if (!username || !password) {
    return std::nullopt;
  }

  for (auto i = 0; i < 4; ++i) {
    if (!detail::read_eq2_16bit_string(reader)) {
      return std::nullopt;
    }
  }

  auto version = detail::read_i16_le(reader);
  if (!version) {
    return std::nullopt;
  }

  for (auto i = 0; i < 2; ++i) {
    if (!detail::read_i32_le(reader)) {
      return std::nullopt;
    }
  }

  return ParsedLoginRequest{
      .access_code = std::move(*access_code),
      .username = std::move(*username),
      .password = std::move(*password),
      .version = *version,
  };
}

inline auto encode_legacy_login_request_fixture(const ParsedLoginRequest& request)
    -> std::vector<std::uint8_t> {
  eq2::protocol::PacketWriter writer;

  const auto append_i16 = [&writer](std::int16_t value) {
    writer.append_u16_le(static_cast<std::uint16_t>(value));
  };

  const auto append_i32 = [&writer](std::int32_t value) {
    writer.append_u32_le(static_cast<std::uint32_t>(value));
  };

  const auto append_string = [&append_i16, &writer](const std::string& value) {
    append_i16(static_cast<std::int16_t>(value.size()));
    writer.append_bytes(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()),
        value.size()));
  };

  append_string(request.access_code);
  append_string("");
  append_string(request.username);
  append_string(request.password);

  for (auto i = 0; i < 4; ++i) {
    append_string("");
  }

  append_i16(request.version);
  append_i32(0);
  append_i32(0);

  return std::move(writer).into_bytes();
}

}  // namespace eq2::login
