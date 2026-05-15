#include <eq2/zone/runtime.h>

#include <cstdlib>
#include <iostream>
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

}  // namespace

int main() {
  zone_commands_mutate_state_only_when_owner_ticks();
  movement_and_combat_are_command_boundaries();
  client_subscriptions_receive_zone_updates_from_snapshots();
  commands_can_be_posted_from_non_owner_threads_without_mutating_directly();

  if (failures != 0) {
    std::cerr << failures << " zone runtime assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
