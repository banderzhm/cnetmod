module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <cxxabi.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <ctime>

module cnetmod.core.crash_dump;
import std;

namespace cnetmod {

auto crash_dump::state() -> internal_state & {
  static internal_state s;
  return s;
}

auto crash_dump::make_timestamp() -> std::string {
  auto now = std::time(nullptr);
  char buf[64]{};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
  return buf;
}
auto crash_dump::make_timestamp_readable() -> std::string {
  auto now = std::time(nullptr);
  char buf[64]{};
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
  return buf;
}

#ifndef CNETMOD_PLATFORM_WINDOWS
auto crash_dump::capture_backtrace_unix() -> std::string {
  constexpr int max_frames = 64;
  void *frames[max_frames]{};
  const int count = backtrace(frames, max_frames);
  char **symbols = backtrace_symbols(frames, count);
  if (!symbols)
    return "  (backtrace_symbols failed)\n";
  std::string result;
  for (int i = 2; i < count; ++i) {
    std::string line = symbols[i];
    auto open = line.find('(');
    auto plus = line.find('+', open);
    if (open != std::string::npos && plus != std::string::npos) {
      auto mangled = line.substr(open + 1, plus - open - 1);
      int status = 0;
      char *demangled =
          abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
      result += std::format("  #{:>2} {}\n", i - 2,
                            status == 0 && demangled ? demangled : line);
      std::free(demangled);
    } else
      result += std::format("  #{:>2} {}\n", i - 2, line);
  }
  std::free(symbols);
  return result.empty() ? "  (no frames captured)\n" : result;
}
#endif

auto crash_dump::capture_stack_trace() -> std::string {
#if __cpp_lib_stacktrace >= 202011L
  auto trace = std::stacktrace::current(2);
  std::string result;
  int n = 0;
  for (const auto &entry : trace)
    result += std::format("  #{:>2} {}\n", n++, std::to_string(entry));
  return result.empty() ? "  (no stack trace available)\n" : result;
#elif !defined(CNETMOD_PLATFORM_WINDOWS)
  return capture_backtrace_unix();
#else
  return "  (std::stacktrace not available)\n";
#endif
}

auto crash_dump::get_pid() -> std::uint32_t {
#ifdef CNETMOD_PLATFORM_WINDOWS
  return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint32_t>(getpid());
#endif
}
auto crash_dump::write_text_report(const crash_info &info) -> std::string {
  auto timestamp = info.timestamp.empty() ? make_timestamp() : info.timestamp;
  auto path = std::format("{}/crash_{}.log", state().dump_dir, timestamp);
  FILE *file = std::fopen(path.c_str(), "w");
  if (!file)
    return {};
  std::fprintf(file,
               "=== CRASH REPORT ===\nApplication : %s\nTimestamp   : "
               "%s\nSignal      : %s (code %d)\n",
               state().app_name.c_str(), make_timestamp_readable().c_str(),
               info.signal_name.c_str(), info.signal_code);
  std::fprintf(file,
               "\n--- Stack Trace ---\n%s\n--- System Info ---\nPID         : "
               "%d\n===================\n",
               info.stack_trace.c_str(), static_cast<int>(get_pid()));
  std::fclose(file);
  return path;
}
void crash_dump::invoke_callback(const crash_info &info) {
  if (state().callback)
    try {
      state().callback(info);
    } catch (...) {
    }
}
auto crash_dump::signal_name(int sig) -> const char * {
#ifdef CNETMOD_PLATFORM_WINDOWS
  (void)sig;
  return "EXCEPTION";
#else
  switch (sig) {
  case SIGSEGV:
    return "SIGSEGV";
  case SIGABRT:
    return "SIGABRT";
  case SIGFPE:
    return "SIGFPE";
  case SIGILL:
    return "SIGILL";
  case SIGBUS:
    return "SIGBUS";
  default:
    return "UNKNOWN";
  }
#endif
}

#ifdef CNETMOD_PLATFORM_WINDOWS
auto crash_dump::exception_code_name(DWORD code) -> std::string {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION:
    return "ACCESS_VIOLATION";
  case EXCEPTION_STACK_OVERFLOW:
    return "STACK_OVERFLOW";
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    return "INT_DIVIDE_BY_ZERO";
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    return "ILLEGAL_INSTRUCTION";
  default:
    return std::format("EXCEPTION_0x{:08X}", code);
  }
}
auto crash_dump::write_minidump(EXCEPTION_POINTERS *ep, const std::string &ts)
    -> std::string {
  auto path = std::format("{}/crash_{}.dmp", state().dump_dir, ts);
  HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return {};
  MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
  const BOOL ok = MiniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), file,
      static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData),
      &mei, nullptr, nullptr);
  CloseHandle(file);
  return ok ? path : std::string{};
}
long __stdcall crash_dump::win_exception_handler(EXCEPTION_POINTERS *ep) {
  crash_info info{exception_code_name(ep->ExceptionRecord->ExceptionCode),
                  static_cast<int>(ep->ExceptionRecord->ExceptionCode),
                  make_timestamp(),
                  capture_stack_trace(),
                  {}};
  auto dump = write_minidump(ep, info.timestamp);
  auto report = write_text_report(info);
  info.dump_file_path = dump.empty() ? report : dump;
  invoke_callback(info);
  return EXCEPTION_CONTINUE_SEARCH;
}
#else
void crash_dump::unix_signal_handler(int sig, siginfo_t *si, void *) {
  crash_info info{
      signal_name(sig), sig, make_timestamp(), capture_backtrace_unix(), {}};
  info.dump_file_path = write_text_report(info);
  char message[512]{};
  const int len =
      std::snprintf(message, sizeof(message),
                    "\n[CRASH] %s (signal %d) at address %p\n  Report: %s\n",
                    info.signal_name.c_str(), sig, si ? si->si_addr : nullptr,
                    info.dump_file_path.c_str());
  if (len > 0) [[maybe_unused]]
    auto ignored = write(STDERR_FILENO, message, static_cast<size_t>(len));
  invoke_callback(info);
  raise(sig);
}
void crash_dump::enable_core_dump() {
  rlimit limit{};
  getrlimit(RLIMIT_CORE, &limit);
  limit.rlim_cur = limit.rlim_max;
  setrlimit(RLIMIT_CORE, &limit);
}
#endif

void crash_dump::install(std::string dump_dir) {
  state().dump_dir = std::move(dump_dir);
  std::filesystem::create_directories(state().dump_dir);
#ifdef CNETMOD_PLATFORM_WINDOWS
  SetUnhandledExceptionFilter(win_exception_handler);
#else
  enable_core_dump();
  struct sigaction action{};
  action.sa_sigaction = unix_signal_handler;
  action.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&action.sa_mask);
  sigaction(SIGSEGV, &action, nullptr);
  sigaction(SIGABRT, &action, nullptr);
  sigaction(SIGFPE, &action, nullptr);
  sigaction(SIGILL, &action, nullptr);
  sigaction(SIGBUS, &action, nullptr);
#endif
  state().installed = true;
}
void crash_dump::set_callback(callback_fn callback) {
  state().callback = std::move(callback);
}
void crash_dump::set_app_name(std::string name) {
  state().app_name = std::move(name);
}
void crash_dump::trigger_crash_report(std::string_view reason) {
  crash_info info{
      std::string(reason), 0, make_timestamp(), capture_stack_trace(), {}};
  info.dump_file_path = write_text_report(info);
  invoke_callback(info);
}

} // namespace cnetmod
