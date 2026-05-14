#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eq2::login {

struct CharacterColor {
  std::int32_t red = 0;
  std::int32_t green = 0;
  std::int32_t blue = 0;
};

struct CharacterAppearanceValue {
  std::string type;
  bool signed_value = false;
  std::int32_t red = 0;
  std::int32_t green = 0;
  std::int32_t blue = 0;
};

struct CharacterEquipment {
  std::int32_t slot = 0;
  std::int32_t equip_type = 0;
  CharacterColor color;
  CharacterColor highlight;
};

struct CharacterListRow {
  std::int32_t character_id = 0;
  std::int32_t server_id = 0;
  std::string name;
  std::int32_t race = 0;
  std::int32_t character_class = 0;
  std::int32_t gender = 0;
  std::int32_t deity = 0;
  double body_size = 0.0;
  double body_age = 0.0;
  std::int32_t current_zone_id = 0;
  std::int32_t level = 0;
  std::int32_t soga_wing_type = 0;
  std::int32_t soga_chest_type = 0;
  std::int32_t soga_legs_type = 0;
  std::int32_t soga_hair_type = 0;
  std::int32_t legs_type = 0;
  std::int32_t chest_type = 0;
  std::int32_t wing_type = 0;
  std::int32_t hair_type = 0;
  std::int64_t created_date = 0;
  std::optional<std::int64_t> last_played;
  std::int32_t login_character_id = 0;
  std::string server_name;
  std::int32_t facial_hair_type = 0;
  std::int32_t soga_facial_hair_type = 0;
  std::int32_t soga_model_type = 0;
  std::int32_t model_type = 0;
};

struct CharacterSelectProfile {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t server_id = 0;
  std::string name;
  std::int32_t race = 0;
  std::int32_t character_class = 0;
  std::int32_t gender = 0;
  std::int32_t deity = 0;
  double body_size = 0.0;
  double body_age = 0.0;
  std::int32_t current_zone_id = 0;
  std::int32_t level = 0;
  std::int32_t soga_wing_type = 0;
  std::int32_t soga_chest_type = 0;
  std::int32_t soga_legs_type = 0;
  std::int32_t soga_hair_type = 0;
  std::int32_t legs_type = 0;
  std::int32_t chest_type = 0;
  std::int32_t wing_type = 0;
  std::int32_t hair_type = 0;
  std::int64_t created_date = 0;
  std::optional<std::int64_t> last_played;
  std::int32_t packet_version = 0;
  std::string server_name;
  std::int32_t hair_face_type = 0;
  std::int32_t soga_hair_face_type = 0;
  std::int32_t soga_race_type = 0;
  std::int32_t race_type = 0;
  std::int32_t mount = 1377;
  std::int32_t mount_color1 = 57;
  std::unordered_map<std::string, CharacterColor> colors;
  std::unordered_map<std::string, std::array<std::int32_t, 3>> signed_appearance_values;
  std::vector<CharacterEquipment> equipment;
};

inline constexpr std::array<std::uint8_t, 13> kLegacyCharacterSelectUnknown11{
    0xff, 0xff, 0xff, 0x61, 0x00, 0x2c, 0x04, 0xa5, 0x09, 0x02, 0x0f, 0x00, 0x00,
};

inline auto character_select_packet_version(std::int16_t client_version) -> std::int32_t {
  if (client_version == 546 || client_version == 561) {
    return 11;
  }

  if (client_version >= 887) {
    return 6;
  }

  return 5;
}

inline auto positive_or_fallback(std::int32_t preferred, std::int32_t fallback) -> std::int32_t {
  return preferred > 0 ? preferred : fallback;
}

inline auto map_character_select_profile(const CharacterListRow& row,
                                         std::int32_t account_id,
                                         std::int16_t client_version,
                                         const std::vector<CharacterAppearanceValue>& appearance,
                                         const std::vector<CharacterEquipment>& equipment)
    -> CharacterSelectProfile {
  auto profile = CharacterSelectProfile{
      .account_id = account_id,
      .character_id = row.character_id,
      .server_id = row.server_id,
      .name = row.name,
      .race = row.race,
      .character_class = row.character_class,
      .gender = row.gender,
      .deity = row.deity,
      .body_size = row.body_size,
      .body_age = row.body_age,
      .current_zone_id = row.current_zone_id,
      .level = row.level,
      .soga_wing_type = positive_or_fallback(row.soga_wing_type, row.wing_type),
      .soga_chest_type = positive_or_fallback(row.soga_chest_type, row.chest_type),
      .soga_legs_type = positive_or_fallback(row.soga_legs_type, row.legs_type),
      .soga_hair_type = positive_or_fallback(row.soga_hair_type, row.hair_type),
      .legs_type = row.legs_type,
      .chest_type = row.chest_type,
      .wing_type = row.wing_type,
      .hair_type = row.hair_type,
      .created_date = row.created_date,
      .last_played = row.last_played,
      .packet_version = character_select_packet_version(client_version),
      .server_name = row.server_name,
      .hair_face_type = row.facial_hair_type,
      .soga_hair_face_type = positive_or_fallback(row.soga_facial_hair_type, row.facial_hair_type),
      .soga_race_type = positive_or_fallback(row.soga_model_type, row.model_type),
      .race_type = row.model_type,
  };

  for (const auto& value : appearance) {
    if (value.signed_value) {
      profile.signed_appearance_values[value.type] = {value.red, value.green, value.blue};
    } else {
      profile.colors[value.type] = CharacterColor{
          .red = value.red,
          .green = value.green,
          .blue = value.blue,
      };
    }
  }

  const auto equipment_count = std::min<std::size_t>(equipment.size(), 24);
  profile.equipment.insert(profile.equipment.end(), equipment.begin(), equipment.begin() + equipment_count);

  return profile;
}

template <typename CharacterRepository>
auto load_character_select_profiles(CharacterRepository& characters,
                                    std::int32_t account_id,
                                    std::int16_t client_version) -> std::vector<CharacterSelectProfile> {
  auto profiles = std::vector<CharacterSelectProfile>{};
  for (const auto& row : characters.load_character_rows(account_id)) {
    profiles.push_back(map_character_select_profile(
        row,
        account_id,
        client_version,
        characters.load_appearance_values(row.login_character_id),
        characters.load_equipment(row.login_character_id)));
  }

  return profiles;
}

}  // namespace eq2::login
