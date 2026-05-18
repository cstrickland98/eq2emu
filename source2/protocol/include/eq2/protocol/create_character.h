#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <eq2/core/endian.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/packet_fields.h>

namespace eq2::protocol {

inline constexpr std::uint16_t kCreateCharacterReplyUnknownVersion = 1189;
inline constexpr std::size_t kCharacterCreateResponsePayloadSize = 9;

struct CreateCharacterAppearanceFiles {
  std::string race_file;
  std::string soga_race_file;
  std::string hair_file;
  std::string soga_hair_file;
  std::string face_file;
  std::string soga_face_file;
  std::string chest_file;
  std::string soga_chest_file;
  std::string legs_file;
  std::string soga_legs_file;
  std::string wing_file;
  std::string soga_wing_file;
};

struct CreateCharacterAppearanceValue {
  std::string type;
  bool signed_value = false;
  std::int32_t red = 0;
  std::int32_t green = 0;
  std::int32_t blue = 0;
};

struct CreateCharacterRequest {
  std::int32_t account_id = 0;
  std::int32_t server_id = 0;
  std::string character_name;
  std::uint8_t race = 0;
  std::uint8_t gender = 0;
  std::uint8_t deity = 0;
  std::uint8_t character_class = 0;
  std::uint8_t level = 0;
  bool appearance_data_present = false;
  double body_size = 0.0;
  double body_age = 0.0;
  CreateCharacterAppearanceFiles appearance_files;
  std::vector<CreateCharacterAppearanceValue> appearance_values;
};

struct CreateCharacterForwardPayload {
  std::int16_t client_version = 0;
  std::vector<std::uint8_t> bytes;
};

struct CreateCharacterResponse {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::uint8_t response = 0;
};

struct CreateCharacterReply {
  std::int32_t account_id = 0;
  std::uint8_t response = 0;
  std::string character_name;
};

namespace detail {

inline constexpr std::uint8_t kLegacyCreateCharacterCustomizationVersion = 4;

inline auto create_character_account_offset(std::uint16_t client_version) -> std::size_t {
  if (client_version <= 283) {
    return 0;
  }
  if (client_version == 373) {
    return 4;
  }
  return 5;
}

inline auto create_character_server_offset(std::uint16_t client_version) -> std::size_t {
  if (client_version <= 283) {
    return 4;
  }
  if (client_version == 373) {
    return 8;
  }
  if (client_version <= 561) {
    return 9;
  }
  return 10;
}

inline auto create_character_name_offset(std::uint16_t client_version) -> std::size_t {
  if (client_version <= 283) {
    return 8;
  }
  if (client_version == 373) {
    return 12;
  }
  if (client_version <= 561) {
    return 13;
  }
  return 14;
}

inline auto read_i32_at(std::span<const std::uint8_t> bytes, std::size_t offset)
    -> std::optional<std::int32_t> {
  if (bytes.size() < offset + sizeof(std::uint32_t)) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(eq2::core::read_u32_le(bytes, offset));
}

inline void write_i32_at(std::span<std::uint8_t> bytes, std::size_t offset, std::int32_t value) {
  eq2::core::write_u32_le(bytes, offset, static_cast<std::uint32_t>(value));
}

inline auto legacy_create_character_tail_skip(std::uint16_t client_version) -> std::size_t {
  return client_version == 561 ? 2U : 3U;
}

inline auto legacy_create_character_customization_version_index(std::uint16_t client_version)
    -> std::optional<std::size_t> {
  if (client_version == 546 || client_version == 561) {
    return legacy_create_character_tail_skip(client_version) - 1U;
  }
  return std::nullopt;
}

inline auto legacy_create_character_hair_gap_size(std::uint16_t client_version) -> std::size_t {
  // The DoF 546 client's EqCustomizationData v4 serializer does not emit the
  // 26-byte PacketParser-era placeholder before hair_file.
  return client_version == 546 ? 0U : 26U;
}

inline auto legacy_signed_component(float value, float multiplier) -> std::int32_t {
  auto scaled = static_cast<std::int32_t>(value * multiplier);
  scaled %= 256;
  if (scaled < 0) {
    scaled += 256;
  }
  if (scaled >= 128) {
    scaled -= 256;
  }
  return scaled;
}

inline auto read_float_triplet(PacketReader& reader) -> std::optional<std::array<float, 3>> {
  auto red = read_f32_le(reader);
  auto green = read_f32_le(reader);
  auto blue = read_f32_le(reader);
  if (!red.has_value() || !green.has_value() || !blue.has_value()) {
    return std::nullopt;
  }

  return std::array<float, 3>{*red, *green, *blue};
}

inline auto read_legacy_signed_appearance(PacketReader& reader,
                                          std::string type,
                                          float multiplier)
    -> std::optional<CreateCharacterAppearanceValue> {
  const auto triplet = read_float_triplet(reader);
  if (!triplet.has_value()) {
    return std::nullopt;
  }

  return CreateCharacterAppearanceValue{
      .type = std::move(type),
      .signed_value = true,
      .red = legacy_signed_component((*triplet)[0], multiplier),
      .green = legacy_signed_component((*triplet)[1], multiplier),
      .blue = legacy_signed_component((*triplet)[2], multiplier),
  };
}

inline auto parse_legacy_create_character_appearance(std::span<const std::uint8_t> bytes,
                                                     std::uint16_t client_version)
    -> std::optional<CreateCharacterRequest> {
  if (bytes.empty()) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  if (!reader.read_bytes(legacy_create_character_tail_skip(client_version)).has_value()) {
    return std::nullopt;
  }

  auto request = CreateCharacterRequest{};
  request.appearance_data_present = true;

  auto read_file = [&](std::string& target) -> bool {
    auto file = read_eq2_16bit_string(reader);
    if (!file.has_value()) {
      return false;
    }
    target = std::move(*file);
    return true;
  };

  auto read_color = [&](std::string type, float multiplier) -> bool {
    auto color = read_legacy_signed_appearance(reader, std::move(type), multiplier);
    if (!color.has_value()) {
      return false;
    }
    request.appearance_values.push_back(std::move(*color));
    return true;
  };

  auto read_shape = [&](std::string source_type, std::string stored_type) -> bool {
    (void)source_type;
    return read_color(std::move(stored_type), 100.0F);
  };

  if (!read_file(request.appearance_files.race_file) ||
      !read_color("skin_color", 250.0F) ||
      !read_color("eye_color", 250.0F) ||
      !read_color("hair_color1", 250.0F) ||
      !read_color("hair_color2", 250.0F) ||
      !read_color("hair_highlight", 250.0F) ||
      !reader.read_bytes(legacy_create_character_hair_gap_size(client_version)).has_value() ||
      !read_file(request.appearance_files.hair_file) ||
      !read_color("hair_type_color", 250.0F) ||
      !read_color("hair_type_highlight_color", 250.0F) ||
      !read_file(request.appearance_files.face_file) ||
      !read_color("hair_face_color", 250.0F) ||
      !read_color("hair_face_highlight_color", 250.0F) ||
      !read_file(request.appearance_files.chest_file) ||
      !read_color("shirt_color", 250.0F) ||
      !read_color("unknown_chest_color", 250.0F) ||
      !read_file(request.appearance_files.legs_file) ||
      !read_color("pants_color", 250.0F) ||
      !read_color("unknown_legs_color", 250.0F) ||
      !read_color("unknown9", 250.0F) ||
      !read_shape("eyes2", "eye_type") ||
      !read_shape("ears", "ear_type") ||
      !read_shape("eye_brows", "eye_brow_type") ||
      !read_shape("cheeks", "cheek_type") ||
      !read_shape("lips", "lip_type") ||
      !read_shape("chin", "chin_type") ||
      !read_shape("nose", "nose_type")) {
    return std::nullopt;
  }

  const auto body_size = read_f32_le(reader);
  const auto body_age = read_f32_le(reader);
  if (!body_size.has_value() || !body_age.has_value()) {
    return std::nullopt;
  }

  request.body_size = *body_size;
  request.body_age = *body_age;
  request.appearance_values.push_back(CreateCharacterAppearanceValue{
      .type = "body_size",
      .signed_value = true,
      .red = legacy_signed_component(*body_size, 100.0F),
      .green = 0,
      .blue = 0,
  });
  return request;
}

inline auto find_create_character_appearance(
    const std::vector<CreateCharacterAppearanceValue>& values,
    std::string_view type) -> const CreateCharacterAppearanceValue* {
  const auto iter = std::find_if(values.begin(), values.end(), [type](const auto& value) {
    return value.type == type;
  });
  return iter == values.end() ? nullptr : &*iter;
}

inline void append_legacy_float_triplet(PacketWriter& writer,
                                        const CreateCharacterRequest& request,
                                        std::string_view type,
                                        float multiplier) {
  const auto* value = find_create_character_appearance(request.appearance_values, type);
  if (value == nullptr || multiplier == 0.0F) {
    append_f32_le(writer, 0.0F);
    append_f32_le(writer, 0.0F);
    append_f32_le(writer, 0.0F);
    return;
  }

  append_f32_le(writer, static_cast<float>(value->red) / multiplier);
  append_f32_le(writer, static_cast<float>(value->green) / multiplier);
  append_f32_le(writer, static_cast<float>(value->blue) / multiplier);
}

}  // namespace detail

inline auto parse_create_character_request(std::span<const std::uint8_t> bytes,
                                           std::uint16_t client_version)
    -> std::optional<CreateCharacterRequest> {
  const auto account_offset = detail::create_character_account_offset(client_version);
  const auto server_offset = detail::create_character_server_offset(client_version);
  const auto name_offset = detail::create_character_name_offset(client_version);
  const auto account_id = detail::read_i32_at(bytes, account_offset);
  const auto server_id = detail::read_i32_at(bytes, server_offset);
  if (!account_id.has_value() || !server_id.has_value() || bytes.size() < name_offset) {
    return std::nullopt;
  }

  PacketReader reader(bytes.subspan(name_offset));
  auto name = read_eq2_16bit_string(reader);
  if (!name.has_value()) {
    return std::nullopt;
  }

  auto request = CreateCharacterRequest{
      .account_id = *account_id,
      .server_id = *server_id,
      .character_name = std::move(*name),
  };

  const auto character_fields_offset = name_offset + reader.consumed();
  if (bytes.size() >= character_fields_offset + 5U) {
    request.race = bytes[character_fields_offset];
    request.gender = bytes[character_fields_offset + 1U];
    request.deity = bytes[character_fields_offset + 2U];
    request.character_class = bytes[character_fields_offset + 3U];
    request.level = bytes[character_fields_offset + 4U];
  }

  const auto appearance_fields_offset = character_fields_offset + 5U;
  if (client_version <= 561 && bytes.size() > appearance_fields_offset) {
    if (auto appearance = detail::parse_legacy_create_character_appearance(
            bytes.subspan(appearance_fields_offset), client_version)) {
      request.appearance_data_present = true;
      request.body_size = appearance->body_size;
      request.body_age = appearance->body_age;
      request.appearance_files = std::move(appearance->appearance_files);
      request.appearance_values = std::move(appearance->appearance_values);
    }
  }

  return request;
}

inline auto encode_create_character_request_fixture(const CreateCharacterRequest& request,
                                                    std::uint16_t client_version)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  if (client_version <= 283) {
    append_i32_le(writer, request.account_id);
    append_i32_le(writer, request.server_id);
  } else if (client_version == 373) {
    append_i32_le(writer, 0);
    append_i32_le(writer, request.account_id);
    append_i32_le(writer, request.server_id);
  } else if (client_version <= 561) {
    writer.append_u8(0);
    append_i32_le(writer, 0);
    append_i32_le(writer, request.account_id);
    append_i32_le(writer, request.server_id);
  } else {
    writer.append_u8(0);
    append_i32_le(writer, 0);
    append_i32_le(writer, request.account_id);
    writer.append_u8(0);
    append_i32_le(writer, request.server_id);
  }

  append_eq2_16bit_string(writer, request.character_name);
  writer.append_u8(request.race);
  writer.append_u8(request.gender);
  writer.append_u8(request.deity);
  writer.append_u8(request.character_class);
  writer.append_u8(request.level);
  if (!request.appearance_data_present || client_version > 561) {
    return std::move(writer).into_bytes();
  }

  const auto tail_skip = detail::legacy_create_character_tail_skip(client_version);
  const auto version_index =
      detail::legacy_create_character_customization_version_index(client_version);
  for (auto index = std::size_t{0}; index < tail_skip; ++index) {
    writer.append_u8(version_index.has_value() && *version_index == index
                         ? detail::kLegacyCreateCharacterCustomizationVersion
                         : 0);
  }
  append_eq2_16bit_string(writer, request.appearance_files.race_file);
  detail::append_legacy_float_triplet(writer, request, "skin_color", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "eye_color", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "hair_color1", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "hair_color2", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "hair_highlight", 250.0F);
  for (auto index = std::size_t{0};
       index < detail::legacy_create_character_hair_gap_size(client_version); ++index) {
    writer.append_u8(0);
  }
  append_eq2_16bit_string(writer, request.appearance_files.hair_file);
  detail::append_legacy_float_triplet(writer, request, "hair_type_color", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "hair_type_highlight_color", 250.0F);
  append_eq2_16bit_string(writer, request.appearance_files.face_file);
  detail::append_legacy_float_triplet(writer, request, "hair_face_color", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "hair_face_highlight_color", 250.0F);
  append_eq2_16bit_string(writer, request.appearance_files.chest_file);
  detail::append_legacy_float_triplet(writer, request, "shirt_color", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "unknown_chest_color", 250.0F);
  append_eq2_16bit_string(writer, request.appearance_files.legs_file);
  detail::append_legacy_float_triplet(writer, request, "pants_color", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "unknown_legs_color", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "unknown9", 250.0F);
  detail::append_legacy_float_triplet(writer, request, "eye_type", 100.0F);
  detail::append_legacy_float_triplet(writer, request, "ear_type", 100.0F);
  detail::append_legacy_float_triplet(writer, request, "eye_brow_type", 100.0F);
  detail::append_legacy_float_triplet(writer, request, "cheek_type", 100.0F);
  detail::append_legacy_float_triplet(writer, request, "lip_type", 100.0F);
  detail::append_legacy_float_triplet(writer, request, "chin_type", 100.0F);
  detail::append_legacy_float_triplet(writer, request, "nose_type", 100.0F);
  append_f32_le(writer, static_cast<float>(request.body_size));
  append_f32_le(writer, static_cast<float>(request.body_age));
  return std::move(writer).into_bytes();
}

inline auto encode_create_character_forward_payload(std::span<const std::uint8_t> request_payload,
                                                    std::uint16_t client_version,
                                                    std::int32_t account_id)
    -> std::optional<CreateCharacterForwardPayload> {
  auto bytes = std::vector<std::uint8_t>{};
  bytes.reserve(request_payload.size() + sizeof(std::uint16_t));
  PacketWriter writer;
  append_i16_le(writer, static_cast<std::int16_t>(client_version));
  writer.append_bytes(request_payload);
  bytes = std::move(writer).into_bytes();

  const auto account_offset = sizeof(std::uint16_t) +
                              detail::create_character_account_offset(client_version);
  if (bytes.size() < account_offset + sizeof(std::uint32_t)) {
    return std::nullopt;
  }

  detail::write_i32_at(std::span<std::uint8_t>(bytes), account_offset, account_id);
  return CreateCharacterForwardPayload{
      .client_version = static_cast<std::int16_t>(client_version),
      .bytes = std::move(bytes),
  };
}

inline auto decode_create_character_response_payload(std::span<const std::uint8_t> bytes)
    -> std::optional<CreateCharacterResponse> {
  if (bytes.size() != kCharacterCreateResponsePayloadSize) {
    return std::nullopt;
  }

  PacketReader reader(bytes);
  auto account_id = read_i32_le(reader);
  auto character_id = read_i32_le(reader);
  auto response = reader.read_u8();
  if (!account_id || !character_id || !response) {
    return std::nullopt;
  }

  return CreateCharacterResponse{
      .account_id = *account_id,
      .character_id = *character_id,
      .response = *response,
  };
}

inline auto encode_create_character_response_payload(const CreateCharacterResponse& response)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  append_i32_le(writer, response.account_id);
  append_i32_le(writer, response.character_id);
  writer.append_u8(response.response);
  return std::move(writer).into_bytes();
}

inline auto encode_create_character_reply_payload(const CreateCharacterReply& reply,
                                                  std::uint16_t client_version)
    -> std::vector<std::uint8_t> {
  PacketWriter writer;
  append_i32_le(writer, reply.account_id);
  if (client_version >= kCreateCharacterReplyUnknownVersion) {
    append_i32_le(writer, -1);
  }
  writer.append_u8(reply.response);
  append_eq2_16bit_string(writer, reply.character_name);
  return std::move(writer).into_bytes();
}

inline auto decode_create_character_reply_payload(std::span<const std::uint8_t> bytes,
                                                  std::uint16_t client_version)
    -> std::optional<CreateCharacterReply> {
  PacketReader reader(bytes);
  auto account_id = read_i32_le(reader);
  if (!account_id.has_value()) {
    return std::nullopt;
  }
  if (client_version >= kCreateCharacterReplyUnknownVersion && !read_i32_le(reader).has_value()) {
    return std::nullopt;
  }
  auto response = reader.read_u8();
  auto name = read_eq2_16bit_string(reader);
  if (!response.has_value() || !name.has_value()) {
    return std::nullopt;
  }

  return CreateCharacterReply{
      .account_id = *account_id,
      .response = *response,
      .character_name = std::move(*name),
  };
}

}  // namespace eq2::protocol
