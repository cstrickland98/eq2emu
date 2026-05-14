#pragma once

#include <cstdint>

#include <eq2/protocol/opcode_version.h>

namespace eq2::login {

inline constexpr std::int16_t kLegacyLoginRequestStructVersion = 1;
inline constexpr std::int16_t kExtendedLoginRequestStructVersion = 1208;

inline auto login_request_struct_version(std::int16_t classic_parsed_version,
                                         const eq2::protocol::OpcodeVersionRanges& opcode_versions)
    -> std::int16_t {
  if (classic_parsed_version == 0 || !opcode_versions.contains_client_version(classic_parsed_version)) {
    return kExtendedLoginRequestStructVersion;
  }

  return kLegacyLoginRequestStructVersion;
}

inline auto world_login_request_struct_version(std::int16_t classic_parsed_version,
                                               const eq2::protocol::OpcodeVersionRanges& opcode_versions)
    -> std::int16_t {
  if (classic_parsed_version == 0 || classic_parsed_version >= kExtendedLoginRequestStructVersion ||
      !opcode_versions.contains_client_version(classic_parsed_version)) {
    return kExtendedLoginRequestStructVersion;
  }

  return kLegacyLoginRequestStructVersion;
}

}  // namespace eq2::login
