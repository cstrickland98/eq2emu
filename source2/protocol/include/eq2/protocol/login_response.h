#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_fields.h>
#include <eq2/protocol/protocol_packet.h>

namespace eq2::protocol {

inline constexpr std::size_t kLoginReplyPayloadSize = 5;
inline constexpr std::size_t kLegacyLoginReply284MinimumPayloadSize = 24;
inline constexpr std::int16_t kLegacyLoginReply284ClientVersion = 284;
inline constexpr std::int16_t kLegacyLoginReply546ClientVersion = 546;
inline constexpr std::int16_t kLegacyLoginReply843ClientVersion = 843;
inline constexpr std::int16_t kLegacyWorldList546ClientVersion = 546;

struct LoginReplyPayload {
  std::uint8_t reply_code = 0;
  std::uint32_t account_id = 0;
};

struct LoginWorldEntry {
  std::int32_t world_id = 0;
  std::string display_name;
  std::string address;
  bool locked = false;
  std::uint16_t player_count = 0;
  bool development_server = false;
};

struct LoginWorldListPayload {
  std::vector<LoginWorldEntry> worlds;
};

inline auto legacy_login_reply_uses_284_layout(std::int16_t client_version) -> bool {
  return client_version >= kLegacyLoginReply284ClientVersion &&
         client_version < kLegacyLoginReply843ClientVersion;
}

inline auto legacy_login_reply_uses_dof_primary_layout(std::int16_t client_version) -> bool {
  return client_version >= kLegacyLoginReply546ClientVersion &&
         client_version < kLegacyLoginReply843ClientVersion;
}

inline auto legacy_world_list_uses_546_layout(std::int16_t client_version) -> bool {
  return client_version >= kLegacyWorldList546ClientVersion;
}

inline auto decode_compact_login_reply_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginReplyPayload> {
  if (bytes.size() < kLoginReplyPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  const auto reply_code = reader.read_u8();
  const auto account_id = reader.read_u32_le();
  if (!reply_code.has_value() || !account_id.has_value()) {
    return std::nullopt;
  }

  return LoginReplyPayload{
      .reply_code = *reply_code,
      .account_id = *account_id,
  };
}

inline auto decode_legacy_login_reply_284_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginReplyPayload> {
  PacketReader reader(bytes);

  const auto reply_code = reader.read_u8();
  if (!reply_code.has_value()) {
    return std::nullopt;
  }

  if (!read_eq2_16bit_string(reader)) {
    return std::nullopt;
  }

  const auto parental_control_flag = reader.read_u8();
  const auto unknown2 = reader.read_bytes(8);
  const auto unknown3 = reader.read_u32_le();
  const auto account_id = reader.read_u32_le();
  if (!parental_control_flag.has_value() || !unknown2.has_value() ||
      !unknown3.has_value() || !account_id.has_value()) {
    return std::nullopt;
  }

  if (!read_eq2_16bit_string(reader)) {
    return std::nullopt;
  }

  const auto reset_appearance = reader.read_u8();
  const auto do_not_force_soga = reader.read_u8();
  if (!reset_appearance.has_value() || !do_not_force_soga.has_value()) {
    return std::nullopt;
  }

  if (reader.remaining() == 0) {
    return LoginReplyPayload{
        .reply_code = *reply_code,
        .account_id = *account_id,
    };
  }

  const auto unknown5 = reader.read_u16_le();
  const auto unknown6 = reader.read_u8();
  const auto unknown7 = reader.read_u32_le();
  const auto unknown8 = reader.read_bytes(2);
  const auto unknown10 = reader.read_u8();
  if (!unknown5.has_value() || !unknown6.has_value() || !unknown7.has_value() ||
      !unknown8.has_value() || !unknown10.has_value()) {
    return std::nullopt;
  }

  if (*unknown10 != 0) {
    const auto class_item_count = reader.read_u8();
    if (!class_item_count.has_value()) {
      return std::nullopt;
    }

    for (auto class_index = std::uint8_t{0}; class_index < *class_item_count; ++class_index) {
      const auto class_id = reader.read_u8();
      const auto item_count = reader.read_u8();
      if (!class_id.has_value() || !item_count.has_value()) {
        return std::nullopt;
      }

      for (auto item_index = std::uint8_t{0}; item_index < *item_count; ++item_index) {
        const auto item_bytes = reader.read_bytes(18);
        if (!item_bytes.has_value()) {
          return std::nullopt;
        }
      }
    }
  }

  const auto unknown_array2_size = reader.read_u8();
  if (!unknown_array2_size.has_value()) {
    return std::nullopt;
  }

  const auto array2_bytes =
      reader.read_bytes(static_cast<std::size_t>(*unknown_array2_size) * sizeof(std::uint32_t));
  if (!array2_bytes.has_value()) {
    return std::nullopt;
  }

  return LoginReplyPayload{
      .reply_code = *reply_code,
      .account_id = *account_id,
  };
}

inline auto decode_login_reply_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginReplyPayload> {
  if (bytes.size() == kLoginReplyPayloadSize) {
    return decode_compact_login_reply_payload(bytes);
  }

  if (auto legacy = decode_legacy_login_reply_284_payload(bytes)) {
    return legacy;
  }

  return decode_compact_login_reply_payload(bytes);
}

inline auto decode_login_reply_payload(std::span<const std::uint8_t> bytes,
                                       std::int16_t client_version)
    -> std::optional<LoginReplyPayload> {
  if (legacy_login_reply_uses_284_layout(client_version)) {
    if (auto legacy = decode_legacy_login_reply_284_payload(bytes)) {
      return legacy;
    }
  }

  return decode_login_reply_payload(bytes);
}

inline auto encode_compact_login_reply_payload(const LoginReplyPayload& payload)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u8(payload.reply_code);
  writer.append_u32_le(payload.account_id);
  return std::move(writer).into_bytes();
}

inline auto encode_legacy_login_reply_284_payload(const LoginReplyPayload& payload,
                                                  bool include_packetparser_tail = true)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u8(payload.reply_code);
  append_eq2_16bit_string(writer, "");
  writer.append_u8(0);
  for (auto index = 0; index < 8; ++index) {
    writer.append_u8(0);
  }
  writer.append_u32_le(0);
  writer.append_u32_le(payload.account_id);
  append_eq2_16bit_string(writer, "");
  writer.append_u8(0);
  writer.append_u8(1);
  if (!include_packetparser_tail) {
    return std::move(writer).into_bytes();
  }

  writer.append_u16_le(0x7cff);
  writer.append_u8(0xff);
  writer.append_u32_le(0x1fffff);
  writer.append_u8(0);
  writer.append_u8(0xee);
  writer.append_u8(0xff);
  writer.append_u8(0);
  writer.append_u8(0);
  return std::move(writer).into_bytes();
}

inline auto encode_login_reply_payload(const LoginReplyPayload& payload)
    -> std::vector<std::uint8_t> {
  return encode_compact_login_reply_payload(payload);
}

inline auto encode_login_reply_payload(const LoginReplyPayload& payload,
                                       std::int16_t client_version)
    -> std::vector<std::uint8_t> {
  if (legacy_login_reply_uses_284_layout(client_version)) {
    return encode_legacy_login_reply_284_payload(
        payload,
        !legacy_login_reply_uses_dof_primary_layout(client_version));
  }

  return encode_compact_login_reply_payload(payload);
}

inline auto login_reply_protocol_frame_size(
    std::uint16_t application_opcode,
    ApplicationOpcodeWidth opcode_width = ApplicationOpcodeWidth::two_bytes,
    bool sequenced = false) -> std::size_t {
  return kProtocolHeaderSize + (sequenced ? 2U : 0U) +
         application_header_size(application_opcode, opcode_width) +
         kLoginReplyPayloadSize;
}

inline auto decode_login_reply_protocol_frame(
    std::span<const std::uint8_t> bytes,
    std::uint16_t application_opcode,
    ApplicationOpcodeWidth opcode_width = ApplicationOpcodeWidth::two_bytes)
    -> std::optional<LoginReplyPayload> {
  const auto decode_app_reply =
      [&](std::span<const std::uint8_t> payload) -> std::optional<LoginReplyPayload> {
    const auto app = decode_application_packet(payload, opcode_width);
    if (!app.has_value() || app->opcode != application_opcode ||
        app->payload.size() < kLoginReplyPayloadSize) {
      return std::nullopt;
    }

    return decode_login_reply_payload(app->payload);
  };

  const auto decode_packet_reply =
      [&](std::span<const std::uint8_t> packet_bytes) -> std::optional<LoginReplyPayload> {
    const auto protocol = decode_protocol_packet(packet_bytes);
    if (!protocol.has_value() || protocol->opcode != kOpPacket) {
      return std::nullopt;
    }

    if (protocol->payload.size() >= 2 + application_header_size(application_opcode, opcode_width) +
                                        kLoginReplyPayloadSize) {
      if (auto sequenced = decode_app_reply(protocol->payload.subspan(2))) {
        return sequenced;
      }
    }

    return decode_app_reply(protocol->payload);
  };

  if (auto direct = decode_packet_reply(bytes)) {
    return direct;
  }

  const auto protocol = decode_protocol_packet(bytes);
  if (!protocol.has_value() || protocol->opcode != kOpCombined) {
    return std::nullopt;
  }

  const auto combined = decode_combined_packet(protocol->payload);
  if (!combined.has_value()) {
    return std::nullopt;
  }

  for (const auto& subpacket : *combined) {
    if (auto reply = decode_packet_reply(subpacket.bytes)) {
      return reply;
    }
  }

  return std::nullopt;
}

inline auto decode_compact_login_world_list_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginWorldListPayload> {
  PacketReader reader(bytes);
  const auto count = reader.read_u16_le();
  if (!count.has_value()) {
    return std::nullopt;
  }

  auto worlds = std::vector<LoginWorldEntry>{};
  worlds.reserve(*count);
  for (auto index = std::size_t{0}; index < *count; ++index) {
    const auto world_id = read_i32_le(reader);
    auto display_name = read_eq2_16bit_string(reader);
    auto address = read_eq2_16bit_string(reader);
    const auto development = reader.read_u8();
    if (!world_id.has_value() || !display_name.has_value() || !address.has_value() ||
        !development.has_value()) {
      return std::nullopt;
    }

    worlds.push_back(LoginWorldEntry{
        .world_id = *world_id,
        .display_name = std::move(*display_name),
        .address = std::move(*address),
        .development_server = *development != 0,
    });
  }

  if (reader.remaining() != 0 && reader.remaining() != 2) {
    return std::nullopt;
  }

  return LoginWorldListPayload{
      .worlds = std::move(worlds),
  };
}

inline auto decode_legacy_login_world_list_546_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginWorldListPayload> {
  PacketReader reader(bytes);
  const auto count = reader.read_u8();
  if (!count.has_value()) {
    return std::nullopt;
  }

  auto worlds = std::vector<LoginWorldEntry>{};
  worlds.reserve(*count);
  for (auto index = std::size_t{0}; index < *count; ++index) {
    const auto world_id = read_i32_le(reader);
    auto display_name = read_eq2_16bit_string(reader);
    auto name2 = read_eq2_16bit_string(reader);
    const auto tag = reader.read_u8();
    const auto locked = reader.read_u8();
    const auto hidden = reader.read_u8();
    const auto unknown = reader.read_u8();
    const auto player_count = reader.read_u16_le();
    const auto load = reader.read_u8();
    const auto number_online_flag = reader.read_u8();
    const auto unknown2 = reader.read_u8();
    const auto allowed_races = reader.read_u32_le();
    if (!world_id.has_value() || !display_name.has_value() || !name2.has_value() ||
        !tag.has_value() || !locked.has_value() || !hidden.has_value() ||
        !unknown.has_value() || !player_count.has_value() || !load.has_value() ||
        !number_online_flag.has_value() || !unknown2.has_value() ||
        !allowed_races.has_value()) {
      return std::nullopt;
    }

    worlds.push_back(LoginWorldEntry{
        .world_id = *world_id,
        .display_name = std::move(*display_name),
        .address = "",
        .locked = *locked != 0,
        .player_count = *player_count,
        .development_server = *load == 1,
    });
  }

  if (reader.remaining() != 0 && reader.remaining() != 2) {
    return std::nullopt;
  }

  return LoginWorldListPayload{
      .worlds = std::move(worlds),
  };
}

inline auto decode_login_world_list_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<LoginWorldListPayload> {
  if (auto compact = decode_compact_login_world_list_payload(bytes)) {
    return compact;
  }

  return decode_legacy_login_world_list_546_payload(bytes);
}

inline auto decode_login_world_list_payload(std::span<const std::uint8_t> bytes,
                                            std::int16_t client_version)
    -> std::optional<LoginWorldListPayload> {
  if (legacy_world_list_uses_546_layout(client_version)) {
    if (auto legacy = decode_legacy_login_world_list_546_payload(bytes)) {
      return legacy;
    }
  }

  return decode_login_world_list_payload(bytes);
}

inline auto encode_compact_login_world_list_payload(const LoginWorldListPayload& payload)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  const auto count = payload.worlds.size() > static_cast<std::size_t>(0xffffU)
                         ? static_cast<std::uint16_t>(0xffffU)
                         : static_cast<std::uint16_t>(payload.worlds.size());
  writer.append_u16_le(count);

  for (auto index = std::size_t{0}; index < count; ++index) {
    const auto& world = payload.worlds[index];
    append_i32_le(writer, world.world_id);
    append_eq2_16bit_string(writer, world.display_name);
    append_eq2_16bit_string(writer, world.address);
    writer.append_u8(world.development_server ? 1 : 0);
  }

  return std::move(writer).into_bytes();
}

inline auto encode_legacy_login_world_list_546_payload(const LoginWorldListPayload& payload)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  const auto count = payload.worlds.size() > static_cast<std::size_t>(
                                             std::numeric_limits<std::uint8_t>::max())
                         ? std::numeric_limits<std::uint8_t>::max()
                         : static_cast<std::uint8_t>(payload.worlds.size());
  writer.append_u8(count);

  for (auto index = std::size_t{0}; index < count; ++index) {
    const auto& world = payload.worlds[index];
    append_i32_le(writer, world.world_id);
    append_eq2_16bit_string(writer, world.display_name);
    append_eq2_16bit_string(writer, world.display_name);
    writer.append_u8(1);
    writer.append_u8(world.locked ? 1 : 0);
    writer.append_u8(0);
    writer.append_u8(static_cast<std::uint8_t>(index + 1));
    writer.append_u16_le(world.player_count);
    writer.append_u8(world.development_server ? 1 : 0);
    writer.append_u8(1);
    writer.append_u8(0);
    writer.append_u32_le(0xffffffffU);
  }

  return std::move(writer).into_bytes();
}

inline auto encode_login_world_list_payload(const LoginWorldListPayload& payload)
    -> std::vector<std::uint8_t> {
  return encode_compact_login_world_list_payload(payload);
}

inline auto encode_login_world_list_payload(const LoginWorldListPayload& payload,
                                            std::int16_t client_version)
    -> std::vector<std::uint8_t> {
  if (legacy_world_list_uses_546_layout(client_version)) {
    return encode_legacy_login_world_list_546_payload(payload);
  }

  return encode_compact_login_world_list_payload(payload);
}

inline auto encode_empty_character_list_payload(std::uint32_t account_id,
                                                std::int16_t client_version)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u8(0);
  writer.append_u32_le(account_id);
  writer.append_u32_le(0xffffffffU);
  writer.append_u16_le(0);
  writer.append_u32_le(client_version <= 561 ? 7U : 10U);
  writer.append_u8(0);

  if (client_version > 561) {
    for (auto index = 0; index < 3; ++index) {
      writer.append_u32_le(0xffffffffU);
    }
    writer.append_u32_le(0);
    writer.append_u8(0);
    writer.append_u8(0);
  }

  return std::move(writer).into_bytes();
}

}  // namespace eq2::protocol
