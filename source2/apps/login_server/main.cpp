#include <eq2/core/config.h>
#include <eq2/db/fake_database.h>
#include <eq2/login/server.h>
#include <eq2/protocol/login_request.h>
#include <eq2/protocol/opcode_version.h>

#include <iostream>
#include <string_view>

namespace {

auto smoke_login() -> int {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  eq2::protocol::OpcodeVersionRanges versions;
  versions.add_range(546, 561);

  eq2::login::LoginServer server(eq2::login::LoginServerConfig{}, accounts, worlds, versions);
  server.start();
  const auto session = server.accept_client();
  if (!session.has_value()) {
    return 1;
  }

  const auto fixture = eq2::protocol::encode_legacy_login_request_fixture(eq2::protocol::LoginRequest{
      .access_code = "station",
      .username = "tester",
      .password = "correct",
      .version = 546,
  });
  const auto outcome = server.handle_login_request(*session, fixture);
  return outcome.status == eq2::login::LoginPacketStatus::accepted &&
                 outcome.reply_code == eq2::login::LoginReplyCode::accepted &&
                 outcome.account.has_value() && outcome.account->id == 42
             ? 0
             : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string_view(argv[1]) == "--smoke-login") {
    return smoke_login();
  }

  eq2::core::MapConfig config_values;
  const auto config = eq2::login::load_login_server_config(config_values);

  std::cout << "eq2_login_server source2 wiring "
            << config.address << ':' << config.port << '\n';
  return 0;
}
