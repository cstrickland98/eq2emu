#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

inline constexpr std::size_t kProtocolHeaderSize = 2;

enum class ApplicationOpcodeWidth : std::uint8_t {
  one_byte = 1,
  two_bytes = 2,
  packed_u16 = 3,
};

inline auto parse_application_opcode_width(std::string_view value)
    -> std::optional<ApplicationOpcodeWidth> {
  if (value == "1" || value == "one" || value == "one_byte" || value == "one-byte" ||
      value == "login_stream") {
    return ApplicationOpcodeWidth::one_byte;
  }
  if (value == "2" || value == "two" || value == "two_byte" || value == "two-byte" ||
      value == "two_bytes" || value == "world_stream") {
    return ApplicationOpcodeWidth::two_bytes;
  }
  if (value == "packed" || value == "packed_u16" || value == "packed-u16" ||
      value == "vetype" || value == "ve_type") {
    return ApplicationOpcodeWidth::packed_u16;
  }

  return std::nullopt;
}

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

  if (width == ApplicationOpcodeWidth::packed_u16) {
    if (opcode < 0xffU) {
      writer.append_u8(static_cast<std::uint8_t>(opcode));
      return 1;
    }
    writer.append_u8(0xff);
    writer.append_u16_le(opcode);
    return 3;
  }

  if ((opcode & 0x00ffU) == 0) {
    writer.append_u8(0);
    writer.append_u16_le(opcode);
    return 3;
  }

  writer.append_u16_le(opcode);
  return 2;
}

inline auto application_header_size(std::uint16_t opcode,
                                    ApplicationOpcodeWidth width) -> std::size_t {
  if (width == ApplicationOpcodeWidth::one_byte) {
    return 1;
  }
  if (width == ApplicationOpcodeWidth::packed_u16) {
    return opcode < 0xffU ? 1U : 3U;
  }

  return (opcode & 0x00ffU) == 0 ? 3 : 2;
}

inline auto minimum_application_header_size(ApplicationOpcodeWidth width) -> std::size_t {
  return width == ApplicationOpcodeWidth::two_bytes ? 2U : 1U;
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

  if (width == ApplicationOpcodeWidth::packed_u16) {
    const auto first = reader.read_u8();
    if (!first.has_value()) {
      return std::nullopt;
    }
    if (*first != 0xff) {
      return ApplicationHeader{
          .opcode = *first,
          .size = 1,
          .has_padding_zero = false,
      };
    }

    const auto opcode = reader.read_u16_le();
    if (!opcode.has_value()) {
      return std::nullopt;
    }

    return ApplicationHeader{
        .opcode = *opcode,
        .size = 3,
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
