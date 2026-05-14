#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_header.h>

namespace eq2::protocol {

struct ApplicationPacketView {
  std::uint16_t opcode = 0;
  std::span<const std::uint8_t> payload;
  std::size_t header_size = 0;
};

inline auto encode_application_packet(std::uint16_t opcode,
                                      std::span<const std::uint8_t> payload,
                                      ApplicationOpcodeWidth opcode_width = ApplicationOpcodeWidth::two_bytes)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  write_application_header(writer, opcode, opcode_width);
  writer.append_bytes(payload);
  return std::move(writer).into_bytes();
}

inline auto decode_application_packet(std::span<const std::uint8_t> bytes,
                                      ApplicationOpcodeWidth opcode_width = ApplicationOpcodeWidth::two_bytes)
    -> std::optional<ApplicationPacketView> {
  PacketReader reader(bytes);
  const auto header = read_application_header(reader, opcode_width);
  if (!header.has_value()) {
    return std::nullopt;
  }

  return ApplicationPacketView{
      .opcode = header->opcode,
      .payload = bytes.subspan(header->size),
      .header_size = header->size,
  };
}

}  // namespace eq2::protocol
