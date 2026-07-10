module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <signal.h>
#endif

module cnetmod.core.crash_dump;

import std;

namespace cnetmod {

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
    crash_info info;
    info.signal_name = std::string(reason);
    info.timestamp = make_timestamp();
    info.stack_trace = capture_stack_trace();
    info.dump_file_path = write_text_report(info);
    invoke_callback(info);
}

} // namespace cnetmod
