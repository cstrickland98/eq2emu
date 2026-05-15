#pragma once

#include <array>
#include <string_view>

namespace eq2::world {

enum class GameplaySystem {
  spawn_lifecycle,
  inventory,
  items,
  quests,
  spells,
  combat,
  tradeskills,
  guilds,
  broker,
  housing,
  achievements,
  collections,
};

enum class GameplayOwner {
  zone_executor,
  player_session,
  world_service,
};

struct GameplayMigration {
  GameplaySystem system = GameplaySystem::spawn_lifecycle;
  GameplayOwner owner = GameplayOwner::world_service;
  std::string_view interface_namespace;
  std::string_view current_behavior_note;
  std::string_view legacy_compatibility_note;
  std::string_view verification;
};

inline constexpr auto kGameplayMigrationCount = 12U;

inline constexpr auto gameplay_migrations() {
  return std::array<GameplayMigration, kGameplayMigrationCount>{
      GameplayMigration{
          .system = GameplaySystem::spawn_lifecycle,
          .owner = GameplayOwner::zone_executor,
          .interface_namespace = "eq2::zone",
          .current_behavior_note = "Legacy spawn containers are mutated from zone, client, and script paths.",
          .legacy_compatibility_note = "Source2 currently models spawn add/move/remove boundaries; detailed spawn serialization remains legacy.",
          .verification = "eq2_zone_runtime_tests",
      },
      GameplayMigration{
          .system = GameplaySystem::inventory,
          .owner = GameplayOwner::player_session,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy inventory mutates player item state from client, item, quest, and Lua paths.",
          .legacy_compatibility_note = "Source2 owner is defined; item persistence and bag rules still need parity migration.",
          .verification = "owner-registry test",
      },
      GameplayMigration{
          .system = GameplaySystem::items,
          .owner = GameplayOwner::player_session,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy item behavior mixes item data, equip rules, script calls, and DB persistence.",
          .legacy_compatibility_note = "Source2 scripting has item events; detailed item stat/equip behavior remains legacy.",
          .verification = "eq2_scripting_tests",
      },
      GameplayMigration{
          .system = GameplaySystem::quests,
          .owner = GameplayOwner::player_session,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy quest state is player-owned but script calls can mutate world and zone objects.",
          .legacy_compatibility_note = "Source2 scripting has quest events; quest journal/reward parity remains legacy.",
          .verification = "eq2_scripting_tests",
      },
      GameplayMigration{
          .system = GameplaySystem::spells,
          .owner = GameplayOwner::zone_executor,
          .interface_namespace = "eq2::zone",
          .current_behavior_note = "Legacy spell effects can mutate combat, player, and spawn state from script paths.",
          .legacy_compatibility_note = "Source2 scripting has spell events; spell effect formulas remain legacy.",
          .verification = "eq2_scripting_tests",
      },
      GameplayMigration{
          .system = GameplaySystem::combat,
          .owner = GameplayOwner::zone_executor,
          .interface_namespace = "eq2::zone",
          .current_behavior_note = "Legacy combat mutates spawn HP, hate, procs, and script state inside zone paths.",
          .legacy_compatibility_note = "Source2 has a combat command boundary; detailed combat formulas remain legacy.",
          .verification = "eq2_zone_runtime_tests",
      },
      GameplayMigration{
          .system = GameplaySystem::tradeskills,
          .owner = GameplayOwner::player_session,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy tradeskills mutate player recipes, XP, items, and counters.",
          .legacy_compatibility_note = "Owner is defined; recipe progression and crafting events remain legacy.",
          .verification = "owner-registry test",
      },
      GameplayMigration{
          .system = GameplaySystem::guilds,
          .owner = GameplayOwner::world_service,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy guild data is shared world state with player-facing mutations.",
          .legacy_compatibility_note = "Owner is defined; guild roster, ranks, and persistence remain legacy.",
          .verification = "owner-registry test",
      },
      GameplayMigration{
          .system = GameplaySystem::broker,
          .owner = GameplayOwner::world_service,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy broker state is loaded at world startup and mutated by player transactions.",
          .legacy_compatibility_note = "Owner is defined; broker search, sale, and escrow behavior remain legacy.",
          .verification = "owner-registry test",
      },
      GameplayMigration{
          .system = GameplaySystem::housing,
          .owner = GameplayOwner::world_service,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy housing spans world metadata, instance selection, and zone entry.",
          .legacy_compatibility_note = "Owner is defined; house access and instance bootstrap remain legacy.",
          .verification = "owner-registry test",
      },
      GameplayMigration{
          .system = GameplaySystem::achievements,
          .owner = GameplayOwner::player_session,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy achievements mutate player progress and persistence.",
          .legacy_compatibility_note = "Owner is defined; achievement criteria and reward parity remain legacy.",
          .verification = "owner-registry test",
      },
      GameplayMigration{
          .system = GameplaySystem::collections,
          .owner = GameplayOwner::player_session,
          .interface_namespace = "eq2::world",
          .current_behavior_note = "Legacy collections mutate player collection progress, rewards, and item state.",
          .legacy_compatibility_note = "Owner is defined; collection completion behavior remains legacy.",
          .verification = "owner-registry test",
      },
  };
}

}  // namespace eq2::world
