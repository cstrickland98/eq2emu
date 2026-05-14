#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include <eq2/core/endian.h>

namespace eq2::protocol {

inline constexpr std::size_t kSessionRequestSize = 12;
inline constexpr std::size_t kSessionResponseSize = 19;

inline constexpr std::uint8_t kFlagCompressed = 0x01;
inline constexpr std::uint8_t kFlagEncoded = 0x04;

struct SessionRequest {
  std::uint32_t unknown_a = 0;
  std::uint32_t session = 0;
  std::uint32_t max_length = 0;
};

struct SessionResponse {
  std::uint32_t session = 0;
  std::uint32_t key = 0;
  std::uint8_t unknown_a = 2;
  std::uint8_t format = 0;
  std::uint8_t unknown_b = 0;
  std::uint32_t max_length = 0;
  std::uint32_t unknown_d = 0;
};

inline auto encode_session_request(const SessionRequest& request)
    -> std::array<std::uint8_t, kSessionRequestSize> {
  std::array<std::uint8_t, kSessionRequestSize> bytes{};
  eq2::core::write_u32_be(bytes, 0, request.unknown_a);
  eq2::core::write_u32_be(bytes, 4, request.session);
  eq2::core::write_u32_be(bytes, 8, request.max_length);
  return bytes;
}

inline auto decode_session_request(std::span<const std::uint8_t> bytes)
    -> std::optional<SessionRequest> {
  if (bytes.size() < kSessionRequestSize) {
    return std::nullopt;
  }

  return SessionRequest{
      .unknown_a = eq2::core::read_u32_be(bytes, 0),
      .session = eq2::core::read_u32_be(bytes, 4),
      .max_length = eq2::core::read_u32_be(bytes, 8),
  };
}

inline auto encode_session_response(const SessionResponse& response)
    -> std::array<std::uint8_t, kSessionResponseSize> {
  std::array<std::uint8_t, kSessionResponseSize> bytes{};
  eq2::core::write_u32_be(bytes, 0, response.session);
  eq2::core::write_u32_be(bytes, 4, response.key);
  bytes[8] = response.unknown_a;
  bytes[9] = response.format;
  bytes[10] = response.unknown_b;
  eq2::core::write_u32_be(bytes, 11, response.max_length);
  eq2::core::write_u32_be(bytes, 15, response.unknown_d);
  return bytes;
}

inline auto decode_session_response(std::span<const std::uint8_t> bytes)
    -> std::optional<SessionResponse> {
  if (bytes.size() < kSessionResponseSize) {
    return std::nullopt;
  }

  return SessionResponse{
      .session = eq2::core::read_u32_be(bytes, 0),
      .key = eq2::core::read_u32_be(bytes, 4),
      .unknown_a = bytes[8],
      .format = bytes[9],
      .unknown_b = bytes[10],
      .max_length = eq2::core::read_u32_be(bytes, 11),
      .unknown_d = eq2::core::read_u32_be(bytes, 15),
  };
}

inline auto session_response_format(bool compressed, bool encoded) -> std::uint8_t {
  std::uint8_t format = 0;
  if (compressed) {
    format |= kFlagCompressed;
  }
  if (encoded) {
    format |= kFlagEncoded;
  }
  return format;
}

}  // namespace eq2::protocol
