/** Public logging API. Runtime state and sinks live in log_impl.cpp. */
export module cnetmod.core.log;

import std;
export import :config;

export namespace logger {

namespace detail {
void write_log(
    level lv, std::string_view message,
    const std::source_location &loc = std::source_location::current());
void write_log_no_src(level lv, std::string_view message);

template <typename... Args> struct fmt_loc {
  std::format_string<Args...> fmt;
  std::source_location loc;

  template <typename S>
  consteval fmt_loc(const S &text, const std::source_location &location =
                                       std::source_location::current())
      : fmt(text), loc(location) {}
};
} // namespace detail

void init(const std::string &name = "cnetmod", level lv = level::info,
          output_format fmt = output_format::text);
void init_with_file(const std::string &name, const std::string &filepath,
                    level lv = level::info,
                    output_format fmt = output_format::text,
                    bool echo_console = true);
void set_level(level lv);
void set_format(output_format fmt);
void set_console_enabled(bool enabled);
auto set_file_output(const std::string &filepath, bool append = true) -> bool;
void disable_file_output();
void set_async_queue_limit(std::size_t max_queue);
auto dropped_messages() -> std::uint64_t;
void flush();
void shutdown();

struct trace {
  template <typename... Args>
  trace(detail::fmt_loc<std::type_identity_t<Args>...> format, Args &&...args) {
    detail::write_log(level::trace,
                      std::format(format.fmt, std::forward<Args>(args)...),
                      format.loc);
  }
  trace(std::string_view message,
        const std::source_location &loc = std::source_location::current());
};
struct debug {
  template <typename... Args>
  debug(detail::fmt_loc<std::type_identity_t<Args>...> format, Args &&...args) {
    detail::write_log(level::debug,
                      std::format(format.fmt, std::forward<Args>(args)...),
                      format.loc);
  }
  debug(std::string_view message,
        const std::source_location &loc = std::source_location::current());
};
struct info {
  template <typename... Args>
  info(detail::fmt_loc<std::type_identity_t<Args>...> format, Args &&...args) {
    detail::write_log(level::info,
                      std::format(format.fmt, std::forward<Args>(args)...),
                      format.loc);
  }
  info(std::string_view message,
       const std::source_location &loc = std::source_location::current());
};
struct warn {
  template <typename... Args>
  warn(detail::fmt_loc<std::type_identity_t<Args>...> format, Args &&...args) {
    detail::write_log(level::warn,
                      std::format(format.fmt, std::forward<Args>(args)...),
                      format.loc);
  }
  warn(std::string_view message,
       const std::source_location &loc = std::source_location::current());
};
struct error {
  template <typename... Args>
  error(detail::fmt_loc<std::type_identity_t<Args>...> format, Args &&...args) {
    detail::write_log(level::error,
                      std::format(format.fmt, std::forward<Args>(args)...),
                      format.loc);
  }
  error(std::string_view message,
        const std::source_location &loc = std::source_location::current());
};
struct critical {
  template <typename... Args>
  critical(detail::fmt_loc<std::type_identity_t<Args>...> format,
           Args &&...args) {
    detail::write_log(level::critical,
                      std::format(format.fmt, std::forward<Args>(args)...),
                      format.loc);
  }
  critical(std::string_view message,
           const std::source_location &loc = std::source_location::current());
};

} // namespace logger
