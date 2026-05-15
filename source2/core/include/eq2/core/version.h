#pragma once

#include <string_view>

#ifndef EQ2_SOURCE2_VERSION
#define EQ2_SOURCE2_VERSION "0.1.0"
#endif

namespace eq2::core {

inline constexpr std::string_view kSource2Version = EQ2_SOURCE2_VERSION;

}  // namespace eq2::core
