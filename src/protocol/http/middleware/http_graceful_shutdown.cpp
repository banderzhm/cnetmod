module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <signal.h>
#endif

module cnetmod.protocol.http.middleware.graceful_shutdown;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;

namespace cnetmod {

shutdown_handler *shutdown_handler::instance_ = nullptr;

void shutdown_handler::install() noexcept {
  instance_ = this;
#ifdef CNETMOD_PLATFORM_WINDOWS
  SetConsoleCtrlHandler(win_handler, TRUE);
#else
  struct sigaction sa{};
  sa.sa_handler = unix_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
#endif
}

auto shutdown_handler::is_signaled() const noexcept -> bool {
  return signaled_.load(std::memory_order_acquire);
}

auto shutdown_handler::in_flight() const noexcept -> std::int64_t {
  return in_flight_.load(std::memory_order_relaxed);
}

auto shutdown_handler::track_middleware() -> http::middleware_fn {
  return [this](http::request_context &ctx, http::next_fn next) -> task<void> {
    if (signaled_.load(std::memory_order_acquire)) {
      ctx.resp().set_header("Connection", "close");
      ctx.json(http::status::service_unavailable,
               R"({"error":"server is shutting down"})");
      co_return;
    }

    in_flight_.fetch_add(1, std::memory_order_relaxed);
    try {
      co_await next();
    } catch (...) {
      in_flight_.fetch_sub(1, std::memory_order_relaxed);
      throw;
    }
    in_flight_.fetch_sub(1, std::memory_order_relaxed);
  };
}

void shutdown_handler::signal() noexcept {
  signaled_.store(true, std::memory_order_release);
}

#ifdef CNETMOD_PLATFORM_WINDOWS
BOOL WINAPI shutdown_handler::win_handler(DWORD ctrl_type) {
  if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT ||
      ctrl_type == CTRL_CLOSE_EVENT) {
    if (instance_)
      instance_->signal();
    return TRUE;
  }
  return FALSE;
}
#else
void shutdown_handler::unix_handler(int) {
  if (instance_)
    instance_->signal();
}
#endif

} // namespace cnetmod
