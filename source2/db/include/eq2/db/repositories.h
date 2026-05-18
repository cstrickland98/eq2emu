#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eq2::db {

struct LoginAccountRecord {
  std::int32_t id = 0;
  std::string name;
};

class LoginAccountRepository {
 public:
  virtual ~LoginAccountRepository() = default;

  virtual auto find_by_name_and_password(std::string_view username, std::string_view password)
      -> std::optional<LoginAccountRecord> = 0;
  virtual auto account_name_exists(std::string_view username) -> bool = 0;
  virtual auto create_account(std::string_view username, std::string_view password)
      -> LoginAccountRecord = 0;
  virtual void record_successful_login(std::int32_t account_id,
                                       std::string_view remote_address,
                                       std::int16_t client_version) {
    (void)account_id;
    (void)remote_address;
    (void)client_version;
  }
};

struct WorldAccountRecord {
  std::int32_t id = 0;
  std::string account;
  std::string display_name;
  std::string password;
  bool disabled = false;
};

class WorldRegistrationRepository {
 public:
  virtual ~WorldRegistrationRepository() = default;

  virtual auto server_version_is_allowed(std::string_view server_version) -> bool = 0;
  virtual auto check_server_account(std::string_view account, std::string_view password)
      -> std::int32_t = 0;
  virtual auto account_is_disabled(std::string_view account) -> bool = 0;
  virtual auto connection_ip_is_banned() -> bool = 0;
  virtual auto advertised_address_is_banned(std::string_view address) -> bool = 0;
  virtual auto display_name_for_account(std::int32_t account_id) -> std::string = 0;
  virtual void record_world_status(std::int32_t world_id,
                                   std::int32_t status,
                                   std::int32_t player_count,
                                   std::int32_t zone_count,
                                   std::int32_t world_max_level) {
    (void)world_id;
    (void)status;
    (void)player_count;
    (void)zone_count;
    (void)world_max_level;
  }
};

struct CharacterListRecord {
  std::int32_t login_character_id = 0;
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
  std::int64_t last_played = 0;
  std::string server_name;
  std::int32_t facial_hair_type = 0;
  std::int32_t soga_facial_hair_type = 0;
  std::int32_t soga_model_type = 0;
  std::int32_t model_type = 0;
  std::string zone_name;
  std::string zone_description;
};

struct CharacterAppearanceRecord {
  std::string type;
  bool signed_value = false;
  std::int32_t red = 0;
  std::int32_t green = 0;
  std::int32_t blue = 0;
};

struct CharacterEquipmentRecord {
  std::int32_t slot = 0;
  std::int32_t equip_type = 0;
  std::int32_t red = 0xff;
  std::int32_t green = 0xff;
  std::int32_t blue = 0xff;
  std::int32_t highlight_red = 0xff;
  std::int32_t highlight_green = 0xff;
  std::int32_t highlight_blue = 0xff;
};

struct CreatedCharacterAppearanceFiles {
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

struct CreatedCharacterRecord {
  std::int32_t account_id = 0;
  std::int32_t server_id = 0;
  std::int32_t character_id = 0;
  std::string name;
  std::int32_t race = 0;
  std::int32_t character_class = 0;
  std::int32_t gender = 0;
  std::int32_t deity = 0;
  std::int32_t level = 0;
  double body_size = 0.0;
  double body_age = 0.0;
  CreatedCharacterAppearanceFiles appearance_files;
  std::vector<CharacterAppearanceRecord> appearance_values;
};

struct WorldZoneUpdateRecord {
  std::int32_t zone_id = 0;
  std::string name;
  std::string description;
};

struct LoginEquipmentUpdateRecord {
  std::int32_t update_id = 0;
  std::int32_t world_character_id = 0;
  std::int32_t equip_type = 0;
  std::int32_t red = 0xff;
  std::int32_t green = 0xff;
  std::int32_t blue = 0xff;
  std::int32_t highlight_red = 0xff;
  std::int32_t highlight_green = 0xff;
  std::int32_t highlight_blue = 0xff;
  std::int32_t slot = 0;
};

struct CharacterPictureRecord {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::int32_t server_id = 0;
  std::vector<std::uint8_t> picture;
};

class CharacterListRepository {
 public:
  virtual ~CharacterListRepository() = default;

  virtual auto load_character_list(std::int32_t account_id) -> std::vector<CharacterListRecord> = 0;
  virtual auto load_character_appearance(std::int32_t login_character_id)
      -> std::vector<CharacterAppearanceRecord> {
    (void)login_character_id;
    return {};
  }
  virtual auto load_character_equipment(std::int32_t login_character_id)
      -> std::vector<CharacterEquipmentRecord> {
    (void)login_character_id;
    return {};
  }
  virtual auto mark_character_deleted(std::int32_t account_id,
                                      std::int32_t character_id,
                                      std::int32_t server_id,
                                      std::string_view name) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)name;
    return false;
  }
  virtual auto save_created_character(const CreatedCharacterRecord& character) -> bool {
    (void)character;
    return false;
  }
  virtual auto save_world_zone_updates(std::int32_t server_id,
                                       const std::vector<WorldZoneUpdateRecord>& updates)
      -> std::size_t {
    (void)server_id;
    (void)updates;
    return 0;
  }
  virtual auto save_login_equipment_updates(
      std::int32_t server_id,
      const std::vector<LoginEquipmentUpdateRecord>& updates) -> std::size_t {
    (void)server_id;
    (void)updates;
    return 0;
  }
  virtual auto save_character_picture(std::int32_t account_id,
                                      std::int32_t character_id,
                                      std::int32_t server_id,
                                      std::span<const std::uint8_t> picture) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)picture;
    return false;
  }
  virtual auto mark_character_deleted_by_world(std::int32_t account_id,
                                               std::int32_t character_id,
                                               std::int32_t server_id) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    return false;
  }
  virtual auto update_character_timestamp(std::int32_t account_id,
                                          std::int32_t character_id,
                                          std::int32_t server_id,
                                          std::int32_t timestamp) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)timestamp;
    return false;
  }
  virtual auto update_character_level(std::int32_t account_id,
                                      std::int32_t character_id,
                                      std::int32_t server_id,
                                      std::int32_t level) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)level;
    return false;
  }
  virtual auto update_character_race(std::int32_t account_id,
                                     std::int32_t character_id,
                                     std::int32_t server_id,
                                     std::int32_t model_type,
                                     std::int32_t race) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)model_type;
    (void)race;
    return false;
  }
  virtual auto update_character_class(std::int32_t account_id,
                                      std::int32_t character_id,
                                      std::int32_t server_id,
                                      std::int32_t character_class) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)character_class;
    return false;
  }
  virtual auto update_character_gender(std::int32_t account_id,
                                       std::int32_t character_id,
                                       std::int32_t server_id,
                                       std::int32_t gender) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)gender;
    return false;
  }
  virtual auto update_character_zone(std::int32_t account_id,
                                     std::int32_t character_id,
                                     std::int32_t server_id,
                                     std::int32_t zone_id) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)zone_id;
    return false;
  }
  virtual auto update_character_name(std::int32_t account_id,
                                     std::int32_t character_id,
                                     std::int32_t server_id,
                                     std::string_view name) -> bool {
    (void)account_id;
    (void)character_id;
    (void)server_id;
    (void)name;
    return false;
  }
};

struct ClientLogRecord {
  std::string type;
  std::string message;
  std::string account_name;
  std::int16_t client_version = 0;
};

class ClientLogRepository {
 public:
  virtual ~ClientLogRepository() = default;

  virtual void save_client_log(const ClientLogRecord& record) {
    (void)record;
  }
};

class LoginMaintenanceRepository {
 public:
  virtual ~LoginMaintenanceRepository() = default;

  virtual void remove_old_world_server_stats() {}
  virtual void remove_deleted_character_data() {}
  virtual void fix_bug_report_encoding() {}
};

struct ZoneBootstrapRecord {
  std::int32_t zone_id = 0;
  std::string name;
  float safe_x = 0.0F;
  float safe_y = 0.0F;
  float safe_z = 0.0F;
  float safe_heading = 0.0F;
};

class ZoneBootstrapRepository {
 public:
  virtual ~ZoneBootstrapRepository() = default;

  virtual auto load_zone(std::int32_t zone_id) -> std::optional<ZoneBootstrapRecord> = 0;
};

}  // namespace eq2::db
