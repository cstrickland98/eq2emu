#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include <eq2/core/endian.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

namespace eq2::protocol {

enum class PacketTransformBoundary : std::uint8_t {
  none,
  transport_payload,
};

struct PacketTransformPolicy {
  bool compressed = false;
  bool encrypted = false;
  PacketTransformBoundary compression_boundary = PacketTransformBoundary::none;
  PacketTransformBoundary encryption_boundary = PacketTransformBoundary::none;
};

struct PacketCrcPolicy {
  bool requires_crc = true;
  bool legacy_app_combined_bypass = false;
};

inline auto packet_transform_policy(std::uint8_t session_format) -> PacketTransformPolicy {
  const auto compressed = (session_format & kFlagCompressed) != 0;
  const auto encrypted = (session_format & kFlagEncoded) != 0;

  return PacketTransformPolicy{
      .compressed = compressed,
      .encrypted = encrypted,
      .compression_boundary = compressed ? PacketTransformBoundary::transport_payload : PacketTransformBoundary::none,
      .encryption_boundary = encrypted ? PacketTransformBoundary::transport_payload : PacketTransformBoundary::none,
  };
}

inline auto protocol_packet_uses_crc(std::uint16_t opcode) -> bool {
  return opcode != kOpSessionRequest && opcode != kOpSessionResponse && opcode != kOpOutOfSession;
}

inline auto packet_crc_policy(std::span<const std::uint8_t> packet_bytes) -> std::optional<PacketCrcPolicy> {
  if (packet_bytes.size() < 2) {
    return std::nullopt;
  }

  const auto opcode = eq2::core::read_u16_be(packet_bytes, 0);
  const auto legacy_app_combined_bypass =
      packet_bytes.size() >= 4 && packet_bytes[2] == 0 && packet_bytes[3] == kOpAppCombined;

  return PacketCrcPolicy{
      .requires_crc = protocol_packet_uses_crc(opcode) && !legacy_app_combined_bypass,
      .legacy_app_combined_bypass = legacy_app_combined_bypass,
  };
}

}  // namespace eq2::protocol
