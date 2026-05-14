#include <eq2/login/login_request.h>

#include <cstdlib>
#include <iostream>
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

void legacy_login_request_decodes_length_prefixed_credentials_and_version() {
  const auto fixture = eq2::login::encode_legacy_login_request_fixture(eq2::login::ParsedLoginRequest{
      .access_code = "station",
      .username = "tester",
      .password = "secret",
      .version = 546,
  });

  const auto parsed = eq2::login::parse_legacy_login_request(fixture);

  require(parsed.has_value(), "legacy login request parses");
  require_eq(parsed->access_code, std::string("station"), "access code field follows legacy order");
  require_eq(parsed->username, std::string("tester"), "username field follows legacy order");
  require_eq(parsed->password, std::string("secret"), "password field follows legacy order");
  require_eq(parsed->version, 546, "client version field follows the four trailing unknown strings");
}

void truncated_login_request_is_rejected_before_authentication() {
  auto fixture = eq2::login::encode_legacy_login_request_fixture(eq2::login::ParsedLoginRequest{
      .access_code = "station",
      .username = "tester",
      .password = "secret",
      .version = 546,
  });
  fixture.pop_back();

  const auto parsed = eq2::login::parse_legacy_login_request(fixture);

  require(!parsed.has_value(), "truncated legacy login request does not produce credentials");
}

}  // namespace

int main() {
  legacy_login_request_decodes_length_prefixed_credentials_and_version();
  truncated_login_request_is_rejected_before_authentication();

  if (failures != 0) {
    std::cerr << failures << " characterization assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
