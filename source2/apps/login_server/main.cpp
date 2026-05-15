#include <eq2/core/config.h>
#include <eq2/core/log.h>
#include <eq2/core/runtime_config.h>
#include <eq2/core/version.h>
#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>
#include <eq2/login/live_login.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/opcode_version.h>
#include <eq2/protocol/protocol_packet.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class SmokeLoginConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    if (request.sql.find("select id, name from account") != std::string::npos &&
        request.parameters.size() >= 2 && request.parameters[0] == "tester" &&
        request.parameters[1] == "correct") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows =
              {
                  eq2::db::QueryRow{.columns = {{"id", "42"}, {"name", "tester"}}},
              },
          .affected_rows = 1,
      });
    }

    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
  }
};

auto supported_login_versions() -> eq2::protocol::OpcodeVersionRanges {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  return ranges;
}

auto make_smoke_login_frame() -> std::vector<std::uint8_t> {
  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = "tester",
          .password = "correct",
          .version = 546,
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      eq2::login::kLoginRequestAppOpcode, login_payload);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, app_packet);
}

auto wait_for_login_count(const eq2::login::LiveLoginService& service, std::size_t count) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    if (service.login_outcomes().size() >= count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

auto run_live_smoke() -> int {
  auto connection = std::make_shared<SmokeLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  if (!service.start()) {
    std::cerr << "failed to start live login smoke transport\n";
    return EXIT_FAILURE;
  }

  const auto sent = eq2::net::send_tcp_loopback(service.port(), make_smoke_login_frame());
  if (!sent || !wait_for_login_count(service, 1)) {
    service.stop();
    std::cerr << "live login smoke did not complete\n";
    return EXIT_FAILURE;
  }

  const auto outcomes = service.login_outcomes();
  service.stop();

  if (outcomes.empty() || outcomes.front().reply_code != eq2::login::LoginReplyCode::accepted ||
      !outcomes.front().account.has_value()) {
    std::cerr << "live login smoke returned unexpected outcome\n";
    return EXIT_FAILURE;
  }

  std::cout << "smoke-login-live accepted account=" << outcomes.front().account->id << '\n';
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--smoke-login-live") {
    return run_live_smoke();
  }

  eq2::core::MapConfig config_values;
  if (argc > 1 && std::string_view(argv[1]) == "--strict-config") {
    config_values.set("login.port", "invalid");
  }

  auto runtime = eq2::core::load_runtime_config(config_values);
  if (!runtime.has_value()) {
    std::cerr << runtime.error().message << '\n';
    return 1;
  }

  eq2::core::ConsoleLogSink log;
  eq2::core::log(log, eq2::core::LogLevel::info, "login",
                 "source2 login composition root validated");

  std::cout << "eq2_login_server source2 wiring "
            << "version=" << eq2::core::kSource2Version << ' '
            << runtime.value().login_listen.address << ':' << runtime.value().login_listen.port << '\n';
  return 0;
}
