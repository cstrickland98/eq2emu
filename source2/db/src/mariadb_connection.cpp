#include <eq2/db/sql_repositories.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#if defined(EQ2_SOURCE2_HAS_MARIADB)
#include <mysql.h>
#endif

namespace eq2::db {
namespace detail {

class MariaDbConnectionState {
 public:
#if defined(EQ2_SOURCE2_HAS_MARIADB)
  MariaDbConnectionState() = default;
  MariaDbConnectionState(const MariaDbConnectionState&) = delete;
  auto operator=(const MariaDbConnectionState&) -> MariaDbConnectionState& = delete;

  ~MariaDbConnectionState() {
    close();
  }

  [[nodiscard]] auto connected() const -> bool {
    return connection_ != nullptr;
  }

  [[nodiscard]] auto connection() const -> MYSQL* {
    return connection_;
  }

  void reset(MYSQL* connection) {
    close();
    connection_ = connection;
  }

  void close() {
    if (connection_ != nullptr) {
      mysql_close(connection_);
      connection_ = nullptr;
    }
  }

 private:
  MYSQL* connection_ = nullptr;
#else
  [[nodiscard]] auto connected() const -> bool {
    return false;
  }
#endif
};

}  // namespace detail

namespace {

auto unavailable(std::string message) -> eq2::core::Result<QueryResult> {
  return eq2::core::Result<QueryResult>::failure(eq2::core::Error{
      .code = eq2::core::ErrorCode::unavailable,
      .message = std::move(message),
  });
}

#if defined(EQ2_SOURCE2_HAS_MARIADB)

auto statement_failure(MYSQL_STMT* statement, std::string_view action)
    -> eq2::core::Result<QueryResult> {
  return unavailable(std::string(action) + ": " + mysql_stmt_error(statement));
}

auto connection_failure(MYSQL* connection, std::string_view action)
    -> eq2::core::Result<QueryResult> {
  return unavailable(std::string(action) + ": " + mysql_error(connection));
}

auto connection_open_failure(MYSQL* connection, const DatabaseConfig& config)
    -> eq2::core::Result<QueryResult> {
  auto message = std::string("MariaDB connection failed for host '") + config.host +
                 "' port " + std::to_string(config.port) +
                 " database '" + config.database +
                 "' user '" + config.username + "': " + mysql_error(connection) +
                 ". Check that MariaDB is listening on the configured address/port, "
                 "the host firewall allows inbound TCP, and the DB user is granted "
                 "from this source2 host.";
  return unavailable(std::move(message));
}

void initialize_mariadb_library() {
  static std::once_flag initialized;
  std::call_once(initialized, [] {
    mysql_library_init(0, nullptr, nullptr);
  });
}

auto connect(detail::MariaDbConnectionState& state, const DatabaseConfig& config)
    -> eq2::core::Result<QueryResult> {
  if (state.connected()) {
    return eq2::core::Result<QueryResult>::success(QueryResult{});
  }

  initialize_mariadb_library();

  auto* connection = mysql_init(nullptr);
  if (connection == nullptr) {
    return unavailable("MariaDB C API could not allocate a connection handle");
  }

  const auto connect_timeout = 5U;
  mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
  const my_bool use_tls = config.use_tls ? 1 : 0;
  mysql_options(connection, MYSQL_OPT_SSL_ENFORCE, &use_tls);
  const my_bool verify_server_cert = 0;
  mysql_options(connection, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify_server_cert);

  const auto* host = config.host.empty() ? nullptr : config.host.c_str();
  const auto* database = config.database.empty() ? nullptr : config.database.c_str();

  if (mysql_real_connect(connection,
                         host,
                         config.username.c_str(),
                         config.password.c_str(),
                         database,
                         config.port,
                         nullptr,
                         CLIENT_MULTI_STATEMENTS) == nullptr) {
    auto failure = connection_open_failure(connection, config);
    mysql_close(connection);
    return failure;
  }

  if (mysql_set_character_set(connection, "utf8mb4") != 0) {
    auto message = std::string("MariaDB character set setup failed: ") + mysql_error(connection);
    mysql_close(connection);
    return unavailable(std::move(message));
  }

  state.reset(connection);
  return eq2::core::Result<QueryResult>::success(QueryResult{});
}

auto read_result_set(MYSQL* connection, MYSQL_RES* result_set, QueryResult& result)
    -> eq2::core::Result<QueryResult> {
  const auto field_count = mysql_num_fields(result_set);
  const auto* fields = mysql_fetch_fields(result_set);

  while (auto* row = mysql_fetch_row(result_set)) {
    const auto* lengths = mysql_fetch_lengths(result_set);
    QueryRow query_row;

    for (auto field_index = 0U; field_index < field_count; ++field_index) {
      const auto* value = row[field_index];
      query_row.columns[fields[field_index].name] =
          value == nullptr ? std::string{} : std::string(value, lengths[field_index]);
    }

    result.rows.push_back(std::move(query_row));
  }

  if (mysql_errno(connection) != 0) {
    return connection_failure(connection, "MariaDB result fetch failed");
  }

  return eq2::core::Result<QueryResult>::success(QueryResult{});
}

auto execute_raw(MYSQL* connection, const QueryRequest& request)
    -> eq2::core::Result<QueryResult> {
  if (mysql_real_query(connection, request.sql.data(), static_cast<unsigned long>(request.sql.size())) !=
      0) {
    return connection_failure(connection, "MariaDB query failed");
  }

  QueryResult result;
  while (true) {
    MYSQL_RES* result_set = mysql_store_result(connection);
    if (result_set != nullptr) {
      auto read_result = read_result_set(connection, result_set, result);
      mysql_free_result(result_set);
      if (!read_result.has_value()) {
        return read_result;
      }
    } else if (mysql_field_count(connection) != 0) {
      return connection_failure(connection, "MariaDB result storage failed");
    }

    const auto affected_rows = mysql_affected_rows(connection);
    if (affected_rows != static_cast<my_ulonglong>(-1)) {
      result.affected_rows += static_cast<std::uint64_t>(affected_rows);
    }

    const auto next_status = mysql_next_result(connection);
    if (next_status == -1) {
      break;
    }

    if (next_status != 0) {
      return connection_failure(connection, "MariaDB next result failed");
    }
  }

  return eq2::core::Result<QueryResult>::success(std::move(result));
}

class StatementHandle {
 public:
  explicit StatementHandle(MYSQL* connection) : statement_(mysql_stmt_init(connection)) {}

  StatementHandle(const StatementHandle&) = delete;
  auto operator=(const StatementHandle&) -> StatementHandle& = delete;

  ~StatementHandle() {
    if (statement_ != nullptr) {
      mysql_stmt_close(statement_);
    }
  }

  [[nodiscard]] auto get() const -> MYSQL_STMT* {
    return statement_;
  }

 private:
  MYSQL_STMT* statement_ = nullptr;
};

class ResultMetadata {
 public:
  explicit ResultMetadata(MYSQL_STMT* statement)
      : result_(mysql_stmt_result_metadata(statement)) {}

  ResultMetadata(const ResultMetadata&) = delete;
  auto operator=(const ResultMetadata&) -> ResultMetadata& = delete;

  ~ResultMetadata() {
    if (result_ != nullptr) {
      mysql_free_result(result_);
    }
  }

  [[nodiscard]] auto get() const -> MYSQL_RES* {
    return result_;
  }

 private:
  MYSQL_RES* result_ = nullptr;
};

struct ParameterBindings {
  std::vector<MYSQL_BIND> binds;
  std::vector<unsigned long> lengths;
};

auto bind_parameters(MYSQL_STMT* statement,
                     const std::vector<QueryParameter>& parameters,
                     ParameterBindings& storage)
    -> eq2::core::Result<void> {
  if (parameters.empty()) {
    return eq2::core::Result<void>::success();
  }

  storage.binds.resize(parameters.size());
  storage.lengths.resize(parameters.size());

  for (std::size_t index = 0; index < parameters.size(); ++index) {
    std::memset(&storage.binds[index], 0, sizeof(MYSQL_BIND));
    storage.lengths[index] = static_cast<unsigned long>(parameters[index].size());

    storage.binds[index].buffer_type = MYSQL_TYPE_STRING;
    storage.binds[index].buffer = const_cast<char*>(parameters[index].data());
    storage.binds[index].buffer_length = storage.lengths[index];
    storage.binds[index].length = &storage.lengths[index];
  }

  if (mysql_stmt_bind_param(statement, storage.binds.data()) != 0) {
    return eq2::core::Result<void>::failure(eq2::core::Error{
        .code = eq2::core::ErrorCode::unavailable,
        .message = std::string("MariaDB parameter binding failed: ") + mysql_stmt_error(statement),
    });
  }

  return eq2::core::Result<void>::success();
}

auto read_prepared_rows(MYSQL_STMT* statement, MYSQL_RES* metadata, QueryResult& result)
    -> eq2::core::Result<QueryResult> {
  const auto field_count = mysql_num_fields(metadata);
  const auto* fields = mysql_fetch_fields(metadata);

  std::vector<MYSQL_BIND> result_binds(field_count);
  std::vector<unsigned long> lengths(field_count);
  std::vector<my_bool> is_null(field_count);
  std::vector<my_bool> errors(field_count);
  std::vector<char> scratch_buffers(field_count);

  for (auto field_index = 0U; field_index < field_count; ++field_index) {
    std::memset(&result_binds[field_index], 0, sizeof(MYSQL_BIND));
    result_binds[field_index].buffer_type = MYSQL_TYPE_STRING;
    result_binds[field_index].buffer = &scratch_buffers[field_index];
    result_binds[field_index].buffer_length = 1;
    result_binds[field_index].length = &lengths[field_index];
    result_binds[field_index].is_null = &is_null[field_index];
    result_binds[field_index].error = &errors[field_index];
  }

  if (mysql_stmt_bind_result(statement, result_binds.data()) != 0) {
    return statement_failure(statement, "MariaDB result binding failed");
  }

  const auto store_status = mysql_stmt_store_result(statement);
  if (store_status != 0) {
    return statement_failure(statement, "MariaDB result storage failed");
  }

  while (true) {
    const auto fetch_status = mysql_stmt_fetch(statement);
    if (fetch_status == MYSQL_NO_DATA) {
      break;
    }

    if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
      return statement_failure(statement, "MariaDB row fetch failed");
    }

    QueryRow row;
    for (auto field_index = 0U; field_index < field_count; ++field_index) {
      if (is_null[field_index]) {
        row.columns[fields[field_index].name] = "";
        continue;
      }

      if (lengths[field_index] == 0) {
        row.columns[fields[field_index].name] = "";
        continue;
      }

      std::string value(lengths[field_index], '\0');
      MYSQL_BIND column_bind;
      std::memset(&column_bind, 0, sizeof(MYSQL_BIND));
      column_bind.buffer_type = MYSQL_TYPE_STRING;
      column_bind.buffer = value.data();
      column_bind.buffer_length = static_cast<unsigned long>(value.size());
      column_bind.length = &lengths[field_index];

      if (mysql_stmt_fetch_column(statement, &column_bind, field_index, 0) != 0) {
        return statement_failure(statement, "MariaDB column fetch failed");
      }

      row.columns[fields[field_index].name] = std::move(value);
    }

    result.rows.push_back(std::move(row));
  }

  return eq2::core::Result<QueryResult>::success(QueryResult{});
}

auto execute_prepared(MYSQL* connection, const QueryRequest& request)
    -> eq2::core::Result<QueryResult> {
  StatementHandle statement(connection);
  if (statement.get() == nullptr) {
    return unavailable("MariaDB statement allocation failed");
  }

  if (mysql_stmt_prepare(statement.get(), request.sql.data(), static_cast<unsigned long>(request.sql.size())) !=
      0) {
    return statement_failure(statement.get(), "MariaDB statement prepare failed");
  }

  ParameterBindings parameter_bindings;
  auto bind_result = bind_parameters(statement.get(), request.parameters, parameter_bindings);
  if (!bind_result.has_value()) {
    return eq2::core::Result<QueryResult>::failure(bind_result.error());
  }

  if (mysql_stmt_execute(statement.get()) != 0) {
    return statement_failure(statement.get(), "MariaDB statement execution failed");
  }

  QueryResult result;
  ResultMetadata metadata(statement.get());
  if (metadata.get() != nullptr) {
    auto rows_result = read_prepared_rows(statement.get(), metadata.get(), result);
    if (!rows_result.has_value()) {
      return rows_result;
    }
  }

  const auto affected_rows = mysql_stmt_affected_rows(statement.get());
  if (affected_rows != static_cast<my_ulonglong>(-1)) {
    result.affected_rows = static_cast<std::uint64_t>(affected_rows);
  }

  return eq2::core::Result<QueryResult>::success(std::move(result));
}

#endif

}  // namespace

MariaDbConnection::MariaDbConnection(DatabaseConfig config)
    : config_(std::move(config)), state_(std::make_unique<detail::MariaDbConnectionState>()) {}

MariaDbConnection::~MariaDbConnection() = default;

auto MariaDbConnection::execute(const QueryRequest& request) -> eq2::core::Result<QueryResult> {
  std::lock_guard lock(mutex_);

#if defined(EQ2_SOURCE2_HAS_MARIADB)
  auto connect_result = connect(*state_, config_);
  if (!connect_result.has_value()) {
    return connect_result;
  }

  if (request.parameters.empty()) {
    return execute_raw(state_->connection(), request);
  }

  return execute_prepared(state_->connection(), request);
#else
  (void)request;
  return unavailable("MariaDB C API adapter is not enabled in this source2 build");
#endif
}

auto MariaDbConnection::config() const -> const DatabaseConfig& {
  return config_;
}

auto MariaDbConnection::is_connected() const -> bool {
  std::lock_guard lock(mutex_);
  return state_ != nullptr && state_->connected();
}

}  // namespace eq2::db
