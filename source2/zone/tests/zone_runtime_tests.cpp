#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>
#include <eq2/scripting/lua_backend.h>
#include <eq2/zone/admission_feature.h>
#include <eq2/zone/bootstrap.h>
#include <eq2/zone/runtime.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
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

auto add_spawn_command(std::int32_t id, std::string_view name, eq2::zone::Position position)
    -> eq2::zone::ZoneCommand {
  return eq2::zone::ZoneCommand{
      .type = eq2::zone::ZoneCommand::Type::add_spawn,
      .spawn =
          eq2::zone::SpawnState{
              .id = eq2::zone::SpawnId{id},
              .name = std::string(name),
              .position = position,
              .hit_points = 100,
          },
  };
}

class ZoneBootstrapConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    requests.push_back(request);
    if (request.parameters.empty() || request.parameters.front() != "10") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows =
            {
                eq2::db::QueryRow{.columns =
                                      {
                                          {"id", "10"},
                                          {"name", "qeynos"},
                                          {"safe_x", "1.5"},
                                          {"safe_y", "2.5"},
                                          {"safe_z", "3.5"},
                                          {"safe_heading", "4.5"},
                                      }},
            },
        .affected_rows = 1,
    });
  }

  std::vector<eq2::db::QueryRequest> requests;
};

class CapturingLogSink final : public eq2::core::LogSink {
 public:
  void write(const eq2::core::LogRecord& record) override {
    records.push_back(record);
  }

  std::vector<eq2::core::LogRecord> records;
};

class CapturingCommandSink final : public eq2::scripting::OwnerCommandSink {
 public:
  auto post_script_mutation(const eq2::scripting::ScriptMutation& mutation) -> bool override {
    mutations.push_back(mutation);
    return true;
  }

  std::vector<eq2::scripting::ScriptMutation> mutations;
};

void zone_commands_mutate_state_only_when_owner_ticks() {
  eq2::zone::ZoneRuntime zone(eq2::zone::ZoneId{10});
  require(zone.post_command(add_spawn_command(1, "guard", {.x = 1.0F, .y = 2.0F, .z = 3.0F})),
          "external system posts add-spawn command");

  require(zone.snapshot().spawns.empty(), "posted command does not mutate snapshot before tick");

  const auto snapshot = zone.tick();
  require(zone.owner_thread().has_value(), "first tick binds owner thread");
  require_eq(snapshot.spawns.size(), static_cast<std::size_t>(1), "owner tick applies add-spawn command");
  require_eq(snapshot.spawns.front().name, std::string_view("guard"), "spawn name is stored");
  require_eq(snapshot.spawns.front().position.x, 1.0F, "spawn position is stored");
}

void movement_and_combat_are_command_boundaries() {
  eq2::zone::ZoneRuntime zone(eq2::zone::ZoneId{10});
  zone.post_command(add_spawn_command(1, "guard", {.x = 1.0F, .y = 2.0F, .z = 3.0F}));
  zone.post_command(eq2::zone::ZoneCommand{
      .type = eq2::zone::ZoneCommand::Type::move_spawn,
      .spawn_id = eq2::zone::SpawnId{1},
      .position = {.x = 4.0F, .y = 5.0F, .z = 6.0F, .heading = 90.0F},
  });
  zone.post_command(eq2::zone::ZoneCommand{
      .type = eq2::zone::ZoneCommand::Type::combat_event,
      .combat =
          eq2::zone::CombatEvent{
              .source = eq2::zone::SpawnId{2},
              .target = eq2::zone::SpawnId{1},
              .damage = 25,
          },
  });

  const auto snapshot = zone.tick();

  require_eq(snapshot.spawns.front().position.x, 4.0F, "move command updates position on tick");
  require_eq(snapshot.spawns.front().hit_points, 75, "combat command updates hit points on tick");
}

void client_subscriptions_receive_zone_updates_from_snapshots() {
  eq2::zone::ZoneRuntime zone(eq2::zone::ZoneId{10});
  const auto client = eq2::zone::ClientId{42};
  zone.post_command(eq2::zone::ZoneCommand{
      .type = eq2::zone::ZoneCommand::Type::subscribe_client,
      .client = client,
  });
  zone.post_command(add_spawn_command(1, "guard", {.x = 1.0F}));

  const auto snapshot = zone.tick();
  const auto updates = zone.updates_for(client);

  require_eq(snapshot.subscribers.size(), static_cast<std::size_t>(1), "client subscription is stored");
  require_eq(updates.size(), static_cast<std::size_t>(1), "subscribed client receives update");
  require_eq(updates.front().type, eq2::zone::ZoneUpdateType::spawn_added,
             "client update reports spawn addition");

  zone.post_command(eq2::zone::ZoneCommand{
      .type = eq2::zone::ZoneCommand::Type::unsubscribe_client,
      .client = client,
  });
  zone.post_command(eq2::zone::ZoneCommand{
      .type = eq2::zone::ZoneCommand::Type::move_spawn,
      .spawn_id = eq2::zone::SpawnId{1},
      .position = {.x = 2.0F},
  });
  zone.tick();
  require(zone.updates_for(client).empty(), "unsubscribed client receives no fanout updates");
}

void commands_can_be_posted_from_non_owner_threads_without_mutating_directly() {
  eq2::zone::ZoneRuntime zone(eq2::zone::ZoneId{10});

  std::thread poster([&zone] {
    zone.post_command(add_spawn_command(1, "guard", {.x = 1.0F}));
  });
  poster.join();

  require(zone.snapshot().spawns.empty(), "cross-thread post does not mutate zone state directly");
  const auto snapshot = zone.tick();
  require_eq(snapshot.spawns.size(), static_cast<std::size_t>(1),
             "owner tick applies cross-thread posted command");
}

void db_bootstrap_admits_selected_character_and_produces_snapshot() {
  auto connection = std::make_shared<ZoneBootstrapConnection>();
  eq2::db::SqlZoneBootstrapRepository repository(connection);
  eq2::zone::ZoneBootstrapService bootstrap(repository);

  const auto result = bootstrap.admit_character(eq2::zone::ZoneAdmissionRequest{
      .account_id = 42,
      .character_id = 2002,
      .character_name = "Alys",
      .zone = eq2::zone::ZoneId{10},
      .access_key = 0x12345678,
      .client = eq2::zone::ClientId{42},
  });

  require(result.accepted, "zone bootstrap admits known character");
  require_eq(connection->requests.front().parameters.front(), std::string_view("10"),
             "zone bootstrap loads metadata through SQL repository");
  require_eq(result.snapshot.zone.value, 10, "zone snapshot carries DB zone id");
  require_eq(result.snapshot.spawns.size(), static_cast<std::size_t>(1),
             "zone admission creates player spawn");
  require_eq(result.snapshot.spawns.front().name, std::string_view("Alys"),
             "zone admission preserves selected character name");
  require_eq(result.snapshot.spawns.front().position.x, 1.5F,
             "zone admission starts at DB safe x");
  require_eq(result.snapshot.subscribers.size(), static_cast<std::size_t>(1),
             "zone admission subscribes admitted client");

  const auto metadata = bootstrap.metadata(eq2::zone::ZoneId{10});
  require(metadata.has_value(), "zone metadata remains available after bootstrap");
  require_eq(metadata->name, std::string_view("qeynos"), "zone metadata carries DB name");

  const auto missing = bootstrap.admit_character(eq2::zone::ZoneAdmissionRequest{
      .account_id = 42,
      .character_id = 2003,
      .zone = eq2::zone::ZoneId{99},
      .client = eq2::zone::ClientId{43},
  });
  require(!missing.accepted, "zone bootstrap rejects missing zone metadata");
}

void zone_admission_feature_validates_legacy_edge_inputs() {
  auto connection = std::make_shared<ZoneBootstrapConnection>();
  eq2::db::SqlZoneBootstrapRepository repository(connection);
  eq2::zone::ZoneBootstrapService bootstrap(repository);
  eq2::zone::ZoneAdmissionFeature feature(bootstrap);

  auto result = feature.admit(eq2::zone::ZoneAdmissionRequest{
      .account_id = 0,
      .character_id = 2002,
      .zone = eq2::zone::ZoneId{10},
      .access_key = 1,
      .client = eq2::zone::ClientId{42},
  });
  require(!result.admission.accepted, "zone admission rejects missing account id");

  result = feature.admit(eq2::zone::ZoneAdmissionRequest{
      .account_id = 42,
      .character_id = 0,
      .zone = eq2::zone::ZoneId{10},
      .access_key = 1,
      .client = eq2::zone::ClientId{42},
  });
  require(!result.admission.accepted, "zone admission rejects missing character id");

  result = feature.admit(eq2::zone::ZoneAdmissionRequest{
      .account_id = 42,
      .character_id = 2002,
      .zone = eq2::zone::ZoneId{10},
      .access_key = 0,
      .client = eq2::zone::ClientId{42},
  });
  require(!result.admission.accepted, "zone admission rejects missing access key");
}

void zone_admission_feature_calls_lua_hook_through_owner_sink() {
  eq2::scripting::LuaBackend backend;
  if (!backend.available()) {
    return;
  }

  auto connection = std::make_shared<ZoneBootstrapConnection>();
  eq2::db::SqlZoneBootstrapRepository repository(connection);
  eq2::zone::ZoneBootstrapService bootstrap(repository);
  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine scripts(backend, log);
  const auto script_id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::zone,
      .name = "zone_admission",
  };
  scripts.load_script(script_id,
                      "function player_entry() PostZoneMutation('admitted', ActorId()) end");

  eq2::zone::ZoneAdmissionFeature feature(bootstrap, scripts, sink, script_id, "player_entry");
  const auto result = feature.admit(eq2::zone::ZoneAdmissionRequest{
      .account_id = 42,
      .character_id = 2002,
      .character_name = "Alys",
      .zone = eq2::zone::ZoneId{10},
      .access_key = 0x12345678,
      .client = eq2::zone::ClientId{42},
  });

  require(result.admission.accepted, "zone admission feature admits valid character");
  require(result.script_called, "zone admission feature invokes Lua hook");
  require(!result.script_error.has_value(), "zone admission feature hook succeeds");
  require_eq(sink.mutations.size(), static_cast<std::size_t>(1),
             "zone admission Lua hook posts to owner sink");
  require_eq(sink.mutations.front().command, std::string_view("admitted"),
             "zone admission Lua hook posts expected command");
  require_eq(sink.mutations.front().ids.front(), 2002,
             "zone admission Lua hook receives admitted character id");
}

void zone_admission_feature_isolates_lua_hook_failure() {
  eq2::scripting::LuaBackend backend;
  if (!backend.available()) {
    return;
  }

  auto connection = std::make_shared<ZoneBootstrapConnection>();
  eq2::db::SqlZoneBootstrapRepository repository(connection);
  eq2::zone::ZoneBootstrapService bootstrap(repository);
  CapturingLogSink log;
  CapturingCommandSink sink;
  eq2::scripting::ScriptEngine scripts(backend, log);
  const auto script_id = eq2::scripting::ScriptId{
      .category = eq2::scripting::ScriptCategory::zone,
      .name = "zone_admission",
  };
  scripts.load_script(script_id, "function player_entry() error('hook failed') end");

  eq2::zone::ZoneAdmissionFeature feature(bootstrap, scripts, sink, script_id, "player_entry");
  const auto result = feature.admit(eq2::zone::ZoneAdmissionRequest{
      .account_id = 42,
      .character_id = 2002,
      .character_name = "Alys",
      .zone = eq2::zone::ZoneId{10},
      .access_key = 0x12345678,
      .client = eq2::zone::ClientId{42},
  });

  require(result.admission.accepted, "zone admission remains accepted when hook fails");
  require(result.script_error.has_value(), "zone admission records Lua hook failure");
  require(!log.records.empty(), "zone admission hook failure is logged");
}

}  // namespace

int main() {
  zone_commands_mutate_state_only_when_owner_ticks();
  movement_and_combat_are_command_boundaries();
  client_subscriptions_receive_zone_updates_from_snapshots();
  commands_can_be_posted_from_non_owner_threads_without_mutating_directly();
  db_bootstrap_admits_selected_character_and_produces_snapshot();
  zone_admission_feature_validates_legacy_edge_inputs();
  zone_admission_feature_calls_lua_hook_through_owner_sink();
  zone_admission_feature_isolates_lua_hook_failure();

  if (failures != 0) {
    std::cerr << failures << " zone runtime assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
