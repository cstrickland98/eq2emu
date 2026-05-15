#include <eq2/core/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/world/session.h>

#include <iostream>
#include <string_view>

namespace {

class SmokeZoneHandoff final : public eq2::world::ZoneHandoff {
 public:
  auto request_zone_entry(const eq2::world::ZoneHandoffRequest& request)
      -> eq2::world::ZoneHandoffResult override {
    return eq2::world::ZoneHandoffResult{
        .accepted = request.account_id == 42 && request.character_id == 2002 && request.zone_id == 10,
    };
  }
};

auto smoke_world() -> int {
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
  SmokeZoneHandoff zone_handoff;
  eq2::world::WorldServer world(eq2::world::WorldServerConfig{}, characters, zone_handoff);
  world.start();
  const auto session_id = world.accept_client();
  if (!session_id.has_value()) {
    return 1;
  }

  const auto session = world.open_session(*session_id, 42, 0x12345678);
  const auto selected = world.select_character(session, 2002);
  if (!selected.accepted) {
    return 1;
  }

  const auto handoff = world.handoff_to_zone(session, selected.character);
  return handoff.accepted ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--smoke-world") {
    return smoke_world();
  }

  eq2::core::MapConfig config_values;
  const auto config = eq2::world::load_world_server_config(config_values);

  std::cout << "eq2_world_server source2 wiring "
            << config.address << ':' << config.port
            << " login=" << config.login_address << ':' << config.login_port << '\n';
  return 0;
}
