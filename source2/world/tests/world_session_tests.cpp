#include <eq2/core/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/login_world.h>
#include <eq2/world/session.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
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

class CapturingZoneHandoff final : public eq2::world::ZoneHandoff {
 public:
  auto request_zone_entry(const eq2::world::ZoneHandoffRequest& request)
      -> eq2::world::ZoneHandoffResult override {
    last_request = request;
    return eq2::world::ZoneHandoffResult{
        .accepted = true,
    };
  }

  std::optional<eq2::world::ZoneHandoffRequest> last_request;
};

void world_config_loads_source2_runtime_settings() {
  eq2::core::MapConfig values;
  values.set("world.address", "127.0.0.1");
  values.set("world.port", "9200");
  values.set("login.address", "127.0.0.2");
  values.set("login.port", "9102");
  values.set("world.name", "Public World");
  values.set("world.account", "world-account");
  values.set("world.password", "secret");
  values.set("world.server_version", "2026.05.14");

  const auto config = eq2::world::load_world_server_config(values);

  require_eq(config.address, std::string_view("127.0.0.1"), "world config reads bind address");
  require_eq(config.port, static_cast<std::uint16_t>(9200), "world config reads bind port");
  require_eq(config.login_address, std::string_view("127.0.0.2"), "world config reads login address");
  require_eq(config.login_port, static_cast<std::uint16_t>(9102), "world config reads login port");
  require_eq(config.world_name, std::string_view("Public World"), "world config reads world name");
}

void world_registration_frame_matches_protocol_boundary() {
  const auto frame = eq2::world::make_world_registration_frame(eq2::world::WorldServerConfig{
      .address = "127.0.0.1",
      .world_name = "Public World",
      .world_account = "world-account",
      .world_password = "secret",
      .protocol_version = "0.5.0",
      .server_version = "2026.05.14",
      .database_version = 1234,
  });

  require(frame.has_value(), "world registration frame encodes");
  const auto decoded_frame = eq2::protocol::decode_interserver_packet(*frame);
  require(decoded_frame.has_value(), "world registration frame decodes");
  require_eq(decoded_frame->opcode, eq2::protocol::kServerOpLsInfo,
             "world registration uses LSInfo opcode");
  const auto info = eq2::protocol::decode_server_ls_info_payload(decoded_frame->payload);
  require(info.has_value(), "world registration LSInfo payload decodes");
  require_eq(info->world_name, std::string_view("Public World"),
             "world registration payload carries world name");
  require_eq(info->account, std::string_view("world-account"),
             "world registration payload carries account");
}

void character_list_and_select_match_known_account_rows() {
  eq2::db::FakeCharacterListRepository characters;
  characters.characters_by_account[42] = {
      eq2::db::CharacterListRecord{
          .login_character_id = 1001,
          .character_id = 2002,
          .server_id = 77,
          .name = "Alys",
          .current_zone_id = 10,
          .level = 12,
      },
  };
  CapturingZoneHandoff zone_handoff;
  eq2::world::WorldServer world(eq2::world::WorldServerConfig{}, characters, zone_handoff);

  world.start();
  const auto session_id = world.accept_client();
  require(session_id.has_value(), "world server accepts client sessions through net");
  const auto session = world.open_session(*session_id, 42, 0x12345678);

  const auto list = world.character_list(session);
  require_eq(list.size(), static_cast<std::size_t>(1), "world returns one known character");
  require_eq(list.front().name, std::string_view("Alys"), "world character list preserves name");
  require_eq(list.front().current_zone_id, 10, "world character list preserves zone id");

  const auto selected = world.select_character(session, 2002);
  require(selected.accepted, "world accepts known character selection");
  require_eq(selected.character.name, std::string_view("Alys"),
             "world select returns selected character summary");
  require(!world.select_character(session, 9999).accepted,
          "world rejects characters outside the authenticated account list");
}

void zone_handoff_uses_interface_without_zone_internals() {
  eq2::db::FakeCharacterListRepository characters;
  CapturingZoneHandoff zone_handoff;
  eq2::world::WorldServer world(eq2::world::WorldServerConfig{}, characters, zone_handoff);

  const auto session = eq2::world::WorldClientSession{
      .session = eq2::net::SessionId{1},
      .account_id = 42,
      .access_key = 0x12345678,
      .authenticated = true,
  };
  const auto character = eq2::world::CharacterSummary{
      .character_id = 2002,
      .server_id = 77,
      .name = "Alys",
      .level = 12,
      .current_zone_id = 10,
  };

  const auto result = world.handoff_to_zone(session, character);

  require(result.accepted, "world accepts zone handoff through interface");
  require(zone_handoff.last_request.has_value(), "zone handoff interface receives a request");
  require_eq(zone_handoff.last_request->account_id, 42, "zone handoff carries account id");
  require_eq(zone_handoff.last_request->character_id, 2002, "zone handoff carries character id");
  require_eq(zone_handoff.last_request->zone_id, 10, "zone handoff carries zone id");
  require_eq(zone_handoff.last_request->access_key, 0x12345678,
             "zone handoff carries access key");
}

}  // namespace

int main() {
  world_config_loads_source2_runtime_settings();
  world_registration_frame_matches_protocol_boundary();
  character_list_and_select_match_known_account_rows();
  zone_handoff_uses_interface_without_zone_internals();

  if (failures != 0) {
    std::cerr << failures << " world session assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
