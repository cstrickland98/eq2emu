#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace eq2::protocol {

inline auto read_u16_be(std::span<const std::uint8_t> bytes, std::size_t offset) -> std::uint16_t {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[offset + 1U]));
}

inline auto read_u32_be(std::span<const std::uint8_t> bytes, std::size_t offset) -> std::uint32_t {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

inline void write_u16_be(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xffU);
}

inline void write_u32_be(std::span<std::uint8_t> bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
  bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
  bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
  bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

template <std::size_t Size>
inline void write_u16_be(std::array<std::uint8_t, Size>& bytes,
                         std::size_t offset,
                         std::uint16_t value) {
  write_u16_be(std::span<std::uint8_t>(bytes), offset, value);
}

template <std::size_t Size>
inline void write_u32_be(std::array<std::uint8_t, Size>& bytes,
                         std::size_t offset,
                         std::uint32_t value) {
  write_u32_be(std::span<std::uint8_t>(bytes), offset, value);
}

}  // namespace eq2::protocol
