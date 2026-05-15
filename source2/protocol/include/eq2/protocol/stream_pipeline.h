#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <eq2/core/endian.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/crc.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

namespace eq2::protocol {

enum class StreamEventType {
  session_requested,
  server_key_requested,
  app_packet,
  keep_alive,
  disconnected,
  malformed,
  unsupported,
};

struct StreamEvent {
  StreamEventType type = StreamEventType::malformed;
  std::uint16_t protocol_opcode = 0;
  std::optional<SessionRequest> session_request;
  std::optional<ApplicationPacketView> app_packet;
  std::vector<std::uint8_t> app_packet_storage;
};

struct StreamPipelineResult {
  std::vector<StreamEvent> events;
  std::vector<std::vector<std::uint8_t>> outbound;
};

struct StreamPipelineOptions {
  std::uint32_t session_key = 0x33624702;
  bool compressed = false;
  bool encoded = false;
  ApplicationOpcodeWidth application_opcode_width = ApplicationOpcodeWidth::two_bytes;
};

class StreamPipeline {
 public:
  explicit StreamPipeline(StreamPipelineOptions options = {}) : options_(options) {}

  auto receive_datagram(std::span<const std::uint8_t> bytes) -> StreamPipelineResult {
    auto result = StreamPipelineResult{};
    const auto framed_bytes = strip_legacy_crc_if_present(bytes, options_.session_key);
    const auto packet = decode_protocol_packet(framed_bytes);
    if (!packet.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
      });
      return result;
    }

    switch (packet->opcode) {
      case kOpSessionRequest:
        handle_session_request(*packet, result);
        break;
      case kOpPacket:
        handle_app_packet(*packet, result);
        break;
      case kOpCombined:
        handle_combined_packet(*packet, result);
        break;
      case kOpKeepAlive:
        result.outbound.push_back(
            append_legacy_crc(encode_protocol_packet(kOpKeepAlive, packet->payload),
                              options_.session_key));
        result.events.push_back(StreamEvent{
            .type = StreamEventType::keep_alive,
            .protocol_opcode = packet->opcode,
        });
        break;
      case kOpServerKeyRequest:
        handle_server_key_request(*packet, result);
        break;
      case kOpAck:
      case kOpOutOfOrderAck:
      case kOpSessionStatResponse:
        break;
      case kOpOutOfSession:
        result.events.push_back(StreamEvent{
            .type = StreamEventType::disconnected,
            .protocol_opcode = packet->opcode,
        });
        break;
      case kOpSessionDisconnect:
        result.events.push_back(StreamEvent{
            .type = StreamEventType::disconnected,
            .protocol_opcode = packet->opcode,
        });
        break;
      default:
        result.events.push_back(StreamEvent{
            .type = StreamEventType::unsupported,
            .protocol_opcode = packet->opcode,
        });
        break;
    }

    return result;
  }

  [[nodiscard]] auto established() const -> bool {
    return established_;
  }

  [[nodiscard]] auto session_id() const -> std::uint32_t {
    return session_id_;
  }

  auto encode_application_protocol_packet(std::span<const std::uint8_t> app_packet)
      -> std::vector<std::uint8_t> {
    if (!established_) {
      return append_legacy_crc(encode_protocol_packet(kOpPacket, app_packet),
                               options_.session_key);
    }

    PacketWriter writer;
    writer.append_u16_be(next_out_sequence_++);
    if (encrypted_) {
      auto encrypted_app = std::vector<std::uint8_t>(app_packet.begin(), app_packet.end());
      server_cipher_.cypher(encrypted_app);
      writer.append_bytes(encrypted_app);
    } else {
      writer.append_bytes(app_packet);
    }
    return append_legacy_crc(encode_protocol_packet(kOpPacket, writer.bytes()),
                             options_.session_key);
  }

 private:
  class LegacyRc4 {
   public:
    LegacyRc4() {
      init(0);
    }

    explicit LegacyRc4(std::uint64_t key) {
      init(key);
    }

    void init(std::uint64_t key) {
      for (std::size_t i = 0; i < state_.size(); ++i) {
        state_[i] = static_cast<std::uint8_t>(i);
      }
      x_ = 0;
      y_ = 0;

      auto key_bytes = std::array<std::uint8_t, 8>{};
      for (std::size_t i = 0; i < key_bytes.size(); ++i) {
        key_bytes[i] = static_cast<std::uint8_t>((key >> (i * 8U)) & 0xffU);
      }

      std::size_t key_index = 0;
      std::size_t state_index = 0;
      for (std::size_t i = 0; i < state_.size(); ++i) {
        const auto temp = state_[i];
        state_index = (state_index + key_bytes[key_index] + temp) & 0xffU;
        state_[i] = state_[state_index];
        state_[state_index] = temp;
        key_index = (key_index + 1U) & 7U;
      }
    }

    void cypher(std::span<std::uint8_t> bytes) {
      for (auto& byte : bytes) {
        ++x_;
        const auto key_val_1 = state_[x_];
        y_ = static_cast<std::uint8_t>(y_ + key_val_1);
        const auto key_val_2 = state_[y_];

        state_[x_] = key_val_2;
        state_[y_] = key_val_1;

        byte ^= state_[(key_val_1 + key_val_2) & 0xffU];
      }
    }

   private:
    std::array<std::uint8_t, 256> state_{};
    std::uint8_t x_ = 0;
    std::uint8_t y_ = 0;
  };

  void handle_session_request(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    const auto request = decode_session_request(packet.payload);
    if (!request.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    session_id_ = request->session;
    established_ = true;
    result.events.push_back(StreamEvent{
        .type = StreamEventType::session_requested,
        .protocol_opcode = packet.opcode,
        .session_request = request,
    });

    const auto response = encode_session_response(SessionResponse{
        .session = request->session,
        .key = options_.session_key,
        .format = session_response_format(options_.compressed, options_.encoded),
        .max_length = request->max_length,
    });
    result.outbound.push_back(encode_protocol_packet(kOpSessionResponse, response));
  }

  void handle_app_packet(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    if (established_ && encrypted_) {
      handle_encrypted_app_packet(packet, result);
      return;
    }

    if (established_ && awaiting_encryption_key_ && packet.payload.size() >= kMinimumRsaKeyPacketSize) {
      handle_encryption_key_packet(packet, result);
      return;
    }

    const auto sequenced_min_size =
        2U + (options_.application_opcode_width == ApplicationOpcodeWidth::one_byte ? 1U : 2U);
    if (established_ && packet.payload.size() >= sequenced_min_size) {
      PacketReader reader(packet.payload);
      const auto sequence = reader.read_u16_be();
      if (sequence.has_value()) {
        const auto sequenced_app =
            decode_application_packet(packet.payload.subspan(2), options_.application_opcode_width);
        if (sequenced_app.has_value()) {
          next_in_sequence_ = static_cast<std::uint16_t>(*sequence + 1U);
          result.outbound.push_back(encode_ack(*sequence, options_.session_key));
          result.events.push_back(StreamEvent{
              .type = StreamEventType::app_packet,
              .protocol_opcode = packet.opcode,
              .app_packet = sequenced_app,
          });
          return;
        }
      }
    }

    const auto app = decode_application_packet(packet.payload, options_.application_opcode_width);
    if (!app.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    result.events.push_back(StreamEvent{
        .type = StreamEventType::app_packet,
        .protocol_opcode = packet.opcode,
        .app_packet = app,
    });
  }

  void handle_combined_packet(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    const auto combined = decode_combined_packet(packet.payload);
    if (!combined.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    for (const auto& subpacket : *combined) {
      auto subresult = receive_datagram(subpacket.bytes);
      result.events.insert(result.events.end(),
                           std::make_move_iterator(subresult.events.begin()),
                           std::make_move_iterator(subresult.events.end()));
      result.outbound.insert(result.outbound.end(),
                             std::make_move_iterator(subresult.outbound.begin()),
                             std::make_move_iterator(subresult.outbound.end()));
    }
  }

  void handle_server_key_request(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    if (packet.payload.size() < kSessionStatsSize) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    auto stats = std::vector<std::uint8_t>(packet.payload.size(), 0);
    stats[0] = packet.payload[0];
    stats[1] = packet.payload[1];
    eq2::core::write_u32_be(stats, 2, current_time_ms());
    eq2::core::write_u32_be(stats, 10, received_packets_);
    eq2::core::write_u32_be(stats, 18, sent_packets_);
    eq2::core::write_u32_be(stats, 26, sent_packets_);
    eq2::core::write_u32_be(stats, 34, received_packets_);

    result.outbound.push_back(
        append_legacy_crc(encode_protocol_packet(kOpSessionStatResponse, stats),
                          options_.session_key));
    awaiting_encryption_key_ = true;
    result.events.push_back(StreamEvent{
        .type = StreamEventType::server_key_requested,
        .protocol_opcode = packet.opcode,
    });
  }

  void handle_encryption_key_packet(const ProtocolPacketView& packet,
                                    StreamPipelineResult& result) {
    if (packet.payload.size() < 2) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    PacketReader reader(packet.payload);
    const auto sequence = reader.read_u16_be();
    if (sequence.has_value()) {
      next_in_sequence_ = static_cast<std::uint16_t>(*sequence + 1U);
      result.outbound.push_back(encode_ack(*sequence, options_.session_key));
    }

    const auto key = rsa_decrypt_legacy_tail(packet.payload);
    if (key == 0) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    rc4_key_ = key;
    client_cipher_.init(~key);
    server_cipher_.init(key);
    auto warmup = std::array<std::uint8_t, 20>{};
    client_cipher_.cypher(warmup);
    server_cipher_.cypher(warmup);
    encrypted_ = true;
    awaiting_encryption_key_ = false;
  }

  void handle_encrypted_app_packet(const ProtocolPacketView& packet,
                                   StreamPipelineResult& result) {
    if (packet.payload.size() <= 2) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    PacketReader reader(packet.payload);
    const auto sequence = reader.read_u16_be();
    if (!sequence.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    next_in_sequence_ = static_cast<std::uint16_t>(*sequence + 1U);
    result.outbound.push_back(encode_ack(*sequence, options_.session_key));

    auto decrypted = std::vector<std::uint8_t>(packet.payload.begin() + 2, packet.payload.end());
    client_cipher_.cypher(decrypted);
    if (decrypted.empty()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    std::uint16_t opcode = decrypted[0];
    std::size_t header_size = 1;
    if (decrypted[0] == 0xff && decrypted.size() > 2) {
      opcode = eq2::core::read_u16_le(decrypted, 1);
      header_size = 3;
    }

    auto event = StreamEvent{
        .type = StreamEventType::app_packet,
        .protocol_opcode = packet.opcode,
    };
    event.app_packet_storage = std::move(decrypted);
    event.app_packet = ApplicationPacketView{
        .opcode = opcode,
        .payload = std::span<const std::uint8_t>(event.app_packet_storage).subspan(header_size),
        .header_size = header_size,
    };
    result.events.push_back(std::move(event));
  }

  static auto encode_ack(std::uint16_t sequence, std::uint32_t key) -> std::vector<std::uint8_t> {
    PacketWriter writer;
    writer.append_u16_be(sequence);
    return append_legacy_crc(encode_protocol_packet(kOpAck, writer.bytes()), key);
  }

  static auto current_time_ms() -> std::uint32_t {
    using Clock = std::chrono::steady_clock;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch());
    return static_cast<std::uint32_t>(now.count() & 0xffffffffU);
  }

  static auto rsa_decrypt_legacy_tail(std::span<const std::uint8_t> bytes) -> std::uint64_t {
    if (bytes.size() < 8) {
      return 0;
    }

    std::uint64_t value = 0;
    const auto offset = bytes.size() - 8;
    for (std::size_t i = 0; i < 8; ++i) {
      value = (value << 8U) | bytes[offset + i];
    }
    return value;
  }

  static constexpr std::size_t kSessionStatsSize = 38;
  static constexpr std::size_t kMinimumRsaKeyPacketSize = 69;

  StreamPipelineOptions options_;
  bool established_ = false;
  bool awaiting_encryption_key_ = false;
  bool encrypted_ = false;
  std::uint32_t session_id_ = 0;
  std::uint16_t next_in_sequence_ = 0;
  std::uint16_t next_out_sequence_ = 0;
  std::uint32_t received_packets_ = 0;
  std::uint32_t sent_packets_ = 0;
  std::uint64_t rc4_key_ = 0;
  LegacyRc4 client_cipher_;
  LegacyRc4 server_cipher_;
};

}  // namespace eq2::protocol
