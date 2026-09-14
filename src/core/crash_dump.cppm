module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_PLATFORM_WINDOWS
    #include <Windows.h>
#else
    #include <signal.h>
#endif

/**
 * @brief Defines the process-crash artifact capture contract.
 *
 * Windows writes a minidump; Unix enables the operating system core-dump
 * policy and emits a companion text report when the runtime permits it.
 */
export module cnetmod.core.crash_dump;

import std;

export namespace cnetmod {

/**
 * @brief Describes the artifact produced for a fatal process failure.
 */
struct crash_info
{
    std::string signal_name;
    int signal_code{0};
    std::string timestamp;
    std::string stack_trace;
    std::string dump_file_path;
};

/**
 * @brief Installs process-wide fatal-crash artifact capture.
 *
 * Install this during process bootstrap, before worker threads or listeners
 * start. Handlers are intentionally independent from application logging and
 * telemetry because those subsystems may be unavailable during a crash.
 */
class crash_dump
{
public:
    using callback_fn = std::function<void(const crash_info&)>;

    /**
     * @brief Installs the platform fatal-error handlers and creates dump_dir.
     * @param dump_dir Directory receiving crash artifacts.
     */
    static void install(std::string dump_dir = "crash");

    /**
     * @brief Sets the best-effort notification invoked after an artifact write.
     * @param fn Callback that must never throw or depend on a live event loop.
     */
    static void set_callback(callback_fn fn);

    /**
     * @brief Sets the application label included in text crash reports.
     * @param name Process name suitable for diagnostics.
     */
    static void set_app_name(std::string name);

    /**
     * @brief Writes a non-fatal diagnostic report for the supplied reason.
     * @param reason Stable failure classification; do not include secrets.
     */
    static void trigger_crash_report(std::string_view reason);

private:
    struct internal_state
    {
        std::string dump_dir = "crash";
        std::string app_name = "cnetmod";
        callback_fn callback;
        bool installed = false;
    };

    static auto state() -> internal_state&;
    static auto make_timestamp() -> std::string;
    static auto make_timestamp_readable() -> std::string;
    static auto capture_stack_trace() -> std::string;
#ifndef CNETMOD_PLATFORM_WINDOWS
    static auto capture_backtrace_unix() -> std::string;
#endif
    static auto write_text_report(const crash_info& info) -> std::string;
    static auto get_pid() -> std::uint32_t;
    static void invoke_callback(const crash_info& info);
    static auto signal_name(int sig) -> const char*;
#ifdef CNETMOD_PLATFORM_WINDOWS
    static long __stdcall win_exception_handler(EXCEPTION_POINTERS* ep);
    static auto write_minidump(EXCEPTION_POINTERS* ep, const std::string& ts)
        -> std::string;
    static auto exception_code_name(DWORD code) -> std::string;
#else
    static void unix_signal_handler(int sig, siginfo_t* si, void* context);
    static void enable_core_dump();
#endif
};

} // namespace cnetmod
