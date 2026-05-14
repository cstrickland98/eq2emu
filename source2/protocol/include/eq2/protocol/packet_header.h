#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

inline constexpr std::size_t kProtocolHeaderSize = 2;

enum class ApplicationOpcodeWidth : std::uint8_t {
  one_byte = 1,
  two_bytes = 2,
};

struct ProtocolHeader {
  std::uint16_t opcode = 0;
};

struct ApplicationHeader {
  std::uint16_t opcode = 0;
  std::size_t size = 0;
  bool has_padding_zero = false;
};

inline void write_protocol_header(PacketWriter& writer, ProtocolHeader header) {
  writer.append_u16_be(header.opcode);
}

inline auto read_protocol_header(PacketReader& reader) -> std::optional<ProtocolHeader> {
  const auto opcode = reader.read_u16_be();
  if (!opcode.has_value()) {
    return std::nullopt;
  }

  return ProtocolHeader{.opcode = *opcode};
}

inline auto parse_protocol_header(std::span<const std::uint8_t> bytes) -> std::optional<ProtocolHeader> {
  PacketReader reader(bytes);
  return read_protocol_header(reader);
}

inline auto write_application_header(PacketWriter& writer,
                                     std::uint16_t opcode,
                                     ApplicationOpcodeWidth width) -> std::size_t {
  if (width == ApplicationOpcodeWidth::one_byte) {
    writer.append_u8(static_cast<std::uint8_t>(opcode & 0xffU));
    return 1;
  }

  if ((opcode & 0x00ffU) == 0) {
    writer.append_u8(0);
    writer.append_u16_le(opcode);
    return 3;
  }

  writer.append_u16_le(opcode);
  return 2;
}

inline auto read_application_header(PacketReader& reader,
                                    ApplicationOpcodeWidth width) -> std::optional<ApplicationHeader> {
  if (width == ApplicationOpcodeWidth::one_byte) {
    const auto opcode = reader.read_u8();
    if (!opcode.has_value()) {
      return std::nullopt;
    }

    return ApplicationHeader{
        .opcode = *opcode,
        .size = 1,
        .has_padding_zero = false,
    };
  }

  const auto first = reader.read_u8();
  if (!first.has_value()) {
    return std::nullopt;
  }

  if (*first == 0) {
    const auto opcode_suffix = reader.read_u16_le();
    if (!opcode_suffix.has_value()) {
      return std::nullopt;
    }

    return ApplicationHeader{
        .opcode = *opcode_suffix,
        .size = 3,
        .has_padding_zero = true,
    };
  }

  const auto second = reader.read_u8();
  if (!second.has_value()) {
    return std::nullopt;
  }

  return ApplicationHeader{
      .opcode = static_cast<std::uint16_t>((static_cast<std::uint16_t>(*second) << 8U) | *first),
      .size = 2,
      .has_padding_zero = false,
  };
}

}  // namespace eq2::protocol
