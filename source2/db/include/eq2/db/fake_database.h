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

  void add_account(std::int32_t id, std::string username, std::string password) {
    accounts_[std::move(username)] = StoredAccount{
        .id = id,
        .password = std::move(password),
    };
    next_id_ = std::max(next_id_, id + 1);
  }

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

  std::map<std::string, WorldAccountRecord> accounts;
  std::set<std::string> allowed_server_versions;
  std::set<std::string> banned_addresses;
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

  std::map<std::int32_t, std::vector<CharacterListRecord>> characters_by_account;
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
