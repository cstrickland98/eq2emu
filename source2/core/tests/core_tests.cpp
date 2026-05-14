#include <eq2/core/blocking_queue.h>
#include <eq2/core/byte_buffer.h>
#include <eq2/core/config.h>
#include <eq2/core/executor.h>
#include <eq2/core/log.h>
#include <eq2/core/result.h>
#include <eq2/core/shutdown.h>
#include <eq2/core/timer.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <span>
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

void result_carries_success_or_error() {
  auto success = eq2::core::Result<int>::success(42);
  require(success.has_value(), "successful result reports value");
  require_eq(success.value(), 42, "successful result carries value");

  auto failure = eq2::core::Result<int>::failure(eq2::core::Error{
      .code = eq2::core::ErrorCode::parse_error,
      .message = "bad config",
  });
  require(!failure.has_value(), "failed result reports no value");
  require_eq(failure.error().code, eq2::core::ErrorCode::parse_error, "failed result carries code");

  auto void_success = eq2::core::Result<void>::success();
  require(void_success.has_value(), "void result reports success");

  auto void_failure = eq2::core::Result<void>::failure(eq2::core::Error{
      .code = eq2::core::ErrorCode::cancelled,
      .message = "stopped",
  });
  require(!void_failure.has_value(), "void result reports failure");
  require_eq(void_failure.error().code, eq2::core::ErrorCode::cancelled, "void result carries error code");
}

void map_config_reads_strings_and_booleans() {
  eq2::core::MapConfig config;
  config.set("login.enabled", "true");
  config.set("login.host", "127.0.0.1");

  require_eq(config.get_or("login.host", "0.0.0.0"), "127.0.0.1", "config returns stored string");
  require_eq(config.get_or("missing", "fallback"), "fallback", "config returns fallback string");
  require(config.get_bool("login.enabled"), "config parses true boolean");
  require(!config.get_bool("missing"), "config returns boolean fallback");
}

class CapturingLogSink final : public eq2::core::LogSink {
 public:
  void write(const eq2::core::LogRecord& record) override {
    records.push_back(record);
  }

  std::vector<eq2::core::LogRecord> records;
};

void log_sink_receives_structured_records() {
  CapturingLogSink sink;
  eq2::core::log(sink, eq2::core::LogLevel::warning, "login", "bad client version");

  require_eq(sink.records.size(), static_cast<std::size_t>(1), "log sink receives one record");
  require_eq(sink.records.front().level, eq2::core::LogLevel::warning, "log record carries level");
  require_eq(sink.records.front().component, "login", "log record carries component");
  require_eq(sink.records.front().message, "bad client version", "log record carries message");
}

void manual_clock_drives_deadline_and_interval_timers() {
  using namespace std::chrono_literals;

  eq2::core::ManualClock clock;
  eq2::core::DeadlineTimer deadline(clock, 10ms);
  require(!deadline.expired(), "deadline starts unexpired");
  clock.advance(9ms);
  require(!deadline.expired(), "deadline remains unexpired before duration");
  clock.advance(1ms);
  require(deadline.expired(), "deadline expires at duration");

  eq2::core::IntervalTimer interval(clock, 5ms);
  require(!interval.consume_tick(), "interval does not consume early");
  clock.advance(5ms);
  require(interval.consume_tick(), "interval consumes when ready");
  require(!interval.consume_tick(), "interval advances to next tick");
}

void byte_buffer_writes_endian_values() {
  eq2::core::ByteBuffer buffer;
  buffer.append_u8(0xaa);
  buffer.append_u16_be(0x1234);
  buffer.append_u16_le(0x5678);
  buffer.append_u32_be(0x90abcdef);
  buffer.append_u32_le(0x10203040);

  const std::vector<std::uint8_t> expected{
      0xaa,
      0x12, 0x34,
      0x78, 0x56,
      0x90, 0xab, 0xcd, 0xef,
      0x40, 0x30, 0x20, 0x10,
  };

  const auto bytes = buffer.bytes();
  require(std::vector<std::uint8_t>(bytes.begin(), bytes.end()) == expected,
          "byte buffer writes big and little endian values");
}

void endian_helpers_read_and_write_spans_and_arrays() {
  std::array<std::uint8_t, 12> bytes{};
  eq2::core::write_u16_be(bytes, 0, 0x1234);
  eq2::core::write_u16_le(bytes, 2, 0x5678);
  eq2::core::write_u32_be(bytes, 4, 0x90abcdef);
  eq2::core::write_u32_le(bytes, 8, 0x10203040);

  require_eq(eq2::core::read_u16_be(bytes, 0), static_cast<std::uint16_t>(0x1234), "array reads big endian u16");
  require_eq(eq2::core::read_u16_le(bytes, 2), static_cast<std::uint16_t>(0x5678), "array reads little endian u16");
  require_eq(eq2::core::read_u32_be(bytes, 4), static_cast<std::uint32_t>(0x90abcdef), "array reads big endian u32");
  require_eq(eq2::core::read_u32_le(bytes, 8), static_cast<std::uint32_t>(0x10203040), "array reads little endian u32");
}

void blocking_queue_preserves_fifo_and_closes() {
  eq2::core::BlockingQueue<int> queue;
  require(queue.push(1), "queue accepts first value");
  require(queue.push(2), "queue accepts second value");

  auto first = queue.pop();
  auto second = queue.pop();
  require(first.has_value() && *first == 1, "queue pops first value");
  require(second.has_value() && *second == 2, "queue pops second value");

  queue.close();
  require(!queue.push(3), "closed queue rejects pushes");
  require(!queue.pop().has_value(), "closed empty queue returns no value");
}

void serial_executor_runs_posted_tasks_in_order() {
  eq2::core::SerialExecutor executor;
  std::mutex mutex;
  std::vector<int> observed;

  require(executor.post([&] {
    std::lock_guard lock(mutex);
    observed.push_back(1);
  }), "executor accepts first task");
  require(executor.post([&] {
    std::lock_guard lock(mutex);
    observed.push_back(2);
  }), "executor accepts second task");

  executor.stop();

  require_eq(observed.size(), static_cast<std::size_t>(2), "executor runs all queued tasks before stop");
  require_eq(observed[0], 1, "executor preserves first task order");
  require_eq(observed[1], 2, "executor preserves second task order");
  require(!executor.post([] {}), "stopped executor rejects new tasks");
}

void shutdown_signal_reports_stop_request() {
  eq2::core::ShutdownSignal signal;
  std::atomic_bool waiter_finished = false;

  std::thread waiter([&] {
    signal.wait();
    waiter_finished = true;
  });

  require(!signal.stop_requested(), "shutdown signal starts clear");
  signal.request_stop();
  waiter.join();

  require(signal.stop_requested(), "shutdown signal records stop request");
  require(waiter_finished, "shutdown signal wakes waiters");
}

}  // namespace

int main() {
  result_carries_success_or_error();
  map_config_reads_strings_and_booleans();
  log_sink_receives_structured_records();
  manual_clock_drives_deadline_and_interval_timers();
  byte_buffer_writes_endian_values();
  endian_helpers_read_and_write_spans_and_arrays();
  blocking_queue_preserves_fifo_and_closes();
  serial_executor_runs_posted_tasks_in_order();
  shutdown_signal_reports_stop_request();

  if (failures != 0) {
    std::cerr << failures << " core assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
