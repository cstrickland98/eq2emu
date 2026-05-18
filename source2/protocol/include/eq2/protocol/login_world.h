#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_buffer.h>

namespace eq2::protocol {

inline constexpr std::uint16_t kServerOpKeepAlive = 0x0001;
inline constexpr std::uint16_t kServerOpLsInfo = 0x1000;
inline constexpr std::uint16_t kServerOpLsStatus = 0x1001;
inline constexpr std::uint16_t kServerOpCharTimeStamp = 0x200f;
inline constexpr std::uint16_t kServerOpBasicCharUpdate = 0x2012;
inline constexpr std::uint16_t kServerOpCharacterCreate = 0x2013;
inline constexpr std::uint16_t kServerOpNameCharUpdate = 0x2014;
inline constexpr std::uint16_t kServerOpRaceUpdate = 0x2018;
inline constexpr std::uint16_t kServerOpZoneUpdate = 0x2019;
inline constexpr std::uint16_t kServerOpZoneUpdates = 0x201c;
inline constexpr std::uint16_t kServerOpLoginEquipment = 0x201d;
inline constexpr std::uint16_t kServerOpCharacterPicture = 0x201e;
inline constexpr std::uint16_t kServerOpUserToWorldRequest = 0xab00;
inline constexpr std::uint16_t kServerOpUserToWorldResponse = 0xab01;
inline constexpr std::uint8_t kCharUpdateLevelFlag = 1;
inline constexpr std::uint8_t kCharUpdateClassFlag = 4;
inline constexpr std::uint8_t kCharUpdateGenderFlag = 8;
inline constexpr std::uint8_t kCharUpdateDeleteFlag = 128;
inline constexpr std::size_t kServerLsInfoPayloadSize = 832;
inline constexpr std::size_t kServerLsStatusPayloadSize = 13;
inline constexpr std::size_t kCharacterTimeStampPayloadSize = 12;
inline constexpr std::size_t kCharDataUpdatePayloadSize = 13;
inline constexpr std::size_t kRaceUpdatePayloadSize = 11;
inline constexpr std::size_t kCharNameUpdateHeaderSize = 9;
inline constexpr std::size_t kCharNameUpdateMaxPayloadSize = 74;
inline constexpr std::size_t kCharZoneUpdateHeaderSize = 13;
inline constexpr std::size_t kCharZoneUpdateMaxPayloadSize = 78;
inline constexpr std::size_t kZoneUpdateRequestPayloadSize = 2;
inline constexpr std::size_t kZoneUpdateListHeaderSize = 2;
inline constexpr std::size_t kZoneUpdateHeaderSize = 6;
inline constexpr std::size_t kMaxZoneUpdates = 20;
inline constexpr std::size_t kEquipmentUpdateRequestPayloadSize = 2;
inline constexpr std::size_t kEquipmentUpdateListHeaderSize = 2;
inline constexpr std::size_t kEquipmentUpdatePayloadSize = 20;
inline constexpr std::size_t kMaxLoginEquipmentUpdates = 100;
inline constexpr std::size_t kCharPictureUpdateHeaderSize = 10;
inline constexpr std::size_t kUserToWorldRequestPayloadSize = 41;
inline constexpr std::size_t kUserToWorldResponsePayloadSize = 109;

struct ServerLsInfo {
  std::string_view world_name;
  std::string_view address;
  std::string_view account;
  std::string_view password;
  std::string_view protocol_version;
  std::string_view server_version;
  std::uint8_t server_type = 0;
  std::uint32_t database_version = 0;
};

struct ServerLsStatus {
  std::int32_t status = 0;
  std::int32_t player_count = 0;
  std::int32_t zone_count = 0;
  std::uint8_t world_max_level = 0;
};

struct UserToWorldRequest {
  std::int32_t login_account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t world_id = 0;
  std::int32_t from_id = 0;
  std::int32_t to_id = 0;
  std::string_view ip_address;
};

struct CharDataUpdate {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::uint8_t update_field = 0;
  std::int32_t update_data = 0;
};

struct CharacterTimeStamp {
  std::int32_t character_id = 0;
  std::int32_t account_id = 0;
  std::int32_t unix_timestamp = 0;
};

struct RaceUpdate {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::int16_t model_type = 0;
  std::uint8_t race = 0;
};

struct CharNameUpdate {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::string_view name;
};

struct CharZoneUpdate {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t zone_id = 0;
  std::string_view zone_name;
};

struct WorldZoneUpdate {
  std::int32_t zone_id = 0;
  std::string_view name;
  std::string_view description;
};

struct EquipmentUpdate {
  std::int32_t id = 0;
  std::int32_t world_character_id = 0;
  std::int16_t equip_type = 0;
  std::uint8_t red = 0xff;
  std::uint8_t green = 0xff;
  std::uint8_t blue = 0xff;
  std::uint8_t highlight_red = 0xff;
  std::uint8_t highlight_green = 0xff;
  std::uint8_t highlight_blue = 0xff;
  std::int32_t slot = 0;
};

struct CharacterPictureUpdate {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::span<const std::uint8_t> picture;
};

struct UserToWorldResponse {
  std::int32_t login_account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t world_id = 0;
  std::int32_t access_key = 0;
  std::uint8_t response = 0;
  std::string_view ip_address;
  std::int32_t port = 0;
  std::int32_t from_id = 0;
  std::int32_t to_id = 0;
};

namespace detail {

inline auto fixed_c_string_view(std::span<const std::uint8_t> bytes) -> std::string_view {
  const auto end = std::find(bytes.begin(), bytes.end(), std::uint8_t{0});
  const auto size = static_cast<std::size_t>(end - bytes.begin());
  return std::string_view(reinterpret_cast<const char*>(bytes.data()), size);
}

inline auto read_fixed_c_string(PacketReader& reader, std::size_t size)
    -> std::optional<std::string_view> {
  const auto bytes = reader.read_bytes(size);
  if (!bytes.has_value()) {
    return std::nullopt;
  }

  return fixed_c_string_view(*bytes);
}

inline auto read_interserver_i32(PacketReader& reader) -> std::optional<std::int32_t> {
  const auto value = reader.read_u32_le();
  if (!value.has_value()) {
    return std::nullopt;
  }

  return static_cast<std::int32_t>(*value);
}

inline void append_interserver_i32(PacketWriter& writer, std::int32_t value) {
  writer.append_u32_le(static_cast<std::uint32_t>(value));
}

inline void append_fixed_c_string(PacketWriter& writer, std::string_view value, std::size_t size) {
  std::vector<std::uint8_t> bytes(size, 0);
  const auto copy_size = std::min(value.size(), size);
  std::copy_n(reinterpret_cast<const std::uint8_t*>(value.data()), copy_size, bytes.data());
  writer.append_bytes(bytes);
}

}  // namespace detail

inline auto decode_server_ls_info_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<ServerLsInfo> {
  if (bytes.size() != kServerLsInfoPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto world_name = detail::read_fixed_c_string(reader, 201);
  auto address = detail::read_fixed_c_string(reader, 250);
  auto account = detail::read_fixed_c_string(reader, 31);
  auto password = detail::read_fixed_c_string(reader, 256);
  auto protocol_version = detail::read_fixed_c_string(reader, 25);
  auto server_version = detail::read_fixed_c_string(reader, 64);
  auto server_type = reader.read_u8();
  auto database_version = reader.read_u32_le();
  if (!world_name || !address || !account || !password || !protocol_version || !server_version ||
      !server_type || !database_version) {
    return std::nullopt;
  }

  return ServerLsInfo{
      .world_name = *world_name,
      .address = *address,
      .account = *account,
      .password = *password,
      .protocol_version = *protocol_version,
      .server_version = *server_version,
      .server_type = *server_type,
      .database_version = *database_version,
  };
}

inline auto encode_server_ls_info_payload(const ServerLsInfo& info) -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_fixed_c_string(writer, info.world_name, 201);
  detail::append_fixed_c_string(writer, info.address, 250);
  detail::append_fixed_c_string(writer, info.account, 31);
  detail::append_fixed_c_string(writer, info.password, 256);
  detail::append_fixed_c_string(writer, info.protocol_version, 25);
  detail::append_fixed_c_string(writer, info.server_version, 64);
  writer.append_u8(info.server_type);
  writer.append_u32_le(info.database_version);
  return std::move(writer).into_bytes();
}

inline auto decode_server_ls_status_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<ServerLsStatus> {
  if (bytes.size() != kServerLsStatusPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto status = detail::read_interserver_i32(reader);
  auto player_count = detail::read_interserver_i32(reader);
  auto zone_count = detail::read_interserver_i32(reader);
  auto world_max_level = reader.read_u8();
  if (!status || !player_count || !zone_count || !world_max_level) {
    return std::nullopt;
  }

  return ServerLsStatus{
      .status = *status,
      .player_count = *player_count,
      .zone_count = *zone_count,
      .world_max_level = *world_max_level,
  };
}

inline auto encode_server_ls_status_payload(const ServerLsStatus& status)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, status.status);
  detail::append_interserver_i32(writer, status.player_count);
  detail::append_interserver_i32(writer, status.zone_count);
  writer.append_u8(status.world_max_level);
  return std::move(writer).into_bytes();
}

inline auto decode_char_data_update_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<CharDataUpdate> {
  if (bytes.size() != kCharDataUpdatePayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto update_field = reader.read_u8();
  auto update_data = detail::read_interserver_i32(reader);
  if (!account_id || !character_id || !update_field || !update_data) {
    return std::nullopt;
  }

  return CharDataUpdate{
      .account_id = *account_id,
      .character_id = *character_id,
      .update_field = *update_field,
      .update_data = *update_data,
  };
}

inline auto encode_char_data_update_payload(const CharDataUpdate& update)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, update.account_id);
  detail::append_interserver_i32(writer, update.character_id);
  writer.append_u8(update.update_field);
  detail::append_interserver_i32(writer, update.update_data);
  return std::move(writer).into_bytes();
}

inline auto decode_character_timestamp_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<CharacterTimeStamp> {
  if (bytes.size() != kCharacterTimeStampPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto character_id = detail::read_interserver_i32(reader);
  auto account_id = detail::read_interserver_i32(reader);
  auto unix_timestamp = detail::read_interserver_i32(reader);
  if (!character_id || !account_id || !unix_timestamp) {
    return std::nullopt;
  }

  return CharacterTimeStamp{
      .character_id = *character_id,
      .account_id = *account_id,
      .unix_timestamp = *unix_timestamp,
  };
}

inline auto encode_character_timestamp_payload(const CharacterTimeStamp& update)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, update.character_id);
  detail::append_interserver_i32(writer, update.account_id);
  detail::append_interserver_i32(writer, update.unix_timestamp);
  return std::move(writer).into_bytes();
}

inline auto decode_race_update_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<RaceUpdate> {
  if (bytes.size() != kRaceUpdatePayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto model_type = reader.read_u16_le();
  auto race = reader.read_u8();
  if (!account_id || !character_id || !model_type || !race) {
    return std::nullopt;
  }

  return RaceUpdate{
      .account_id = *account_id,
      .character_id = *character_id,
      .model_type = static_cast<std::int16_t>(*model_type),
      .race = *race,
  };
}

inline auto encode_race_update_payload(const RaceUpdate& update)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, update.account_id);
  detail::append_interserver_i32(writer, update.character_id);
  writer.append_u16_le(static_cast<std::uint16_t>(update.model_type));
  writer.append_u8(update.race);
  return std::move(writer).into_bytes();
}

inline auto decode_char_name_update_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<CharNameUpdate> {
  if (bytes.size() < kCharNameUpdateHeaderSize ||
      bytes.size() > kCharNameUpdateMaxPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto name_length = reader.read_u8();
  if (!account_id || !character_id || !name_length || *name_length == 0 ||
      *name_length >= 64 || reader.remaining() < *name_length) {
    return std::nullopt;
  }

  auto name = reader.read_bytes(*name_length);
  if (!name.has_value()) {
    return std::nullopt;
  }

  return CharNameUpdate{
      .account_id = *account_id,
      .character_id = *character_id,
      .name = detail::fixed_c_string_view(*name),
  };
}

inline auto encode_char_name_update_payload(const CharNameUpdate& update)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, update.account_id);
  detail::append_interserver_i32(writer, update.character_id);
  const auto size = static_cast<std::uint8_t>(std::min<std::size_t>(update.name.size(), 63));
  writer.append_u8(size);
  writer.append_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(update.name.data()), size));
  return std::move(writer).into_bytes();
}

inline auto decode_char_zone_update_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<CharZoneUpdate> {
  if (bytes.size() < kCharZoneUpdateHeaderSize ||
      bytes.size() > kCharZoneUpdateMaxPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto zone_id = detail::read_interserver_i32(reader);
  auto zone_length = reader.read_u8();
  if (!account_id || !character_id || !zone_id || !zone_length ||
      *zone_length >= 64 || reader.remaining() < *zone_length) {
    return std::nullopt;
  }

  auto zone_name = reader.read_bytes(*zone_length);
  if (!zone_name.has_value()) {
    return std::nullopt;
  }

  return CharZoneUpdate{
      .account_id = *account_id,
      .character_id = *character_id,
      .zone_id = *zone_id,
      .zone_name = detail::fixed_c_string_view(*zone_name),
  };
}

inline auto encode_char_zone_update_payload(const CharZoneUpdate& update)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, update.account_id);
  detail::append_interserver_i32(writer, update.character_id);
  detail::append_interserver_i32(writer, update.zone_id);
  const auto size = static_cast<std::uint8_t>(std::min<std::size_t>(update.zone_name.size(), 63));
  writer.append_u8(size);
  writer.append_bytes(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(update.zone_name.data()), size));
  return std::move(writer).into_bytes();
}

inline auto encode_zone_update_request_payload(std::uint16_t max_per_batch = kMaxZoneUpdates)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u16_le(max_per_batch);
  return std::move(writer).into_bytes();
}

inline auto decode_world_zone_updates_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<std::vector<WorldZoneUpdate>> {
  if (bytes.size() < kZoneUpdateListHeaderSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  const auto total_updates = reader.read_u16_le();
  if (!total_updates.has_value() || *total_updates > kMaxZoneUpdates) {
    return std::nullopt;
  }

  auto updates = std::vector<WorldZoneUpdate>{};
  updates.reserve(*total_updates);
  for (std::uint16_t index = 0; index < *total_updates; ++index) {
    if (reader.remaining() < kZoneUpdateHeaderSize) {
      return std::nullopt;
    }

    const auto zone_id = detail::read_interserver_i32(reader);
    const auto name_length = reader.read_u8();
    const auto description_length = reader.read_u8();
    if (!zone_id || !name_length || !description_length ||
        reader.remaining() < static_cast<std::size_t>(*name_length) +
                                 static_cast<std::size_t>(*description_length)) {
      return std::nullopt;
    }

    const auto name = reader.read_bytes(*name_length);
    const auto description = reader.read_bytes(*description_length);
    if (!name.has_value() || !description.has_value()) {
      return std::nullopt;
    }

    updates.push_back(WorldZoneUpdate{
        .zone_id = *zone_id,
        .name = std::string_view(reinterpret_cast<const char*>(name->data()), name->size()),
        .description = std::string_view(reinterpret_cast<const char*>(description->data()),
                                        description->size()),
    });
  }

  return updates;
}

inline auto encode_world_zone_updates_payload(std::span<const WorldZoneUpdate> updates)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u16_le(static_cast<std::uint16_t>(
      std::min<std::size_t>(updates.size(), kMaxZoneUpdates)));

  auto count = std::size_t{0};
  for (const auto& update : updates) {
    if (count++ >= kMaxZoneUpdates) {
      break;
    }
    detail::append_interserver_i32(writer, update.zone_id);
    const auto name_size =
        static_cast<std::uint8_t>(std::min<std::size_t>(update.name.size(), 255));
    const auto description_size =
        static_cast<std::uint8_t>(std::min<std::size_t>(update.description.size(), 255));
    writer.append_u8(name_size);
    writer.append_u8(description_size);
    writer.append_bytes(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(update.name.data()), name_size));
    writer.append_bytes(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(update.description.data()), description_size));
  }

  return std::move(writer).into_bytes();
}

inline auto encode_equipment_update_request_payload(
    std::uint16_t max_per_batch = kMaxLoginEquipmentUpdates) -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u16_le(max_per_batch);
  return std::move(writer).into_bytes();
}

inline auto decode_login_equipment_update_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<std::vector<EquipmentUpdate>> {
  if (bytes.size() < kEquipmentUpdateListHeaderSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  const auto total_updates = reader.read_u16_le();
  if (!total_updates.has_value() || *total_updates > kMaxLoginEquipmentUpdates ||
      reader.remaining() < static_cast<std::size_t>(*total_updates) *
                               kEquipmentUpdatePayloadSize) {
    return std::nullopt;
  }

  auto updates = std::vector<EquipmentUpdate>{};
  updates.reserve(*total_updates);
  for (std::uint16_t index = 0; index < *total_updates; ++index) {
    const auto id = detail::read_interserver_i32(reader);
    const auto world_character_id = detail::read_interserver_i32(reader);
    const auto equip_type = reader.read_u16_le();
    const auto red = reader.read_u8();
    const auto green = reader.read_u8();
    const auto blue = reader.read_u8();
    const auto highlight_red = reader.read_u8();
    const auto highlight_green = reader.read_u8();
    const auto highlight_blue = reader.read_u8();
    const auto slot = detail::read_interserver_i32(reader);
    if (!id || !world_character_id || !equip_type || !red || !green || !blue ||
        !highlight_red || !highlight_green || !highlight_blue || !slot) {
      return std::nullopt;
    }

    updates.push_back(EquipmentUpdate{
        .id = *id,
        .world_character_id = *world_character_id,
        .equip_type = static_cast<std::int16_t>(*equip_type),
        .red = *red,
        .green = *green,
        .blue = *blue,
        .highlight_red = *highlight_red,
        .highlight_green = *highlight_green,
        .highlight_blue = *highlight_blue,
        .slot = *slot,
    });
  }

  return updates;
}

inline auto encode_login_equipment_update_payload(std::span<const EquipmentUpdate> updates)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  writer.append_u16_le(static_cast<std::uint16_t>(
      std::min<std::size_t>(updates.size(), kMaxLoginEquipmentUpdates)));

  auto count = std::size_t{0};
  for (const auto& update : updates) {
    if (count++ >= kMaxLoginEquipmentUpdates) {
      break;
    }
    detail::append_interserver_i32(writer, update.id);
    detail::append_interserver_i32(writer, update.world_character_id);
    writer.append_u16_le(static_cast<std::uint16_t>(update.equip_type));
    writer.append_u8(update.red);
    writer.append_u8(update.green);
    writer.append_u8(update.blue);
    writer.append_u8(update.highlight_red);
    writer.append_u8(update.highlight_green);
    writer.append_u8(update.highlight_blue);
    detail::append_interserver_i32(writer, update.slot);
  }

  return std::move(writer).into_bytes();
}

inline auto decode_character_picture_update_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<CharacterPictureUpdate> {
  if (bytes.size() < kCharPictureUpdateHeaderSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  const auto account_id = detail::read_interserver_i32(reader);
  const auto character_id = detail::read_interserver_i32(reader);
  const auto picture_size = reader.read_u16_le();
  if (!account_id || !character_id || !picture_size || reader.remaining() < *picture_size) {
    return std::nullopt;
  }

  const auto picture = reader.read_bytes(*picture_size);
  if (!picture.has_value()) {
    return std::nullopt;
  }

  return CharacterPictureUpdate{
      .account_id = *account_id,
      .character_id = *character_id,
      .picture = *picture,
  };
}

inline auto encode_character_picture_update_payload(const CharacterPictureUpdate& update)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, update.account_id);
  detail::append_interserver_i32(writer, update.character_id);
  writer.append_u16_le(static_cast<std::uint16_t>(std::min<std::size_t>(
      update.picture.size(), std::numeric_limits<std::uint16_t>::max())));
  writer.append_bytes(update.picture.first(std::min<std::size_t>(
      update.picture.size(), std::numeric_limits<std::uint16_t>::max())));
  return std::move(writer).into_bytes();
}

inline auto decode_user_to_world_request_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<UserToWorldRequest> {
  if (bytes.size() != kUserToWorldRequestPayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto login_account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto world_id = detail::read_interserver_i32(reader);
  auto from_id = detail::read_interserver_i32(reader);
  auto to_id = detail::read_interserver_i32(reader);
  auto ip_address = detail::read_fixed_c_string(reader, 21);
  if (!login_account_id || !character_id || !world_id || !from_id || !to_id || !ip_address) {
    return std::nullopt;
  }

  return UserToWorldRequest{
      .login_account_id = *login_account_id,
      .character_id = *character_id,
      .world_id = *world_id,
      .from_id = *from_id,
      .to_id = *to_id,
      .ip_address = *ip_address,
  };
}

inline auto encode_user_to_world_request_payload(const UserToWorldRequest& request)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, request.login_account_id);
  detail::append_interserver_i32(writer, request.character_id);
  detail::append_interserver_i32(writer, request.world_id);
  detail::append_interserver_i32(writer, request.from_id);
  detail::append_interserver_i32(writer, request.to_id);
  detail::append_fixed_c_string(writer, request.ip_address, 21);
  return std::move(writer).into_bytes();
}

inline auto decode_user_to_world_response_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<UserToWorldResponse> {
  if (bytes.size() != kUserToWorldResponsePayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto login_account_id = detail::read_interserver_i32(reader);
  auto character_id = detail::read_interserver_i32(reader);
  auto world_id = detail::read_interserver_i32(reader);
  auto access_key = detail::read_interserver_i32(reader);
  auto response = reader.read_u8();
  auto ip_address = detail::read_fixed_c_string(reader, 80);
  auto port = detail::read_interserver_i32(reader);
  auto from_id = detail::read_interserver_i32(reader);
  auto to_id = detail::read_interserver_i32(reader);
  if (!login_account_id || !character_id || !world_id || !access_key || !response ||
      !ip_address || !port || !from_id || !to_id) {
    return std::nullopt;
  }

  return UserToWorldResponse{
      .login_account_id = *login_account_id,
      .character_id = *character_id,
      .world_id = *world_id,
      .access_key = *access_key,
      .response = *response,
      .ip_address = *ip_address,
      .port = *port,
      .from_id = *from_id,
      .to_id = *to_id,
  };
}

inline auto encode_user_to_world_response_payload(const UserToWorldResponse& response)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  detail::append_interserver_i32(writer, response.login_account_id);
  detail::append_interserver_i32(writer, response.character_id);
  detail::append_interserver_i32(writer, response.world_id);
  detail::append_interserver_i32(writer, response.access_key);
  writer.append_u8(response.response);
  detail::append_fixed_c_string(writer, response.ip_address, 80);
  detail::append_interserver_i32(writer, response.port);
  detail::append_interserver_i32(writer, response.from_id);
  detail::append_interserver_i32(writer, response.to_id);
  return std::move(writer).into_bytes();
}

}  // namespace eq2::protocol
