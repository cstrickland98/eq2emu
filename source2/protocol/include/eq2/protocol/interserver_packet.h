#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <eq2/protocol/packet_buffer.h>

#include <zlib.h>

namespace eq2::protocol {

inline constexpr std::size_t kInterserverPacketHeaderSize = 7;
inline constexpr std::uint8_t kInterserverPacketCompressedFlag = 0x01;
inline constexpr std::uint8_t kInterserverPacketDestinationFlag = 0x02;
inline constexpr std::uint32_t kMaxInterserverInflatedPayloadSize = 16U * 1024U * 1024U;

struct InterserverPacketEncodeOptions {
  bool compressed = false;
  std::uint32_t inflated_size = 0;
  std::optional<std::int32_t> destination;
};

struct InterserverPacket {
  std::uint16_t opcode = 0;
  std::span<const std::uint8_t> payload;
  bool compressed = false;
  std::uint32_t inflated_size = 0;
  std::optional<std::int32_t> destination;
};

inline auto encode_interserver_packet(std::uint16_t opcode,
                                      std::span<const std::uint8_t> payload,
                                      const InterserverPacketEncodeOptions& options = {})
    -> std::optional<std::vector<std::uint8_t>> {
  const auto extra_size =
      (options.compressed ? 4U : 0U) + (options.destination.has_value() ? 4U : 0U);
  const auto total_size = kInterserverPacketHeaderSize + extra_size + payload.size();
  if (total_size > std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }

  std::uint8_t flags = 0;
  if (options.compressed) {
    flags |= kInterserverPacketCompressedFlag;
  }
  if (options.destination.has_value()) {
    flags |= kInterserverPacketDestinationFlag;
  }

  PacketWriter writer;
  writer.append_u32_le(static_cast<std::uint32_t>(total_size));
  writer.append_u8(flags);
  writer.append_u16_le(opcode);
  if (options.compressed) {
    writer.append_u32_le(options.inflated_size);
  }
  if (options.destination.has_value()) {
    writer.append_u32_le(static_cast<std::uint32_t>(*options.destination));
  }
  writer.append_bytes(payload);

  return std::move(writer).into_bytes();
}

inline auto decode_interserver_packet(std::span<const std::uint8_t> bytes)
    -> std::optional<InterserverPacket> {
  if (bytes.size() < kInterserverPacketHeaderSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  const auto wire_size = reader.read_u32_le();
  const auto flags = reader.read_u8();
  const auto opcode = reader.read_u16_le();
  if (!wire_size.has_value() || !flags.has_value() || !opcode.has_value()) {
    return std::nullopt;
  }

  if (*wire_size != bytes.size()) {
    return std::nullopt;
  }

  const auto compressed = (*flags & kInterserverPacketCompressedFlag) != 0;
  const auto has_destination = (*flags & kInterserverPacketDestinationFlag) != 0;
  auto inflated_size = std::uint32_t{0};
  if (compressed) {
    const auto value = reader.read_u32_le();
    if (!value.has_value()) {
      return std::nullopt;
    }
    inflated_size = *value;
  }

  std::optional<std::int32_t> destination;
  if (has_destination) {
    const auto value = reader.read_u32_le();
    if (!value.has_value()) {
      return std::nullopt;
    }
    destination = static_cast<std::int32_t>(*value);
  }

  const auto payload = reader.read_bytes(reader.remaining());
  if (!payload.has_value()) {
    return std::nullopt;
  }

  return InterserverPacket{
      .opcode = *opcode,
      .payload = *payload,
      .compressed = compressed,
      .inflated_size = inflated_size,
      .destination = destination,
  };
}

inline auto inflate_interserver_payload(const InterserverPacket& packet)
    -> std::optional<std::vector<std::uint8_t>> {
  if (!packet.compressed) {
    return std::vector<std::uint8_t>(packet.payload.begin(), packet.payload.end());
  }

  if (packet.inflated_size == 0 ||
      packet.inflated_size > kMaxInterserverInflatedPayloadSize) {
    return std::nullopt;
  }

  auto inflated = std::vector<std::uint8_t>(packet.inflated_size);
  auto inflated_size = static_cast<uLongf>(inflated.size());
  const auto result = uncompress(inflated.data(),
                                 &inflated_size,
                                 reinterpret_cast<const Bytef*>(packet.payload.data()),
                                 static_cast<uLong>(packet.payload.size()));
  if (result != Z_OK) {
    return std::nullopt;
  }

  inflated.resize(static_cast<std::size_t>(inflated_size));
  return inflated;
}

}  // namespace eq2::protocol
