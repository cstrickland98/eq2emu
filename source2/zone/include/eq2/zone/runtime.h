#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <eq2/core/blocking_queue.h>

namespace eq2::zone {

struct ZoneId {
  std::int32_t value = 0;

  friend auto operator==(ZoneId lhs, ZoneId rhs) -> bool = default;
};

struct SpawnId {
  std::int32_t value = 0;

  friend auto operator==(SpawnId lhs, SpawnId rhs) -> bool = default;
  friend auto operator<(SpawnId lhs, SpawnId rhs) -> bool {
    return lhs.value < rhs.value;
  }
};

struct ClientId {
  std::uint64_t value = 0;

  friend auto operator==(ClientId lhs, ClientId rhs) -> bool = default;
};

struct Position {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
  float heading = 0.0F;

  friend auto operator==(const Position& lhs, const Position& rhs) -> bool = default;
};

struct SpawnState {
  SpawnId id;
  std::string name;
  Position position;
  std::int32_t hit_points = 1;
};

struct CombatEvent {
  SpawnId source;
  SpawnId target;
  std::int32_t damage = 0;
};

enum class ZoneUpdateType {
  spawn_added,
  spawn_moved,
  spawn_removed,
  combat_resolved,
};

struct ZoneUpdate {
  ZoneUpdateType type = ZoneUpdateType::spawn_added;
  SpawnId spawn;
  Position position;
  std::int32_t value = 0;
};

struct ZoneSnapshot {
  ZoneId zone;
  std::uint64_t tick = 0;
  std::vector<SpawnState> spawns;
  std::vector<ClientId> subscribers;
};

struct ZoneCommand {
  enum class Type {
    add_spawn,
    move_spawn,
    remove_spawn,
    combat_event,
    subscribe_client,
    unsubscribe_client,
  };

  Type type = Type::add_spawn;
  SpawnState spawn;
  SpawnId spawn_id;
  Position position;
  CombatEvent combat;
  ClientId client;
};

class ZoneRuntime {
 public:
  explicit ZoneRuntime(ZoneId zone) : zone_(zone) {
    refresh_snapshot();
  }

  auto post_command(ZoneCommand command) -> bool {
    return commands_.push(std::move(command));
  }

  auto tick() -> ZoneSnapshot {
    if (!bind_owner_thread()) {
      return snapshot();
    }
    last_updates_.clear();

    while (auto command = commands_.try_pop()) {
      apply(*command);
    }

    ++tick_;
    refresh_snapshot();
    return snapshot();
  }

  [[nodiscard]] auto snapshot() const -> ZoneSnapshot {
    std::lock_guard lock(snapshot_mutex_);
    return snapshot_;
  }

  [[nodiscard]] auto updates_for(ClientId client) const -> std::vector<ZoneUpdate> {
    if (!is_subscribed(client)) {
      return {};
    }

    return last_updates_;
  }

  [[nodiscard]] auto owner_thread() const -> std::optional<std::thread::id> {
    return owner_thread_;
  }

 private:
  auto bind_owner_thread() -> bool {
    const auto current = std::this_thread::get_id();
    if (!owner_thread_.has_value()) {
      owner_thread_ = current;
      return true;
    }

    return *owner_thread_ == current;
  }

  [[nodiscard]] auto is_subscribed(ClientId client) const -> bool {
    return std::find(subscribers_.begin(), subscribers_.end(), client) != subscribers_.end();
  }

  void apply(const ZoneCommand& command) {
    switch (command.type) {
      case ZoneCommand::Type::add_spawn:
        spawns_[command.spawn.id] = command.spawn;
        last_updates_.push_back(ZoneUpdate{
            .type = ZoneUpdateType::spawn_added,
            .spawn = command.spawn.id,
            .position = command.spawn.position,
        });
        break;
      case ZoneCommand::Type::move_spawn:
        if (auto iter = spawns_.find(command.spawn_id); iter != spawns_.end()) {
          iter->second.position = command.position;
          last_updates_.push_back(ZoneUpdate{
              .type = ZoneUpdateType::spawn_moved,
              .spawn = command.spawn_id,
              .position = command.position,
          });
        }
        break;
      case ZoneCommand::Type::remove_spawn:
        spawns_.erase(command.spawn_id);
        last_updates_.push_back(ZoneUpdate{
            .type = ZoneUpdateType::spawn_removed,
            .spawn = command.spawn_id,
        });
        break;
      case ZoneCommand::Type::combat_event:
        if (auto iter = spawns_.find(command.combat.target); iter != spawns_.end()) {
          iter->second.hit_points = std::max(0, iter->second.hit_points - command.combat.damage);
          last_updates_.push_back(ZoneUpdate{
              .type = ZoneUpdateType::combat_resolved,
              .spawn = command.combat.target,
              .position = iter->second.position,
              .value = command.combat.damage,
          });
        }
        break;
      case ZoneCommand::Type::subscribe_client:
        if (!is_subscribed(command.client)) {
          subscribers_.push_back(command.client);
        }
        break;
      case ZoneCommand::Type::unsubscribe_client:
        subscribers_.erase(std::remove(subscribers_.begin(), subscribers_.end(), command.client),
                           subscribers_.end());
        break;
    }
  }

  void refresh_snapshot() {
    ZoneSnapshot next{
        .zone = zone_,
        .tick = tick_,
        .subscribers = subscribers_,
    };
    for (const auto& [_, spawn] : spawns_) {
      next.spawns.push_back(spawn);
    }

    std::lock_guard lock(snapshot_mutex_);
    snapshot_ = std::move(next);
  }

  ZoneId zone_;
  eq2::core::BlockingQueue<ZoneCommand> commands_;
  std::map<SpawnId, SpawnState> spawns_;
  std::vector<ClientId> subscribers_;
  std::vector<ZoneUpdate> last_updates_;
  std::uint64_t tick_ = 0;
  std::optional<std::thread::id> owner_thread_;
  mutable std::mutex snapshot_mutex_;
  ZoneSnapshot snapshot_;
};

}  // namespace eq2::zone
