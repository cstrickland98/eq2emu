#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <eq2/core/endian.h>

namespace eq2::protocol {

class PacketWriter {
 public:
  void append_u8(std::uint8_t value) {
    bytes_.push_back(value);
  }

  void append_u16_be(std::uint16_t value) {
    const auto offset = grow(2);
    eq2::core::write_u16_be(bytes_, offset, value);
  }

  void append_u16_le(std::uint16_t value) {
    const auto offset = grow(2);
    eq2::core::write_u16_le(bytes_, offset, value);
  }

  void append_u32_be(std::uint32_t value) {
    const auto offset = grow(4);
    eq2::core::write_u32_be(bytes_, offset, value);
  }

  void append_u32_le(std::uint32_t value) {
    const auto offset = grow(4);
    eq2::core::write_u32_le(bytes_, offset, value);
  }

  void append_bytes(std::span<const std::uint8_t> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  [[nodiscard]] auto bytes() const -> std::span<const std::uint8_t> {
    return bytes_;
  }

  [[nodiscard]] auto into_bytes() && -> std::vector<std::uint8_t> {
    return std::move(bytes_);
  }

 private:
  auto grow(std::size_t count) -> std::size_t {
    const auto offset = bytes_.size();
    bytes_.resize(bytes_.size() + count);
    return offset;
  }

  std::vector<std::uint8_t> bytes_;
};

class PacketReader {
 public:
  explicit PacketReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] auto remaining() const -> std::size_t {
    return bytes_.size() - offset_;
  }

  [[nodiscard]] auto consumed() const -> std::size_t {
    return offset_;
  }

  [[nodiscard]] auto read_u8() -> std::optional<std::uint8_t> {
    if (remaining() < 1) {
      return std::nullopt;
    }

    return bytes_[offset_++];
  }

  [[nodiscard]] auto read_u16_be() -> std::optional<std::uint16_t> {
    if (remaining() < 2) {
      return std::nullopt;
    }

    const auto value = eq2::core::read_u16_be(bytes_, offset_);
    offset_ += 2;
    return value;
  }

  [[nodiscard]] auto read_u16_le() -> std::optional<std::uint16_t> {
    if (remaining() < 2) {
      return std::nullopt;
    }

    const auto value = eq2::core::read_u16_le(bytes_, offset_);
    offset_ += 2;
    return value;
  }

  [[nodiscard]] auto read_u32_be() -> std::optional<std::uint32_t> {
    if (remaining() < 4) {
      return std::nullopt;
    }

    const auto value = eq2::core::read_u32_be(bytes_, offset_);
    offset_ += 4;
    return value;
  }

  [[nodiscard]] auto read_u32_le() -> std::optional<std::uint32_t> {
    if (remaining() < 4) {
      return std::nullopt;
    }

    const auto value = eq2::core::read_u32_le(bytes_, offset_);
    offset_ += 4;
    return value;
  }

  [[nodiscard]] auto read_bytes(std::size_t count) -> std::optional<std::span<const std::uint8_t>> {
    if (remaining() < count) {
      return std::nullopt;
    }

    const auto result = bytes_.subspan(offset_, count);
    offset_ += count;
    return result;
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t offset_ = 0;
};

}  // namespace eq2::protocol
