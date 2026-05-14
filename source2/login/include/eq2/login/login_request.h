#pragma once

#include <eq2/protocol/login_request.h>

namespace eq2::login {

using ParsedLoginRequest = eq2::protocol::LoginRequest;

using eq2::protocol::encode_legacy_login_request_fixture;
using eq2::protocol::parse_legacy_login_request;

}  // namespace eq2::login
