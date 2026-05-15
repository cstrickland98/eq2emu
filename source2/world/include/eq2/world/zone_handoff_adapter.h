#pragma once

#include <cstdint>
#include <string>

#include <eq2/world/session.h>
#include <eq2/zone/bootstrap.h>

namespace eq2::world {

class ZoneRuntimeHandoff final : public ZoneHandoff {
 public:
  explicit ZoneRuntimeHandoff(eq2::zone::ZoneBootstrapService& zones) : zones_(zones) {}

  auto request_zone_entry(const ZoneHandoffRequest& request) -> ZoneHandoffResult override {
    auto result = zones_.admit_character(eq2::zone::ZoneAdmissionRequest{
        .account_id = request.account_id,
        .character_id = request.character_id,
        .zone = eq2::zone::ZoneId{request.zone_id},
        .access_key = request.access_key,
        .client = eq2::zone::ClientId{static_cast<std::uint64_t>(request.account_id)},
    });

    return ZoneHandoffResult{
        .accepted = result.accepted,
        .reason = result.reason,
    };
  }

 private:
  eq2::zone::ZoneBootstrapService& zones_;
};

}  // namespace eq2::world
