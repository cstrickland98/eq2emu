#include <eq2/world/gameplay_systems.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
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

void every_planned_gameplay_group_has_an_owner_and_notes() {
  const auto migrations = eq2::world::gameplay_migrations();
  require_eq(migrations.size(), static_cast<std::size_t>(eq2::world::kGameplayMigrationCount),
             "gameplay registry has the expected number of systems");

  for (const auto& migration : migrations) {
    require(!migration.interface_namespace.empty(), "gameplay migration has interface placement");
    require(!migration.current_behavior_note.empty(), "gameplay migration has current behavior note");
    require(!migration.legacy_compatibility_note.empty(),
            "gameplay migration has compatibility note");
    require(!migration.verification.empty(), "gameplay migration has verification note");
  }
}

void gameplay_owner_decisions_match_source2_boundaries() {
  const auto migrations = eq2::world::gameplay_migrations();
  const auto find = [&migrations](eq2::world::GameplaySystem system) {
    return std::find_if(migrations.begin(), migrations.end(), [system](const auto& migration) {
      return migration.system == system;
    });
  };

  require_eq(find(eq2::world::GameplaySystem::spawn_lifecycle)->owner,
             eq2::world::GameplayOwner::zone_executor,
             "spawn lifecycle is owned by zone executor");
  require_eq(find(eq2::world::GameplaySystem::combat)->owner,
             eq2::world::GameplayOwner::zone_executor,
             "combat is owned by zone executor");
  require_eq(find(eq2::world::GameplaySystem::inventory)->owner,
             eq2::world::GameplayOwner::player_session,
             "inventory is owned by player session");
  require_eq(find(eq2::world::GameplaySystem::quests)->owner,
             eq2::world::GameplayOwner::player_session,
             "quests are owned by player session");
  require_eq(find(eq2::world::GameplaySystem::broker)->owner,
             eq2::world::GameplayOwner::world_service,
             "broker is owned by world service");
  require_eq(find(eq2::world::GameplaySystem::guilds)->owner,
             eq2::world::GameplayOwner::world_service,
             "guilds are owned by world service");
}

void gameplay_registry_has_unique_system_entries() {
  const auto migrations = eq2::world::gameplay_migrations();
  auto systems = std::vector<eq2::world::GameplaySystem>{};
  for (const auto& migration : migrations) {
    systems.push_back(migration.system);
  }

  std::sort(systems.begin(), systems.end());
  const auto duplicate = std::adjacent_find(systems.begin(), systems.end());
  require(duplicate == systems.end(), "gameplay migration registry does not duplicate systems");
}

}  // namespace

int main() {
  every_planned_gameplay_group_has_an_owner_and_notes();
  gameplay_owner_decisions_match_source2_boundaries();
  gameplay_registry_has_unique_system_entries();

  if (failures != 0) {
    std::cerr << failures << " gameplay registry assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
