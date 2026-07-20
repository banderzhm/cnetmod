/**
 * @file graceful_shutdown.cppm
 * @brief Graceful shutdown — signal handling + in-flight request waiting
 *
 * Registers SIGINT/SIGTERM (Windows: SetConsoleCtrlHandler),
 * wakes up waiters after receiving signal, works with drain to wait for
 * in-flight requests to complete.
 *
 * Usage example:
 *   import cnetmod.protocol.http.middleware.graceful_shutdown;
 *
 *   cnetmod::shutdown_handler sh;
 *   sh.install();  // Register signal handler
 *
 *   // In-flight request tracking (middleware auto +1/-1)
 *   svr.use(sh.track_middleware());
 *
 *   // Main coroutine waits for shutdown signal
 *   auto sleeper = [&](auto dur) { return async_sleep(io, dur); };
 *   co_await sh.wait_for_signal(sleeper);
 *   logger::info("Shutting down...");
 *
 *   // Wait for in-flight requests to complete (max 5 seconds)
 *   co_await sh.drain(sleeper, std::chrono::seconds{5});
 *
 *   srv.stop();
 *   ctx.stop();
 */
module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <Windows.h>
#else
#include <signal.h>
#endif

export module cnetmod.protocol.http.middleware.graceful_shutdown;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.core.log;

namespace cnetmod {

// =============================================================================
// shutdown_handler — Graceful shutdown controller
// =============================================================================

export class shutdown_handler {
public:
  shutdown_handler() noexcept = default;

  /// Register signal handler (SIGINT/SIGTERM / Windows Ctrl+C)
  /// Sets signaled_ and notifies waiters after receiving signal
  void install() noexcept;

  /// Whether shutdown signal has been received
  [[nodiscard]] auto is_signaled() const noexcept -> bool;

  /// Current number of in-flight requests
  [[nodiscard]] auto in_flight() const noexcept -> std::int64_t;

  // =========================================================================
  // wait_for_signal — Coroutine waits for shutdown signal
  // =========================================================================

  /// Implements signal waiting via polling (avoids cross-platform eventfd/pipe
  /// complexity) Checks every 100ms, returns after receiving signal
  ///
  /// @param sleep_fn Async sleep function, signature: (duration) -> task<void>
  ///   Typical usage: [&](auto dur) { return async_sleep(io, dur); }
  template <typename SleepFn>
  auto wait_for_signal(SleepFn sleep_fn) -> task<void> {
    while (!signaled_.load(std::memory_order_acquire)) {
      co_await sleep_fn(std::chrono::milliseconds{100});
    }
    logger::info("Shutdown signal received");
  }

  // =========================================================================
  // drain — Wait for in-flight requests to complete
  // =========================================================================

  /// Wait for all in-flight requests to complete, or force return after timeout
  /// @param sleep_fn Async sleep function, same as wait_for_signal
  /// @param timeout Maximum wait time
  /// @return true indicates normal drain, false indicates timeout
  template <typename SleepFn>
  auto drain(SleepFn sleep_fn, std::chrono::steady_clock::duration timeout)
      -> task<bool> {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (in_flight_.load(std::memory_order_acquire) > 0) {
      if (std::chrono::steady_clock::now() >= deadline) {
        logger::warn("Drain timeout, {} requests still in-flight",
                     in_flight_.load(std::memory_order_relaxed));
        co_return false;
      }
      co_await sleep_fn(std::chrono::milliseconds{50});
    }
    logger::info("All in-flight requests drained");
    co_return true;
  }

  // =========================================================================
  // track_middleware — In-flight request tracking middleware
  // =========================================================================

  /// Place in middleware chain to automatically track in-flight request count
  /// New requests after shutdown signal directly return 503
  auto track_middleware() -> http::middleware_fn;

private:
  std::atomic<bool> signaled_{false};
  std::atomic<std::int64_t> in_flight_{0};

  // Global instance pointer (signal handler must be static/global function)
  static shutdown_handler *instance_;

  void signal() noexcept;

#ifdef CNETMOD_PLATFORM_WINDOWS
  static BOOL WINAPI win_handler(DWORD ctrl_type);
#else
  static void unix_handler(int sig);
#endif
};

} // namespace cnetmod
