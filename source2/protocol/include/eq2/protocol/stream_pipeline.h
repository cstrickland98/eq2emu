#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

namespace eq2::protocol {

enum class StreamEventType {
  session_requested,
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
    const auto packet = decode_protocol_packet(bytes);
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
        result.events.push_back(StreamEvent{
            .type = StreamEventType::keep_alive,
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

 private:
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
      result.events.insert(result.events.end(), subresult.events.begin(), subresult.events.end());
      result.outbound.insert(result.outbound.end(),
                             std::make_move_iterator(subresult.outbound.begin()),
                             std::make_move_iterator(subresult.outbound.end()));
    }
  }

  StreamPipelineOptions options_;
  bool established_ = false;
  std::uint32_t session_id_ = 0;
};

}  // namespace eq2::protocol
