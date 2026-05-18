#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/db/query.h>
#include <eq2/db/repositories.h>

namespace eq2::db {

class FakeQueryConnection final : public QueryConnection {
 public:
  auto execute(const QueryRequest& request) -> eq2::core::Result<QueryResult> override {
    executed_sql.push_back(request.sql);
    if (next_error.has_value()) {
      auto error = std::move(*next_error);
      next_error.reset();
      return eq2::core::Result<QueryResult>::failure(std::move(error));
    }

    return eq2::core::Result<QueryResult>::success(next_result);
  }

  QueryResult next_result;
  std::optional<eq2::core::Error> next_error;
  std::vector<std::string> executed_sql;
};

class FakeLoginAccountRepository final : public LoginAccountRepository {
 public:
  auto find_by_name_and_password(std::string_view username, std::string_view password)
      -> std::optional<LoginAccountRecord> override {
    const auto iter = accounts_.find(std::string(username));
    if (iter == accounts_.end() || iter->second.password != password) {
      return std::nullopt;
    }

    return LoginAccountRecord{
        .id = iter->second.id,
        .name = iter->first,
    };
  }

  auto account_name_exists(std::string_view username) -> bool override {
    return accounts_.contains(std::string(username));
  }

  auto create_account(std::string_view username, std::string_view password)
      -> LoginAccountRecord override {
    const auto key = std::string(username);
    const auto id = next_id_++;
    accounts_[key] = StoredAccount{
        .id = id,
        .password = std::string(password),
    };
    return LoginAccountRecord{
        .id = id,
        .name = key,
    };
  }

  void record_successful_login(std::int32_t account_id,
                               std::string_view remote_address,
                               std::int16_t client_version) override {
    successful_logins.push_back(SuccessfulLogin{
        .account_id = account_id,
        .remote_address = std::string(remote_address),
        .client_version = client_version,
    });
  }

  void add_account(std::int32_t id, std::string username, std::string password) {
    accounts_[std::move(username)] = StoredAccount{
        .id = id,
        .password = std::move(password),
    };
    next_id_ = std::max(next_id_, id + 1);
  }

  struct SuccessfulLogin {
    std::int32_t account_id = 0;
    std::string remote_address;
    std::int16_t client_version = 0;
  };

  std::vector<SuccessfulLogin> successful_logins;

 private:
  struct StoredAccount {
    std::int32_t id = 0;
    std::string password;
  };

  std::map<std::string, StoredAccount> accounts_;
  std::int32_t next_id_ = 1;
};

class FakeWorldRegistrationRepository final : public WorldRegistrationRepository {
 public:
  auto server_version_is_allowed(std::string_view server_version) -> bool override {
    return allowed_server_versions.empty() ||
           allowed_server_versions.contains(std::string(server_version));
  }

  auto check_server_account(std::string_view account, std::string_view password)
      -> std::int32_t override {
    const auto iter = accounts.find(std::string(account));
    if (iter == accounts.end() || iter->second.password != password) {
      return 0;
    }

    return iter->second.id;
  }

  auto account_is_disabled(std::string_view account) -> bool override {
    const auto iter = accounts.find(std::string(account));
    return iter != accounts.end() && iter->second.disabled;
  }

  auto connection_ip_is_banned() -> bool override {
    return connection_banned;
  }

  auto advertised_address_is_banned(std::string_view address) -> bool override {
    return banned_addresses.contains(std::string(address));
  }

  auto display_name_for_account(std::int32_t account_id) -> std::string override {
    for (const auto& [_, account] : accounts) {
      if (account.id == account_id) {
        return account.display_name;
      }
    }

    return {};
  }

  void record_world_status(std::int32_t world_id,
                           std::int32_t status,
                           std::int32_t player_count,
                           std::int32_t zone_count,
                           std::int32_t world_max_level) override {
    world_statuses.push_back(WorldStatusRecord{
        .world_id = world_id,
        .status = status,
        .player_count = player_count,
        .zone_count = zone_count,
        .world_max_level = world_max_level,
    });
  }

  struct WorldStatusRecord {
    std::int32_t world_id = 0;
    std::int32_t status = 0;
    std::int32_t player_count = 0;
    std::int32_t zone_count = 0;
    std::int32_t world_max_level = 0;
  };

  std::map<std::string, WorldAccountRecord> accounts;
  std::set<std::string> allowed_server_versions;
  std::set<std::string> banned_addresses;
  std::vector<WorldStatusRecord> world_statuses;
  bool connection_banned = false;
};

class FakeCharacterListRepository final : public CharacterListRepository {
 public:
  auto load_character_list(std::int32_t account_id) -> std::vector<CharacterListRecord> override {
    const auto iter = characters_by_account.find(account_id);
    if (iter == characters_by_account.end()) {
      return {};
    }

    return iter->second;
  }

  auto load_character_appearance(std::int32_t login_character_id)
      -> std::vector<CharacterAppearanceRecord> override {
    const auto iter = appearance_by_login_character.find(login_character_id);
    if (iter == appearance_by_login_character.end()) {
      return {};
    }

    return iter->second;
  }

  auto load_character_equipment(std::int32_t login_character_id)
      -> std::vector<CharacterEquipmentRecord> override {
    const auto iter = equipment_by_login_character.find(login_character_id);
    if (iter == equipment_by_login_character.end()) {
      return {};
    }

    return iter->second;
  }

  auto mark_character_deleted(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::string_view name) -> bool override {
    const auto iter = characters_by_account.find(account_id);
    if (iter == characters_by_account.end()) {
      return false;
    }

    auto& characters = iter->second;
    const auto character_iter =
        std::find_if(characters.begin(), characters.end(), [&](const auto& character) {
          return character.character_id == character_id && character.server_id == server_id &&
                 character.name == name;
        });
    if (character_iter == characters.end()) {
      return false;
    }

    deleted_characters.push_back(*character_iter);
    characters.erase(character_iter);
    return true;
  }

  auto save_created_character(const CreatedCharacterRecord& character) -> bool override {
    saved_created_characters.push_back(character);
    auto& characters = characters_by_account[character.account_id];
    const auto duplicate =
        std::find_if(characters.begin(), characters.end(), [&](const auto& existing) {
          return existing.character_id == character.character_id &&
                 existing.server_id == character.server_id;
        });
    if (duplicate != characters.end()) {
      characters.erase(duplicate);
    }

    const auto login_character_id = next_login_character_id++;
    characters.push_back(CharacterListRecord{
        .login_character_id = login_character_id,
        .character_id = character.character_id,
        .server_id = character.server_id,
        .name = character.name,
        .race = character.race,
        .character_class = character.character_class,
        .gender = character.gender,
        .deity = character.deity,
        .body_size = character.body_size,
        .body_age = character.body_age,
        .level = character.level,
    });
    appearance_by_login_character[login_character_id] = character.appearance_values;
    return true;
  }

  auto save_world_zone_updates(std::int32_t server_id,
                               const std::vector<WorldZoneUpdateRecord>& updates)
      -> std::size_t override {
    auto& zones = world_zones_by_server[server_id];
    for (const auto& update : updates) {
      const auto existing =
          std::find_if(zones.begin(), zones.end(), [&](const auto& zone) {
            return zone.zone_id == update.zone_id;
          });
      if (existing == zones.end()) {
        zones.push_back(update);
      } else {
        *existing = update;
      }
    }
    return updates.size();
  }

  auto save_login_equipment_updates(
      std::int32_t server_id,
      const std::vector<LoginEquipmentUpdateRecord>& updates) -> std::size_t override {
    auto saved = std::size_t{0};
    for (const auto& update : updates) {
      auto* character = find_character_by_world_id(server_id, update.world_character_id);
      if (character == nullptr) {
        continue;
      }

      auto& equipment = equipment_by_login_character[character->login_character_id];
      const auto existing =
          std::find_if(equipment.begin(), equipment.end(), [&](const auto& item) {
            return item.slot == update.slot;
          });
      const auto record = CharacterEquipmentRecord{
          .slot = update.slot,
          .equip_type = update.equip_type,
          .red = update.red,
          .green = update.green,
          .blue = update.blue,
          .highlight_red = update.highlight_red,
          .highlight_green = update.highlight_green,
          .highlight_blue = update.highlight_blue,
      };
      if (existing == equipment.end()) {
        equipment.push_back(record);
      } else {
        *existing = record;
      }
      saved_equipment_updates.push_back(update);
      ++saved;
    }
    return saved;
  }

  auto save_character_picture(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::span<const std::uint8_t> picture) -> bool override {
    saved_character_pictures.push_back(CharacterPictureRecord{
        .account_id = account_id,
        .character_id = character_id,
        .server_id = server_id,
        .picture = {picture.begin(), picture.end()},
    });
    return true;
  }

  auto mark_character_deleted_by_world(std::int32_t account_id,
                                       std::int32_t character_id,
                                       std::int32_t server_id) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }

    deleted_characters.push_back(*character);
    auto& characters = characters_by_account[account_id];
    characters.erase(std::remove_if(characters.begin(),
                                    characters.end(),
                                    [&](const auto& existing) {
                                      return existing.character_id == character_id &&
                                             existing.server_id == server_id;
                                    }),
                     characters.end());
    return true;
  }

  auto update_character_timestamp(std::int32_t account_id,
                                  std::int32_t character_id,
                                  std::int32_t server_id,
                                  std::int32_t timestamp) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }
    character->last_played = timestamp;
    return true;
  }

  auto update_character_level(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::int32_t level) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }
    character->level = level;
    return true;
  }

  auto update_character_race(std::int32_t account_id,
                             std::int32_t character_id,
                             std::int32_t server_id,
                             std::int32_t model_type,
                             std::int32_t race) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }
    character->model_type = model_type;
    character->race = race;
    return true;
  }

  auto update_character_class(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::int32_t character_class) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }
    character->character_class = character_class;
    return true;
  }

  auto update_character_gender(std::int32_t account_id,
                               std::int32_t character_id,
                               std::int32_t server_id,
                               std::int32_t gender) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }
    character->gender = gender;
    return true;
  }

  auto update_character_zone(std::int32_t account_id,
                             std::int32_t character_id,
                             std::int32_t server_id,
                             std::int32_t zone_id) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }
    character->current_zone_id = zone_id;
    return true;
  }

  auto update_character_name(std::int32_t account_id,
                             std::int32_t character_id,
                             std::int32_t server_id,
                             std::string_view name) -> bool override {
    const auto character = find_character(account_id, character_id, server_id);
    if (character == nullptr) {
      return false;
    }
    character->name = std::string(name);
    return true;
  }

  std::map<std::int32_t, std::vector<CharacterListRecord>> characters_by_account;
  std::map<std::int32_t, std::vector<CharacterAppearanceRecord>> appearance_by_login_character;
  std::map<std::int32_t, std::vector<CharacterEquipmentRecord>> equipment_by_login_character;
  std::map<std::int32_t, std::vector<WorldZoneUpdateRecord>> world_zones_by_server;
  std::vector<CharacterListRecord> deleted_characters;
  std::vector<CreatedCharacterRecord> saved_created_characters;
  std::vector<LoginEquipmentUpdateRecord> saved_equipment_updates;
  std::vector<CharacterPictureRecord> saved_character_pictures;
  std::int32_t next_login_character_id = 1;

 private:
  auto find_character_by_world_id(std::int32_t server_id,
                                  std::int32_t character_id) -> CharacterListRecord* {
    for (auto& [_, characters] : characters_by_account) {
      const auto character_iter =
          std::find_if(characters.begin(), characters.end(), [&](const auto& character) {
            return character.character_id == character_id && character.server_id == server_id;
          });
      if (character_iter != characters.end()) {
        return &*character_iter;
      }
    }

    return nullptr;
  }

  auto find_character(std::int32_t account_id,
                      std::int32_t character_id,
                      std::int32_t server_id) -> CharacterListRecord* {
    const auto iter = characters_by_account.find(account_id);
    if (iter == characters_by_account.end()) {
      return nullptr;
    }

    auto& characters = iter->second;
    const auto character_iter =
        std::find_if(characters.begin(), characters.end(), [&](const auto& character) {
          return character.character_id == character_id && character.server_id == server_id;
        });
    return character_iter == characters.end() ? nullptr : &*character_iter;
  }
};

class FakeClientLogRepository final : public ClientLogRepository {
 public:
  void save_client_log(const ClientLogRecord& record) override {
    records.push_back(record);
  }

  std::vector<ClientLogRecord> records;
};

class FakeLoginMaintenanceRepository final : public LoginMaintenanceRepository {
 public:
  void remove_old_world_server_stats() override {
    ++old_world_stats_removed;
  }

  void remove_deleted_character_data() override {
    ++deleted_character_data_removed;
  }

  void fix_bug_report_encoding() override {
    ++bug_report_encoding_fixed;
  }

  int old_world_stats_removed = 0;
  int deleted_character_data_removed = 0;
  int bug_report_encoding_fixed = 0;
};

class FakeZoneBootstrapRepository final : public ZoneBootstrapRepository {
 public:
  auto load_zone(std::int32_t zone_id) -> std::optional<ZoneBootstrapRecord> override {
    const auto iter = zones.find(zone_id);
    if (iter == zones.end()) {
      return std::nullopt;
    }

    return iter->second;
  }

  std::map<std::int32_t, ZoneBootstrapRecord> zones;
};

}  // namespace eq2::db
