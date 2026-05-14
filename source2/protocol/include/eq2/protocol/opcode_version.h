#pragma once

#include <cstdint>
#include <map>

namespace eq2::protocol {

class OpcodeVersionRanges {
 public:
  void add_range(std::int16_t min_version, std::int16_t max_version) {
    ranges_[min_version] = max_version;
  }

  [[nodiscard]] auto opcode_version_for(std::int16_t client_version) const -> std::int16_t {
    for (const auto& [min_version, max_version] : ranges_) {
      if (client_version >= min_version && client_version <= max_version) {
        return min_version;
      }
    }

    return client_version;
  }

  [[nodiscard]] auto contains_client_version(std::int16_t client_version) const -> bool {
    return opcode_version_for(client_version) != client_version || ranges_.contains(client_version);
  }

 private:
  std::map<std::int16_t, std::int16_t> ranges_;
};

}  // namespace eq2::protocol
