#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <eq2/db/repositories.h>
#include <eq2/zone/runtime.h>

namespace eq2::zone {

struct ZoneRuntimeConfig {
  std::uint32_t tick_milliseconds = 50;
};

struct ZoneMetadata {
  ZoneId id;
  std::string name;
  Position safe_position;
};

struct ZoneAdmissionRequest {
  std::int32_t account_id = 0;
  std::int32_t character_id = 0;
  std::string character_name;
  ZoneId zone;
  std::int32_t access_key = 0;
  ClientId client;
};

struct ZoneAdmissionResult {
  bool accepted = false;
  std::string reason;
  ZoneSnapshot snapshot;
};

class ZoneBootstrapService {
 public:
  explicit ZoneBootstrapService(eq2::db::ZoneBootstrapRepository& zones,
                                ZoneRuntimeConfig config = {})
      : zones_(zones), config_(config) {}

  auto bootstrap_zone(ZoneId zone) -> std::optional<ZoneMetadata> {
    std::lock_guard lock(mutex_);
    if (const auto existing = metadata_.find(zone.value); existing != metadata_.end()) {
      return existing->second;
    }

    const auto record = zones_.load_zone(zone.value);
    if (!record.has_value()) {
      return std::nullopt;
    }

    auto metadata = ZoneMetadata{
        .id = ZoneId{record->zone_id},
        .name = record->name,
        .safe_position =
            Position{
                .x = record->safe_x,
                .y = record->safe_y,
                .z = record->safe_z,
                .heading = record->safe_heading,
            },
    };
    runtimes_.emplace(metadata.id.value, std::make_unique<ZoneRuntime>(metadata.id));
    metadata_.emplace(metadata.id.value, metadata);
    return metadata;
  }

  auto admit_character(const ZoneAdmissionRequest& request) -> ZoneAdmissionResult {
    auto metadata = bootstrap_zone(request.zone);
    if (!metadata.has_value()) {
      return ZoneAdmissionResult{
          .accepted = false,
          .reason = "zone metadata not found",
      };
    }

    std::lock_guard lock(mutex_);
    auto* runtime = runtime_for_locked(metadata->id);
    if (runtime == nullptr) {
      return ZoneAdmissionResult{
          .accepted = false,
          .reason = "zone runtime not available",
      };
    }

    runtime->post_command(ZoneCommand{
        .type = ZoneCommand::Type::subscribe_client,
        .client = request.client,
    });
    runtime->post_command(ZoneCommand{
        .type = ZoneCommand::Type::add_spawn,
        .spawn =
            SpawnState{
                .id = SpawnId{request.character_id},
                .name = request.character_name.empty()
                            ? "character-" + std::to_string(request.character_id)
                            : request.character_name,
                .position = metadata->safe_position,
                .hit_points = 100,
            },
    });

    return ZoneAdmissionResult{
        .accepted = true,
        .snapshot = runtime->tick(),
    };
  }

  [[nodiscard]] auto snapshot(ZoneId zone) const -> std::optional<ZoneSnapshot> {
    std::lock_guard lock(mutex_);
    const auto iter = runtimes_.find(zone.value);
    if (iter == runtimes_.end()) {
      return std::nullopt;
    }
    return iter->second->snapshot();
  }

  [[nodiscard]] auto metadata(ZoneId zone) const -> std::optional<ZoneMetadata> {
    std::lock_guard lock(mutex_);
    const auto iter = metadata_.find(zone.value);
    if (iter == metadata_.end()) {
      return std::nullopt;
    }
    return iter->second;
  }

  [[nodiscard]] auto config() const -> const ZoneRuntimeConfig& {
    return config_;
  }

 private:
  auto runtime_for_locked(ZoneId zone) -> ZoneRuntime* {
    const auto iter = runtimes_.find(zone.value);
    if (iter == runtimes_.end()) {
      return nullptr;
    }
    return iter->second.get();
  }

  eq2::db::ZoneBootstrapRepository& zones_;
  ZoneRuntimeConfig config_;
  mutable std::mutex mutex_;
  std::map<std::int32_t, std::unique_ptr<ZoneRuntime>> runtimes_;
  std::map<std::int32_t, ZoneMetadata> metadata_;
};

}  // namespace eq2::zone
