#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

inline auto read_i16_le(PacketReader& reader) -> std::optional<std::int16_t> {
  const auto value = reader.read_u16_le();
  if (!value.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::int16_t>(*value);
}

inline auto read_i32_le(PacketReader& reader) -> std::optional<std::int32_t> {
  const auto value = reader.read_u32_le();
  if (!value.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::int32_t>(*value);
}

inline void append_i16_le(PacketWriter& writer, std::int16_t value) {
  writer.append_u16_le(static_cast<std::uint16_t>(value));
}

inline void append_i32_le(PacketWriter& writer, std::int32_t value) {
  writer.append_u32_le(static_cast<std::uint32_t>(value));
}

inline auto read_eq2_8bit_string(PacketReader& reader) -> std::optional<std::string> {
  const auto size = reader.read_u8();
  if (!size.has_value()) {
    return std::nullopt;
  }

  const auto bytes = reader.read_bytes(*size);
  if (!bytes.has_value()) {
    return std::nullopt;
  }

  if (bytes->empty()) {
    return std::string{};
  }

  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

inline auto read_eq2_16bit_string(PacketReader& reader) -> std::optional<std::string> {
  const auto size = reader.read_u16_le();
  if (!size.has_value()) {
    return std::nullopt;
  }

  const auto bytes = reader.read_bytes(*size);
  if (!bytes.has_value()) {
    return std::nullopt;
  }

  if (bytes->empty()) {
    return std::string{};
  }

  return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

inline void append_eq2_8bit_string(PacketWriter& writer, std::string_view value) {
  const auto clamped_size =
      value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint8_t>::max())
          ? std::numeric_limits<std::uint8_t>::max()
          : static_cast<std::uint8_t>(value.size());
  writer.append_u8(clamped_size);

  if (clamped_size == 0) {
    return;
  }

  writer.append_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(value.data()),
      static_cast<std::size_t>(clamped_size)));
}

inline void append_eq2_16bit_string(PacketWriter& writer, std::string_view value) {
  const auto clamped_size =
      value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())
          ? std::numeric_limits<std::uint16_t>::max()
          : static_cast<std::uint16_t>(value.size());
  writer.append_u16_le(clamped_size);

  if (clamped_size == 0) {
    return;
  }

  writer.append_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(value.data()),
      static_cast<std::size_t>(clamped_size)));
}

}  // namespace eq2::protocol
