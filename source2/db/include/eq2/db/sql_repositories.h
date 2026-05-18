#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <eq2/db/config.h>
#include <eq2/db/query.h>
#include <eq2/db/repositories.h>

namespace eq2::db {

namespace detail {

class MariaDbConnectionState;

inline auto to_i32(const std::optional<std::string>& value, std::int32_t fallback = 0) -> std::int32_t {
  if (!value.has_value()) {
    return fallback;
  }

  try {
    return static_cast<std::int32_t>(std::stol(*value));
  } catch (...) {
    return fallback;
  }
}

inline auto to_float(const std::optional<std::string>& value, float fallback = 0.0F) -> float {
  if (!value.has_value()) {
    return fallback;
  }

  try {
    return std::stof(*value);
  } catch (...) {
    return fallback;
  }
}

inline auto first_row(eq2::core::Result<QueryResult>& result) -> std::optional<QueryRow> {
  if (!result.has_value() || result.value().rows.empty()) {
    return std::nullopt;
  }

  return result.value().rows.front();
}

inline auto bytes_to_hex(std::span<const std::uint8_t> bytes) -> std::string {
  static constexpr char kHex[] = "0123456789abcdef";
  auto output = std::string{};
  output.reserve(bytes.size() * 2U);
  for (const auto byte : bytes) {
    output.push_back(kHex[(byte >> 4U) & 0x0fU]);
    output.push_back(kHex[byte & 0x0fU]);
  }
  return output;
}

}  // namespace detail

struct LoginOpcodeSet {
  std::uint16_t login_request_opcode = 0;
  std::uint16_t login_reply_opcode = 0;
  std::uint16_t world_list_opcode = 0;
  std::uint16_t all_worlds_request_opcode = 0;
  std::uint16_t characters_request_opcode = 0;
  std::uint16_t characters_reply_opcode = 0;
  std::uint16_t create_character_request_opcode = 0;
  std::uint16_t create_character_reply_opcode = 0;
  std::uint16_t delete_character_request_opcode = 0;
  std::uint16_t delete_character_reply_opcode = 0;
  std::uint16_t play_character_request_opcode = 0;
  std::uint16_t play_character_reply_opcode = 0;
  std::uint16_t client_crashlog_reply_opcode = 0;
  std::uint16_t client_eq2_crashlog_reply_opcode = 0;
  std::uint16_t client_verifylog_reply_opcode = 0;
  std::uint16_t client_alertlog_reply_opcode = 0;
  std::uint16_t client_baselog_reply_opcode = 0;
  std::uint16_t key_request_opcode = 0;
};

struct OpcodeVersionRange {
  std::int16_t min_version = 0;
  std::int16_t max_version = 0;
};

inline auto load_login_opcode_version_ranges(QueryConnection& connection,
                                             std::int16_t client_version)
    -> eq2::core::Result<std::vector<OpcodeVersionRange>> {
  auto result = connection.execute(QueryRequest{
      .sql = "select distinct version_range1, version_range2 from opcodes "
             "where ? between version_range1 and version_range2 "
             "order by version_range1, version_range2",
      .parameters = {std::to_string(client_version)},
  });
  if (!result.has_value()) {
    return eq2::core::Result<std::vector<OpcodeVersionRange>>::failure(eq2::core::Error{
        .code = result.error().code,
        .message = "login opcode version range lookup failed: " + result.error().message,
    });
  }

  auto ranges = std::vector<OpcodeVersionRange>{};
  for (const auto& row : result.value().rows) {
    const auto min_version = detail::to_i32(row.get("version_range1"), -1);
    const auto max_version = detail::to_i32(row.get("version_range2"), -1);
    if (min_version < 0 || min_version > 0x7fff || max_version < 0 ||
        max_version > 0x7fff || min_version > max_version) {
      return eq2::core::Result<std::vector<OpcodeVersionRange>>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::parse_error,
          .message = "login opcode version range lookup returned an invalid range",
      });
    }

    ranges.push_back(OpcodeVersionRange{
        .min_version = static_cast<std::int16_t>(min_version),
        .max_version = static_cast<std::int16_t>(max_version),
    });
  }

  if (ranges.empty()) {
    return eq2::core::Result<std::vector<OpcodeVersionRange>>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::not_found,
        .message = "login opcode version range lookup found no range for client version " +
                   std::to_string(client_version),
    });
  }

  return eq2::core::Result<std::vector<OpcodeVersionRange>>::success(std::move(ranges));
}

inline auto load_login_opcode_set(QueryConnection& connection,
                                  std::int16_t client_version)
    -> eq2::core::Result<LoginOpcodeSet> {
  auto result = connection.execute(QueryRequest{
      .sql = "select name, opcode from opcodes "
             "where ? between version_range1 and version_range2 "
             "and name in ('OP_LoginRequestMsg', 'OP_LoginReplyMsg', 'OP_WorldListMsg', "
             "'OP_AllWSDescRequestMsg', 'OP_AllCharactersDescRequestMsg', "
             "'OP_AllCharactersDescReplyMsg', 'OP_CreateCharacterRequestMsg', "
             "'OP_CreateCharacterReplyMsg', 'OP_DeleteCharacterRequestMsg', "
             "'OP_DeleteCharacterReplyMsg', 'OP_PlayCharacterRequestMsg', "
             "'OP_PlayCharacterReplyMsg', 'OP_LsClientCrashlogReplyMsg', "
             "'OP_LsClientEq2CrashLogReplyMsg', 'OP_LsClientVerifylogReplyMsg', "
             "'OP_LsClientAlertlogReplyMsg', "
             "'OP_LsClientBaselogReplyMsg', 'OP_WSLoginRequestMsg') "
             "order by version_range1, id",
      .parameters = {std::to_string(client_version)},
  });
  if (!result.has_value()) {
    return eq2::core::Result<LoginOpcodeSet>::failure(eq2::core::Error{
        .code = result.error().code,
        .message = "login opcode lookup failed: " + result.error().message,
    });
  }

  auto opcodes = LoginOpcodeSet{};
  auto has_request = false;
  auto has_reply = false;
  auto has_world_list = false;
  auto has_all_worlds_request = false;
  auto has_characters_request = false;
  auto has_characters_reply = false;
  auto has_create_character_request = false;
  auto has_create_character_reply = false;
  auto has_delete_character_request = false;
  auto has_delete_character_reply = false;
  auto has_play_character_request = false;
  auto has_play_character_reply = false;
  auto has_client_crashlog_reply = false;
  auto has_client_eq2_crashlog_reply = false;
  auto has_client_verifylog_reply = false;
  auto has_client_alertlog_reply = false;
  auto has_client_baselog_reply = false;
  auto has_key_request = false;

  for (const auto& row : result.value().rows) {
    const auto name = row.get("name").value_or("");
    const auto opcode_value = detail::to_i32(row.get("opcode"), -1);
    if (opcode_value < 0 || opcode_value > 0xffff) {
      return eq2::core::Result<LoginOpcodeSet>::failure(eq2::core::Error{
          .code = eq2::core::ErrorCode::parse_error,
          .message = "login opcode lookup returned an invalid opcode for " + name,
      });
    }

    const auto opcode = static_cast<std::uint16_t>(opcode_value);
    if (name == "OP_LoginRequestMsg") {
      opcodes.login_request_opcode = opcode;
      has_request = true;
    } else if (name == "OP_LoginReplyMsg") {
      opcodes.login_reply_opcode = opcode;
      has_reply = true;
    } else if (name == "OP_WorldListMsg") {
      opcodes.world_list_opcode = opcode;
      has_world_list = true;
    } else if (name == "OP_AllWSDescRequestMsg") {
      opcodes.all_worlds_request_opcode = opcode;
      has_all_worlds_request = true;
    } else if (name == "OP_AllCharactersDescRequestMsg") {
      opcodes.characters_request_opcode = opcode;
      has_characters_request = true;
    } else if (name == "OP_AllCharactersDescReplyMsg") {
      opcodes.characters_reply_opcode = opcode;
      has_characters_reply = true;
    } else if (name == "OP_CreateCharacterRequestMsg") {
      opcodes.create_character_request_opcode = opcode;
      has_create_character_request = true;
    } else if (name == "OP_CreateCharacterReplyMsg") {
      opcodes.create_character_reply_opcode = opcode;
      has_create_character_reply = true;
    } else if (name == "OP_DeleteCharacterRequestMsg") {
      opcodes.delete_character_request_opcode = opcode;
      has_delete_character_request = true;
    } else if (name == "OP_DeleteCharacterReplyMsg") {
      opcodes.delete_character_reply_opcode = opcode;
      has_delete_character_reply = true;
    } else if (name == "OP_PlayCharacterRequestMsg") {
      opcodes.play_character_request_opcode = opcode;
      has_play_character_request = true;
    } else if (name == "OP_PlayCharacterReplyMsg") {
      opcodes.play_character_reply_opcode = opcode;
      has_play_character_reply = true;
    } else if (name == "OP_LsClientCrashlogReplyMsg") {
      opcodes.client_crashlog_reply_opcode = opcode;
      has_client_crashlog_reply = true;
    } else if (name == "OP_LsClientEq2CrashLogReplyMsg") {
      opcodes.client_eq2_crashlog_reply_opcode = opcode;
      has_client_eq2_crashlog_reply = true;
    } else if (name == "OP_LsClientVerifylogReplyMsg") {
      opcodes.client_verifylog_reply_opcode = opcode;
      has_client_verifylog_reply = true;
    } else if (name == "OP_LsClientAlertlogReplyMsg") {
      opcodes.client_alertlog_reply_opcode = opcode;
      has_client_alertlog_reply = true;
    } else if (name == "OP_LsClientBaselogReplyMsg") {
      opcodes.client_baselog_reply_opcode = opcode;
      has_client_baselog_reply = true;
    } else if (name == "OP_WSLoginRequestMsg") {
      opcodes.key_request_opcode = opcode;
      has_key_request = true;
    }
  }

  // The DoF client sends eq2_crash.log as type 215 and shifts alert/verify to 216/217.
  if (!has_client_eq2_crashlog_reply && client_version >= 546 &&
      opcodes.client_baselog_reply_opcode == 213U &&
      opcodes.client_crashlog_reply_opcode == 214U &&
      opcodes.client_alertlog_reply_opcode == 215U &&
      opcodes.client_verifylog_reply_opcode == 216U) {
    opcodes.client_eq2_crashlog_reply_opcode = 215U;
    opcodes.client_alertlog_reply_opcode = 216U;
    opcodes.client_verifylog_reply_opcode = 217U;
    has_client_eq2_crashlog_reply = true;
  }

  auto missing = std::string{};
  if (!has_request) {
    missing += " OP_LoginRequestMsg";
  }
  if (!has_reply) {
    missing += " OP_LoginReplyMsg";
  }
  if (!has_world_list) {
    missing += " OP_WorldListMsg";
  }
  if (!has_all_worlds_request) {
    missing += " OP_AllWSDescRequestMsg";
  }
  if (!has_characters_request) {
    missing += " OP_AllCharactersDescRequestMsg";
  }
  if (!has_characters_reply) {
    missing += " OP_AllCharactersDescReplyMsg";
  }
  if (!has_create_character_request) {
    missing += " OP_CreateCharacterRequestMsg";
  }
  if (!has_create_character_reply) {
    missing += " OP_CreateCharacterReplyMsg";
  }
  if (!has_delete_character_request) {
    missing += " OP_DeleteCharacterRequestMsg";
  }
  if (!has_delete_character_reply) {
    missing += " OP_DeleteCharacterReplyMsg";
  }
  if (!has_play_character_request) {
    missing += " OP_PlayCharacterRequestMsg";
  }
  if (!has_play_character_reply) {
    missing += " OP_PlayCharacterReplyMsg";
  }
  if (!has_client_crashlog_reply) {
    missing += " OP_LsClientCrashlogReplyMsg";
  }
  if (!has_client_verifylog_reply) {
    missing += " OP_LsClientVerifylogReplyMsg";
  }
  if (!has_client_alertlog_reply) {
    missing += " OP_LsClientAlertlogReplyMsg";
  }
  if (!has_client_baselog_reply) {
    missing += " OP_LsClientBaselogReplyMsg";
  }
  if (!has_key_request) {
    missing += " OP_WSLoginRequestMsg";
  }
  if (!missing.empty()) {
    return eq2::core::Result<LoginOpcodeSet>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::not_found,
        .message = "login opcode lookup missing" + missing +
                   " for client version " + std::to_string(client_version),
    });
  }

  return eq2::core::Result<LoginOpcodeSet>::success(opcodes);
}

class SqlLoginAccountRepository final : public LoginAccountRepository {
 public:
  explicit SqlLoginAccountRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto find_by_name_and_password(std::string_view username, std::string_view password)
      -> std::optional<LoginAccountRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id, name from account where name = ? and passwd = sha2(?, 512)",
        .parameters = {std::string(username), std::string(password)},
    });
    auto row = detail::first_row(result);
    if (!row.has_value()) {
      return std::nullopt;
    }

    return LoginAccountRecord{
        .id = detail::to_i32(row->get("id")),
        .name = row->get("name").value_or(std::string(username)),
    };
  }

  auto account_name_exists(std::string_view username) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id from account where name = ?",
        .parameters = {std::string(username)},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto create_account(std::string_view username, std::string_view password)
      -> LoginAccountRecord override {
    auto insert = connection_->execute(QueryRequest{
        .sql = "insert into account (name, passwd, created_date) "
               "values (?, sha2(?, 512), unix_timestamp())",
        .parameters = {std::string(username), std::string(password)},
    });
    if (!insert.has_value()) {
      return LoginAccountRecord{
          .id = 0,
          .name = std::string(username),
      };
    }

    if (auto row = detail::first_row(insert); row.has_value()) {
      return LoginAccountRecord{
          .id = detail::to_i32(row->get("id")),
          .name = row->get("name").value_or(std::string(username)),
      };
    }

    return find_by_name_and_password(username, password).value_or(LoginAccountRecord{
        .id = 0,
        .name = std::string(username),
    });
  }

  void record_successful_login(std::int32_t account_id,
                               std::string_view remote_address,
                               std::int16_t client_version) override {
    connection_->execute(QueryRequest{
        .sql = "update account set ip_address = ? where id = ?",
        .parameters = {std::string(remote_address), std::to_string(account_id)},
    });
    connection_->execute(QueryRequest{
        .sql = "update account set last_client_version = ? where id = ?",
        .parameters = {std::to_string(client_version), std::to_string(account_id)},
    });
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

class SqlWorldRegistrationRepository final : public WorldRegistrationRepository {
 public:
  explicit SqlWorldRegistrationRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto server_version_is_allowed(std::string_view server_version) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select version from login_versions where version = ? or version = '*'",
        .parameters = {std::string(server_version)},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto check_server_account(std::string_view account, std::string_view password)
      -> std::int32_t override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id from login_worldservers "
               "where account = ? and disabled = 0 "
               "and (lower(password) = lower(?) or lower(password) = lower(sha2(?, 512)))",
        .parameters = {std::string(account), std::string(password), std::string(password)},
    });
    auto row = detail::first_row(result);
    return row.has_value() ? detail::to_i32(row->get("id")) : 0;
  }

  auto account_is_disabled(std::string_view account) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select disabled from login_worldservers where account = ?",
        .parameters = {std::string(account)},
    });
    auto row = detail::first_row(result);
    return row.has_value() && detail::to_i32(row->get("disabled")) != 0;
  }

  auto connection_ip_is_banned() -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select ip from login_bannedips where ip = ?",
        .parameters = {connection_ip_},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto advertised_address_is_banned(std::string_view address) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select ip from login_bannedips where ip = ?",
        .parameters = {std::string(address)},
    });
    return result.has_value() && !result.value().rows.empty();
  }

  auto display_name_for_account(std::int32_t account_id) -> std::string override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select name from login_worldservers where id = ?",
        .parameters = {std::to_string(account_id)},
    });
    auto row = detail::first_row(result);
    return row.has_value() ? row->get("name").value_or("") : "";
  }

  void record_world_status(std::int32_t world_id,
                           std::int32_t status,
                           std::int32_t player_count,
                           std::int32_t zone_count,
                           std::int32_t world_max_level) override {
    connection_->execute(QueryRequest{
        .sql = "insert into login_worldstats "
               "(world_id, world_status, current_players, current_zones, last_update, "
               "world_max_level) values (?, ?, ?, ?, now(), ?) "
               "on duplicate key update current_players = ?, current_zones = ?, "
               "world_max_level = ?, world_status = ?, last_update = now()",
        .parameters = {std::to_string(world_id),
                       std::to_string(status),
                       std::to_string(player_count),
                       std::to_string(zone_count),
                       std::to_string(world_max_level),
                       std::to_string(player_count),
                       std::to_string(zone_count),
                       std::to_string(world_max_level),
                       std::to_string(status)},
    });
    connection_->execute(QueryRequest{
        .sql = "update login_worldservers set lastseen = unix_timestamp() where id = ?",
        .parameters = {std::to_string(world_id)},
    });
  }

  void set_connection_ip(std::string ip) {
    connection_ip_ = std::move(ip);
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
  std::string connection_ip_;
};

class SqlCharacterListRepository final : public CharacterListRepository {
 public:
  explicit SqlCharacterListRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto load_character_list(std::int32_t account_id) -> std::vector<CharacterListRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select lc.id, lc.char_id as character_id, lc.server_id, lc.name, lc.race, "
               "lc.class, lc.gender, lc.deity, lc.body_size, lc.body_age, lc.current_zone_id, "
               "lc.level, lc.soga_wing_type, lc.soga_chest_type, lc.soga_legs_type, "
               "lc.soga_hair_type, lc.legs_type, lc.chest_type, lc.wing_type, lc.hair_type, "
               "unix_timestamp(lc.created_date) as created_date, "
               "unix_timestamp(lc.last_played) as last_played, lw.name as server_name, "
               "lc.facial_hair_type, lc.soga_facial_hair_type, lc.soga_model_type, "
               "lc.model_type, coalesce(lzw.name, z.name, z.file, ' ') as zone_name, "
               "coalesce(lzw.description, z.description, ' ') as zone_description "
               "from login_characters lc "
               "left join login_worldservers lw on lw.id = lc.server_id "
               "left join ls_world_zones lzw on lzw.server_id = lc.server_id "
               "and lzw.zone_id = lc.current_zone_id "
               "left join zones z on z.id = lc.current_zone_id "
               "where lc.account_id = ? and lc.deleted = 0",
        .parameters = {std::to_string(account_id)},
    });
    if (!result.has_value()) {
      return {};
    }

    auto records = std::vector<CharacterListRecord>{};
    for (const auto& row : result.value().rows) {
      records.push_back(CharacterListRecord{
          .login_character_id = detail::to_i32(row.get("id")),
          .character_id = detail::to_i32(row.get("character_id")),
          .server_id = detail::to_i32(row.get("server_id")),
          .name = row.get("name").value_or(""),
          .race = detail::to_i32(row.get("race")),
          .character_class = detail::to_i32(row.get("class")),
          .gender = detail::to_i32(row.get("gender")),
          .deity = detail::to_i32(row.get("deity")),
          .body_size = static_cast<double>(detail::to_float(row.get("body_size"))),
          .body_age = static_cast<double>(detail::to_float(row.get("body_age"))),
          .current_zone_id = detail::to_i32(row.get("current_zone_id")),
          .level = detail::to_i32(row.get("level")),
          .soga_wing_type = detail::to_i32(row.get("soga_wing_type")),
          .soga_chest_type = detail::to_i32(row.get("soga_chest_type")),
          .soga_legs_type = detail::to_i32(row.get("soga_legs_type")),
          .soga_hair_type = detail::to_i32(row.get("soga_hair_type")),
          .legs_type = detail::to_i32(row.get("legs_type")),
          .chest_type = detail::to_i32(row.get("chest_type")),
          .wing_type = detail::to_i32(row.get("wing_type")),
          .hair_type = detail::to_i32(row.get("hair_type")),
          .created_date = detail::to_i32(row.get("created_date")),
          .last_played = detail::to_i32(row.get("last_played")),
          .server_name = row.get("server_name").value_or(""),
          .facial_hair_type = detail::to_i32(row.get("facial_hair_type")),
          .soga_facial_hair_type = detail::to_i32(row.get("soga_facial_hair_type")),
          .soga_model_type = detail::to_i32(row.get("soga_model_type")),
          .model_type = detail::to_i32(row.get("model_type")),
          .zone_name = row.get("zone_name").value_or(" "),
          .zone_description = row.get("zone_description").value_or(" "),
      });
    }
    return records;
  }

  auto load_character_appearance(std::int32_t login_character_id)
      -> std::vector<CharacterAppearanceRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select type, signed_value, red, green, blue "
               "from login_char_colors where login_characters_id = ?",
        .parameters = {std::to_string(login_character_id)},
    });
    if (!result.has_value()) {
      return {};
    }

    auto records = std::vector<CharacterAppearanceRecord>{};
    for (const auto& row : result.value().rows) {
      records.push_back(CharacterAppearanceRecord{
          .type = row.get("type").value_or(""),
          .signed_value = detail::to_i32(row.get("signed_value")) != 0,
          .red = detail::to_i32(row.get("red")),
          .green = detail::to_i32(row.get("green")),
          .blue = detail::to_i32(row.get("blue")),
      });
    }
    return records;
  }

  auto load_character_equipment(std::int32_t login_character_id)
      -> std::vector<CharacterEquipmentRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select slot, equip_type, red, green, blue, highlight_red, "
               "highlight_green, highlight_blue from login_equipment "
               "where login_characters_id = ? order by slot",
        .parameters = {std::to_string(login_character_id)},
    });
    if (!result.has_value()) {
      return {};
    }

    auto records = std::vector<CharacterEquipmentRecord>{};
    for (const auto& row : result.value().rows) {
      records.push_back(CharacterEquipmentRecord{
          .slot = detail::to_i32(row.get("slot")),
          .equip_type = detail::to_i32(row.get("equip_type")),
          .red = detail::to_i32(row.get("red"), 0xff),
          .green = detail::to_i32(row.get("green"), 0xff),
          .blue = detail::to_i32(row.get("blue"), 0xff),
          .highlight_red = detail::to_i32(row.get("highlight_red"), 0xff),
          .highlight_green = detail::to_i32(row.get("highlight_green"), 0xff),
          .highlight_blue = detail::to_i32(row.get("highlight_blue"), 0xff),
      });
    }
    return records;
  }

  auto mark_character_deleted(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::string_view name) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "update login_characters set deleted = 1 "
               "where char_id = ? and account_id = ? and server_id = ? and name = ?",
        .parameters = {std::to_string(character_id),
                       std::to_string(account_id),
                       std::to_string(server_id),
                       std::string(name)},
    });
    return result.has_value() && result.value().affected_rows == 1;
  }

  auto save_created_character(const CreatedCharacterRecord& character) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "insert into login_characters "
               "(account_id, server_id, char_id, name, race, class, gender, deity, "
               "body_size, body_age, soga_wing_type, soga_chest_type, soga_legs_type, "
               "soga_hair_type, soga_facial_hair_type, legs_type, chest_type, wing_type, "
               "hair_type, facial_hair_type, soga_model_type, model_type) "
               "values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        .parameters = {std::to_string(character.account_id),
                       std::to_string(character.server_id),
                       std::to_string(character.character_id),
                       character.name,
                       std::to_string(character.race),
                       std::to_string(character.character_class),
                       std::to_string(character.gender),
                       std::to_string(character.deity),
                       std::to_string(character.body_size),
                       std::to_string(character.body_age),
                       std::to_string(resolve_appearance_id(character.appearance_files.soga_wing_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.soga_chest_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.soga_legs_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.soga_hair_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.soga_face_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.legs_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.chest_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.wing_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.hair_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.face_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.soga_race_file)),
                       std::to_string(resolve_appearance_id(character.appearance_files.race_file))},
    });
    if (!result.has_value()) {
      return false;
    }

    const auto login_character_id = inserted_login_character_id(character);
    if (!login_character_id.has_value()) {
      return true;
    }

    connection_->execute(QueryRequest{
        .sql = "update login_characters set deleted = 1 "
               "where char_id = ? and server_id = ? and id != ?",
        .parameters = {std::to_string(character.character_id),
                       std::to_string(character.server_id),
                       std::to_string(*login_character_id)},
    });

    for (const auto& appearance : character.appearance_values) {
      save_created_character_appearance(*login_character_id, appearance);
    }
    return true;
  }

  auto save_world_zone_updates(std::int32_t server_id,
                               const std::vector<WorldZoneUpdateRecord>& updates)
      -> std::size_t override {
    auto saved = std::size_t{0};
    for (const auto& update : updates) {
      auto result = connection_->execute(QueryRequest{
          .sql = "replace into ls_world_zones "
                 "(server_id, zone_id, name, description) values (?, ?, ?, ?)",
          .parameters = {std::to_string(server_id),
                         std::to_string(update.zone_id),
                         update.name,
                         update.description},
      });
      if (result.has_value()) {
        ++saved;
      }
    }
    return saved;
  }

  auto save_login_equipment_updates(
      std::int32_t server_id,
      const std::vector<LoginEquipmentUpdateRecord>& updates) -> std::size_t override {
    auto saved = std::size_t{0};
    for (const auto& update : updates) {
      auto login_character_result = connection_->execute(QueryRequest{
          .sql = "select id from login_characters "
                 "where server_id = ? and char_id = ? and deleted = 0 limit 1",
          .parameters = {std::to_string(server_id),
                         std::to_string(update.world_character_id)},
      });
      auto row = detail::first_row(login_character_result);
      if (!row.has_value()) {
        continue;
      }

      const auto login_character_id = detail::to_i32(row->get("id"));
      if (login_character_id == 0) {
        continue;
      }

      auto result = connection_->execute(QueryRequest{
          .sql = "replace into login_equipment "
                 "(login_characters_id, equip_type, red, green, blue, highlight_red, "
                 "highlight_green, highlight_blue, slot) values (?, ?, ?, ?, ?, ?, ?, ?, ?)",
          .parameters = {std::to_string(login_character_id),
                         std::to_string(update.equip_type),
                         std::to_string(update.red),
                         std::to_string(update.green),
                         std::to_string(update.blue),
                         std::to_string(update.highlight_red),
                         std::to_string(update.highlight_green),
                         std::to_string(update.highlight_blue),
                         std::to_string(update.slot)},
      });
      if (result.has_value()) {
        ++saved;
      }
    }
    return saved;
  }

  auto save_character_picture(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::span<const std::uint8_t> picture) -> bool override {
    const auto hex_picture = detail::bytes_to_hex(picture);
    auto result = connection_->execute(QueryRequest{
        .sql = "insert into ls_character_picture "
               "(server_id, account_id, character_id, picture) values (?, ?, ?, ?) "
               "on duplicate key update picture = ?",
        .parameters = {std::to_string(server_id),
                       std::to_string(account_id),
                       std::to_string(character_id),
                       hex_picture,
                       hex_picture},
    });
    return result.has_value();
  }

  auto mark_character_deleted_by_world(std::int32_t account_id,
                                       std::int32_t character_id,
                                       std::int32_t server_id) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "update login_characters set deleted = 1 "
               "where char_id = ? and account_id = ? and server_id = ?",
        .parameters = {std::to_string(character_id),
                       std::to_string(account_id),
                       std::to_string(server_id)},
    });
    return result.has_value() && result.value().affected_rows == 1;
  }

  auto update_character_timestamp(std::int32_t account_id,
                                  std::int32_t character_id,
                                  std::int32_t server_id,
                                  std::int32_t timestamp) -> bool override {
    return execute_character_update(
        "update login_characters set unix_timestamp = ? "
        "where char_id = ? and account_id = ? and server_id = ?",
        account_id,
        character_id,
        server_id,
        timestamp);
  }

  auto update_character_level(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::int32_t level) -> bool override {
    return execute_character_update(
        "update login_characters set level = ? "
        "where char_id = ? and account_id = ? and server_id = ?",
        account_id,
        character_id,
        server_id,
        level);
  }

  auto update_character_race(std::int32_t account_id,
                             std::int32_t character_id,
                             std::int32_t server_id,
                             std::int32_t model_type,
                             std::int32_t race) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "update login_characters set race_type = ?, race = ? "
               "where char_id = ? and account_id = ? and server_id = ?",
        .parameters = {std::to_string(model_type),
                       std::to_string(race),
                       std::to_string(character_id),
                       std::to_string(account_id),
                       std::to_string(server_id)},
    });
    return result.has_value() && result.value().affected_rows == 1;
  }

  auto update_character_class(std::int32_t account_id,
                              std::int32_t character_id,
                              std::int32_t server_id,
                              std::int32_t character_class) -> bool override {
    return execute_character_update(
        "update login_characters set class = ? "
        "where char_id = ? and account_id = ? and server_id = ?",
        account_id,
        character_id,
        server_id,
        character_class);
  }

  auto update_character_gender(std::int32_t account_id,
                               std::int32_t character_id,
                               std::int32_t server_id,
                               std::int32_t gender) -> bool override {
    return execute_character_update(
        "update login_characters set gender = ? "
        "where char_id = ? and account_id = ? and server_id = ?",
        account_id,
        character_id,
        server_id,
        gender);
  }

  auto update_character_zone(std::int32_t account_id,
                             std::int32_t character_id,
                             std::int32_t server_id,
                             std::int32_t zone_id) -> bool override {
    return execute_character_update(
        "update login_characters set current_zone_id = ? "
        "where char_id = ? and account_id = ? and server_id = ?",
        account_id,
        character_id,
        server_id,
        zone_id);
  }

  auto update_character_name(std::int32_t account_id,
                             std::int32_t character_id,
                             std::int32_t server_id,
                             std::string_view name) -> bool override {
    auto result = connection_->execute(QueryRequest{
        .sql = "update login_characters set name = ? "
               "where char_id = ? and account_id = ? and server_id = ?",
        .parameters = {std::string(name),
                       std::to_string(character_id),
                       std::to_string(account_id),
                       std::to_string(server_id)},
    });
    return result.has_value() && result.value().affected_rows == 1;
  }

 private:
  auto resolve_appearance_id(std::string_view name) -> std::int32_t {
    if (name.empty()) {
      return 0;
    }

    auto result = connection_->execute(QueryRequest{
        .sql = "select appearance_id from appearances where name = ?",
        .parameters = {std::string(name)},
    });
    if (!result.has_value() || result.value().rows.size() != 1U) {
      return 0;
    }

    return detail::to_i32(result.value().rows.front().get("appearance_id"));
  }

  auto inserted_login_character_id(const CreatedCharacterRecord& character)
      -> std::optional<std::int32_t> {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id from login_characters "
               "where account_id = ? and server_id = ? and char_id = ? and name = ? "
               "and deleted = 0 order by id desc limit 1",
        .parameters = {std::to_string(character.account_id),
                       std::to_string(character.server_id),
                       std::to_string(character.character_id),
                       character.name},
    });
    auto row = detail::first_row(result);
    if (!row.has_value()) {
      return std::nullopt;
    }

    const auto id = detail::to_i32(row->get("id"));
    return id == 0 ? std::nullopt : std::optional<std::int32_t>{id};
  }

  void save_created_character_appearance(std::int32_t login_character_id,
                                         const CharacterAppearanceRecord& appearance) {
    connection_->execute(QueryRequest{
        .sql = "insert into login_char_colors "
               "(login_characters_id, type, red, green, blue, signed_value) "
               "values (?, ?, ?, ?, ?, ?)",
        .parameters = {std::to_string(login_character_id),
                       appearance.type,
                       std::to_string(appearance.red),
                       std::to_string(appearance.green),
                       std::to_string(appearance.blue),
                       appearance.signed_value ? "1" : "0"},
    });
  }

  auto execute_character_update(std::string sql,
                                std::int32_t account_id,
                                std::int32_t character_id,
                                std::int32_t server_id,
                                std::int32_t value) -> bool {
    auto result = connection_->execute(QueryRequest{
        .sql = std::move(sql),
        .parameters = {std::to_string(value),
                       std::to_string(character_id),
                       std::to_string(account_id),
                       std::to_string(server_id)},
    });
    return result.has_value() && result.value().affected_rows == 1;
  }

  std::shared_ptr<QueryConnection> connection_;
};

class SqlClientLogRepository final : public ClientLogRepository {
 public:
  explicit SqlClientLogRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  void save_client_log(const ClientLogRecord& record) override {
    connection_->execute(QueryRequest{
        .sql = "insert into log_messages (type, message, name, version) "
               "values (?, ?, ?, ?)",
        .parameters = {record.type,
                       record.message,
                       record.account_name,
                       std::to_string(record.client_version)},
    });
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

class SqlLoginMaintenanceRepository final : public LoginMaintenanceRepository {
 public:
  explicit SqlLoginMaintenanceRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  void remove_old_world_server_stats() override {
    connection_->execute(QueryRequest{
        .sql = "delete from login_worldstats "
               "where (unix_timestamp(now()) - unix_timestamp(last_update)) > 86400",
    });
  }

  void remove_deleted_character_data() override {
    connection_->execute(QueryRequest{
        .sql = "delete from login_char_colors where login_characters_id in "
               "(select id from login_characters where deleted = 1)",
    });
    connection_->execute(QueryRequest{
        .sql = "delete from login_equipment where login_characters_id in "
               "(select id from login_characters where deleted = 1)",
    });
  }

  void fix_bug_report_encoding() override {
    connection_->execute(QueryRequest{
        .sql = "update bugs set description = replace(description, substring(description, "
               "instr(description, '%'), 3), char(conv(substring(description, "
               "instr(description, '%') + 1, 2), 16, 10))), summary = "
               "replace(summary, substring(summary, instr(summary, '%'), 3), "
               "char(conv(substring(summary, instr(summary, '%') + 1, 2), 16, 10)))",
    });
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

class SqlZoneBootstrapRepository final : public ZoneBootstrapRepository {
 public:
  explicit SqlZoneBootstrapRepository(std::shared_ptr<QueryConnection> connection)
      : connection_(std::move(connection)) {}

  auto load_zone(std::int32_t zone_id) -> std::optional<ZoneBootstrapRecord> override {
    auto result = connection_->execute(QueryRequest{
        .sql = "select id, name, safe_x, safe_y, safe_z, safe_heading from zones where id = ?",
        .parameters = {std::to_string(zone_id)},
    });
    auto row = detail::first_row(result);
    if (!row.has_value()) {
      return std::nullopt;
    }

    return ZoneBootstrapRecord{
        .zone_id = detail::to_i32(row->get("id")),
        .name = row->get("name").value_or(""),
        .safe_x = detail::to_float(row->get("safe_x")),
        .safe_y = detail::to_float(row->get("safe_y")),
        .safe_z = detail::to_float(row->get("safe_z")),
        .safe_heading = detail::to_float(row->get("safe_heading")),
    };
  }

 private:
  std::shared_ptr<QueryConnection> connection_;
};

inline auto preflight_login_database_schema(QueryConnection& connection)
    -> eq2::core::Result<void> {
  struct SchemaProbe {
    std::string_view label;
    std::string sql;
  };

  const auto probes = std::vector<SchemaProbe>{
      SchemaProbe{
          .label = "account table",
          .sql = "select id, name, passwd, created_date from account where 1 = 0",
      },
      SchemaProbe{
          .label = "login_versions table",
          .sql = "select version from login_versions where 1 = 0",
      },
      SchemaProbe{
          .label = "login_worldservers table",
          .sql = "select id, account, password, disabled, name from login_worldservers where 1 = 0",
      },
      SchemaProbe{
          .label = "login_bannedips table",
          .sql = "select ip from login_bannedips where 1 = 0",
      },
      SchemaProbe{
          .label = "opcodes table",
          .sql = "select name, opcode, version_range1, version_range2 from opcodes where 1 = 0",
      },
      SchemaProbe{
          .label = "login_characters table",
          .sql = "select id, char_id, server_id, name, race, class, gender, current_zone_id, "
                 "level, deleted from login_characters where 1 = 0",
      },
      SchemaProbe{
          .label = "login_equipment table",
          .sql = "select login_characters_id, equip_type, red, green, blue, highlight_red, "
                 "highlight_green, highlight_blue, slot from login_equipment where 1 = 0",
      },
      SchemaProbe{
          .label = "login_char_colors table",
          .sql = "select login_characters_id, type, signed_value, red, green, blue "
                 "from login_char_colors where 1 = 0",
      },
      SchemaProbe{
          .label = "ls_world_zones table",
          .sql = "select server_id, zone_id, name, description from ls_world_zones where 1 = 0",
      },
      SchemaProbe{
          .label = "log_messages table",
          .sql = "select type, message, name, version from log_messages where 1 = 0",
      },
  };

  for (const auto& probe : probes) {
    auto result = connection.execute(QueryRequest{.sql = probe.sql});
    if (!result.has_value()) {
      return eq2::core::Result<void>::failure(eq2::core::Error{
          .code = result.error().code,
          .message = "login database schema preflight failed for " +
                     std::string(probe.label) + ": " + result.error().message,
      });
    }
  }

  auto account_lookup = connection.execute(QueryRequest{
      .sql = "select id, name from account where name = ? and passwd = sha2(?, 512)",
      .parameters = {"__source2_preflight__", "__source2_preflight__"},
  });
  if (!account_lookup.has_value()) {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = account_lookup.error().code,
        .message = "login database schema preflight failed for parameterized account lookup: " +
                   account_lookup.error().message,
    });
  }

  return eq2::core::Result<void>::success();
}

inline auto preflight_world_database_schema(QueryConnection& connection)
    -> eq2::core::Result<void> {
  auto zones = connection.execute(QueryRequest{
      .sql = "select id, name, safe_x, safe_y, safe_z, safe_heading from zones where 1 = 0",
  });
  if (!zones.has_value()) {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = zones.error().code,
        .message = "world database schema preflight failed for zones table: " +
                   zones.error().message,
    });
  }

  auto zone_lookup = connection.execute(QueryRequest{
      .sql = "select id, name, safe_x, safe_y, safe_z, safe_heading from zones where id = ?",
      .parameters = {"0"},
  });
  if (!zone_lookup.has_value()) {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = zone_lookup.error().code,
        .message = "world database schema preflight failed for parameterized zone lookup: " +
                   zone_lookup.error().message,
    });
  }

  return eq2::core::Result<void>::success();
}

class MariaDbConnection final : public QueryConnection {
 public:
  explicit MariaDbConnection(DatabaseConfig config);
  MariaDbConnection(const MariaDbConnection&) = delete;
  auto operator=(const MariaDbConnection&) -> MariaDbConnection& = delete;
  MariaDbConnection(MariaDbConnection&&) = delete;
  auto operator=(MariaDbConnection&&) -> MariaDbConnection& = delete;
  ~MariaDbConnection() override;

  auto execute(const QueryRequest& request) -> eq2::core::Result<QueryResult> override;

  [[nodiscard]] auto config() const -> const DatabaseConfig&;
  [[nodiscard]] auto is_connected() const -> bool;

 private:
  DatabaseConfig config_;
  std::unique_ptr<detail::MariaDbConnectionState> state_;
  mutable std::mutex mutex_;
};

}  // namespace eq2::db
