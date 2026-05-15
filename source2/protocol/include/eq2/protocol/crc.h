#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <eq2/core/endian.h>
#include <eq2/protocol/protocol_packet.h>

namespace eq2::protocol {

inline auto legacy_crc16(std::span<const std::uint8_t> bytes, std::uint32_t key)
    -> std::uint16_t {
  auto crc = std::uint32_t{0xffffffffU};
  const auto update = [&crc](std::uint8_t value) {
    crc ^= value;
    for (auto bit = 0; bit < 8; ++bit) {
      const auto mask = static_cast<std::uint32_t>(0U - (crc & 1U));
      crc = (crc >> 1U) ^ (0xedb88320U & mask);
    }
  };

  update(static_cast<std::uint8_t>(key & 0xffU));
  update(static_cast<std::uint8_t>((key >> 8U) & 0xffU));
  update(static_cast<std::uint8_t>((key >> 16U) & 0xffU));
  update(static_cast<std::uint8_t>((key >> 24U) & 0xffU));

  for (const auto byte : bytes) {
    update(byte);
  }

  return static_cast<std::uint16_t>(~crc);
}

inline auto packet_uses_legacy_crc(std::span<const std::uint8_t> packet_bytes) -> bool {
  if (packet_bytes.size() < 2) {
    return false;
  }

  const auto opcode = eq2::core::read_u16_be(packet_bytes, 0);
  if (opcode == kOpSessionRequest || opcode == kOpSessionResponse || opcode == kOpOutOfSession) {
    return false;
  }

  if (packet_bytes.size() >= 4 && packet_bytes[2] == 0 && packet_bytes[3] == kOpAppCombined) {
    return false;
  }

  return true;
}

inline auto packet_has_valid_legacy_crc(std::span<const std::uint8_t> packet_bytes,
                                        std::uint32_t key) -> bool {
  if (!packet_uses_legacy_crc(packet_bytes)) {
    return true;
  }
  if (packet_bytes.size() < 4) {
    return false;
  }

  const auto packet_crc = eq2::core::read_u16_be(packet_bytes, packet_bytes.size() - 2);
  return packet_crc == 0 ||
         packet_crc == legacy_crc16(packet_bytes.first(packet_bytes.size() - 2), key);
}

inline auto strip_legacy_crc_if_present(std::span<const std::uint8_t> packet_bytes,
                                        std::uint32_t key) -> std::span<const std::uint8_t> {
  if (!packet_uses_legacy_crc(packet_bytes) || packet_bytes.size() < 4) {
    return packet_bytes;
  }

  const auto packet_crc = eq2::core::read_u16_be(packet_bytes, packet_bytes.size() - 2);
  if (packet_crc == 0 || !packet_has_valid_legacy_crc(packet_bytes, key)) {
    return packet_bytes;
  }

  return packet_bytes.first(packet_bytes.size() - 2);
}

inline auto append_legacy_crc(std::vector<std::uint8_t> packet_bytes, std::uint32_t key)
    -> std::vector<std::uint8_t> {
  if (!packet_uses_legacy_crc(packet_bytes)) {
    return packet_bytes;
  }

  const auto crc = legacy_crc16(packet_bytes, key);
  const auto offset = packet_bytes.size();
  packet_bytes.resize(packet_bytes.size() + 2);
  eq2::core::write_u16_be(packet_bytes, offset, crc);
  return packet_bytes;
}

}  // namespace eq2::protocol
