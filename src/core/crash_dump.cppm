module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <signal.h>
#endif

/** Crash reporting public contract. */
export module cnetmod.core.crash_dump;

import std;

export namespace cnetmod {

struct crash_info {
  std::string signal_name;
  int signal_code{0};
  std::string timestamp;
  std::string stack_trace;
  std::string dump_file_path;
};

class crash_dump {
public:
  using callback_fn = std::function<void(const crash_info &)>;

  static void install(std::string dump_dir = "crash");
  static void set_callback(callback_fn fn);
  static void set_app_name(std::string name);
  static void trigger_crash_report(std::string_view reason);

private:
  struct internal_state {
    std::string dump_dir = "crash";
    std::string app_name = "cnetmod";
    callback_fn callback;
    bool installed = false;
  };

  static auto state() -> internal_state &;
  static auto make_timestamp() -> std::string;
  static auto make_timestamp_readable() -> std::string;
  static auto capture_stack_trace() -> std::string;
#ifndef CNETMOD_PLATFORM_WINDOWS
  static auto capture_backtrace_unix() -> std::string;
#endif
  static auto write_text_report(const crash_info &info) -> std::string;
  static auto get_pid() -> std::uint32_t;
  static void invoke_callback(const crash_info &info);
  static auto signal_name(int sig) -> const char *;
#ifdef CNETMOD_PLATFORM_WINDOWS
  static long __stdcall win_exception_handler(EXCEPTION_POINTERS *ep);
  static auto write_minidump(EXCEPTION_POINTERS *ep, const std::string &ts)
      -> std::string;
  static auto exception_code_name(DWORD code) -> std::string;
#else
  static void unix_signal_handler(int sig, siginfo_t *si, void *context);
  static void enable_core_dump();
#endif
};

} // namespace cnetmod
