#include <eq2/core/config.h>
#include <eq2/core/endian.h>
#include <eq2/db/fake_database.h>
#include <eq2/db/query.h>
#include <eq2/db/sql_repositories.h>
#include <eq2/login/live_login.h>
#include <eq2/login/server.h>
#include <eq2/protocol/application_packet.h>
#include <eq2/protocol/combined_packet.h>
#include <eq2/protocol/crc.h>
#include <eq2/protocol/create_character.h>
#include <eq2/protocol/delete_character.h>
#include <eq2/protocol/interserver_packet.h>
#include <eq2/protocol/packet_buffer.h>
#include <eq2/protocol/play_character.h>
#include <eq2/protocol/protocol_packet.h>
#include <eq2/protocol/session.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
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

auto supported_login_versions() -> eq2::protocol::OpcodeVersionRanges {
  eq2::protocol::OpcodeVersionRanges ranges;
  ranges.add_range(546, 561);
  return ranges;
}

class TestLegacyRc4 {
 public:
  explicit TestLegacyRc4(std::uint64_t key) {
    for (std::size_t i = 0; i < state_.size(); ++i) {
      state_[i] = static_cast<std::uint8_t>(i);
    }

    auto key_bytes = std::array<std::uint8_t, 8>{};
    for (std::size_t i = 0; i < key_bytes.size(); ++i) {
      key_bytes[i] = static_cast<std::uint8_t>((key >> (i * 8U)) & 0xffU);
    }

    std::size_t key_index = 0;
    std::size_t state_index = 0;
    for (std::size_t i = 0; i < state_.size(); ++i) {
      const auto temp = state_[i];
      state_index = (state_index + key_bytes[key_index] + temp) & 0xffU;
      state_[i] = state_[state_index];
      state_[state_index] = temp;
      key_index = (key_index + 1U) & 7U;
    }
  }

  void cypher(std::span<std::uint8_t> bytes) {
    for (auto& byte : bytes) {
      ++x_;
      const auto key_val_1 = state_[x_];
      y_ = static_cast<std::uint8_t>(y_ + key_val_1);
      const auto key_val_2 = state_[y_];
      state_[x_] = key_val_2;
      state_[y_] = key_val_1;
      byte ^= state_[(key_val_1 + key_val_2) & 0xffU];
    }
  }

 private:
  std::array<std::uint8_t, 256> state_{};
  std::uint8_t x_ = 0;
  std::uint8_t y_ = 0;
};

class ScriptedLoginConnection final : public eq2::db::QueryConnection {
 public:
  auto execute(const eq2::db::QueryRequest& request)
      -> eq2::core::Result<eq2::db::QueryResult> override {
    requests.push_back(request);

    if (request.sql.find("select id, name from account") != std::string::npos &&
        request.parameters.size() >= 2) {
      const auto& username = request.parameters[0];
      const auto& password = request.parameters[1];
      if (username == "tester" && password == "correct") {
        return row(42, "tester");
      }
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    if (request.sql.find("select id from account") != std::string::npos &&
        !request.parameters.empty()) {
      if (existing_accounts_contains(request.parameters.front())) {
        return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
            .rows = {eq2::db::QueryRow{.columns = {{"id", "42"}}}},
        });
      }
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    if (request.sql.find("insert into account") != std::string::npos &&
        !request.parameters.empty()) {
      created_accounts.push_back(request.parameters.front());
      return row(88, request.parameters.front());
    }

    if (request.sql.find("select version from login_versions") != std::string::npos) {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"version", "*"}}}},
      });
    }

    if (request.sql.find("select id from login_worldservers") != std::string::npos &&
        request.parameters.size() >= 2 && request.parameters[0] == "world-account" &&
        request.parameters[1] == "secret") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"id", "77"}}}},
      });
    }

    if (request.sql.find("select disabled from login_worldservers") != std::string::npos &&
        !request.parameters.empty() && request.parameters[0] == "world-account") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"disabled", "0"}}}},
      });
    }

    if (request.sql.find("select ip from login_bannedips") != std::string::npos) {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
    }

    if (request.sql.find("select name from login_worldservers") != std::string::npos &&
        !request.parameters.empty() && request.parameters[0] == "77") {
      return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
          .rows = {eq2::db::QueryRow{.columns = {{"name", "Public World"}}}},
      });
    }

    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{});
  }

  std::vector<eq2::db::QueryRequest> requests;
  std::vector<std::string> created_accounts;

 private:
  static auto row(std::int32_t id, std::string_view name)
      -> eq2::core::Result<eq2::db::QueryResult> {
    return eq2::core::Result<eq2::db::QueryResult>::success(eq2::db::QueryResult{
        .rows =
            {
                eq2::db::QueryRow{
                    .columns = {{"id", std::to_string(id)}, {"name", std::string(name)}}},
            },
        .affected_rows = 1,
    });
  }

  static auto existing_accounts_contains(std::string_view username) -> bool {
    return username == "tester";
  }
};

auto make_login_fixture(std::int16_t version) -> std::vector<std::uint8_t> {
  return eq2::protocol::encode_legacy_login_request_fixture(eq2::protocol::LoginRequest{
      .access_code = "station",
      .username = "tester",
      .password = "correct",
      .version = version,
  });
}

auto make_live_login_frame(std::string_view username,
                           std::string_view password,
                           std::int16_t version,
                           std::uint16_t login_request_opcode = eq2::login::kLoginRequestAppOpcode,
                           eq2::protocol::ApplicationOpcodeWidth opcode_width =
                               eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::vector<std::uint8_t> {
  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = std::string(username),
          .password = std::string(password),
          .version = version,
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      login_request_opcode, login_payload, opcode_width);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, app_packet);
}

auto make_sequenced_live_login_frame(std::string_view username,
                                     std::string_view password,
                                     std::int16_t version,
                                     std::uint16_t sequence,
                                     std::uint16_t login_request_opcode =
                                         eq2::login::kLoginRequestAppOpcode,
                                     eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                         eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::vector<std::uint8_t> {
  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = std::string(username),
          .password = std::string(password),
          .version = version,
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      login_request_opcode, login_payload, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto make_sequenced_all_worlds_request_frame(
    std::uint16_t sequence,
    std::uint16_t all_worlds_request_opcode = eq2::login::kAllWorldsRequestAppOpcode,
    eq2::protocol::ApplicationOpcodeWidth opcode_width =
        eq2::protocol::ApplicationOpcodeWidth::two_bytes) -> std::vector<std::uint8_t> {
  const auto app_packet = eq2::protocol::encode_application_packet(
      all_worlds_request_opcode, std::span<const std::uint8_t>{}, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto make_sequenced_delete_character_frame(
    std::int32_t character_id,
    std::int32_t server_id,
    std::string_view name,
    std::uint16_t sequence,
    std::uint16_t delete_request_opcode = eq2::login::kDeleteCharacterRequestAppOpcode,
    eq2::protocol::ApplicationOpcodeWidth opcode_width =
        eq2::protocol::ApplicationOpcodeWidth::two_bytes) -> std::vector<std::uint8_t> {
  const auto delete_payload = eq2::protocol::encode_delete_character_request_fixture(
      eq2::protocol::DeleteCharacterRequest{
          .character_id = character_id,
          .server_id = server_id,
          .unknown = 0,
          .character_name = std::string(name),
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      delete_request_opcode, delete_payload, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto make_sequenced_play_character_frame(
    std::int32_t character_id,
    std::int32_t server_id,
    std::uint16_t sequence,
    std::string_view character_name = {},
    std::uint16_t play_request_opcode = eq2::login::kPlayCharacterRequestAppOpcode,
    eq2::protocol::ApplicationOpcodeWidth opcode_width =
        eq2::protocol::ApplicationOpcodeWidth::two_bytes) -> std::vector<std::uint8_t> {
  const auto play_payload = eq2::protocol::encode_modern_play_character_request_fixture(
      eq2::protocol::PlayCharacterRequest{
          .character_id = character_id,
          .server_id = server_id,
          .character_name = std::string(character_name),
      });
  const auto app_packet = eq2::protocol::encode_application_packet(
      play_request_opcode, play_payload, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto make_sequenced_create_character_frame(
    std::int32_t server_id,
    std::string_view name,
    std::uint16_t sequence,
    std::uint16_t create_request_opcode = eq2::login::kCreateCharacterRequestAppOpcode,
    eq2::protocol::ApplicationOpcodeWidth opcode_width =
        eq2::protocol::ApplicationOpcodeWidth::two_bytes) -> std::vector<std::uint8_t> {
  const auto create_payload = eq2::protocol::encode_create_character_request_fixture(
      eq2::protocol::CreateCharacterRequest{
          .account_id = 0,
          .server_id = server_id,
          .character_name = std::string(name),
          .race = 1,
          .gender = 2,
          .deity = 3,
          .character_class = 4,
          .level = 1,
          .appearance_data_present = true,
          .body_size = 0.75,
          .body_age = 0.25,
          .appearance_files =
              eq2::protocol::CreateCharacterAppearanceFiles{
                  .race_file = "model/human_male",
                  .hair_file = "hair/short",
              },
          .appearance_values =
              {
                  eq2::protocol::CreateCharacterAppearanceValue{
                      .type = "skin_color",
                      .signed_value = true,
                      .red = 25,
                      .green = 50,
                      .blue = 75,
                  },
              },
      },
      546);
  const auto app_packet = eq2::protocol::encode_application_packet(
      create_request_opcode, create_payload, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto make_sequenced_client_log_frame(
    std::string_view message,
    std::uint16_t sequence,
    std::uint16_t log_opcode = eq2::login::kClientCrashlogReplyAppOpcode,
    std::int16_t client_version = 546,
    eq2::protocol::ApplicationOpcodeWidth opcode_width =
        eq2::protocol::ApplicationOpcodeWidth::two_bytes) -> std::vector<std::uint8_t> {
  const auto log_payload =
      eq2::protocol::encode_client_log_payload_fixture(message, client_version);
  const auto app_packet =
      eq2::protocol::encode_application_packet(log_opcode, log_payload, opcode_width);
  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_bytes(app_packet);
  return eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes());
}

auto login_over_session_handshake(std::uint16_t port,
                                  std::string_view username,
                                  std::string_view password,
                                  std::int16_t version,
                                  std::uint16_t login_request_opcode =
                                      eq2::login::kLoginRequestAppOpcode,
                                  eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                      eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::optional<std::vector<std::uint8_t>> {
  eq2::net::TcpSocketClient client;
  if (!client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = port})) {
    return std::nullopt;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return std::nullopt;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return std::nullopt;
  }

  const auto protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return std::nullopt;
  }

  if (!client.send(make_sequenced_live_login_frame(
          username, password, version, 0, login_request_opcode, opcode_width))) {
    return std::nullopt;
  }

  for (auto attempt = 0; attempt < 4; ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      return std::nullopt;
    }

    const auto response_protocol = eq2::protocol::decode_protocol_packet(*response);
    if (response_protocol.has_value() && response_protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }
    return response;
  }

  return std::nullopt;
}

auto login_over_udp_session_handshake(std::uint16_t port,
                                      std::string_view username,
                                      std::string_view password,
                                      std::int16_t version,
                                      std::uint16_t login_request_opcode =
                                          eq2::login::kLoginRequestAppOpcode,
                                      eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                          eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::optional<std::vector<std::uint8_t>> {
  eq2::net::UdpSocketClient client;
  if (!client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = port})) {
    return std::nullopt;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return std::nullopt;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return std::nullopt;
  }

  const auto protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return std::nullopt;
  }

  if (!client.send(make_sequenced_live_login_frame(
          username, password, version, 0, login_request_opcode, opcode_width))) {
    return std::nullopt;
  }

  for (auto attempt = 0; attempt < 4; ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      return std::nullopt;
    }

    const auto response_protocol = eq2::protocol::decode_protocol_packet(*response);
    if (response_protocol.has_value() && response_protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }
    return response;
  }

  return std::nullopt;
}

auto make_server_key_request_frame() -> std::vector<std::uint8_t> {
  auto client_stats = std::vector<std::uint8_t>(38, 0);
  client_stats[0] = 0x34;
  client_stats[1] = 0x12;
  return eq2::protocol::append_legacy_crc(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpServerKeyRequest, client_stats),
      0x33624702);
}

auto request_udp_login_encryption(eq2::net::UdpSocketClient& client,
                                  const eq2::login::LiveLoginOptions& options) -> bool {
  if (!client.send(make_server_key_request_frame())) {
    return false;
  }

  auto saw_stats_response = false;
  auto saw_key_request = false;
  const auto inspect_protocol = [&](const eq2::protocol::ProtocolPacketView& protocol) {
    if (protocol.opcode == eq2::protocol::kOpSessionStatResponse) {
      saw_stats_response = true;
      return;
    }

    if (protocol.opcode != eq2::protocol::kOpPacket) {
      return;
    }

    if (protocol.payload.size() >= 2) {
      const auto app =
          eq2::protocol::decode_application_packet(protocol.payload.subspan(2),
                                                   options.opcode_width);
      if (app.has_value() && app->opcode == options.key_request_opcode) {
        saw_key_request = true;
      }
    }
  };

  for (auto attempt = 0; attempt < 4 && (!saw_stats_response || !saw_key_request); ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      return false;
    }

    const auto stripped =
        eq2::protocol::strip_legacy_crc_if_present(*response, 0x33624702);
    const auto protocol = eq2::protocol::decode_protocol_packet(stripped);
    if (!protocol.has_value()) {
      continue;
    }

    if (protocol->opcode == eq2::protocol::kOpCombined) {
      const auto combined = eq2::protocol::decode_combined_packet(protocol->payload);
      if (combined.has_value()) {
        for (const auto& subpacket : *combined) {
          const auto subprotocol = eq2::protocol::decode_protocol_packet(subpacket.bytes);
          if (subprotocol.has_value()) {
            inspect_protocol(*subprotocol);
          }
        }
      }
      continue;
    }

    inspect_protocol(*protocol);
  }

  return saw_stats_response && saw_key_request;
}

auto make_live_encrypted_embedded_login_frame(
    std::string_view username,
    std::string_view password,
    std::int16_t version,
    const eq2::login::LiveLoginOptions& options,
    std::uint64_t rc4_key,
    std::uint16_t sequence = 0) -> std::vector<std::uint8_t> {
  auto rsa_key_subpacket = std::vector<std::uint8_t>(68, 0xff);
  for (std::size_t i = 0; i < 8; ++i) {
    rsa_key_subpacket[rsa_key_subpacket.size() - 8 + i] =
        static_cast<std::uint8_t>((rc4_key >> ((7U - i) * 8U)) & 0xffU);
  }

  const auto login_payload = eq2::protocol::encode_legacy_login_request_fixture(
      eq2::protocol::LoginRequest{
          .access_code = "station",
          .username = std::string(username),
          .password = std::string(password),
          .version = version,
      });
  auto encrypted_login = eq2::protocol::encode_application_packet(
      options.login_request_opcode, login_payload, options.opcode_width);

  TestLegacyRc4 client_cipher(~rc4_key);
  auto warmup = std::array<std::uint8_t, 20>{};
  client_cipher.cypher(warmup);
  client_cipher.cypher(encrypted_login);

  const auto subpackets = std::array<std::span<const std::uint8_t>, 2>{
      std::span<const std::uint8_t>(rsa_key_subpacket),
      std::span<const std::uint8_t>(encrypted_login),
  };
  const auto combined = eq2::protocol::encode_combined_packet(
      std::span<const std::span<const std::uint8_t>>(subpackets.data(), subpackets.size()));
  if (!combined.has_value()) {
    return {};
  }

  eq2::protocol::PacketWriter writer;
  writer.append_u16_be(sequence);
  writer.append_u8(0);
  writer.append_u8(eq2::protocol::kOpAppCombined);
  writer.append_bytes(*combined);

  return eq2::protocol::append_legacy_crc(
      eq2::protocol::encode_protocol_packet(eq2::protocol::kOpPacket, writer.bytes()),
      0x33624702);
}

auto login_over_udp_encrypted_embedded_handshake(
    std::uint16_t port,
    std::string_view username,
    std::string_view password,
    std::int16_t version,
    const eq2::login::LiveLoginOptions& options,
    std::uint64_t rc4_key) -> std::optional<std::vector<std::uint8_t>> {
  eq2::net::UdpSocketClient client;
  if (!client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = port})) {
    return std::nullopt;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return std::nullopt;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return std::nullopt;
  }

  const auto session_protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!session_protocol.has_value() ||
      session_protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return std::nullopt;
  }

  if (!request_udp_login_encryption(client, options)) {
    return std::nullopt;
  }

  const auto encrypted_login =
      make_live_encrypted_embedded_login_frame(username, password, version, options, rc4_key);
  if (encrypted_login.empty() || !client.send(encrypted_login)) {
    return std::nullopt;
  }

  for (auto attempt = 0; attempt < 6; ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      return std::nullopt;
    }

    const auto protocol = eq2::protocol::decode_protocol_packet(
        eq2::protocol::strip_legacy_crc_if_present(*response, 0x33624702));
    if (protocol.has_value() && protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }
    if (protocol.has_value() && (protocol->opcode == eq2::protocol::kOpPacket ||
                                 protocol->opcode == eq2::protocol::kOpCombined)) {
      return response;
    }
  }

  return std::nullopt;
}

auto open_login_session(eq2::net::TcpSocketClient& client, std::uint16_t port) -> bool {
  if (!client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1", .port = port})) {
    return false;
  }

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  if (!client.send(session_request)) {
    return false;
  }

  const auto session_response = client.receive();
  if (!session_response.has_value()) {
    return false;
  }

  const auto protocol = eq2::protocol::decode_protocol_packet(*session_response);
  if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpSessionResponse) {
    return false;
  }

  return true;
}

auto decode_reply_account_id(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint32_t>;

auto decode_reply_account_id(std::span<const std::uint8_t> login_response,
                             std::uint16_t login_reply_opcode,
                             eq2::protocol::ApplicationOpcodeWidth opcode_width)
    -> std::optional<std::uint32_t> {
  const auto stripped =
      eq2::protocol::strip_legacy_crc_if_present(login_response,
                                                 eq2::protocol::kDefaultSessionKey);
  const auto reply = eq2::protocol::decode_login_reply_protocol_frame(
      stripped, login_reply_opcode, opcode_width);
  if (!reply.has_value() ||
      reply->reply_code != static_cast<std::uint8_t>(eq2::login::LoginReplyCode::accepted)) {
    return std::nullopt;
  }

  return reply->account_id;
}

auto decode_reply_account_id(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint32_t> {
  return decode_reply_account_id(login_response,
                                 eq2::login::kLoginReplyAppOpcode,
                                 eq2::protocol::ApplicationOpcodeWidth::two_bytes);
}

auto decode_encrypted_one_byte_login_reply(std::span<const std::uint8_t> login_response,
                                           std::uint64_t rc4_key,
                                           std::uint16_t login_reply_opcode,
                                           std::int16_t client_version)
    -> std::optional<eq2::protocol::LoginReplyPayload> {
  const auto stripped =
      eq2::protocol::strip_legacy_crc_if_present(login_response, 0x33624702);
  const auto protocol = eq2::protocol::decode_protocol_packet(stripped);
  if (!protocol.has_value()) {
    return std::nullopt;
  }

  const auto decode_protocol =
      [&](const eq2::protocol::ProtocolPacketView& packet)
      -> std::optional<eq2::protocol::LoginReplyPayload> {
    if (packet.opcode != eq2::protocol::kOpPacket || packet.payload.size() <= 2) {
      return std::nullopt;
    }

    auto encrypted_payload = std::vector<std::uint8_t>(
        packet.payload.begin() + 2, packet.payload.end());
    TestLegacyRc4 server_cipher(rc4_key);
    auto warmup = std::array<std::uint8_t, 20>{};
    server_cipher.cypher(warmup);
    server_cipher.cypher(encrypted_payload);

    auto app_bytes = std::span<const std::uint8_t>(encrypted_payload);
    if (!app_bytes.empty() && app_bytes.front() == 0) {
      app_bytes = app_bytes.subspan(1);
    }

    const auto app =
        eq2::protocol::decode_application_packet(app_bytes,
                                                 eq2::protocol::ApplicationOpcodeWidth::one_byte);
    if (!app.has_value() || app->opcode != login_reply_opcode) {
      return std::nullopt;
    }

    return eq2::protocol::decode_login_reply_payload(app->payload, client_version);
  };

  if (auto direct = decode_protocol(*protocol)) {
    return direct;
  }

  if (protocol->opcode != eq2::protocol::kOpCombined) {
    return std::nullopt;
  }

  const auto combined = eq2::protocol::decode_combined_packet(protocol->payload);
  if (!combined.has_value()) {
    return std::nullopt;
  }

  for (const auto& subpacket : *combined) {
    const auto subprotocol = eq2::protocol::decode_protocol_packet(subpacket.bytes);
    if (subprotocol.has_value()) {
      if (auto reply = decode_protocol(*subprotocol)) {
        return reply;
      }
    }
  }

  return std::nullopt;
}

auto split_protocol_response(std::span<const std::uint8_t> response)
    -> std::vector<std::vector<std::uint8_t>> {
  const auto stripped =
      eq2::protocol::strip_legacy_crc_if_present(response,
                                                 eq2::protocol::kDefaultSessionKey);
  const auto protocol = eq2::protocol::decode_protocol_packet(stripped);
  if (!protocol.has_value()) {
    return {};
  }

  if (protocol->opcode != eq2::protocol::kOpCombined) {
    return {std::vector<std::uint8_t>(stripped.begin(), stripped.end())};
  }

  const auto combined = eq2::protocol::decode_combined_packet(protocol->payload);
  if (!combined.has_value()) {
    return {};
  }

  std::vector<std::vector<std::uint8_t>> packets;
  packets.reserve(combined->size());
  for (const auto& subpacket : *combined) {
    packets.emplace_back(subpacket.bytes.begin(), subpacket.bytes.end());
  }
  return packets;
}

template <typename DecodePayload>
auto decode_application_response(std::span<const std::uint8_t> response,
                                 std::uint16_t application_opcode,
                                 DecodePayload decode_payload,
                                 eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                     eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> decltype(decode_payload(std::span<const std::uint8_t>{})) {
  const auto packets = split_protocol_response(response);
  for (const auto& packet_bytes : packets) {
    const auto protocol = eq2::protocol::decode_protocol_packet(packet_bytes);
    if (!protocol.has_value() || protocol->opcode != eq2::protocol::kOpPacket) {
      continue;
    }

    const auto decode_app =
        [&](std::span<const std::uint8_t> payload)
        -> decltype(decode_payload(std::span<const std::uint8_t>{})) {
      const auto app = eq2::protocol::decode_application_packet(payload, opcode_width);
      if (!app.has_value() || app->opcode != application_opcode) {
        return std::nullopt;
      }

      return decode_payload(app->payload);
    };

    if (protocol->payload.size() >= 2) {
      if (auto sequenced = decode_app(protocol->payload.subspan(2))) {
        return sequenced;
      }
    }

    if (auto unsequenced = decode_app(protocol->payload)) {
      return unsequenced;
    }
  }

  return std::nullopt;
}

auto decode_world_list_response(std::span<const std::uint8_t> login_response)
    -> std::optional<eq2::protocol::LoginWorldListPayload> {
  return decode_application_response(
      login_response,
      eq2::login::kWorldListReplyAppOpcode,
      [](std::span<const std::uint8_t> payload)
      -> std::optional<eq2::protocol::LoginWorldListPayload> {
        return eq2::protocol::decode_login_world_list_payload(payload);
      });
}

auto decode_character_list_account_id(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint32_t> {
  return decode_application_response(
      login_response,
      eq2::login::kCharactersReplyAppOpcode,
      [](std::span<const std::uint8_t> payload) -> std::optional<std::uint32_t> {
        if (payload.size() < 5) {
          return std::nullopt;
        }

        eq2::protocol::PacketReader reader(payload);
        const auto character_count = reader.read_u8();
        const auto account_id = reader.read_u32_le();
        if (!character_count.has_value() || !account_id.has_value() || *character_count != 0) {
          return std::nullopt;
        }
        return account_id;
      });
}

auto character_list_payload(std::span<const std::uint8_t> login_response)
    -> std::optional<std::vector<std::uint8_t>> {
  return decode_application_response(
      login_response,
      eq2::login::kCharactersReplyAppOpcode,
      [](std::span<const std::uint8_t> payload) -> std::optional<std::vector<std::uint8_t>> {
        return std::vector<std::uint8_t>(payload.begin(), payload.end());
      });
}

auto skip_eq2_16bit_string(eq2::protocol::PacketReader& reader) -> bool {
  const auto size = reader.read_u16_le();
  return size.has_value() && reader.read_bytes(*size).has_value();
}

auto first_character_net_appearance(std::span<const std::uint8_t> payload)
    -> std::optional<std::span<const std::uint8_t>> {
  eq2::protocol::PacketReader reader(payload);
  const auto count = reader.read_u8();
  if (!count.has_value() || *count == 0) {
    return std::nullopt;
  }

  if (!reader.read_u32_le().has_value() || !reader.read_u32_le().has_value() ||
      !skip_eq2_16bit_string(reader)) {
    return std::nullopt;
  }

  if (!reader.read_u8().has_value() || !reader.read_u8().has_value() ||
      !reader.read_u8().has_value() || !reader.read_u32_le().has_value() ||
      !skip_eq2_16bit_string(reader)) {
    return std::nullopt;
  }

  for (auto index = 0; index < 6; ++index) {
    if (!reader.read_u32_le().has_value()) {
      return std::nullopt;
    }
  }

  if (!skip_eq2_16bit_string(reader) || !skip_eq2_16bit_string(reader) ||
      !reader.read_u32_le().has_value()) {
    return std::nullopt;
  }

  return reader.read_bytes(337);
}

auto payload_contains_ascii(std::span<const std::uint8_t> payload,
                            std::string_view text) -> bool {
  return std::search(payload.begin(),
                     payload.end(),
                     text.begin(),
                     text.end()) != payload.end();
}

auto decode_delete_character_response(std::span<const std::uint8_t> login_response,
                                      std::uint16_t delete_reply_opcode =
                                          eq2::login::kDeleteCharacterReplyAppOpcode,
                                      eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                          eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::optional<eq2::protocol::DeleteCharacterResponse> {
  return decode_application_response(
      login_response,
      delete_reply_opcode,
      [&](std::span<const std::uint8_t> payload)
      -> std::optional<eq2::protocol::DeleteCharacterResponse> {
        return eq2::protocol::decode_delete_character_response_payload(payload);
      },
      opcode_width);
}

auto decode_create_character_reply(std::span<const std::uint8_t> login_response,
                                   std::uint16_t client_version,
                                   std::uint16_t create_reply_opcode =
                                       eq2::login::kCreateCharacterReplyAppOpcode,
                                   eq2::protocol::ApplicationOpcodeWidth opcode_width =
                                       eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::optional<eq2::protocol::CreateCharacterReply> {
  return decode_application_response(
      login_response,
      create_reply_opcode,
      [&](std::span<const std::uint8_t> payload)
      -> std::optional<eq2::protocol::CreateCharacterReply> {
        return eq2::protocol::decode_create_character_reply_payload(payload, client_version);
      },
      opcode_width);
}

auto decode_play_character_response(
    std::span<const std::uint8_t> login_response,
    std::uint16_t client_version,
    std::uint16_t play_reply_opcode = eq2::login::kPlayCharacterReplyAppOpcode,
    eq2::protocol::ApplicationOpcodeWidth opcode_width =
        eq2::protocol::ApplicationOpcodeWidth::two_bytes)
    -> std::optional<eq2::protocol::PlayCharacterResponse> {
  return decode_application_response(
      login_response,
      play_reply_opcode,
      [&](std::span<const std::uint8_t> payload)
      -> std::optional<eq2::protocol::PlayCharacterResponse> {
        return eq2::protocol::decode_play_character_response_payload(payload, client_version);
      },
      opcode_width);
}

auto decode_login_reply_code(std::span<const std::uint8_t> login_response)
    -> std::optional<std::uint8_t> {
  const auto stripped =
      eq2::protocol::strip_legacy_crc_if_present(login_response,
                                                 eq2::protocol::kDefaultSessionKey);
  const auto reply = eq2::protocol::decode_login_reply_protocol_frame(
      stripped, eq2::login::kLoginReplyAppOpcode);
  if (!reply.has_value()) {
    return std::nullopt;
  }
  return reply->reply_code;
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

auto wait_for_event(const eq2::login::LiveLoginService& service,
                    eq2::login::LiveLoginEventType type) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    const auto events = service.events();
    if (std::any_of(events.begin(), events.end(), [type](const auto& event) {
          return event.type == type;
        })) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

auto wait_for_event_count(const eq2::login::LiveLoginService& service,
                          eq2::login::LiveLoginEventType type,
                          std::size_t count) -> bool {
  for (auto attempt = 0; attempt < 80; ++attempt) {
    const auto events = service.events();
    const auto observed = static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [type](const auto& event) {
          return event.type == type;
        }));
    if (observed >= count) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return false;
}

auto connection_has_request(const ScriptedLoginConnection& connection,
                            std::string_view sql_fragment,
                            std::string_view first_parameter) -> bool {
  return std::any_of(connection.requests.begin(),
                     connection.requests.end(),
                     [&](const auto& request) {
                       return request.sql.find(sql_fragment) != std::string::npos &&
                              !request.parameters.empty() &&
                              request.parameters.front() == first_parameter;
                     });
}

auto make_ls_info_frame(std::string_view account, std::string_view password)
    -> std::vector<std::uint8_t> {
  eq2::protocol::PacketWriter writer;
  const auto append_fixed = [&writer](std::string_view value, std::size_t size) {
    std::vector<std::uint8_t> bytes(size, 0);
    const auto copy_size = std::min(value.size(), size);
    std::copy_n(reinterpret_cast<const std::uint8_t*>(value.data()), copy_size, bytes.data());
    writer.append_bytes(bytes);
  };

  append_fixed("Public World", 201);
  append_fixed("127.0.0.1", 250);
  append_fixed(account, 31);
  append_fixed(password, 256);
  append_fixed("0.5.0", 25);
  append_fixed("2026.05.14", 64);
  writer.append_u8(0);
  writer.append_u32_le(1);

  return *eq2::protocol::encode_interserver_packet(eq2::protocol::kServerOpLsInfo, writer.bytes());
}

auto make_ls_status_frame(std::int32_t status,
                          std::int32_t players,
                          std::int32_t zones,
                          std::uint8_t max_level) -> std::vector<std::uint8_t> {
  const auto payload = eq2::protocol::encode_server_ls_status_payload(
      eq2::protocol::ServerLsStatus{
          .status = status,
          .player_count = players,
          .zone_count = zones,
          .world_max_level = max_level,
      });
  return *eq2::protocol::encode_interserver_packet(eq2::protocol::kServerOpLsStatus, payload);
}

void login_server_config_loads_from_core_config() {
  eq2::core::MapConfig values;
  values.set("login.address", "127.0.0.1");
  values.set("login.port", "9101");
  values.set("login.account_creation_allowed", "true");

  const auto config = eq2::login::load_login_server_config(values);

  require_eq(config.address, std::string_view("127.0.0.1"), "login config reads address");
  require_eq(config.port, static_cast<std::uint16_t>(9101), "login config reads port");
  require(config.account_creation_allowed, "login config reads account creation flag");
}

void client_login_reaches_legacy_success_outcome_through_source2_boundaries() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  eq2::login::LoginServer server(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 9100},
      accounts,
      worlds,
      supported_login_versions());

  server.start();
  const auto session = server.accept_client();
  require(session.has_value(), "login server accepts a source2 net session");

  const auto outcome = server.handle_login_request(*session, make_login_fixture(546));

  require_eq(outcome.status, eq2::login::LoginPacketStatus::accepted,
             "valid login reaches accepted outcome");
  require_eq(outcome.reply_code, eq2::login::LoginReplyCode::accepted,
             "valid login uses legacy accepted reply code");
  require(outcome.account.has_value(), "accepted login carries account");
  require_eq(outcome.account->id, 42, "accepted login uses DB account id");
  require_eq(outcome.client_version, static_cast<std::int16_t>(546),
             "accepted login preserves parsed client version");
  require(outcome.should_send_world_list_after_login, "accepted login schedules world list");
  require(server.last_transport_event().has_value(), "login server records transport event");
  require_eq(server.last_transport_event()->type, eq2::net::SessionEventType::received,
             "login bytes arrive through net receive event");
}

void unsupported_client_version_reaches_legacy_bad_version_outcome() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  eq2::login::LoginServer server(
      eq2::login::LoginServerConfig{},
      accounts,
      worlds,
      supported_login_versions());

  server.start();
  const auto session = server.accept_client();
  require(session.has_value(), "login server accepts session for bad-version test");

  const auto outcome = server.handle_login_request(*session, make_login_fixture(1208));

  require_eq(outcome.status, eq2::login::LoginPacketStatus::rejected,
             "unsupported version is rejected");
  require_eq(outcome.reply_code, eq2::login::LoginReplyCode::bad_client_version,
             "unsupported version uses legacy bad-version reply code");
  require(!outcome.account.has_value(), "bad version does not return an account");
}

void world_registration_uses_protocol_frame_and_db_repository() {
  eq2::db::FakeLoginAccountRepository accounts;
  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.allowed_server_versions.insert("2026.05.14");
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  eq2::login::LoginServer server(
      eq2::login::LoginServerConfig{},
      accounts,
      worlds,
      supported_login_versions());

  const auto frame = make_ls_info_frame("world-account", "secret");
  const auto result = server.handle_world_registration(frame, false);

  require_eq(result.status, eq2::login::WorldRegistrationStatus::accepted,
             "LSInfo world registration is accepted through source2 db repository");
  require(result.world.has_value(), "accepted world registration returns world record");
  require_eq(result.world->account_id, 77, "accepted world registration carries account id");
  require_eq(result.world->display_name, std::string_view("Public World"),
             "accepted world registration carries display name");
}

void live_login_slice_uses_real_tcp_protocol_pipeline_and_sql_repository() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login service starts real loopback TCP transport");
  require(service.port() != 0, "live login service binds an ephemeral test port");

  const auto login_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(login_response.has_value(),
          "live login writes valid login reply after real TCP session handshake");
  if (login_response.has_value()) {
    const auto protocol = eq2::protocol::decode_protocol_packet(*login_response);
    require(protocol.has_value(), "live login TCP reply decodes as a protocol packet");
    require(protocol.has_value() && protocol->opcode == eq2::protocol::kOpPacket,
            "live login TCP reply uses application protocol opcode");
    require_eq(decode_reply_account_id(*login_response).value_or(0), std::uint32_t{42},
               "live login TCP reply carries SQL-backed account id");
  }
  require(wait_for_login_count(service, 1), "live login service handles valid login frame");
  auto outcomes = service.login_outcomes();
  require_eq(outcomes[0].status, eq2::login::LoginPacketStatus::accepted,
             "live login accepts valid SQL-backed credentials");
  require_eq(outcomes[0].reply_code, eq2::login::LoginReplyCode::accepted,
             "live login preserves accepted reply code");
  require(outcomes[0].account.has_value(), "live login returns SQL-backed account");
  require_eq(outcomes[0].account->id, 42, "live login reads account id from SQL repository");
  require(!connection->requests.empty(), "live login exercises SQL account repository");
  require_eq(connection->requests.front().parameters.front(), std::string_view("tester"),
             "live login passes username through SQL repository parameters");
  require(connection_has_request(*connection, "update account set ip_address", "127.0.0.1"),
          "live login records successful login remote IP");
  require(connection_has_request(*connection, "update account set last_client_version", "546"),
          "live login records successful login client version");
  require(!service.outbound_packets().empty(), "live login captures protocol reply boundary");

  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("tester", "correct", 546)),
          "live login smoke harness sends second valid login over real TCP");
  require(wait_for_login_count(service, 2), "live login service handles second valid login frame");
  outcomes = service.login_outcomes();
  require_eq(outcomes[1].reply_code, eq2::login::LoginReplyCode::accepted,
             "live login preserves accepted reply code for second valid login");

  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("tester", "wrong", 546)),
          "live login smoke harness sends invalid password over real TCP");
  require(wait_for_login_count(service, 3), "live login service handles invalid password frame");
  outcomes = service.login_outcomes();
  require_eq(outcomes[2].reply_code, eq2::login::LoginReplyCode::invalid_username_or_password,
             "live login preserves invalid password reply code");

  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("tester", "correct", 1208)),
          "live login smoke harness sends bad client version over real TCP");
  require(wait_for_login_count(service, 4), "live login service handles bad client version frame");
  outcomes = service.login_outcomes();
  require_eq(outcomes[3].reply_code, eq2::login::LoginReplyCode::bad_client_version,
             "live login preserves bad client version reply code");

  const std::vector<std::uint8_t> malformed{0x00};
  require(eq2::net::send_tcp_loopback(service.port(), malformed),
          "live login smoke harness sends malformed protocol bytes");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::malformed),
          "live login records malformed protocol packet");

  const std::vector<std::uint8_t> empty_payload;
  const auto disconnect = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionDisconnect, empty_payload);
  require(eq2::net::send_tcp_loopback(service.port(), disconnect),
          "live login smoke harness sends protocol disconnect");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::disconnected),
          "live login records graceful disconnect");

  const auto counters = service.diagnostic_counters();
  require_eq(counters.login_attempts, static_cast<std::size_t>(4),
             "diagnostic counters report login attempts");
  require_eq(counters.login_failures, static_cast<std::size_t>(2),
             "diagnostic counters report rejected logins");
  require(counters.malformed_packets >= 1, "diagnostic counters report malformed packets");

  service.stop();
}

void live_login_event_formatting_is_redacted() {
  const auto message = eq2::login::format_live_login_event(eq2::login::LiveLoginEvent{
      .type = eq2::login::LiveLoginEventType::login_rejected,
      .session = eq2::net::SessionId{7},
      .protocol_opcode = eq2::protocol::kOpPacket,
      .application_opcode = eq2::login::kLoginRequestAppOpcode,
      .reply_code = eq2::login::LoginReplyCode::invalid_username_or_password,
      .client_version = 546,
      .reason = "invalid credentials",
  });
  require(message.find("type=login_rejected") != std::string::npos,
          "diagnostic formatter names event type");
  require(message.find("application_opcode=") != std::string::npos,
          "diagnostic formatter includes opcode context");
  require(message.find("client_version=546") != std::string::npos,
          "diagnostic formatter includes client version");
  require(message.find("correct") == std::string::npos &&
              message.find("password") == std::string::npos,
          "diagnostic formatter does not include credential values");
}

void live_login_slice_accepts_udp_client_login() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login starts TCP world listener and UDP client listener");
  require(service.port() != 0, "live login binds a shared TCP/UDP login port");

  const auto login_response =
      login_over_udp_session_handshake(service.port(), "tester", "correct", 546);
  require(login_response.has_value(),
          "live login writes valid login reply after real UDP session handshake");
  if (login_response.has_value()) {
    require_eq(decode_reply_account_id(*login_response).value_or(0), std::uint32_t{42},
               "live login UDP reply carries SQL-backed account id");
  }
  require(wait_for_login_count(service, 1), "live login service handles UDP login frame");

  const auto outcomes = service.login_outcomes();
  require_eq(outcomes.front().status, eq2::login::LoginPacketStatus::accepted,
             "live login accepts valid UDP credentials");
  require(outcomes.front().account.has_value(), "live login UDP path returns SQL-backed account");
  require_eq(outcomes.front().account->id, 42, "live login UDP path reads account id");

  service.stop();
}

void live_login_stores_legacy_client_log_packets() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::db::FakeClientLogRepository client_logs;
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      client_logs,
      supported_login_versions());

  require(service.start(), "client log login service starts TCP and UDP listeners");

  eq2::net::TcpSocketClient client;
  require(open_login_session(client, service.port()),
          "client log test completes login stream session handshake");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "client log test sends login frame");
  require(wait_for_login_count(service, 1), "client log test reaches accepted login");

  const auto log_frame = make_sequenced_client_log_frame("client stack trace", 1);
  require(!log_frame.empty(), "client log fixture encodes compressed payload");
  require(client.send(log_frame), "client log test sends compressed client log frame");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::client_log_recorded),
          "live login records compressed client log frame");

  const auto eq2_crash_log_frame = make_sequenced_client_log_frame(
      "eq2 crash report", 2, eq2::login::kClientEq2CrashlogReplyAppOpcode);
  require(!eq2_crash_log_frame.empty(), "eq2 crash log fixture encodes compressed payload");
  require(client.send(eq2_crash_log_frame), "client log test sends eq2 crash log frame");
  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::client_log_recorded, 2),
          "live login records eq2 crash log frame");

  require_eq(client_logs.records.size(), static_cast<std::size_t>(2),
             "client log repository receives both records");
  if (!client_logs.records.empty()) {
    require_eq(client_logs.records.front().type, std::string_view("Crash Log"),
               "client log repository receives legacy crash log type");
    require_eq(client_logs.records.front().message, std::string_view("client stack trace"),
               "client log repository receives decompressed message");
    require_eq(client_logs.records.front().account_name, std::string_view("tester"),
               "client log repository receives account name");
    require_eq(client_logs.records.front().client_version, static_cast<std::int16_t>(546),
               "client log repository receives client version");
  }
  if (client_logs.records.size() >= 2) {
    require_eq(client_logs.records[1].type, std::string_view("EQ2 Crash Log"),
               "client log repository receives eq2 crash log type");
    require_eq(client_logs.records[1].message, std::string_view("eq2 crash report"),
               "client log repository receives eq2 crash log text");
  }

  service.stop();
}

void live_login_slice_accepts_configured_one_byte_login_opcodes() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  const auto live_options = eq2::login::LiveLoginOptions{
      .requested_port = 0,
      .login_request_opcode = 0x01,
      .login_reply_opcode = 0x02,
      .world_list_reply_opcode = 0x03,
      .all_worlds_request_opcode = 0x04,
      .characters_request_opcode = 0x05,
      .characters_reply_opcode = 0x06,
      .opcode_width = eq2::protocol::ApplicationOpcodeWidth::one_byte,
  };
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions(),
      live_options);

  require(service.start(), "live login starts with configured one-byte login opcodes");
  require(service.port() != 0, "configured one-byte login binds a shared TCP/UDP login port");

  const auto login_response =
      login_over_udp_session_handshake(service.port(),
                                       "tester",
                                       "correct",
                                       546,
                                       live_options.login_request_opcode,
                                       live_options.opcode_width);
  require(login_response.has_value(),
          "configured one-byte login writes a reply after UDP session handshake");
  if (login_response.has_value()) {
    require_eq(decode_reply_account_id(*login_response,
                                       live_options.login_reply_opcode,
                                       live_options.opcode_width)
                   .value_or(0),
               std::uint32_t{42},
               "configured one-byte login reply carries SQL-backed account id");
  }
  require(wait_for_login_count(service, 1),
          "configured one-byte login service handles the login frame");

  const auto outcomes = service.login_outcomes();
  require_eq(outcomes.front().status, eq2::login::LoginPacketStatus::accepted,
             "configured one-byte login accepts valid credentials");

  service.stop();
}

void live_login_slice_accepts_legacy_encrypted_embedded_login() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  const auto live_options = eq2::login::LiveLoginOptions{
      .requested_port = 0,
      .login_request_opcode = 0x00,
      .login_reply_opcode = 0x04,
      .world_list_reply_opcode = 0x08,
      .all_worlds_request_opcode = 0x07,
      .characters_request_opcode = 0x09,
      .characters_reply_opcode = 0x0a,
      .opcode_width = eq2::protocol::ApplicationOpcodeWidth::one_byte,
      .key_request_opcode = 0x02,
  };
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions(),
      live_options);

  require(service.start(),
          "legacy encrypted embedded login starts TCP and UDP login listeners");
  require(service.port() != 0, "legacy encrypted embedded login binds a shared port");

  constexpr auto rc4_key = std::uint64_t{0x1c2e9b505da1df06ULL};
  const auto login_response =
      login_over_udp_encrypted_embedded_handshake(service.port(),
                                                  "tester",
                                                  "correct",
                                                  546,
                                                  live_options,
                                                  rc4_key);
  require(login_response.has_value(),
          "legacy encrypted embedded login receives a server login reply");
  if (login_response.has_value()) {
    const auto reply = decode_encrypted_one_byte_login_reply(
        *login_response, rc4_key, live_options.login_reply_opcode, 546);
    require(reply.has_value(),
            "legacy encrypted embedded login reply decrypts with legacy framing");
    if (reply.has_value()) {
      require_eq(reply->reply_code,
                 static_cast<std::uint8_t>(eq2::login::LoginReplyCode::accepted),
                 "legacy encrypted embedded login reply is accepted");
      require_eq(reply->account_id, std::uint32_t{42},
                 "legacy encrypted embedded login reply carries SQL-backed account id");
    }
  }

  require(wait_for_login_count(service, 1),
          "legacy encrypted embedded login reaches the login service");
  const auto outcomes = service.login_outcomes();
  require_eq(outcomes.front().status, eq2::login::LoginPacketStatus::accepted,
             "legacy encrypted embedded login accepts valid credentials");
  require(outcomes.front().account.has_value(),
          "legacy encrypted embedded login returns SQL-backed account");
  require_eq(outcomes.front().account->id, 42,
             "legacy encrypted embedded login reads account id");

  service.stop();
}

void live_login_registered_world_reaches_udp_world_list() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::db::SqlWorldRegistrationRepository worlds(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      supported_login_versions());

  require(service.start(), "live login world-list test starts TCP and UDP listeners");
  require(service.port() != 0, "live login world-list test binds shared port");

  eq2::net::TcpSocketClient world_client;
  require(world_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "legacy world registration client connects over TCP");
  require(world_client.send(make_ls_info_frame("world-account", "secret")),
          "legacy world registration client sends LSInfo frame");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_registered),
          "live login accepts world registration over real TCP");
  require(world_client.send(make_ls_status_frame(-2, 23, 4, 90)),
          "legacy world registration client sends LSStatus frame");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_status_updated),
          "live login accepts world status update over real TCP");
  require(connection_has_request(*connection, "insert into login_worldstats", "77"),
          "live login records world status in SQL repository");

  const auto registrations = service.world_registration_results();
  require(!registrations.empty(), "live login records world registration result");
  if (!registrations.empty()) {
    require_eq(registrations.front().status, eq2::login::WorldRegistrationStatus::accepted,
               "live login accepts scripted world account");
    require(registrations.front().world.has_value(),
            "accepted world registration exposes registered world");
  }

  eq2::net::UdpSocketClient login_client;
  require(login_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "login client connects to UDP listener after world registration");

  const auto session_request = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionRequest,
      eq2::protocol::encode_session_request(eq2::protocol::SessionRequest{
          .unknown_a = 0,
          .session = 0x10203040,
          .max_length = 512,
      }));
  require(login_client.send(session_request),
          "login client sends session request before world-list login");
  const auto session_response = login_client.receive();
  require(session_response.has_value(), "login client receives session response");

  require(login_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "login client sends sequenced login frame after world registration");

  auto account_id = std::optional<std::uint32_t>{};
  for (auto attempt = 0; attempt < 6 && !account_id.has_value(); ++attempt) {
    auto response = login_client.receive();
    if (!response.has_value()) {
      break;
    }

    const auto protocol = eq2::protocol::decode_protocol_packet(*response);
    if (protocol.has_value() && protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }

    if (!account_id.has_value()) {
      account_id = decode_reply_account_id(*response);
    }
  }

  require_eq(account_id.value_or(0), static_cast<std::uint32_t>(42),
             "login client receives accepted login reply before world list");
  require(login_client.send(make_sequenced_all_worlds_request_frame(1)),
          "login client sends legacy all-worlds description request");

  auto world_list = std::optional<eq2::protocol::LoginWorldListPayload>{};
  auto character_list_account_id = std::optional<std::uint32_t>{};
  auto post_world_login_reply = std::optional<std::uint8_t>{};
  for (auto attempt = 0; attempt < 8 &&
                         (!world_list.has_value() || !character_list_account_id.has_value() ||
                          !post_world_login_reply.has_value());
       ++attempt) {
    auto response = login_client.receive();
    if (!response.has_value()) {
      break;
    }

    const auto protocol = eq2::protocol::decode_protocol_packet(*response);
    if (protocol.has_value() && protocol->opcode == eq2::protocol::kOpAck) {
      continue;
    }

    if (!world_list.has_value()) {
      world_list = decode_world_list_response(*response);
    }
    if (!character_list_account_id.has_value()) {
      character_list_account_id = decode_character_list_account_id(*response);
    }
    if (!post_world_login_reply.has_value()) {
      const auto code = decode_login_reply_code(*response);
      if (code.has_value() && *code == 10) {
        post_world_login_reply = code;
      }
    }
  }

  require(world_list.has_value(), "login client receives world-list response after login");
  if (world_list.has_value()) {
    require_eq(world_list->worlds.size(), static_cast<std::size_t>(1),
               "world-list response includes registered world");
    require_eq(world_list->worlds.front().world_id, 77,
               "world-list response uses registered world account id");
    require_eq(world_list->worlds.front().display_name, std::string_view("Public World"),
               "world-list response uses world display name from DB");
    require(world_list->worlds.front().locked,
            "world-list response exposes locked world status");
    require_eq(world_list->worlds.front().player_count, static_cast<std::uint16_t>(23),
               "world-list response exposes world player count");
  }
  require_eq(character_list_account_id.value_or(0), static_cast<std::uint32_t>(42),
             "login client receives empty character-list response for logged-in account");
  require_eq(post_world_login_reply.value_or(0), static_cast<std::uint8_t>(10),
             "login client receives legacy post-world-list login flag reply");
  require(wait_for_login_count(service, 1),
          "live login handles UDP login after world registration");

  login_client.close();
  world_client.close();
  service.stop();
}

void live_login_registered_world_returns_non_empty_character_list() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  eq2::db::FakeCharacterListRepository characters;
  characters.characters_by_account[42] = {
      eq2::db::CharacterListRecord{
          .login_character_id = 11,
          .character_id = 2002,
          .server_id = 77,
          .name = "Alys",
          .race = 4,
          .character_class = 2,
          .gender = 1,
          .body_size = 0.75,
          .body_age = 0.25,
          .current_zone_id = 10,
          .level = 25,
          .soga_hair_type = 88,
          .legs_type = 12,
          .chest_type = 34,
          .hair_type = 78,
          .created_date = 1'700'000'000,
          .last_played = 1'700'000'100,
          .server_name = "Public World",
          .facial_hair_type = 6,
          .soga_facial_hair_type = 66,
          .soga_model_type = 223,
          .model_type = 222,
          .zone_name = "qeynos",
          .zone_description = "Qeynos",
      },
  };
  characters.appearance_by_login_character[11] = {
      eq2::db::CharacterAppearanceRecord{
          .type = "skin_color",
          .red = 10,
          .green = 20,
          .blue = 30,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "eye_color",
          .red = 40,
          .green = 50,
          .blue = 60,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "hair_type_color",
          .red = 11,
          .green = 12,
          .blue = 13,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "hair_type_highlight_color",
          .red = 14,
          .green = 15,
          .blue = 16,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "soga_hair_color1",
          .red = 31,
          .green = 32,
          .blue = 33,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "soga_hair_color2",
          .red = 34,
          .green = 35,
          .blue = 36,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "soga_hair_color3",
          .red = 37,
          .green = 38,
          .blue = 39,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "soga_hair_type_color",
          .red = 41,
          .green = 42,
          .blue = 43,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "soga_hair_type_highlight_color",
          .red = 44,
          .green = 45,
          .blue = 46,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "soga_hair_face_color",
          .red = 47,
          .green = 48,
          .blue = 49,
      },
      eq2::db::CharacterAppearanceRecord{
          .type = "soga_hair_face_highlight_color",
          .red = 50,
          .green = 51,
          .blue = 52,
      },
  };
  characters.equipment_by_login_character[11] = {
      eq2::db::CharacterEquipmentRecord{
          .slot = 0,
          .equip_type = 900,
          .red = 1,
          .green = 2,
          .blue = 3,
          .highlight_red = 4,
          .highlight_green = 5,
          .highlight_blue = 6,
      },
  };

  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      characters,
      supported_login_versions());

  require(service.start(), "non-empty character-list test starts login listeners");

  eq2::net::TcpSocketClient world_client;
  require(world_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "non-empty character-list test connects a world");
  require(world_client.send(make_ls_info_frame("world-account", "secret")),
          "non-empty character-list test registers world");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_registered),
          "non-empty character-list test waits for registered world");

  eq2::net::TcpSocketClient client;
  require(open_login_session(client, service.port()),
          "non-empty character-list client completes session handshake");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "non-empty character-list client sends login frame");
  require(client.receive().has_value(),
          "non-empty character-list client receives login reply");
  require(wait_for_login_count(service, 1),
          "non-empty character-list test reaches login service");

  require(client.send(make_sequenced_all_worlds_request_frame(1)),
          "non-empty character-list client sends all-worlds request");

  auto payload = std::optional<std::vector<std::uint8_t>>{};
  for (auto attempt = 0; attempt < 8 && !payload.has_value(); ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      break;
    }
    payload = character_list_payload(*response);
  }

  require(payload.has_value(), "non-empty character-list client receives character-list payload");
  if (payload.has_value()) {
    require(!payload->empty() && payload->front() == 1,
            "non-empty character-list payload reports one character");
    require(payload_contains_ascii(*payload, "Alys"),
            "non-empty character-list payload carries character name");
    require(payload_contains_ascii(*payload, "qeynos"),
            "non-empty character-list payload carries zone name");
    const auto net_appearance = first_character_net_appearance(*payload);
    require(net_appearance.has_value(),
            "non-empty character-list payload carries legacy NetAppearance block");
    if (net_appearance.has_value()) {
      const auto block = *net_appearance;
      require_eq(block.size(), static_cast<std::size_t>(337),
                 "legacy NetAppearance block has DoF v11 wire size");
      require_eq(block[0], static_cast<std::uint8_t>(11),
                 "legacy NetAppearance block writes version 11");
      require_eq(eq2::core::read_u16_le(block, 1), static_cast<std::uint16_t>(222),
                 "legacy NetAppearance block writes primary model");
      require_eq(eq2::core::read_u16_le(block, 9), static_cast<std::uint16_t>(900),
                 "legacy NetAppearance block writes equipment slot zero");
      require_eq(eq2::core::read_u16_le(block, 9 + 23 * 8),
                 static_cast<std::uint16_t>(78),
                 "legacy NetAppearance block writes hair as client group 23");
      require_eq(block[9 + 23 * 8 + 2], static_cast<std::uint8_t>(11),
                 "legacy NetAppearance block writes hair color in group 23");
      require_eq(block[310], static_cast<std::uint8_t>(75),
                 "legacy NetAppearance block writes SOGA body size in the morph block");
      require_eq(block[311], static_cast<std::uint8_t>(25),
                 "legacy NetAppearance block writes SOGA body age in the morph block");
      require_eq(block[312], static_cast<std::uint8_t>(31),
                 "legacy NetAppearance block writes SOGA hair color after morph bytes");
      require_eq(eq2::core::read_u16_le(block, 321), static_cast<std::uint16_t>(88),
                 "legacy NetAppearance block writes SOGA hair type at client offset 0x46");
      require_eq(eq2::core::read_u16_le(block, 329), static_cast<std::uint16_t>(66),
                 "legacy NetAppearance block writes SOGA facial hair type at client offset 0x48");
    }
  }

  client.close();
  world_client.close();
  service.stop();
}

void live_login_forwards_play_request_and_returns_world_response() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  eq2::db::FakeCharacterListRepository characters;
  characters.characters_by_account[42] = {
      eq2::db::CharacterListRecord{
          .login_character_id = 11,
          .character_id = 2002,
          .server_id = 77,
          .name = "Alys",
          .level = 25,
      },
  };

  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      characters,
      supported_login_versions());

  require(service.start(), "play-forward test starts login listeners");

  eq2::net::TcpSocketClient world_client;
  require(world_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "play-forward test connects a world");
  require(world_client.send(make_ls_info_frame("world-account", "secret")),
          "play-forward test registers world");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_registered),
          "play-forward test waits for registered world");

  eq2::net::TcpSocketClient client;
  require(open_login_session(client, service.port()),
          "play-forward client completes session handshake");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "play-forward client sends login frame");
  require(client.receive().has_value(), "play-forward client receives login reply");
  require(wait_for_login_count(service, 1), "play-forward test reaches login service");

  require(client.send(make_sequenced_play_character_frame(2002, 77, 1, "Alys")),
          "play-forward client sends play-character request");

  auto forwarded = std::optional<eq2::protocol::UserToWorldRequest>{};
  for (auto attempt = 0; attempt < 6 && !forwarded.has_value(); ++attempt) {
    auto frame = world_client.receive();
    if (!frame.has_value()) {
      break;
    }
    const auto decoded = eq2::protocol::decode_interserver_packet(*frame);
    if (!decoded.has_value() ||
        decoded->opcode != eq2::protocol::kServerOpUserToWorldRequest) {
      continue;
    }
    forwarded = eq2::protocol::decode_user_to_world_request_payload(decoded->payload);
  }

  require(forwarded.has_value(), "play-forward world receives user-to-world request");
  if (forwarded.has_value()) {
    require_eq(forwarded->login_account_id, std::int32_t{42},
               "play-forward request carries login account id");
    require_eq(forwarded->character_id, std::int32_t{2002},
               "play-forward request carries character id");
    require_eq(forwarded->world_id, std::int32_t{77},
               "play-forward request carries world id");
  }

  const auto world_response_payload = eq2::protocol::encode_user_to_world_response_payload(
      eq2::protocol::UserToWorldResponse{
          .login_account_id = 42,
          .character_id = 2002,
          .world_id = 77,
          .access_key = 0x654321,
          .response = 1,
          .ip_address = "192.168.1.50",
          .port = 9101,
      });
  const auto world_response = eq2::protocol::encode_interserver_packet(
      eq2::protocol::kServerOpUserToWorldResponse, world_response_payload);
  require(world_response.has_value() && world_client.send(*world_response),
          "play-forward world sends user-to-world response");

  auto play_response = std::optional<eq2::protocol::PlayCharacterResponse>{};
  for (auto attempt = 0; attempt < 6 && !play_response.has_value(); ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      break;
    }
    play_response = decode_play_character_response(*response, 546);
  }

  require(play_response.has_value(),
          "play-forward client receives play-character response");
  if (play_response.has_value()) {
    require_eq(play_response->response, std::uint8_t{1},
               "play-forward response accepts character");
    require_eq(play_response->server, std::string_view("192.168.1.50"),
               "play-forward response carries world address");
    require_eq(play_response->port, std::uint16_t{9101},
               "play-forward response carries world port");
    require_eq(play_response->account_id, std::int32_t{42},
               "play-forward response carries account id");
    require_eq(play_response->access_code, std::int32_t{0x654321},
               "play-forward response carries access key");
  }
  require(wait_for_event(service, eq2::login::LiveLoginEventType::play_accepted),
          "play-forward service records accepted play event");

  client.close();
  world_client.close();
  service.stop();
}

void live_login_forwards_create_request_and_handles_world_response() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  eq2::db::FakeCharacterListRepository characters;

  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      characters,
      supported_login_versions());

  require(service.start(), "create-forward test starts login listeners");

  eq2::net::TcpSocketClient world_client;
  require(world_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "create-forward test connects a world");
  require(world_client.send(make_ls_info_frame("world-account", "secret")),
          "create-forward test registers world");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_registered),
          "create-forward test waits for registered world");

  eq2::net::TcpSocketClient client;
  require(open_login_session(client, service.port()),
          "create-forward client completes session handshake");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "create-forward client sends login frame");
  require(client.receive().has_value(), "create-forward client receives login reply");
  require(wait_for_login_count(service, 1), "create-forward test reaches login service");

  require(client.send(make_sequenced_create_character_frame(77, "Newchar", 1)),
          "create-forward client sends create-character request");

  auto forwarded = std::optional<eq2::protocol::CreateCharacterRequest>{};
  for (auto attempt = 0; attempt < 6 && !forwarded.has_value(); ++attempt) {
    auto frame = world_client.receive();
    if (!frame.has_value()) {
      break;
    }
    const auto decoded = eq2::protocol::decode_interserver_packet(*frame);
    if (!decoded.has_value() ||
        decoded->opcode != eq2::protocol::kServerOpCharacterCreate ||
        decoded->payload.size() < 2) {
      continue;
    }
    const auto version = eq2::core::read_u16_le(decoded->payload, 0);
    require_eq(version, std::uint16_t{546},
               "create-forward request prepends client version");
    forwarded = eq2::protocol::parse_create_character_request(
        decoded->payload.subspan(2), version);
  }

  require(forwarded.has_value(), "create-forward world receives character-create request");
  if (forwarded.has_value()) {
    require_eq(forwarded->account_id, std::int32_t{42},
               "create-forward request patches login account id");
    require_eq(forwarded->server_id, std::int32_t{77},
               "create-forward request carries server id");
    require_eq(forwarded->character_name, std::string_view("Newchar"),
               "create-forward request carries character name");
  }

  const auto world_response_payload = eq2::protocol::encode_create_character_response_payload(
      eq2::protocol::CreateCharacterResponse{
          .account_id = 42,
          .character_id = 3003,
          .response = 1,
      });
  const auto world_response = eq2::protocol::encode_interserver_packet(
      eq2::protocol::kServerOpCharacterCreate, world_response_payload);
  require(world_response.has_value() && world_client.send(*world_response),
          "create-forward world sends character-create response");

  auto create_reply = std::optional<eq2::protocol::CreateCharacterReply>{};
  auto saw_character_list_refresh = false;
  for (auto attempt = 0; attempt < 8 &&
                         (!create_reply.has_value() || !saw_character_list_refresh);
       ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      break;
    }
    if (!create_reply.has_value()) {
      create_reply = decode_create_character_reply(*response, 546);
    }
    if (!saw_character_list_refresh) {
      if (auto payload = character_list_payload(*response)) {
        saw_character_list_refresh = true;
      }
    }
  }

  require(create_reply.has_value(), "create-forward client receives create reply");
  if (create_reply.has_value()) {
    require_eq(create_reply->account_id, std::int32_t{42},
               "create-forward reply carries account id");
    require_eq(create_reply->response, std::uint8_t{1},
               "create-forward reply accepts character");
    require_eq(create_reply->character_name, std::string_view("Newchar"),
               "create-forward reply carries character name");
  }
  require(saw_character_list_refresh,
          "create-forward request refreshes character list");
  require_eq(characters.saved_created_characters.size(), static_cast<std::size_t>(1),
             "create-forward request saves login character row");
  if (!characters.saved_created_characters.empty()) {
    require_eq(characters.saved_created_characters.front().character_id, std::int32_t{3003},
               "create-forward save stores world character id");
    require_eq(characters.saved_created_characters.front().body_size, 0.75,
               "create-forward save stores created body size");
    require_eq(characters.saved_created_characters.front().appearance_files.race_file,
               std::string_view("model/human_male"),
               "create-forward save stores created race appearance file");
    require(!characters.saved_created_characters.front().appearance_values.empty(),
            "create-forward save stores created appearance colors");
  }
  const auto saved_account = characters.characters_by_account.find(42);
  require(saved_account != characters.characters_by_account.end() &&
              !saved_account->second.empty(),
          "create-forward save makes the created character visible to character select");
  if (saved_account != characters.characters_by_account.end() &&
      !saved_account->second.empty()) {
    const auto login_character_id = saved_account->second.front().login_character_id;
    const auto appearance = characters.appearance_by_login_character.find(login_character_id);
    require(appearance != characters.appearance_by_login_character.end() &&
                !appearance->second.empty(),
            "create-forward save stores colors against the login character id");
    if (appearance != characters.appearance_by_login_character.end() &&
        !appearance->second.empty()) {
      require_eq(appearance->second.front().type, std::string_view("skin_color"),
                 "create-forward save preserves the legacy color type");
      require(appearance->second.front().signed_value,
              "create-forward save preserves signed legacy color rows");
    }
  }

  auto auto_play = std::optional<eq2::protocol::UserToWorldRequest>{};
  for (auto attempt = 0; attempt < 6 && !auto_play.has_value(); ++attempt) {
    auto frame = world_client.receive();
    if (!frame.has_value()) {
      break;
    }
    const auto decoded = eq2::protocol::decode_interserver_packet(*frame);
    if (!decoded.has_value() ||
        decoded->opcode != eq2::protocol::kServerOpUserToWorldRequest) {
      continue;
    }
    auto_play = eq2::protocol::decode_user_to_world_request_payload(decoded->payload);
  }
  require(auto_play.has_value(),
          "create-forward legacy client receives automatic play handoff");
  if (auto_play.has_value()) {
    require_eq(auto_play->login_account_id, std::int32_t{42},
               "create-forward automatic play carries account id");
    require_eq(auto_play->character_id, std::int32_t{3003},
               "create-forward automatic play carries new character id");
    require_eq(auto_play->world_id, std::int32_t{77},
               "create-forward automatic play carries world id");
  }

  require(wait_for_event(service, eq2::login::LiveLoginEventType::character_create_accepted),
          "create-forward service records accepted create event");

  client.close();
  world_client.close();
  service.stop();
}

void live_login_applies_world_character_metadata_updates() {
  eq2::db::FakeLoginAccountRepository accounts;
  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  eq2::db::FakeCharacterListRepository characters;
  characters.characters_by_account[42] = {
      eq2::db::CharacterListRecord{
          .login_character_id = 11,
          .character_id = 2002,
          .server_id = 77,
          .name = "Alys",
          .race = 1,
          .character_class = 2,
          .gender = 3,
          .level = 10,
      },
  };

  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      characters,
      supported_login_versions());
  require(service.start(), "world-update test starts login listeners");

  eq2::net::TcpSocketClient world_client;
  require(world_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "world-update test connects a world");
  require(world_client.send(make_ls_info_frame("world-account", "secret")),
          "world-update test registers world");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_registered),
          "world-update test waits for registered world");

  const auto send_update = [&](std::uint16_t opcode,
                               const std::vector<std::uint8_t>& payload) {
    const auto frame = eq2::protocol::encode_interserver_packet(opcode, payload);
    require(frame.has_value() && world_client.send(*frame),
            "world-update test sends interserver character update");
  };

  send_update(eq2::protocol::kServerOpCharTimeStamp,
              eq2::protocol::encode_character_timestamp_payload(
                  eq2::protocol::CharacterTimeStamp{
                      .character_id = 2002,
                      .account_id = 42,
                      .unix_timestamp = 123456,
                  }));
  send_update(eq2::protocol::kServerOpBasicCharUpdate,
              eq2::protocol::encode_char_data_update_payload(
                  eq2::protocol::CharDataUpdate{
                      .account_id = 42,
                      .character_id = 2002,
                      .update_field = eq2::protocol::kCharUpdateLevelFlag,
                      .update_data = 35,
                  }));
  send_update(eq2::protocol::kServerOpRaceUpdate,
              eq2::protocol::encode_race_update_payload(
                  eq2::protocol::RaceUpdate{
                      .account_id = 42,
                      .character_id = 2002,
                      .model_type = 77,
                      .race = 9,
                  }));
  send_update(eq2::protocol::kServerOpNameCharUpdate,
              eq2::protocol::encode_char_name_update_payload(
                  eq2::protocol::CharNameUpdate{
                      .account_id = 42,
                      .character_id = 2002,
                      .name = "Renamed",
                  }));
  send_update(eq2::protocol::kServerOpZoneUpdate,
              eq2::protocol::encode_char_zone_update_payload(
                  eq2::protocol::CharZoneUpdate{
                      .account_id = 42,
                      .character_id = 2002,
                      .zone_id = 12,
                      .zone_name = "qeynos",
                  }));
  const std::vector<eq2::protocol::WorldZoneUpdate> zone_updates{
      eq2::protocol::WorldZoneUpdate{
          .zone_id = 12,
          .name = "qeynos",
          .description = "Qeynos",
      },
  };
  send_update(eq2::protocol::kServerOpZoneUpdates,
              eq2::protocol::encode_world_zone_updates_payload(zone_updates));
  const std::vector<eq2::protocol::EquipmentUpdate> equipment_updates{
      eq2::protocol::EquipmentUpdate{
          .id = 99,
          .world_character_id = 2002,
          .equip_type = 44,
          .red = 1,
          .green = 2,
          .blue = 3,
          .highlight_red = 4,
          .highlight_green = 5,
          .highlight_blue = 6,
          .slot = 7,
      },
  };
  send_update(eq2::protocol::kServerOpLoginEquipment,
              eq2::protocol::encode_login_equipment_update_payload(equipment_updates));
  const std::array<std::uint8_t, 4> picture_bytes{0xde, 0xad, 0xbe, 0xef};
  send_update(eq2::protocol::kServerOpCharacterPicture,
              eq2::protocol::encode_character_picture_update_payload(
                  eq2::protocol::CharacterPictureUpdate{
                      .account_id = 42,
                      .character_id = 2002,
                      .picture = picture_bytes,
                  }));

  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::character_updated, 7),
          "world-update test records all world character updates");
  const auto list = characters.load_character_list(42);
  require_eq(list.size(), static_cast<std::size_t>(1),
             "world-update test keeps the character row");
  if (!list.empty()) {
    require_eq(list.front().last_played, static_cast<std::int64_t>(123456),
               "world-update timestamp updates login character row");
    require_eq(list.front().level, std::int32_t{35},
               "world-update level updates login character row");
    require_eq(list.front().model_type, std::int32_t{77},
               "world-update race update stores model type");
    require_eq(list.front().race, std::int32_t{9},
               "world-update race updates login character row");
    require_eq(list.front().name, std::string_view("Renamed"),
               "world-update name updates login character row");
    require_eq(list.front().current_zone_id, std::int32_t{12},
               "world-update zone updates login character row");
  }
  require_eq(characters.world_zones_by_server[77].front().description,
             std::string_view("Qeynos"),
             "world-update zone list stores world zone descriptions");
  require_eq(characters.load_character_equipment(11).front().highlight_blue, 6,
             "world-update equipment list stores login equipment appearances");
  require_eq(characters.saved_character_pictures.front().picture,
             std::vector<std::uint8_t>(picture_bytes.begin(), picture_bytes.end()),
             "world-update character picture stores image bytes");

  world_client.close();
  service.stop();
}

void live_login_slice_deletes_character_and_refreshes_character_list() {
  eq2::db::FakeLoginAccountRepository accounts;
  accounts.add_account(42, "tester", "correct");
  eq2::db::FakeWorldRegistrationRepository worlds;
  worlds.accounts["world-account"] = eq2::db::WorldAccountRecord{
      .id = 77,
      .account = "world-account",
      .display_name = "Public World",
      .password = "secret",
  };
  eq2::db::FakeCharacterListRepository characters;
  characters.characters_by_account[42] = {
      eq2::db::CharacterListRecord{
          .login_character_id = 11,
          .character_id = 2002,
          .server_id = 77,
          .name = "Alys",
          .level = 25,
      },
  };

  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      worlds,
      characters,
      supported_login_versions());

  require(service.start(), "live login delete-character test starts TCP/UDP listeners");
  require(service.port() != 0, "live login delete-character test binds a shared port");

  eq2::net::TcpSocketClient world_client;
  require(world_client.connect_to(eq2::net::SocketEndpoint{.address = "127.0.0.1",
                                                           .port = service.port()}),
          "delete-character test connects a world");
  require(world_client.send(make_ls_info_frame("world-account", "secret")),
          "delete-character test registers world");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::world_registered),
          "delete-character test waits for registered world");

  eq2::net::TcpSocketClient client;
  require(open_login_session(client, service.port()),
          "delete-character client completes session handshake");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "delete-character client sends login frame");
  const auto login_response = client.receive();
  require(login_response.has_value(), "delete-character client receives login reply");
  require(wait_for_login_count(service, 1),
          "live login handles login before delete-character request");

  require(client.send(make_sequenced_delete_character_frame(2002, 77, "Alys", 1)),
          "delete-character client sends legacy delete request");

  auto forwarded_delete = std::optional<eq2::protocol::CharDataUpdate>{};
  for (auto attempt = 0; attempt < 6 && !forwarded_delete.has_value(); ++attempt) {
    auto frame = world_client.receive();
    if (!frame.has_value()) {
      break;
    }
    const auto decoded = eq2::protocol::decode_interserver_packet(*frame);
    if (!decoded.has_value() ||
        decoded->opcode != eq2::protocol::kServerOpBasicCharUpdate) {
      continue;
    }
    forwarded_delete = eq2::protocol::decode_char_data_update_payload(decoded->payload);
  }
  require(forwarded_delete.has_value(),
          "delete-character request forwards BasicCharUpdate to world");
  if (forwarded_delete.has_value()) {
    require_eq(forwarded_delete->account_id, std::int32_t{42},
               "delete-character world update carries account id");
    require_eq(forwarded_delete->character_id, std::int32_t{2002},
               "delete-character world update carries character id");
    require_eq(forwarded_delete->update_field, eq2::protocol::kCharUpdateDeleteFlag,
               "delete-character world update carries delete flag");
    require_eq(forwarded_delete->update_data, std::int32_t{1},
               "delete-character world update carries delete data");
  }

  auto delete_response = std::optional<eq2::protocol::DeleteCharacterResponse>{};
  auto character_list_account_id = std::optional<std::uint32_t>{};
  for (auto attempt = 0; attempt < 6 &&
                         (!delete_response.has_value() ||
                          !character_list_account_id.has_value());
       ++attempt) {
    auto response = client.receive();
    if (!response.has_value()) {
      break;
    }

    if (!delete_response.has_value()) {
      delete_response = decode_delete_character_response(*response);
    }
    if (!character_list_account_id.has_value()) {
      character_list_account_id = decode_character_list_account_id(*response);
    }
  }

  require(delete_response.has_value(),
          "delete-character client receives delete-character reply");
  if (delete_response.has_value()) {
    require_eq(delete_response->response, static_cast<std::uint8_t>(1),
               "delete-character reply accepts verified delete");
    require_eq(delete_response->server_id, static_cast<std::int32_t>(77),
               "delete-character reply carries server id");
    require_eq(delete_response->character_id, static_cast<std::int32_t>(2002),
               "delete-character reply carries character id");
    require_eq(delete_response->character_name, std::string_view("Alys"),
               "delete-character reply carries character name");
  }
  require_eq(character_list_account_id.value_or(0), static_cast<std::uint32_t>(42),
             "delete-character request refreshes character list");
  require_eq(characters.deleted_characters.size(), static_cast<std::size_t>(1),
             "delete-character request marks repository character deleted");
  require(wait_for_event(service, eq2::login::LiveLoginEventType::character_deleted),
          "live login records delete-character event");

  client.close();
  world_client.close();
  service.stop();
}

void live_login_slice_can_create_accounts_when_policy_allows_it() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{
          .address = "127.0.0.1",
          .port = 0,
          .account_creation_allowed = true,
      },
      accounts,
      supported_login_versions());

  require(service.start(), "live login creation test starts real loopback TCP transport");
  require(eq2::net::send_tcp_loopback(service.port(), make_live_login_frame("newbie", "newpass", 546)),
          "live login smoke harness sends new account request over real TCP");
  require(wait_for_login_count(service, 1), "live login service handles account creation frame");

  const auto outcomes = service.login_outcomes();
  require_eq(outcomes.front().reply_code, eq2::login::LoginReplyCode::accepted,
             "live login accepts account creation when configured");
  require(outcomes.front().account.has_value(), "live login returns created account");
  require_eq(outcomes.front().account->id, 88, "live login maps created account id");
  require_eq(connection->created_accounts.front(), std::string_view("newbie"),
             "live login creates account through SQL repository boundary");

  service.stop();
}

void live_login_slice_processes_simultaneous_tcp_clients() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login concurrent test starts real loopback TCP transport");

  eq2::net::TcpSocketClient first_client;
  eq2::net::TcpSocketClient second_client;
  require(open_login_session(first_client, service.port()),
          "first live login concurrent client completes session handshake");
  require(open_login_session(second_client, service.port()),
          "second live login concurrent client completes session handshake");

  require(first_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "first live login concurrent client sends login frame");
  require(second_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "second live login concurrent client sends login frame");

  const auto first_response = first_client.receive();
  const auto second_response = second_client.receive();
  require(first_response.has_value(), "first live login concurrent client receives reply");
  require(second_response.has_value(), "second live login concurrent client receives reply");
  if (first_response.has_value()) {
    require_eq(decode_reply_account_id(*first_response).value_or(0), std::uint32_t{42},
               "first live login concurrent reply carries account id");
  }
  if (second_response.has_value()) {
    require_eq(decode_reply_account_id(*second_response).value_or(0), std::uint32_t{42},
               "second live login concurrent reply carries account id");
  }
  require(wait_for_login_count(service, 2),
          "live login service handles two simultaneous TCP clients");
  const auto outcomes = service.login_outcomes();
  const auto duplicate_count = static_cast<std::size_t>(
      std::count_if(outcomes.begin(), outcomes.begin() + 2, [](const auto& outcome) {
        return outcome.should_disconnect_existing_session;
      }));
  require_eq(duplicate_count, static_cast<std::size_t>(1),
             "live login flags exactly one active duplicate account session");

  first_client.close();
  second_client.close();
  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::disconnected, 2),
          "live login records both concurrent client disconnects");

  const auto reconnect_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(reconnect_response.has_value(), "live login accepts clean reconnect after disconnects");
  require(wait_for_login_count(service, 3), "live login handles clean reconnect after disconnects");
  const auto reconnect_outcomes = service.login_outcomes();
  require(!reconnect_outcomes[2].should_disconnect_existing_session,
          "live login releases active account state after disconnect");

  service.stop();
}

void live_login_same_session_relogin_does_not_create_duplicate_state() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login same-session relogin test starts TCP transport");

  eq2::net::TcpSocketClient client;
  require(open_login_session(client, service.port()),
          "same-session relogin client completes session handshake");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "same-session relogin client sends first login frame");
  const auto first_response = client.receive();
  require(first_response.has_value(), "same-session relogin client receives first reply");
  require(client.send(make_sequenced_live_login_frame("tester", "correct", 546, 1)),
          "same-session relogin client sends second login frame");
  const auto second_response = client.receive();
  require(second_response.has_value(), "same-session relogin client receives second reply");
  require(wait_for_login_count(service, 2),
          "live login handles same-session relogin frames");

  const auto outcomes = service.login_outcomes();
  require(!outcomes[0].should_disconnect_existing_session,
          "first same-session login is not a duplicate");
  require(!outcomes[1].should_disconnect_existing_session,
          "second same-session login does not create duplicate state");

  client.close();
  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::disconnected, 1),
          "same-session relogin client disconnect releases session state");

  const auto reconnect_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(reconnect_response.has_value(),
          "same-session relogin allows clean reconnect after disconnect");
  require(wait_for_login_count(service, 3),
          "live login handles reconnect after same-session relogin disconnect");
  const auto reconnect_outcomes = service.login_outcomes();
  require(!reconnect_outcomes[2].should_disconnect_existing_session,
          "same-session relogin does not poison later reconnect duplicate state");

  service.stop();
}

void live_login_protocol_disconnect_releases_account_state() {
  auto connection = std::make_shared<ScriptedLoginConnection>();
  eq2::db::SqlLoginAccountRepository accounts(connection);
  eq2::login::LiveLoginService service(
      eq2::login::LoginServerConfig{.address = "127.0.0.1", .port = 0},
      accounts,
      supported_login_versions());

  require(service.start(), "live login protocol disconnect test starts TCP transport");

  eq2::net::TcpSocketClient first_client;
  require(open_login_session(first_client, service.port()),
          "protocol disconnect client completes session handshake");
  require(first_client.send(make_sequenced_live_login_frame("tester", "correct", 546, 0)),
          "protocol disconnect client sends login frame");
  const auto first_response = first_client.receive();
  require(first_response.has_value(), "protocol disconnect client receives login reply");
  require(wait_for_login_count(service, 1),
          "live login handles pre-disconnect login frame");

  const std::vector<std::uint8_t> empty_payload;
  const auto disconnect = eq2::protocol::encode_protocol_packet(
      eq2::protocol::kOpSessionDisconnect, empty_payload);
  require(first_client.send(disconnect),
          "protocol disconnect client sends session disconnect frame");
  require(wait_for_event_count(service, eq2::login::LiveLoginEventType::disconnected, 1),
          "live login records protocol-level disconnect");

  const auto second_response =
      login_over_session_handshake(service.port(), "tester", "correct", 546);
  require(second_response.has_value(),
          "live login accepts reconnect after protocol-level disconnect");
  require(wait_for_login_count(service, 2),
          "live login handles reconnect after protocol-level disconnect");
  const auto outcomes = service.login_outcomes();
  require(!outcomes[1].should_disconnect_existing_session,
          "protocol-level disconnect releases active account state");

  first_client.close();
  service.stop();
}

}  // namespace

int main() {
  login_server_config_loads_from_core_config();
  client_login_reaches_legacy_success_outcome_through_source2_boundaries();
  unsupported_client_version_reaches_legacy_bad_version_outcome();
  world_registration_uses_protocol_frame_and_db_repository();
  live_login_slice_uses_real_tcp_protocol_pipeline_and_sql_repository();
  live_login_event_formatting_is_redacted();
  live_login_slice_accepts_udp_client_login();
  live_login_stores_legacy_client_log_packets();
  live_login_slice_accepts_configured_one_byte_login_opcodes();
  live_login_slice_accepts_legacy_encrypted_embedded_login();
  live_login_registered_world_reaches_udp_world_list();
  live_login_registered_world_returns_non_empty_character_list();
  live_login_forwards_play_request_and_returns_world_response();
  live_login_forwards_create_request_and_handles_world_response();
  live_login_applies_world_character_metadata_updates();
  live_login_slice_deletes_character_and_refreshes_character_list();
  live_login_slice_can_create_accounts_when_policy_allows_it();
  live_login_slice_processes_simultaneous_tcp_clients();
  live_login_same_session_relogin_does_not_create_duplicate_state();
  live_login_protocol_disconnect_releases_account_state();

  if (failures != 0) {
    std::cerr << failures << " login server assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
