/**
 * @file crash_dump.cppm
 * @brief 崩溃核心转存 — 信号/SEH + 栈回溯 + MiniDump/Core Dump
 *
 * Windows: SetUnhandledExceptionFilter → MiniDump (.dmp) + 文本报告
 * Unix:    sigaction (SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS) → 文本报告 + core dump
 *
 * 使用示例:
 *   import cnetmod.core.crash_dump;
 *
 *   // 程序入口处安装
 *   cnetmod::crash_dump::install();                  // 默认输出到 ./crash/
 *   cnetmod::crash_dump::install("logs/crash");      // 自定义目录
 *   cnetmod::crash_dump::set_callback([](const auto& info) {
 *       // 自定义回调，如上报到远程服务
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
// crash_info — 崩溃上下文信息
// =============================================================================

export struct crash_info {
    std::string signal_name;       // 信号/异常名称
    int         signal_code{0};    // 信号编号 / Windows exception code
    std::string timestamp;         // 崩溃时间
    std::string stack_trace;       // 栈回溯文本
    std::string dump_file_path;    // MiniDump / report 文件路径
};

// =============================================================================
// crash_dump — 崩溃转存控制器
// =============================================================================

export class crash_dump {
public:
    using callback_fn = std::function<void(const crash_info&)>;

    /// 安装崩溃处理器
    /// @param dump_dir 转存文件输出目录（自动创建）
    static void install(std::string dump_dir = "crash") {
        state().dump_dir = std::move(dump_dir);

        // 确保目录存在
        std::filesystem::create_directories(state().dump_dir);

#ifdef CNETMOD_PLATFORM_WINDOWS
        SetUnhandledExceptionFilter(win_exception_handler);
#else
        // 允许生成 core dump
        enable_core_dump();

        // 注册信号处理
        struct sigaction sa{};
        sa.sa_sigaction = unix_signal_handler;
        sa.sa_flags     = SA_SIGINFO | SA_RESETHAND; // 一次性，防止递归
        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGFPE,  &sa, nullptr);
        sigaction(SIGILL,  &sa, nullptr);
        sigaction(SIGBUS,  &sa, nullptr);
#endif
        state().installed = true;
    }

    /// 设置崩溃回调（在转存完成后、进程退出前调用）
    static void set_callback(callback_fn fn) {
        state().callback = std::move(fn);
    }

    /// 设置应用名称（用于报告头）
    static void set_app_name(std::string name) {
        state().app_name = std::move(name);
    }

    /// 手动触发崩溃报告（用于 fatal error 场景）
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
    // 时间戳
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
    // 栈回溯
    // =========================================================================

    static auto capture_stack_trace() -> std::string {
        // C++23 std::stacktrace（MSVC 完整支持，Clang/GCC 可能受限）
#if __cpp_lib_stacktrace >= 202011L
        auto st = std::stacktrace::current(2); // 跳过自身 + 调用者
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
        for (int i = 2; i < count; ++i) { // 跳过自身帧
            // 尝试 demangle
            std::string line = symbols[i];
            // 格式通常: "/path/to/lib(mangled_name+0xoffset) [0xaddr]"
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
    // 文本报告写入
    // =========================================================================

    static auto write_text_report(const crash_info& info) -> std::string {
        auto ts   = info.timestamp.empty() ? make_timestamp() : info.timestamp;
        auto path = std::format("{}/crash_{}.log", state().dump_dir, ts);

        // 使用 C FILE* 写入（信号处理上下文更安全）
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
    // 回调调用
    // =========================================================================

    static void invoke_callback(const crash_info& info) {
        if (state().callback) {
            try {
                state().callback(info);
            } catch (...) {
                // 崩溃回调本身不能再崩溃
            }
        }
    }

    // =========================================================================
    // 信号名称
    // =========================================================================

    static auto signal_name(int sig) -> const char* {
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

        // 写入 MiniDump
        auto dmp_path = write_minidump(ep, info.timestamp);

        // 写入文本报告
        auto txt_path = write_text_report(info);
        info.dump_file_path = dmp_path.empty() ? txt_path : dmp_path;

        // 输出到 stderr
        std::fprintf(stderr,
            "\n[CRASH] %s (0x%08X)\n"
            "  Dump: %s\n"
            "  Report: %s\n",
            info.signal_name.c_str(), info.signal_code,
            dmp_path.c_str(), txt_path.c_str());

        invoke_callback(info);

        return EXCEPTION_CONTINUE_SEARCH; // 让系统默认处理也执行
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

        // MiniDumpWithDataSegs: 包含全局变量
        // MiniDumpWithHandleData: 包含句柄信息
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

        // 在信号处理上下文中获取栈回溯
        // 注意: backtrace() 不是 async-signal-safe，但崩溃后进程即将终止，
        // 实践中被广泛使用（glibc/libunwind 实现通常可在此场景工作）
        info.stack_trace = capture_backtrace_unix();

        // 写入文本报告
        auto path = write_text_report(info);
        info.dump_file_path = path;

        // 尽量输出到 stderr（write 是 async-signal-safe 的）
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

        // 重新发送信号让系统生成 core dump（SA_RESETHAND 已还原默认处理）
        raise(sig);
    }

    static void enable_core_dump() {
        struct rlimit rl{};
        getrlimit(RLIMIT_CORE, &rl);
        rl.rlim_cur = rl.rlim_max; // 设置为系统允许的最大值
        setrlimit(RLIMIT_CORE, &rl);
    }
#endif
};

} // namespace cnetmod
