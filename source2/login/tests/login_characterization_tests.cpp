#include <eq2/login/authentication.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

auto failures = 0;

void require(bool condition, std::string_view message) {
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

template <typename T, typename U>
void require_eq(const T& actual, const U& expected, std::string_view message) {
  if (!(actual == expected)) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

struct FakeStoredAccount {
  std::int32_t id = 0;
  std::string username;
  std::string password;
};

class FakeAccountRepository {
 public:
  void add_account(std::int32_t id, std::string username, std::string password) {
    accounts_.push_back(FakeStoredAccount{
        .id = id,
        .username = std::move(username),
        .password = std::move(password),
    });
  }

  auto find_by_name_and_password(std::string_view username, std::string_view password)
      -> std::optional<eq2::login::LoginAccount> {
    for (const auto& account : accounts_) {
      if (account.username == username && account.password == password) {
        return eq2::login::LoginAccount{
            .id = account.id,
            .name = account.username,
        };
      }
    }

    return std::nullopt;
  }

  [[nodiscard]] auto account_name_exists(std::string_view username) const -> bool {
    for (const auto& account : accounts_) {
      if (account.username == username) {
        return true;
      }
    }

    return false;
  }

  auto create_account(std::string_view username, std::string_view password)
      -> std::optional<eq2::login::LoginAccount> {
    const auto id = next_account_id_++;
    add_account(id, std::string(username), std::string(password));

    return eq2::login::LoginAccount{
        .id = id,
        .name = std::string(username),
    };
  }

 private:
  std::int32_t next_account_id_ = 1000;
  std::vector<FakeStoredAccount> accounts_;
};

auto make_request(std::string_view username, std::string_view password)
    -> eq2::login::LoginAuthenticationRequest {
  return eq2::login::LoginAuthenticationRequest{
      .credentials =
          eq2::login::LoginCredentials{
              .username = username,
              .password = password,
          },
      .client_version_is_supported = true,
      .account_creation_is_allowed = false,
      .account_already_has_session = false,
  };
}

void known_account_with_correct_password_is_accepted() {
  FakeAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");

  const auto result = eq2::login::authenticate_login(make_request("tester", "correct"), accounts);

  require_eq(result.reply_code, eq2::login::LoginReplyCode::accepted,
             "known account with matching password is accepted");
  require(result.account.has_value(), "accepted login carries the account");
  require_eq(result.account->id, 42, "accepted login carries the account id");
  require(!result.should_disconnect_current_session, "accepted login keeps current session open");
  require(result.should_send_world_list_after_login, "accepted login marks world list for send");
}

void known_account_with_wrong_password_is_denied() {
  FakeAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");

  const auto result = eq2::login::authenticate_login(make_request("tester", "wrong"), accounts);

  require_eq(result.reply_code, eq2::login::LoginReplyCode::invalid_username_or_password,
             "wrong password returns legacy reply code 1");
  require(!result.account.has_value(), "denied login does not carry an account");
  require(result.should_disconnect_current_session, "denied login starts current-session disconnect");
}

void unsupported_client_version_is_denied_before_account_lookup() {
  FakeAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");

  auto request = make_request("tester", "correct");
  request.client_version_is_supported = false;

  const auto result = eq2::login::authenticate_login(request, accounts);

  require_eq(result.reply_code, eq2::login::LoginReplyCode::bad_client_version,
             "unsupported version returns legacy reply code 6");
  require(!result.account.has_value(), "bad version does not carry an account");
  require(result.should_disconnect_current_session, "bad version starts current-session disconnect");
}

void missing_account_is_created_only_when_creation_is_enabled() {
  FakeAccountRepository accounts;

  auto denied_request = make_request("new-account", "secret");
  denied_request.account_creation_is_allowed = false;
  const auto denied = eq2::login::authenticate_login(denied_request, accounts);

  require_eq(denied.reply_code, eq2::login::LoginReplyCode::invalid_username_or_password,
             "missing account is denied when account creation is disabled");

  auto created_request = make_request("new-account", "secret");
  created_request.account_creation_is_allowed = true;
  const auto created = eq2::login::authenticate_login(created_request, accounts);

  require_eq(created.reply_code, eq2::login::LoginReplyCode::accepted,
             "missing account is created when account creation is enabled");
  require(created.account.has_value(), "created account is returned as authenticated");
  require_eq(created.account->id, 1000, "created account receives repository id");
}

void successful_duplicate_login_disconnects_existing_session() {
  FakeAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");

  auto request = make_request("tester", "correct");
  request.account_already_has_session = true;

  const auto result = eq2::login::authenticate_login(request, accounts);

  require_eq(result.reply_code, eq2::login::LoginReplyCode::accepted,
             "duplicate login is still accepted for the new connection");
  require(result.should_disconnect_existing_session,
          "duplicate login requests disconnect for the previous connection");
}

}  // namespace

int main() {
  known_account_with_correct_password_is_accepted();
  known_account_with_wrong_password_is_denied();
  unsupported_client_version_is_denied_before_account_lookup();
  missing_account_is_created_only_when_creation_is_enabled();
  successful_duplicate_login_disconnects_existing_session();

  if (failures != 0) {
    std::cerr << failures << " characterization assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
