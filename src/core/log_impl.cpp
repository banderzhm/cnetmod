module;

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

module cnetmod.core.log;
import std;

namespace logger::detail {

struct log_event {
  level severity{};
  output_format format{output_format::text};
  bool has_source{};
  std::string timestamp;
  std::string thread_id;
  std::string source;
  std::string message;
};

struct logger_state {
  std::mutex mutex;
  std::condition_variable wake;
  std::condition_variable drained;
  std::deque<log_event> queue;
  std::jthread worker;
  std::ofstream file;
  std::string name{"cnetmod"};
  std::size_t queue_limit{65536};
  std::size_t active_writes{};
  std::uint64_t dropped{};
  level threshold{level::info};
  output_format format{output_format::text};
  bool console{true};
  bool stopping{};
  bool ansi{};
};

auto state() -> logger_state & {
  static logger_state instance;
  return instance;
}

auto level_name(level value) -> std::string_view {
  switch (value) {
  case level::trace:
    return "trace";
  case level::debug:
    return "debug";
  case level::info:
    return "info";
  case level::warn:
    return "warn";
  case level::error:
    return "error";
  case level::critical:
    return "critical";
  case level::off:
    return "off";
  }
  return "unknown";
}

auto timestamp() -> std::string {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto day = floor<days>(now);
  const year_month_day ymd{day};
  auto time = now - day;
  const auto hours_part = floor<hours>(time);
  time -= hours_part;
  const auto minutes_part = floor<minutes>(time);
  time -= minutes_part;
  const auto seconds_part = floor<seconds>(time);
  time -= seconds_part;
  return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
                     static_cast<int>(ymd.year()),
                     static_cast<unsigned>(ymd.month()),
                     static_cast<unsigned>(ymd.day()), hours_part.count(),
                     minutes_part.count(), seconds_part.count(),
                     floor<milliseconds>(time).count());
}

auto thread_id() -> std::string {
  thread_local const auto cached =
      std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id()));
  return cached;
}
auto filename(std::string_view path) -> std::string_view {
  const auto pos = path.find_last_of("/\\");
  return pos == std::string_view::npos ? path : path.substr(pos + 1);
}
auto json_escape(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text)
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (static_cast<unsigned char>(c) < 0x20)
        out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
      else
        out += c;
    }
  return out;
}
auto can_enable_ansi() -> bool {
#ifdef _WIN32
  HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
  DWORD mode{};
  return handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode) &&
         SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
  return true;
#endif
}

void sink(logger_state &s, const log_event &event) {
  std::string line;
  if (event.format == output_format::json) {
    line =
        event.has_source
            ? std::format(
                  R"({{"timestamp":"{}","level":"{}","thread":"{}","source":"{}","message":"{}"}})",
                  event.timestamp, level_name(event.severity), event.thread_id,
                  event.source, json_escape(event.message))
            : std::format(
                  R"({{"timestamp":"{}","level":"{}","thread":"{}","message":"{}"}})",
                  event.timestamp, level_name(event.severity), event.thread_id,
                  json_escape(event.message));
  } else {
    line = event.has_source
               ? std::format("[{}] [{}] [{}] [{}] {}", event.timestamp,
                             level_name(event.severity), event.thread_id,
                             event.source, event.message)
               : std::format("[{}] [{}] [{}] {}", event.timestamp,
                             level_name(event.severity), event.thread_id,
                             event.message);
  }
  if (s.console)
    std::println(std::cerr, "{}", line);
  if (s.file.is_open())
    s.file << line << '\n';
}

void worker_loop(std::stop_token token) {
  auto &s = state();
  std::stop_callback wake_on_stop{token, [&s] { s.wake.notify_all(); }};
  for (;;) {
    log_event event;
    {
      std::unique_lock lock(s.mutex);
      s.wake.wait(lock, [&] {
        return token.stop_requested() || s.stopping || !s.queue.empty();
      });
      if (s.queue.empty()) {
        if (token.stop_requested() || s.stopping)
          break;
        continue;
      }
      event = std::move(s.queue.front());
      s.queue.pop_front();
      ++s.active_writes;
    }
    {
      std::lock_guard lock(s.mutex);
      sink(s, event);
      --s.active_writes;
      if (s.queue.empty() && s.active_writes == 0)
        s.drained.notify_all();
    }
  }
  std::lock_guard lock(s.mutex);
  s.drained.notify_all();
}
void ensure_worker() {
  auto &s = state();
  if (s.worker.joinable())
    return;
  s.stopping = false;
  s.worker = std::jthread([](std::stop_token token) { worker_loop(token); });
}
void stop_worker() {
  auto &s = state();
  std::jthread worker;
  {
    std::lock_guard lock(s.mutex);
    if (!s.worker.joinable())
      return;
    s.stopping = true;
    s.wake.notify_all();
    worker = std::move(s.worker);
  }
  worker.request_stop();
  worker.join();
}

void write_log(level value, std::string_view message,
               const std::source_location &location) {
  auto &s = state();
  std::lock_guard lock(s.mutex);
  if (value < s.threshold || s.threshold == level::off)
    return;
  ensure_worker();
  if (s.queue.size() >= s.queue_limit) {
    ++s.dropped;
    return;
  }
  s.queue.push_back(
      {value, s.format, true, timestamp(), thread_id(),
       std::format("{}:{}", filename(location.file_name()), location.line()),
       std::string(message)});
  s.wake.notify_one();
}
void write_log_no_src(level value, std::string_view message) {
  auto &s = state();
  std::lock_guard lock(s.mutex);
  if (value < s.threshold || s.threshold == level::off)
    return;
  ensure_worker();
  if (s.queue.size() >= s.queue_limit) {
    ++s.dropped;
    return;
  }
  s.queue.push_back({value,
                     s.format,
                     false,
                     timestamp(),
                     thread_id(),
                     {},
                     std::string(message)});
  s.wake.notify_one();
}

} // namespace logger::detail

namespace logger {
void init(const std::string &name, level value, output_format format) {
  auto &s = detail::state();
  std::lock_guard lock(s.mutex);
  s.name = name;
  s.threshold = value;
  s.format = format;
  s.console = true;
  s.ansi = format == output_format::text && detail::can_enable_ansi();
  if (s.file.is_open())
    s.file.close();
  detail::ensure_worker();
}
void init_with_file(const std::string &name, const std::string &path,
                    level value, output_format format, bool echo_console) {
  init(name, value, format);
  auto &s = detail::state();
  std::lock_guard lock(s.mutex);
  s.console = echo_console;
  s.file.open(path, std::ios::app);
}
void set_level(level value) {
  std::lock_guard lock(detail::state().mutex);
  detail::state().threshold = value;
}
void set_format(output_format value) {
  auto &s = detail::state();
  std::lock_guard lock(s.mutex);
  s.format = value;
  s.ansi = value == output_format::text && detail::can_enable_ansi();
}
void set_console_enabled(bool enabled) {
  std::lock_guard lock(detail::state().mutex);
  detail::state().console = enabled;
}
auto set_file_output(const std::string &path, bool append) -> bool {
  auto &s = detail::state();
  std::lock_guard lock(s.mutex);
  if (s.file.is_open())
    s.file.close();
  s.file.open(path, append ? std::ios::app : std::ios::trunc);
  return s.file.is_open();
}
void disable_file_output() {
  auto &s = detail::state();
  std::lock_guard lock(s.mutex);
  if (s.file.is_open()) {
    s.file.flush();
    s.file.close();
  }
}
void set_async_queue_limit(std::size_t limit) {
  std::lock_guard lock(detail::state().mutex);
  detail::state().queue_limit = std::max<std::size_t>(limit, 1024);
}
auto dropped_messages() -> std::uint64_t {
  std::lock_guard lock(detail::state().mutex);
  return detail::state().dropped;
}
void flush() {
  auto &s = detail::state();
  std::unique_lock lock(s.mutex);
  if (s.worker.joinable()) {
    s.wake.notify_all();
    s.drained.wait(lock,
                   [&] { return s.queue.empty() && s.active_writes == 0; });
  }
  if (s.file.is_open())
    s.file.flush();
}
void shutdown() {
  flush();
  detail::stop_worker();
  disable_file_output();
}
trace::trace(std::string_view message, const std::source_location &location) {
  detail::write_log(level::trace, message, location);
}
debug::debug(std::string_view message, const std::source_location &location) {
  detail::write_log(level::debug, message, location);
}
info::info(std::string_view message, const std::source_location &location) {
  detail::write_log(level::info, message, location);
}
warn::warn(std::string_view message, const std::source_location &location) {
  detail::write_log(level::warn, message, location);
}
error::error(std::string_view message, const std::source_location &location) {
  detail::write_log(level::error, message, location);
}
critical::critical(std::string_view message,
                   const std::source_location &location) {
  detail::write_log(level::critical, message, location);
}
} // namespace logger
