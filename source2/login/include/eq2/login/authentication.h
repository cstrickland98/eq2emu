#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace eq2::login {

enum class LoginReplyCode : std::uint8_t {
  accepted = 0,
  invalid_username_or_password = 1,
  bad_client_version = 6,
};

struct LoginCredentials {
  std::string_view username;
  std::string_view password;
};

struct LoginAccount {
  std::int32_t id = 0;
  std::string name;
};

struct LoginAuthenticationRequest {
  LoginCredentials credentials;
  bool client_version_is_supported = false;
  bool account_creation_is_allowed = false;
  bool account_already_has_session = false;
};

struct LoginAuthenticationResult {
  LoginReplyCode reply_code = LoginReplyCode::invalid_username_or_password;
  std::optional<LoginAccount> account;
  bool should_disconnect_current_session = true;
  bool should_disconnect_existing_session = false;
  bool should_send_world_list_after_login = false;
};

template <typename AccountRepository>
auto authenticate_login(const LoginAuthenticationRequest& request, AccountRepository& accounts)
    -> LoginAuthenticationResult {
  if (!request.client_version_is_supported) {
    return LoginAuthenticationResult{
        .reply_code = LoginReplyCode::bad_client_version,
    };
  }

  auto account = accounts.find_by_name_and_password(request.credentials.username, request.credentials.password);

  if (!account && request.account_creation_is_allowed &&
      !accounts.account_name_exists(request.credentials.username)) {
    account = accounts.create_account(request.credentials.username, request.credentials.password);
  }

  if (!account) {
    return LoginAuthenticationResult{
        .reply_code = LoginReplyCode::invalid_username_or_password,
    };
  }

  return LoginAuthenticationResult{
      .reply_code = LoginReplyCode::accepted,
      .account = account,
      .should_disconnect_current_session = false,
      .should_disconnect_existing_session = request.account_already_has_session,
      .should_send_world_list_after_login = true,
  };
}

}  // namespace eq2::login
