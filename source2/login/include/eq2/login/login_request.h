#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eq2::login {

struct ParsedLoginRequest {
  std::string access_code;
  std::string username;
  std::string password;
  std::int16_t version = 0;
};

namespace detail {

inline auto read_i16_le(std::span<const std::uint8_t> bytes, std::size_t& offset)
    -> std::optional<std::int16_t> {
  if (offset + 2 > bytes.size()) {
    return std::nullopt;
  }

  const auto value = static_cast<std::uint16_t>(bytes[offset]) |
                     static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
  offset += 2;
  return static_cast<std::int16_t>(value);
}

inline auto read_i32_le(std::span<const std::uint8_t> bytes, std::size_t& offset)
    -> std::optional<std::int32_t> {
  if (offset + 4 > bytes.size()) {
    return std::nullopt;
  }

  const auto value = static_cast<std::uint32_t>(bytes[offset]) |
                     (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
                     (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
                     (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
  offset += 4;
  return static_cast<std::int32_t>(value);
}

inline auto read_eq2_16bit_string(std::span<const std::uint8_t> bytes, std::size_t& offset)
    -> std::optional<std::string> {
  const auto size = read_i16_le(bytes, offset);
  if (!size || *size < 0) {
    return std::nullopt;
  }

  const auto string_size = static_cast<std::size_t>(*size);
  if (offset + string_size > bytes.size()) {
    return std::nullopt;
  }

  auto value = std::string(reinterpret_cast<const char*>(bytes.data() + offset), string_size);
  offset += string_size;
  return value;
}

}  // namespace detail

inline auto parse_legacy_login_request(std::span<const std::uint8_t> bytes)
    -> std::optional<ParsedLoginRequest> {
  auto offset = std::size_t{0};

  auto access_code = detail::read_eq2_16bit_string(bytes, offset);
  if (!access_code) {
    return std::nullopt;
  }

  if (!detail::read_eq2_16bit_string(bytes, offset)) {
    return std::nullopt;
  }

  auto username = detail::read_eq2_16bit_string(bytes, offset);
  auto password = detail::read_eq2_16bit_string(bytes, offset);
  if (!username || !password) {
    return std::nullopt;
  }

  for (auto i = 0; i < 4; ++i) {
    if (!detail::read_eq2_16bit_string(bytes, offset)) {
      return std::nullopt;
    }
  }

  auto version = detail::read_i16_le(bytes, offset);
  if (!version) {
    return std::nullopt;
  }

  for (auto i = 0; i < 2; ++i) {
    if (!detail::read_i32_le(bytes, offset)) {
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
  auto bytes = std::vector<std::uint8_t>{};

  const auto append_i16 = [&bytes](std::int16_t value) {
    const auto unsigned_value = static_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(unsigned_value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((unsigned_value >> 8U) & 0xffU));
  };

  const auto append_i32 = [&bytes](std::int32_t value) {
    const auto unsigned_value = static_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(unsigned_value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((unsigned_value >> 8U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((unsigned_value >> 16U) & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((unsigned_value >> 24U) & 0xffU));
  };

  const auto append_string = [&append_i16, &bytes](const std::string& value) {
    append_i16(static_cast<std::int16_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
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

  return bytes;
}

}  // namespace eq2::login
