#pragma once

#include <optional>
#include <string>
#include <utility>

#include <eq2/core/result.h>
#include <eq2/scripting/engine.h>
#include <eq2/zone/bootstrap.h>

namespace eq2::zone {

struct ZoneAdmissionFeatureResult {
  ZoneAdmissionResult admission;
  bool script_called = false;
  std::optional<eq2::core::Error> script_error;
};

class ZoneAdmissionFeature {
 public:
  explicit ZoneAdmissionFeature(ZoneBootstrapService& zones) : zones_(zones) {}

  ZoneAdmissionFeature(ZoneBootstrapService& zones,
                       eq2::scripting::ScriptEngine& scripts,
                       eq2::scripting::OwnerCommandSink& script_sink,
                       eq2::scripting::ScriptId script_id,
                       std::string script_function)
      : zones_(zones),
        scripts_(&scripts),
        script_sink_(&script_sink),
        script_id_(std::move(script_id)),
        script_function_(std::move(script_function)) {}

  auto admit(ZoneAdmissionRequest request) -> ZoneAdmissionFeatureResult {
    if (request.account_id <= 0) {
      return rejected("invalid account id");
    }
    if (request.character_id <= 0) {
      return rejected("invalid character id");
    }
    if (request.access_key == 0) {
      return rejected("invalid access key");
    }
    if (request.zone.value <= 0) {
      return rejected("invalid zone id");
    }

    auto result = ZoneAdmissionFeatureResult{
        .admission = zones_.admit_character(request),
    };

    if (result.admission.accepted && scripts_ != nullptr && script_sink_ != nullptr) {
      result.script_called = true;
      auto script_result = scripts_->call_event(
          script_id_,
          eq2::scripting::zone_event(script_function_, request.zone.value, request.character_id),
          *script_sink_);
      if (!script_result.has_value()) {
        result.script_error = script_result.error();
      }
    }

    return result;
  }

 private:
  static auto rejected(std::string reason) -> ZoneAdmissionFeatureResult {
    return ZoneAdmissionFeatureResult{
        .admission =
            ZoneAdmissionResult{
                .accepted = false,
                .reason = std::move(reason),
            },
    };
  }

  ZoneBootstrapService& zones_;
  eq2::scripting::ScriptEngine* scripts_ = nullptr;
  eq2::scripting::OwnerCommandSink* script_sink_ = nullptr;
  eq2::scripting::ScriptId script_id_;
  std::string script_function_;
};

}  // namespace eq2::zone
