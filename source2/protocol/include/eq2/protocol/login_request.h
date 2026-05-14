#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <eq2/protocol/opcode_version.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_fields.h>

namespace eq2::protocol {

inline constexpr std::int16_t kLegacyLoginRequestStructVersion = 1;
inline constexpr std::int16_t kExtendedLoginRequestStructVersion = 1208;

struct LoginRequest {
  std::string access_code;
  std::string username;
  std::string password;
  std::int16_t version = 0;
};

struct LoginByNumRequest {
  std::int32_t account_id = 0;
  std::int32_t access_code = 0;
  std::int16_t version = 0;
};

inline auto parse_legacy_login_request(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginRequest> {
  PacketReader reader(bytes);

  auto access_code = read_eq2_16bit_string(reader);
  if (!access_code) {
    return std::nullopt;
  }

  if (!read_eq2_16bit_string(reader)) {
    return std::nullopt;
  }

  auto username = read_eq2_16bit_string(reader);
  auto password = read_eq2_16bit_string(reader);
  if (!username || !password) {
    return std::nullopt;
  }

  for (auto i = 0; i < 4; ++i) {
    if (!read_eq2_16bit_string(reader)) {
      return std::nullopt;
    }
  }

  auto version = read_i16_le(reader);
  if (!version) {
    return std::nullopt;
  }

  for (auto i = 0; i < 2; ++i) {
    if (!read_i32_le(reader)) {
      return std::nullopt;
    }
  }

  return LoginRequest{
      .access_code = std::move(*access_code),
      .username = std::move(*username),
      .password = std::move(*password),
      .version = *version,
  };
}

inline auto encode_legacy_login_request_fixture(const LoginRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;

  append_eq2_16bit_string(writer, request.access_code);
  append_eq2_16bit_string(writer, "");
  append_eq2_16bit_string(writer, request.username);
  append_eq2_16bit_string(writer, request.password);

  for (auto i = 0; i < 4; ++i) {
    append_eq2_16bit_string(writer, "");
  }

  append_i16_le(writer, request.version);
  append_i32_le(writer, 0);
  append_i32_le(writer, 0);

  return std::move(writer).into_bytes();
}

inline auto parse_legacy_login_by_num_request(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginByNumRequest> {
  PacketReader reader(bytes);

  auto account_id = read_i32_le(reader);
  auto access_code = read_i32_le(reader);
  auto version = read_i16_le(reader);
  if (!account_id || !access_code || !version) {
    return std::nullopt;
  }

  for (auto i = 0; i < 5; ++i) {
    if (!read_i32_le(reader)) {
      return std::nullopt;
    }
  }

  return LoginByNumRequest{
      .account_id = *account_id,
      .access_code = *access_code,
      .version = *version,
  };
}

inline auto parse_extended_login_by_num_request(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginByNumRequest> {
  PacketReader reader(bytes);

  auto account_id = read_i32_le(reader);
  auto access_code = read_i32_le(reader);
  auto unknown1 = read_i32_le(reader);
  auto unknown2 = read_i16_le(reader);
  auto version = read_i16_le(reader);
  if (!account_id || !access_code || !unknown1 || !unknown2 || !version) {
    return std::nullopt;
  }

  for (auto i = 0; i < 6; ++i) {
    if (!read_i32_le(reader)) {
      return std::nullopt;
    }
  }

  return LoginByNumRequest{
      .account_id = *account_id,
      .access_code = *access_code,
      .version = *version,
  };
}

inline auto parse_login_by_num_request(std::span<const std::uint8_t> bytes,
                                       std::int16_t struct_version)
    -> std::optional<LoginByNumRequest> {
  if (struct_version >= kExtendedLoginRequestStructVersion) {
    return parse_extended_login_by_num_request(bytes);
  }

  return parse_legacy_login_by_num_request(bytes);
}

inline auto encode_legacy_login_by_num_request_fixture(const LoginByNumRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;

  append_i32_le(writer, request.account_id);
  append_i32_le(writer, request.access_code);
  append_i16_le(writer, request.version);

  for (auto i = 0; i < 5; ++i) {
    append_i32_le(writer, 0);
  }

  return std::move(writer).into_bytes();
}

inline auto encode_extended_login_by_num_request_fixture(const LoginByNumRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;

  append_i32_le(writer, request.account_id);
  append_i32_le(writer, request.access_code);
  append_i32_le(writer, 0);
  append_i16_le(writer, 0);
  append_i16_le(writer, request.version);

  for (auto i = 0; i < 6; ++i) {
    append_i32_le(writer, 0);
  }

  return std::move(writer).into_bytes();
}

inline auto login_request_struct_version(std::int16_t classic_parsed_version,
                                         const OpcodeVersionRanges& opcode_versions)
    -> std::int16_t {
  if (classic_parsed_version == 0 || !opcode_versions.contains_client_version(classic_parsed_version)) {
    return kExtendedLoginRequestStructVersion;
  }

  return kLegacyLoginRequestStructVersion;
}

inline auto world_login_request_struct_version(std::int16_t classic_parsed_version,
                                               const OpcodeVersionRanges& opcode_versions)
    -> std::int16_t {
  if (classic_parsed_version == 0 || classic_parsed_version >= kExtendedLoginRequestStructVersion ||
      !opcode_versions.contains_client_version(classic_parsed_version)) {
    return kExtendedLoginRequestStructVersion;
  }

  return kLegacyLoginRequestStructVersion;
}

inline auto parse_world_login_by_num_request(std::span<const std::uint8_t> bytes,
                                             const OpcodeVersionRanges& opcode_versions)
    -> std::optional<LoginByNumRequest> {
  const auto classic = parse_legacy_login_by_num_request(bytes);
  if (!classic.has_value()) {
    return std::nullopt;
  }

  const auto struct_version = world_login_request_struct_version(classic->version, opcode_versions);
  if (struct_version == kLegacyLoginRequestStructVersion) {
    return classic;
  }

  return parse_extended_login_by_num_request(bytes);
}

}  // namespace eq2::protocol
