#pragma once

#include <cstdint>
#include <string>

namespace eq2::db {

struct DatabaseConfig {
  std::string host = "127.0.0.1";
  std::uint16_t port = 3306;
  std::string database;
  std::string username;
  std::string password;
  std::uint16_t max_connections = 4;
};

}  // namespace eq2::db
