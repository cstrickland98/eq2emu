#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <eq2/core/endian.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/crc.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

namespace eq2::protocol {

inline constexpr std::uint32_t kDefaultSessionKey = 0x33624702;
inline constexpr std::size_t kLegacyCombinedDatagramMaxSize = 255;

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
  std::uint32_t session_key = kDefaultSessionKey;
  bool compressed = false;
  bool encoded = false;
  ApplicationOpcodeWidth application_opcode_width = ApplicationOpcodeWidth::two_bytes;
};

inline auto coalesce_legacy_protocol_datagrams(
    std::span<const std::vector<std::uint8_t>> packets,
    std::uint32_t session_key = kDefaultSessionKey) -> std::optional<std::vector<std::uint8_t>> {
  if (packets.size() < 2) {
    return std::nullopt;
  }

  std::vector<std::span<const std::uint8_t>> stripped_packets;
  stripped_packets.reserve(packets.size());
  std::size_t combined_payload_size = 0;

  for (const auto& packet : packets) {
    const auto raw = std::span<const std::uint8_t>(packet.data(), packet.size());
    if (!packet_uses_legacy_crc(raw) || raw.size() < 4 ||
        !packet_has_valid_legacy_crc(raw, session_key)) {
      return std::nullopt;
    }

    const auto packet_crc = eq2::core::read_u16_be(raw, raw.size() - 2);
    if (packet_crc == 0) {
      return std::nullopt;
    }

    const auto stripped = raw.first(raw.size() - 2);
    if (!decode_protocol_packet(stripped).has_value()) {
      return std::nullopt;
    }

    const auto prefix_size = combined_packet_length_prefix_size(stripped.size());
    if (!prefix_size.has_value()) {
      return std::nullopt;
    }
    combined_payload_size += *prefix_size + stripped.size();
    stripped_packets.push_back(stripped);
  }

  if (2U + combined_payload_size + 2U >= kLegacyCombinedDatagramMaxSize) {
    return std::nullopt;
  }

  const auto combined_payload = encode_combined_packet(
      std::span<const std::span<const std::uint8_t>>(stripped_packets.data(),
                                                     stripped_packets.size()));
  if (!combined_payload.has_value()) {
    return std::nullopt;
  }

  return append_legacy_crc(encode_protocol_packet(kOpCombined, *combined_payload),
                           session_key);
}

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
      case kOpFragment:
        handle_fragment_packet(*packet, result);
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
        handle_ack_packet(*packet, result);
        break;
      case kOpOutOfOrderAck:
        handle_out_of_order_ack_packet(*packet, result);
        break;
      case kOpSessionStatResponse:
        break;
      case kOpOutOfSession:
        result.events.push_back(StreamEvent{
            .type = StreamEventType::disconnected,
            .protocol_opcode = packet->opcode,
        });
        break;
      case kOpSessionDisconnect:
        handle_session_disconnect(*packet, result);
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

    const auto sequence = next_out_sequence_++;
    PacketWriter writer;
    writer.append_u16_be(sequence);
    if (encrypted_) {
      auto encrypted_app = prepare_encrypted_application_packet(app_packet);
      server_cipher_.cypher(encrypted_app);
      writer.append_bytes(encrypted_app);
    } else {
      writer.append_bytes(app_packet);
    }
    auto outbound = append_legacy_crc(encode_protocol_packet(kOpPacket, writer.bytes()),
                                      options_.session_key);
    outbound_reliable_packets_[sequence] = outbound;
    ++sent_packets_;
    return outbound;
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

  auto prepare_encrypted_application_packet(std::span<const std::uint8_t> app_packet)
      -> std::vector<std::uint8_t> {
    if (app_packet.empty() || app_packet.front() == 2) {
      return {app_packet.begin(), app_packet.end()};
    }

    auto prepared = std::vector<std::uint8_t>{};
    prepared.reserve(app_packet.size() + 1U);
    prepared.push_back(0);
    prepared.insert(prepared.end(), app_packet.begin(), app_packet.end());
    return prepared;
  }

  void handle_session_request(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    const auto request = decode_session_request(packet.payload);
    if (!request.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    reset_session_state();
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

  void reset_session_state() {
    established_ = false;
    awaiting_encryption_key_ = false;
    encrypted_ = false;
    next_in_sequence_ = 0;
    next_out_sequence_ = 0;
    received_packets_ = 0;
    sent_packets_ = 0;
    rc4_key_ = 0;
    client_cipher_.init(0);
    server_cipher_.init(0);
    queued_packets_.clear();
    outbound_reliable_packets_.clear();
    fragment_buffer_.reset();
    fragment_expected_length_ = 0;
  }

  void handle_session_disconnect(const ProtocolPacketView& packet,
                                 StreamPipelineResult& result) {
    auto session = session_id_;
    if (packet.payload.size() >= 4) {
      session = eq2::core::read_u32_be(packet.payload, 0);
    }

    auto payload = std::vector<std::uint8_t>(6, 0);
    eq2::core::write_u32_be(std::span<std::uint8_t>(payload), 0, session);
    payload[5] = 0x06;
    result.outbound.push_back(append_legacy_crc(
        encode_protocol_packet(kOpSessionDisconnect, payload), options_.session_key));
    result.events.push_back(StreamEvent{
        .type = StreamEventType::disconnected,
        .protocol_opcode = packet.opcode,
    });
    reset_session_state();
  }

  void handle_ack_packet(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    PacketReader reader(packet.payload);
    const auto sequence = reader.read_u16_be();
    if (!sequence.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    outbound_reliable_packets_.erase(*sequence);
  }

  void handle_out_of_order_ack_packet(const ProtocolPacketView& packet,
                                      StreamPipelineResult& result) {
    PacketReader reader(packet.payload);
    const auto sequence = reader.read_u16_be();
    if (!sequence.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }

    const auto resend = outbound_reliable_packets_.find(*sequence);
    if (resend != outbound_reliable_packets_.end()) {
      result.outbound.push_back(resend->second);
    }
  }

  void handle_app_packet(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    if (established_ && packet.payload.size() >= 4) {
      PacketReader reader(packet.payload);
      const auto sequence = reader.read_u16_be();
      if (sequence.has_value()) {
        if (!sequence_is_processable(*sequence, packet, result)) {
          return;
        }
        const auto sequenced_payload = packet.payload.subspan(2);
        if (handle_embedded_app_combined_packet(*sequence,
                                                sequenced_payload,
                                                packet.opcode,
                                                result)) {
          drain_queued_packets(result);
          return;
        }
      }
    }

    if (established_ && encrypted_) {
      handle_encrypted_app_packet(packet, result);
      drain_queued_packets(result);
      return;
    }

    if (established_ && awaiting_encryption_key_ &&
        packet.payload.size() >= kMinimumRsaKeyPacketSize) {
      handle_encryption_key_packet(packet, result);
      drain_queued_packets(result);
      return;
    }

    const auto sequenced_min_size =
        2U + minimum_application_header_size(options_.application_opcode_width);
    if (established_ && packet.payload.size() >= sequenced_min_size) {
      PacketReader reader(packet.payload);
      const auto sequence = reader.read_u16_be();
      if (sequence.has_value()) {
        if (!sequence_is_processable(*sequence, packet, result)) {
          return;
        }
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
          drain_queued_packets(result);
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

  void handle_fragment_packet(const ProtocolPacketView& packet, StreamPipelineResult& result) {
    if (!established_ || packet.payload.size() < 4) {
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

    if (!sequence_is_processable(*sequence, packet, result)) {
      return;
    }

    next_in_sequence_ = static_cast<std::uint16_t>(*sequence + 1U);
    result.outbound.push_back(encode_ack(*sequence, options_.session_key));

    if (!fragment_buffer_.has_value()) {
      if (reader.remaining() < 4) {
        result.events.push_back(StreamEvent{
            .type = StreamEventType::malformed,
            .protocol_opcode = packet.opcode,
        });
        return;
      }
      const auto length = reader.read_u32_be();
      if (!length.has_value() || *length == 0) {
        result.events.push_back(StreamEvent{
            .type = StreamEventType::malformed,
            .protocol_opcode = packet.opcode,
        });
        return;
      }
      fragment_expected_length_ = *length;
      fragment_buffer_ = std::vector<std::uint8_t>{};
      fragment_buffer_->reserve(fragment_expected_length_);
    }

    const auto bytes = reader.read_bytes(reader.remaining());
    if (!bytes.has_value() ||
        fragment_buffer_->size() + bytes->size() > fragment_expected_length_) {
      fragment_buffer_.reset();
      fragment_expected_length_ = 0;
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = packet.opcode,
      });
      return;
    }
    fragment_buffer_->insert(fragment_buffer_->end(), bytes->begin(), bytes->end());

    if (fragment_buffer_->size() == fragment_expected_length_) {
      auto complete = std::move(*fragment_buffer_);
      fragment_buffer_.reset();
      fragment_expected_length_ = 0;
      process_reassembled_fragment(complete, packet.opcode, result);
    }

    drain_queued_packets(result);
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

  auto handle_embedded_app_combined_packet(std::uint16_t sequence,
                                           std::span<const std::uint8_t> sequenced_payload,
                                           std::uint16_t protocol_opcode,
                                           StreamPipelineResult& result) -> bool {
    if (sequenced_payload.size() < 2 || sequenced_payload[0] != 0 ||
        sequenced_payload[1] != kOpAppCombined) {
      return false;
    }

    const auto combined = decode_combined_packet(sequenced_payload.subspan(2));
    if (!combined.has_value()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = protocol_opcode,
      });
      return true;
    }

    next_in_sequence_ = static_cast<std::uint16_t>(sequence + 1U);
    result.outbound.push_back(encode_ack(sequence, options_.session_key));

    for (const auto& subpacket : *combined) {
      if (awaiting_encryption_key_ && !encrypted_) {
        if (subpacket.bytes.size() <= 8) {
          result.events.push_back(StreamEvent{
              .type = StreamEventType::malformed,
              .protocol_opcode = protocol_opcode,
          });
          continue;
        }
        const auto key = rsa_decrypt_legacy_tail(subpacket.bytes);
        if (key == 0) {
          result.events.push_back(StreamEvent{
              .type = StreamEventType::malformed,
              .protocol_opcode = protocol_opcode,
          });
          continue;
        }
        initialize_encryption_key(key);
        continue;
      }

      if (encrypted_) {
        emit_encrypted_application_packet(subpacket.bytes, protocol_opcode, result);
      } else {
        const auto app = decode_application_packet(subpacket.bytes, options_.application_opcode_width);
        if (app.has_value()) {
          result.events.push_back(StreamEvent{
              .type = StreamEventType::app_packet,
              .protocol_opcode = protocol_opcode,
              .app_packet = app,
          });
        } else {
          result.events.push_back(StreamEvent{
              .type = StreamEventType::malformed,
              .protocol_opcode = protocol_opcode,
          });
        }
      }
    }

    return true;
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

    initialize_encryption_key(key);
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

    emit_encrypted_application_packet(packet.payload.subspan(2), packet.opcode, result);
  }

  void initialize_encryption_key(std::uint64_t key) {
    rc4_key_ = key;
    client_cipher_.init(~key);
    server_cipher_.init(key);
    auto warmup = std::array<std::uint8_t, 20>{};
    client_cipher_.cypher(warmup);
    server_cipher_.cypher(warmup);
    encrypted_ = true;
    awaiting_encryption_key_ = false;
  }

  void emit_encrypted_application_packet(std::span<const std::uint8_t> encrypted_bytes,
                                         std::uint16_t protocol_opcode,
                                         StreamPipelineResult& result) {
    auto decrypted = std::vector<std::uint8_t>(encrypted_bytes.begin(), encrypted_bytes.end());
    client_cipher_.cypher(decrypted);
    if (decrypted.empty()) {
      result.events.push_back(StreamEvent{
          .type = StreamEventType::malformed,
          .protocol_opcode = protocol_opcode,
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
        .protocol_opcode = protocol_opcode,
    };
    event.app_packet_storage = std::move(decrypted);
    event.app_packet = ApplicationPacketView{
        .opcode = opcode,
        .payload = std::span<const std::uint8_t>(event.app_packet_storage).subspan(header_size),
        .header_size = header_size,
    };
    result.events.push_back(std::move(event));
  }

  auto sequence_is_processable(std::uint16_t sequence,
                               const ProtocolPacketView& packet,
                               StreamPipelineResult& result) -> bool {
    if (sequence == next_in_sequence_) {
      return true;
    }

    if (sequence_is_before(sequence, next_in_sequence_)) {
      result.outbound.push_back(encode_out_of_order_ack(sequence, options_.session_key));
      return false;
    }

    queued_packets_[sequence] = encode_protocol_packet(packet.opcode, packet.payload);
    return false;
  }

  void drain_queued_packets(StreamPipelineResult& result) {
    while (true) {
      const auto queued = queued_packets_.find(next_in_sequence_);
      if (queued == queued_packets_.end()) {
        return;
      }

      auto bytes = std::move(queued->second);
      queued_packets_.erase(queued);
      auto subresult = receive_datagram(bytes);
      result.events.insert(result.events.end(),
                           std::make_move_iterator(subresult.events.begin()),
                           std::make_move_iterator(subresult.events.end()));
      result.outbound.insert(result.outbound.end(),
                             std::make_move_iterator(subresult.outbound.begin()),
                             std::make_move_iterator(subresult.outbound.end()));
    }
  }

  void process_reassembled_fragment(std::span<const std::uint8_t> complete,
                                    std::uint16_t protocol_opcode,
                                    StreamPipelineResult& result) {
    if (complete.size() >= 2 && complete[0] == 0 && complete[1] == kOpAppCombined) {
      const auto combined = decode_combined_packet(complete.subspan(2));
      if (!combined.has_value()) {
        result.events.push_back(StreamEvent{
            .type = StreamEventType::malformed,
            .protocol_opcode = protocol_opcode,
        });
        return;
      }

      for (const auto& subpacket : *combined) {
        if (awaiting_encryption_key_ && !encrypted_) {
          if (subpacket.bytes.size() <= 8) {
            result.events.push_back(StreamEvent{
                .type = StreamEventType::malformed,
                .protocol_opcode = protocol_opcode,
            });
            continue;
          }
          const auto key = rsa_decrypt_legacy_tail(subpacket.bytes);
          if (key == 0) {
            result.events.push_back(StreamEvent{
                .type = StreamEventType::malformed,
                .protocol_opcode = protocol_opcode,
            });
            continue;
          }
          initialize_encryption_key(key);
          continue;
        }

        if (encrypted_) {
          emit_encrypted_application_packet(subpacket.bytes, protocol_opcode, result);
          continue;
        }

        emit_plain_application_packet(subpacket.bytes, protocol_opcode, result);
      }
      return;
    }

    if (encrypted_) {
      emit_encrypted_application_packet(complete, protocol_opcode, result);
      return;
    }

    emit_plain_application_packet(complete, protocol_opcode, result);
  }

  void emit_plain_application_packet(std::span<const std::uint8_t> bytes,
                                     std::uint16_t protocol_opcode,
                                     StreamPipelineResult& result) {
    auto event = StreamEvent{
        .type = StreamEventType::app_packet,
        .protocol_opcode = protocol_opcode,
    };
    event.app_packet_storage = {bytes.begin(), bytes.end()};
    result.events.push_back(std::move(event));

    auto& stored = result.events.back();
    const auto app =
        decode_application_packet(stored.app_packet_storage, options_.application_opcode_width);
    if (!app.has_value()) {
      stored.type = StreamEventType::malformed;
      return;
    }

    stored.app_packet = ApplicationPacketView{
        .opcode = app->opcode,
        .payload = std::span<const std::uint8_t>(stored.app_packet_storage).subspan(
            app->header_size),
        .header_size = app->header_size,
    };
  }

  static auto encode_ack(std::uint16_t sequence, std::uint32_t key) -> std::vector<std::uint8_t> {
    PacketWriter writer;
    writer.append_u16_be(sequence);
    return append_legacy_crc(encode_protocol_packet(kOpAck, writer.bytes()), key);
  }

  static auto encode_out_of_order_ack(std::uint16_t sequence, std::uint32_t key)
      -> std::vector<std::uint8_t> {
    PacketWriter writer;
    writer.append_u16_be(sequence);
    return append_legacy_crc(encode_protocol_packet(kOpOutOfOrderAck, writer.bytes()), key);
  }

  static auto sequence_is_before(std::uint16_t candidate, std::uint16_t reference) -> bool {
    return candidate != reference &&
           static_cast<std::uint16_t>(reference - candidate) < 0x8000U;
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
  std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> queued_packets_;
  std::unordered_map<std::uint16_t, std::vector<std::uint8_t>> outbound_reliable_packets_;
  std::optional<std::vector<std::uint8_t>> fragment_buffer_;
  std::uint32_t fragment_expected_length_ = 0;
};

}  // namespace eq2::protocol
