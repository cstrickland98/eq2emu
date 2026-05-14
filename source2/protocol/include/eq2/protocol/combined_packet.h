#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

inline constexpr std::uint8_t kExtendedCombinedPacketLength = 0xff;

struct CombinedPacketView {
  std::span<const std::uint8_t> bytes;
  std::size_t length_prefix_size = 0;
};

inline auto write_combined_packet_length(PacketWriter& writer, std::size_t length) -> bool {
  if (length > 0xffff) {
    return false;
  }

  if (length >= kExtendedCombinedPacketLength) {
    writer.append_u8(kExtendedCombinedPacketLength);
    writer.append_u16_be(static_cast<std::uint16_t>(length));
    return true;
  }

  writer.append_u8(static_cast<std::uint8_t>(length));
  return true;
}

inline auto encode_combined_packet(std::span<const std::span<const std::uint8_t>> subpackets)
    -> std::optional<std::vector<std::uint8_t>> {
  PacketWriter writer;

  for (const auto subpacket : subpackets) {
    if (!write_combined_packet_length(writer, subpacket.size())) {
      return std::nullopt;
    }

    writer.append_bytes(subpacket);
  }

  return std::move(writer).into_bytes();
}

inline auto decode_combined_packet(std::span<const std::uint8_t> bytes)
    -> std::optional<std::vector<CombinedPacketView>> {
  PacketReader reader(bytes);
  std::vector<CombinedPacketView> subpackets;

  while (reader.remaining() > 0) {
    const auto marker = reader.read_u8();
    if (!marker.has_value()) {
      return std::nullopt;
    }

    std::size_t length = *marker;
    std::size_t prefix_size = 1;
    if (*marker == kExtendedCombinedPacketLength) {
      const auto extended_length = reader.read_u16_be();
      if (!extended_length.has_value()) {
        return std::nullopt;
      }

      length = *extended_length;
      prefix_size = 3;
    }

    const auto subpacket = reader.read_bytes(length);
    if (!subpacket.has_value()) {
      return std::nullopt;
    }

    subpackets.push_back(CombinedPacketView{
        .bytes = *subpacket,
        .length_prefix_size = prefix_size,
    });
  }

  return subpackets;
}

}  // namespace eq2::protocol
