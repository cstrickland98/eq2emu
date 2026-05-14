#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <eq2/core/endian.h>

namespace eq2::core {

class ByteBuffer {
 public:
  void append_u8(std::uint8_t value) {
    bytes_.push_back(value);
  }

  void append_u16_be(std::uint16_t value) {
    const auto offset = grow(2);
    write_u16_be(bytes_, offset, value);
  }

  void append_u16_le(std::uint16_t value) {
    const auto offset = grow(2);
    write_u16_le(bytes_, offset, value);
  }

  void append_u32_be(std::uint32_t value) {
    const auto offset = grow(4);
    write_u32_be(bytes_, offset, value);
  }

  void append_u32_le(std::uint32_t value) {
    const auto offset = grow(4);
    write_u32_le(bytes_, offset, value);
  }

  void append_bytes(std::span<const std::uint8_t> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  [[nodiscard]] auto bytes() const -> std::span<const std::uint8_t> {
    return bytes_;
  }

  [[nodiscard]] auto size() const -> std::size_t {
    return bytes_.size();
  }

 private:
  auto grow(std::size_t count) -> std::size_t {
    const auto offset = bytes_.size();
    bytes_.resize(bytes_.size() + count);
    return offset;
  }

  std::vector<std::uint8_t> bytes_;
};

}  // namespace eq2::core
