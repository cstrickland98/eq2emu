#include <eq2/login/character_select.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto failures = 0;

void require(bool condition, std::string_view message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

template <typename T, typename U>
void require_eq(const T& actual, const U& expected, std::string_view message) {
  if (!(actual == expected)) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

auto sample_character_row() -> eq2::login::CharacterListRow {
  return eq2::login::CharacterListRow{
      .character_id = 1001,
      .server_id = 3,
      .name = "Alys",
      .race = 4,
      .character_class = 2,
      .gender = 1,
      .deity = 9,
      .body_size = 0.85,
      .body_age = 0.15,
      .current_zone_id = 253,
      .level = 37,
      .soga_wing_type = 0,
      .soga_chest_type = 91,
      .soga_legs_type = 0,
      .soga_hair_type = 71,
      .legs_type = 12,
      .chest_type = 34,
      .wing_type = 56,
      .hair_type = 78,
      .created_date = 1'700'000'000,
      .last_played = 1'700'005'000,
      .login_character_id = 44,
      .server_name = "Permafrost",
      .facial_hair_type = 6,
      .soga_facial_hair_type = 0,
      .soga_model_type = 0,
      .model_type = 222,
  };
}

void character_select_mapping_preserves_legacy_fallbacks_and_constants() {
  const auto profile = eq2::login::map_character_select_profile(
      sample_character_row(),
      7,
      546,
      {
          eq2::login::CharacterAppearanceValue{
              .type = "skin_color",
              .signed_value = false,
              .red = 11,
              .green = 22,
              .blue = 33,
          },
          eq2::login::CharacterAppearanceValue{
              .type = "eye_type",
              .signed_value = true,
              .red = -4,
              .green = 5,
              .blue = 6,
          },
      },
      {
          eq2::login::CharacterEquipment{
              .slot = 3,
              .equip_type = 900,
              .color = {.red = 1, .green = 2, .blue = 3},
              .highlight = {.red = 4, .green = 5, .blue = 6},
          },
      });

  require_eq(profile.account_id, 7, "character list mapping carries login account id");
  require_eq(profile.character_id, 1001, "character id comes from login_characters char_id");
  require_eq(profile.server_id, 3, "server id comes from login_characters server_id");
  require_eq(profile.packet_version, 11, "classic clients use legacy character-select packet version 11");
  require_eq(profile.soga_wing_type, 56, "missing SOGA wing falls back to normal wing");
  require_eq(profile.soga_chest_type, 91, "present SOGA chest overrides normal chest");
  require_eq(profile.soga_legs_type, 12, "missing SOGA legs falls back to normal legs");
  require_eq(profile.soga_hair_type, 71, "present SOGA hair overrides normal hair");
  require_eq(profile.soga_hair_face_type, 6, "missing SOGA facial hair falls back to normal facial hair");
  require_eq(profile.soga_race_type, 222, "missing SOGA model falls back to normal model");
  require_eq(profile.mount, 1377, "legacy character select assigns default mount");
  require_eq(profile.mount_color1, 57, "legacy character select assigns default mount color");
  require_eq(profile.colors.at("skin_color").green, 22, "unsigned appearance rows map as colors");
  require_eq(profile.signed_appearance_values.at("eye_type")[0], -4,
             "signed appearance rows map as signed triplets");
  require_eq(profile.equipment.size(), static_cast<std::size_t>(1),
             "equipment rows are attached to the profile");
  require_eq(profile.equipment[0].slot, 3, "equipment slot is preserved");
}

void character_select_mapping_caps_equipment_at_legacy_limit() {
  auto equipment = std::vector<eq2::login::CharacterEquipment>{};
  for (auto slot = 0; slot < 25; ++slot) {
    equipment.push_back(eq2::login::CharacterEquipment{.slot = slot});
  }

  const auto profile = eq2::login::map_character_select_profile(
      sample_character_row(),
      7,
      887,
      {},
      equipment);

  require_eq(profile.packet_version, 6, "887 and newer clients use character-select packet version 6");
  require_eq(profile.equipment.size(), static_cast<std::size_t>(24),
             "legacy loader only copies the first twenty-four equipment rows");
  require_eq(profile.equipment.back().slot, 23, "equipment cap preserves original query order");
}

class FakeCharacterRepository {
 public:
  auto load_character_rows(std::int32_t account_id) -> std::vector<eq2::login::CharacterListRow> {
    loaded_account_id = account_id;
    return rows;
  }

  auto load_appearance_values(std::int32_t login_character_id)
      -> std::vector<eq2::login::CharacterAppearanceValue> {
    appearance_lookup_ids.push_back(login_character_id);
    return appearance;
  }

  auto load_equipment(std::int32_t login_character_id) -> std::vector<eq2::login::CharacterEquipment> {
    equipment_lookup_ids.push_back(login_character_id);
    return equipment;
  }

  std::vector<eq2::login::CharacterListRow> rows;
  std::vector<eq2::login::CharacterAppearanceValue> appearance;
  std::vector<eq2::login::CharacterEquipment> equipment;
  std::int32_t loaded_account_id = 0;
  std::vector<std::int32_t> appearance_lookup_ids;
  std::vector<std::int32_t> equipment_lookup_ids;
};

void character_repository_loader_uses_account_and_login_character_ids() {
  FakeCharacterRepository repository;
  repository.rows.push_back(sample_character_row());
  repository.appearance.push_back(eq2::login::CharacterAppearanceValue{
      .type = "skin_color",
      .red = 1,
      .green = 2,
      .blue = 3,
  });
  repository.equipment.push_back(eq2::login::CharacterEquipment{.slot = 8});

  const auto profiles = eq2::login::load_character_select_profiles(repository, 77, 561);

  require_eq(repository.loaded_account_id, 77, "repository receives the requested account id");
  require_eq(repository.appearance_lookup_ids.size(), static_cast<std::size_t>(1),
             "appearance repository is queried once per character");
  require_eq(repository.appearance_lookup_ids[0], 44,
             "appearance repository uses login_characters id instead of world char id");
  require_eq(repository.equipment_lookup_ids[0], 44,
             "equipment repository uses login_characters id instead of world char id");
  require_eq(profiles.size(), static_cast<std::size_t>(1), "repository rows map into profiles");
  require_eq(profiles[0].colors.at("skin_color").blue, 3,
             "repository appearance values are included in mapped profile");
  require_eq(profiles[0].equipment[0].slot, 8, "repository equipment values are included in mapped profile");
}

}  // namespace

int main() {
  character_select_mapping_preserves_legacy_fallbacks_and_constants();
  character_select_mapping_caps_equipment_at_legacy_limit();
  character_repository_loader_uses_account_and_login_character_ids();

  if (failures != 0) {
    std::cerr << failures << " characterization assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
