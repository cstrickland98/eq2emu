#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include <eq2/core/endian.h>
#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

struct ClientLogPayload {
  std::uint32_t inflated_size = 0;
  std::string message;
};

inline auto inflate_client_log_message(std::span<const std::uint8_t> compressed,
                                       std::uint32_t inflated_size)
    -> std::optional<std::string> {
  if (compressed.empty() || inflated_size == 0) {
    return std::nullopt;
  }

  auto output = std::string{};
  output.resize(inflated_size);

  z_stream stream{};
  stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(compressed.data()));
  stream.avail_in = static_cast<uInt>(compressed.size());
  stream.next_out = reinterpret_cast<Bytef*>(output.data());
  stream.avail_out = static_cast<uInt>(output.size());

  if (inflateInit(&stream) != Z_OK) {
    return std::nullopt;
  }

  const auto status = inflate(&stream, Z_FINISH);
  const auto produced = stream.total_out;
  inflateEnd(&stream);
  if (status != Z_STREAM_END || produced == 0) {
    return std::nullopt;
  }

  output.resize(static_cast<std::size_t>(produced));
  if (!output.empty() && output.back() == '\0') {
    output.pop_back();
  }
  return output.empty() ? std::nullopt : std::optional<std::string>{std::move(output)};
}

inline auto decode_client_log_payload(std::span<const std::uint8_t> bytes,
                                      std::int16_t client_version)
    -> std::optional<ClientLogPayload> {
  auto inflated_size = std::uint32_t{0xffff};
  auto compressed = std::span<const std::uint8_t>{};
  if (client_version >= 546) {
    if (bytes.size() < 8) {
      return std::nullopt;
    }
    const auto compressed_size = eq2::core::read_u32_le(bytes, 0);
    inflated_size = eq2::core::read_u32_le(bytes, 4);
    if (compressed_size == 0 || compressed_size > 0x20000U ||
        bytes.size() < 8U + compressed_size) {
      return std::nullopt;
    }
    compressed = bytes.subspan(8, compressed_size);
  } else {
    if (bytes.size() < 2) {
      return std::nullopt;
    }
    compressed = bytes.subspan(2);
  }

  auto message = inflate_client_log_message(compressed, inflated_size + 1U);
  if (!message.has_value()) {
    return std::nullopt;
  }

  return ClientLogPayload{
      .inflated_size = inflated_size,
      .message = std::move(*message),
  };
}

inline auto encode_client_log_payload_fixture(std::string_view message,
                                              std::int16_t client_version)
    -> std::vector<std::uint8_t> {
  const auto source_size = static_cast<uLong>(message.size());
  auto compressed = std::vector<std::uint8_t>(compressBound(source_size));
  auto compressed_size = static_cast<uLongf>(compressed.size());
  if (compress2(compressed.data(),
                &compressed_size,
                reinterpret_cast<const Bytef*>(message.data()),
                source_size,
                Z_DEFAULT_COMPRESSION) != Z_OK) {
    return {};
  }
  compressed.resize(static_cast<std::size_t>(compressed_size));

  PacketWriter writer;
  if (client_version >= 546) {
    writer.append_u32_le(static_cast<std::uint32_t>(compressed.size()));
    writer.append_u32_le(static_cast<std::uint32_t>(message.size()));
  } else {
    writer.append_u16_le(0);
  }
  writer.append_bytes(compressed);
  return std::move(writer).into_bytes();
}

}  // namespace eq2::protocol
