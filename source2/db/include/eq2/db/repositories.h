#pragma once

#include <cstdint>
#include <optional>
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
};

struct CharacterListRecord {
  std::int32_t login_character_id = 0;
  std::int32_t character_id = 0;
  std::int32_t server_id = 0;
  std::string name;
  std::int32_t race = 0;
  std::int32_t character_class = 0;
  std::int32_t gender = 0;
  std::int32_t current_zone_id = 0;
  std::int32_t level = 0;
};

class CharacterListRepository {
 public:
  virtual ~CharacterListRepository() = default;

  virtual auto load_character_list(std::int32_t account_id) -> std::vector<CharacterListRecord> = 0;
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
