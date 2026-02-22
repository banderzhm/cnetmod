/**
 * @file crash_dump.cppm
 * @brief Crash core dump — Signal/SEH + Stack trace + MiniDump/Core Dump
 *
 * Windows: SetUnhandledExceptionFilter → MiniDump (.dmp) + text report
 * Unix:    sigaction (SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS) → text report + core dump
 *
 * Usage example:
 *   import cnetmod.core.crash_dump;
 *
 *   // Install at program entry
 *   cnetmod::crash_dump::install();                  // Default output to ./crash/
 *   cnetmod::crash_dump::install("logs/crash");      // Custom directory
 *   cnetmod::crash_dump::set_callback([](const auto& info) {
 *       // Custom callback, e.g. report to remote service
 *   });
 */
module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>
#include <execinfo.h>   // backtrace / backtrace_symbols_fd
#include <cxxabi.h>     // __cxa_demangle
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

export module cnetmod.core.crash_dump;

import std;

namespace cnetmod {

// =============================================================================
// crash_info — Crash context information
// =============================================================================

export struct crash_info {
    std::string signal_name;       // Signal/exception name
    int         signal_code{0};    // Signal number / Windows exception code
    std::string timestamp;         // Crash time
    std::string stack_trace;       // Stack trace text
    std::string dump_file_path;    // MiniDump / report file path
};

// =============================================================================
// crash_dump — Crash dump controller
// =============================================================================

export class crash_dump {
public:
    using callback_fn = std::function<void(const crash_info&)>;

    /// Install crash handler
    /// @param dump_dir Dump file output directory (auto-created)
    static void install(std::string dump_dir = "crash") {
        state().dump_dir = std::move(dump_dir);

        // Ensure directory exists
        std::filesystem::create_directories(state().dump_dir);

#ifdef CNETMOD_PLATFORM_WINDOWS
        SetUnhandledExceptionFilter(win_exception_handler);
#else
        // Allow core dump generation
        enable_core_dump();

        // Register signal handlers
        struct sigaction sa{};
        sa.sa_sigaction = unix_signal_handler;
        sa.sa_flags     = SA_SIGINFO | SA_RESETHAND; // One-shot, prevent recursion
        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGFPE,  &sa, nullptr);
        sigaction(SIGILL,  &sa, nullptr);
        sigaction(SIGBUS,  &sa, nullptr);
#endif
        state().installed = true;
    }

    /// Set crash callback (called after dump completes, before process exits)
    static void set_callback(callback_fn fn) {
        state().callback = std::move(fn);
    }

    /// Set application name (for report header)
    static void set_app_name(std::string name) {
        state().app_name = std::move(name);
    }

    /// Manually trigger crash report (for fatal error scenarios)
    static void trigger_crash_report(std::string_view reason) {
        crash_info info;
        info.signal_name = std::string(reason);
        info.timestamp   = make_timestamp();
        info.stack_trace = capture_stack_trace();

        auto path = write_text_report(info);
        info.dump_file_path = path;

        invoke_callback(info);
    }

private:
    struct internal_state {
        std::string  dump_dir   = "crash";
        std::string  app_name   = "cnetmod";
        callback_fn  callback;
        bool         installed  = false;
    };

    static auto state() -> internal_state& {
        static internal_state s;
        return s;
    }

    // =========================================================================
    // Timestamp
    // =========================================================================

    static auto make_timestamp() -> std::string {
        auto now = std::time(nullptr);
        char buf[64]{};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
        return buf;
    }

    static auto make_timestamp_readable() -> std::string {
        auto now = std::time(nullptr);
        char buf[64]{};
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return buf;
    }

    // =========================================================================
    // Stack trace
    // =========================================================================

    static auto capture_stack_trace() -> std::string {
        // C++23 std::stacktrace (full support in MSVC, may be limited in Clang/GCC)
#if __cpp_lib_stacktrace >= 202011L
        auto st = std::stacktrace::current(2); // Skip self + caller
        std::string result;
        int frame_no = 0;
        for (const auto& entry : st) {
            result += std::format("  #{:>2} {}\n", frame_no++, std::to_string(entry));
        }
        if (result.empty()) result = "  (no stack trace available)\n";
        return result;
#elif !defined(CNETMOD_PLATFORM_WINDOWS)
        // Unix fallback: backtrace()
        return capture_backtrace_unix();
#else
        return "  (std::stacktrace not available)\n";
#endif
    }

#ifndef CNETMOD_PLATFORM_WINDOWS
    static auto capture_backtrace_unix() -> std::string {
        constexpr int max_frames = 64;
        void* frames[max_frames]{};
        int count = backtrace(frames, max_frames);

        char** symbols = backtrace_symbols(frames, count);
        if (!symbols) return "  (backtrace_symbols failed)\n";

        std::string result;
        for (int i = 2; i < count; ++i) { // Skip self frame
            // Try demangle
            std::string line = symbols[i];
            // Format usually: "/path/to/lib(mangled_name+0xoffset) [0xaddr]"
            auto paren_start = line.find('(');
            auto plus_pos    = line.find('+', paren_start);
            if (paren_start != std::string::npos && plus_pos != std::string::npos) {
                auto mangled = line.substr(paren_start + 1, plus_pos - paren_start - 1);
                int demangle_status = 0;
                char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr,
                                                       &demangle_status);
                if (demangle_status == 0 && demangled) {
                    result += std::format("  #{:>2} {}\n", i - 2, demangled);
                    std::free(demangled);
                } else {
                    result += std::format("  #{:>2} {}\n", i - 2, line);
                }
            } else {
                result += std::format("  #{:>2} {}\n", i - 2, line);
            }
        }
        std::free(symbols);
        if (result.empty()) result = "  (no frames captured)\n";
        return result;
    }
#endif

    // =========================================================================
    // Text report writing
    // =========================================================================

    static auto write_text_report(const crash_info& info) -> std::string {
        auto ts   = info.timestamp.empty() ? make_timestamp() : info.timestamp;
        auto path = std::format("{}/crash_{}.log", state().dump_dir, ts);

        // Use C FILE* for writing (safer in signal handler context)
        FILE* fp = std::fopen(path.c_str(), "w");
        if (!fp) return {};

        std::fprintf(fp, "=== CRASH REPORT ===\n");
        std::fprintf(fp, "Application : %s\n", state().app_name.c_str());
        std::fprintf(fp, "Timestamp   : %s\n", make_timestamp_readable().c_str());
        std::fprintf(fp, "Signal      : %s (code %d)\n",
                     info.signal_name.c_str(), info.signal_code);
        std::fprintf(fp, "\n--- Stack Trace ---\n");
        std::fprintf(fp, "%s", info.stack_trace.c_str());
        std::fprintf(fp, "\n--- System Info ---\n");
#ifdef CNETMOD_PLATFORM_WINDOWS
        std::fprintf(fp, "Platform    : Windows\n");
#elif defined(CNETMOD_PLATFORM_LINUX)
        std::fprintf(fp, "Platform    : Linux\n");
#elif defined(CNETMOD_PLATFORM_MACOS)
        std::fprintf(fp, "Platform    : macOS\n");
#endif
        std::fprintf(fp, "PID         : %d\n", static_cast<int>(get_pid()));
        std::fprintf(fp, "===================\n");
        std::fclose(fp);

        return path;
    }

    static auto get_pid() -> std::uint32_t {
#ifdef CNETMOD_PLATFORM_WINDOWS
        return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
        return static_cast<std::uint32_t>(getpid());
#endif
    }

    // =========================================================================
    // Callback invocation
    // =========================================================================

    static void invoke_callback(const crash_info& info) {
        if (state().callback) {
            try {
                state().callback(info);
            } catch (...) {
                // Crash callback itself must not crash
            }
        }
    }

    // =========================================================================
    // Signal names
    // =========================================================================

    static auto signal_name([[maybe_unused]] int sig) -> const char* {
#ifdef CNETMOD_PLATFORM_WINDOWS
        return "EXCEPTION";
#else
        switch (sig) {
            case SIGSEGV: return "SIGSEGV";
            case SIGABRT: return "SIGABRT";
            case SIGFPE:  return "SIGFPE";
            case SIGILL:  return "SIGILL";
            case SIGBUS:  return "SIGBUS";
            default:      return "UNKNOWN";
        }
#endif
    }

    // =========================================================================
    // Windows SEH
    // =========================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
    static LONG WINAPI win_exception_handler(EXCEPTION_POINTERS* ep) {
        crash_info info;
        info.signal_name = exception_code_name(ep->ExceptionRecord->ExceptionCode);
        info.signal_code = static_cast<int>(ep->ExceptionRecord->ExceptionCode);
        info.timestamp   = make_timestamp();
        info.stack_trace = capture_stack_trace();

        // Write MiniDump
        auto dmp_path = write_minidump(ep, info.timestamp);

        // Write text report
        auto txt_path = write_text_report(info);
        info.dump_file_path = dmp_path.empty() ? txt_path : dmp_path;

        // Output to stderr
        std::fprintf(stderr,
            "\n[CRASH] %s (0x%08X)\n"
            "  Dump: %s\n"
            "  Report: %s\n",
            info.signal_name.c_str(), info.signal_code,
            dmp_path.c_str(), txt_path.c_str());

        invoke_callback(info);

        return EXCEPTION_CONTINUE_SEARCH; // Let system default handler also execute
    }

    static auto write_minidump(EXCEPTION_POINTERS* ep,
                               const std::string& ts) -> std::string
    {
        auto path = std::format("{}/crash_{}.dmp", state().dump_dir, ts);

        HANDLE file = CreateFileA(
            path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file == INVALID_HANDLE_VALUE) return {};

        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;

        // MiniDumpWithDataSegs: Include global variables
        // MiniDumpWithHandleData: Include handle information
        auto dump_type = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithDataSegs | MiniDumpWithHandleData);

        BOOL ok = MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(),
            file, dump_type, &mei, nullptr, nullptr);

        CloseHandle(file);
        return ok ? path : std::string{};
    }

    static auto exception_code_name(DWORD code) -> std::string {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:    return "ACCESS_VIOLATION";
            case EXCEPTION_STACK_OVERFLOW:      return "STACK_OVERFLOW";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:  return "INT_DIVIDE_BY_ZERO";
            case EXCEPTION_INT_OVERFLOW:        return "INT_OVERFLOW";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:  return "FLT_DIVIDE_BY_ZERO";
            case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
            case EXCEPTION_BREAKPOINT:          return "BREAKPOINT";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
            case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
            default:
                return std::format("EXCEPTION_0x{:08X}", code);
        }
    }

#else
    // =========================================================================
    // Unix signal handler
    // =========================================================================

    static void unix_signal_handler(int sig, siginfo_t* si, void* /*ctx*/) {
        crash_info info;
        info.signal_name = signal_name(sig);
        info.signal_code = sig;
        info.timestamp   = make_timestamp();

        // Get stack trace in signal handler context
        // Note: backtrace() is not async-signal-safe, but process is about to terminate after crash,
        // widely used in practice (glibc/libunwind implementations usually work in this scenario)
        info.stack_trace = capture_backtrace_unix();

        // Write text report
        auto path = write_text_report(info);
        info.dump_file_path = path;

        // Try to output to stderr (write is async-signal-safe)
        char msg[512]{};
        int len = std::snprintf(msg, sizeof(msg),
            "\n[CRASH] %s (signal %d) at address %p\n"
            "  Report: %s\n"
            "  Core dump should be generated by OS (check ulimit -c)\n",
            info.signal_name.c_str(), sig,
            si ? si->si_addr : nullptr,
            path.c_str());
        if (len > 0) {
            [[maybe_unused]] auto _ = write(STDERR_FILENO, msg,
                                             static_cast<size_t>(len));
        }

        invoke_callback(info);

        // Re-send signal to let system generate core dump (SA_RESETHAND has restored default handler)
        raise(sig);
    }

    static void enable_core_dump() {
        struct rlimit rl{};
        getrlimit(RLIMIT_CORE, &rl);
        rl.rlim_cur = rl.rlim_max; // Set to system-allowed maximum
        setrlimit(RLIMIT_CORE, &rl);
    }
#endif
};

} // namespace cnetmod
