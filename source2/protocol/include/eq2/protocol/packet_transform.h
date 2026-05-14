#pragma once

#include <cstdint>

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

}  // namespace eq2::protocol
