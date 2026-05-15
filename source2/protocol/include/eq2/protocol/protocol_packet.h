#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <eq2/core/endian.h>

namespace eq2::protocol {

inline constexpr std::uint16_t kOpSessionRequest = 0x01;
inline constexpr std::uint16_t kOpSessionResponse = 0x02;
inline constexpr std::uint16_t kOpCombined = 0x03;
inline constexpr std::uint16_t kOpSessionDisconnect = 0x05;
inline constexpr std::uint16_t kOpKeepAlive = 0x06;
inline constexpr std::uint16_t kOpServerKeyRequest = 0x07;
inline constexpr std::uint16_t kOpSessionStatResponse = 0x08;
inline constexpr std::uint16_t kOpPacket = 0x09;
inline constexpr std::uint16_t kOpFragment = 0x0d;
inline constexpr std::uint16_t kOpOutOfOrderAck = 0x11;
inline constexpr std::uint16_t kOpAck = 0x15;
inline constexpr std::uint16_t kOpAppCombined = 0x19;
inline constexpr std::uint16_t kOpOutOfSession = 0x1d;

struct ProtocolPacketView {
  std::uint16_t opcode = 0;
  std::span<const std::uint8_t> payload;
};

inline auto encode_protocol_packet(std::uint16_t opcode,
                                   std::span<const std::uint8_t> payload,
                                   std::size_t payload_offset = 0) -> std::vector<std::uint8_t> {
  if (payload_offset > payload.size()) {
    payload_offset = payload.size();
  }

  std::vector<std::uint8_t> bytes(2 + payload.size() - payload_offset);
  eq2::core::write_u16_be(std::span<std::uint8_t>(bytes), 0, opcode);
  std::copy(payload.begin() + static_cast<std::ptrdiff_t>(payload_offset), payload.end(), bytes.begin() + 2);
  return bytes;
}

inline auto decode_protocol_packet(std::span<const std::uint8_t> bytes)
    -> std::optional<ProtocolPacketView> {
  if (bytes.size() < 2) {
    return std::nullopt;
  }

  return ProtocolPacketView{
      .opcode = eq2::core::read_u16_be(bytes, 0),
      .payload = bytes.subspan(2),
  };
}

inline auto is_protocol_packet_opcode(std::uint16_t opcode) -> bool {
  switch (opcode) {
    case kOpSessionRequest:
    case kOpSessionResponse:
    case kOpCombined:
    case kOpSessionDisconnect:
    case kOpKeepAlive:
    case kOpServerKeyRequest:
    case kOpSessionStatResponse:
    case kOpPacket:
    case kOpFragment:
    case kOpOutOfOrderAck:
    case kOpAck:
    case kOpOutOfSession:
      return true;
    default:
      return false;
  }
}

}  // namespace eq2::protocol
