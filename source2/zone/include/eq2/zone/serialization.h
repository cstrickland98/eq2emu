#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_fields.h>
#include <eq2/zone/runtime.h>

namespace eq2::zone {

inline constexpr std::uint8_t kZoneUpdateSpawnAdded = 1;
inline constexpr std::uint8_t kZoneUpdateSpawnMoved = 2;
inline constexpr std::uint8_t kZoneUpdateSpawnRemoved = 3;
inline constexpr std::uint8_t kZoneUpdateCombatResolved = 4;

inline void append_position(eq2::protocol::PacketWriter& writer, const Position& position) {
  eq2::protocol::append_f32_le(writer, position.x);
  eq2::protocol::append_f32_le(writer, position.y);
  eq2::protocol::append_f32_le(writer, position.z);
  eq2::protocol::append_f32_le(writer, position.heading);
}

inline auto read_position(eq2::protocol::PacketReader& reader) -> std::optional<Position> {
  const auto x = eq2::protocol::read_f32_le(reader);
  const auto y = eq2::protocol::read_f32_le(reader);
  const auto z = eq2::protocol::read_f32_le(reader);
  const auto heading = eq2::protocol::read_f32_le(reader);
  if (!x.has_value() || !y.has_value() || !z.has_value() || !heading.has_value()) {
    return std::nullopt;
  }

  return Position{
      .x = *x,
      .y = *y,
      .z = *z,
      .heading = *heading,
  };
}

inline auto update_type_to_wire(ZoneUpdateType type) -> std::uint8_t {
  switch (type) {
    case ZoneUpdateType::spawn_added:
      return kZoneUpdateSpawnAdded;
    case ZoneUpdateType::spawn_moved:
      return kZoneUpdateSpawnMoved;
    case ZoneUpdateType::spawn_removed:
      return kZoneUpdateSpawnRemoved;
    case ZoneUpdateType::combat_resolved:
      return kZoneUpdateCombatResolved;
  }

  return kZoneUpdateSpawnAdded;
}

inline auto update_type_from_wire(std::uint8_t value) -> std::optional<ZoneUpdateType> {
  switch (value) {
    case kZoneUpdateSpawnAdded:
      return ZoneUpdateType::spawn_added;
    case kZoneUpdateSpawnMoved:
      return ZoneUpdateType::spawn_moved;
    case kZoneUpdateSpawnRemoved:
      return ZoneUpdateType::spawn_removed;
    case kZoneUpdateCombatResolved:
      return ZoneUpdateType::combat_resolved;
    default:
      return std::nullopt;
  }
}

inline auto encode_zone_update_payload(const ZoneUpdate& update) -> std::vector<std::uint8_t> {
  eq2::protocol::PacketWriter writer;
  writer.append_u8(update_type_to_wire(update.type));
  eq2::protocol::append_i32_le(writer, update.spawn.value);
  append_position(writer, update.position);
  eq2::protocol::append_i32_le(writer, update.value);
  return std::move(writer).into_bytes();
}

inline auto decode_zone_update_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<ZoneUpdate> {
  eq2::protocol::PacketReader reader(bytes);
  const auto type_value = reader.read_u8();
  if (!type_value.has_value()) {
    return std::nullopt;
  }

  const auto type = update_type_from_wire(*type_value);
  const auto spawn = eq2::protocol::read_i32_le(reader);
  auto position = read_position(reader);
  const auto value = eq2::protocol::read_i32_le(reader);
  if (!type.has_value() || !spawn.has_value() || !position.has_value() || !value.has_value() ||
      reader.remaining() != 0) {
    return std::nullopt;
  }

  return ZoneUpdate{
      .type = *type,
      .spawn = SpawnId{*spawn},
      .position = *position,
      .value = *value,
  };
}

inline auto encode_zone_snapshot_payload(const ZoneSnapshot& snapshot)
    -> std::vector<std::uint8_t> {
  eq2::protocol::PacketWriter writer;
  eq2::protocol::append_i32_le(writer, snapshot.zone.value);
  writer.append_u32_le(static_cast<std::uint32_t>(snapshot.tick & 0xffffffffULL));
  writer.append_u32_le(static_cast<std::uint32_t>((snapshot.tick >> 32U) & 0xffffffffULL));

  const auto count = snapshot.spawns.size() > static_cast<std::size_t>(0xffffU)
                         ? static_cast<std::uint16_t>(0xffffU)
                         : static_cast<std::uint16_t>(snapshot.spawns.size());
  writer.append_u16_le(count);
  for (auto index = std::size_t{0}; index < count; ++index) {
    const auto& spawn = snapshot.spawns[index];
    eq2::protocol::append_i32_le(writer, spawn.id.value);
    eq2::protocol::append_eq2_16bit_string(writer, spawn.name);
    append_position(writer, spawn.position);
    eq2::protocol::append_i32_le(writer, spawn.hit_points);
  }

  return std::move(writer).into_bytes();
}

inline auto decode_zone_snapshot_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<ZoneSnapshot> {
  eq2::protocol::PacketReader reader(bytes);
  const auto zone = eq2::protocol::read_i32_le(reader);
  const auto tick_low = reader.read_u32_le();
  const auto tick_high = reader.read_u32_le();
  const auto count = reader.read_u16_le();
  if (!zone.has_value() || !tick_low.has_value() || !tick_high.has_value() || !count.has_value()) {
    return std::nullopt;
  }

  auto spawns = std::vector<SpawnState>{};
  spawns.reserve(*count);
  for (auto index = std::size_t{0}; index < *count; ++index) {
    const auto spawn_id = eq2::protocol::read_i32_le(reader);
    auto name = eq2::protocol::read_eq2_16bit_string(reader);
    auto position = read_position(reader);
    const auto hit_points = eq2::protocol::read_i32_le(reader);
    if (!spawn_id.has_value() || !name.has_value() || !position.has_value() ||
        !hit_points.has_value()) {
      return std::nullopt;
    }

    spawns.push_back(SpawnState{
        .id = SpawnId{*spawn_id},
        .name = std::move(*name),
        .position = *position,
        .hit_points = *hit_points,
    });
  }

  if (reader.remaining() != 0) {
    return std::nullopt;
  }

  return ZoneSnapshot{
      .zone = ZoneId{*zone},
      .tick = (static_cast<std::uint64_t>(*tick_high) << 32U) |
              static_cast<std::uint64_t>(*tick_low),
      .spawns = std::move(spawns),
  };
}

}  // namespace eq2::zone
