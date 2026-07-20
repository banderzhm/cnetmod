module;

#include <cnetmod/config.hpp>

export module cnetmod.io.io_context;

import std;
import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// post_node — lock-free MPSC queue node
// =============================================================================

/// Post queue node, supports two modes:
/// 1) Coroutine mode: coroutine.resume()
/// 2) Callback mode: callback(callback_arg)
export struct post_node {
  std::atomic<post_node *> next{nullptr};
  std::coroutine_handle<> coroutine{}; // Mode 1
  void (*callback)(void *) = nullptr;  // Mode 2
  void *callback_arg = nullptr;
  void (*callback_cleanup)(void *) =
      nullptr;             // Releases callback_arg if never dispatched
  bool heap_owned = false; // true = delete after drain

  void dispatch() noexcept;
};

// =============================================================================
// I/O Context abstract base class
// =============================================================================

/// I/O execution context interface
/// Encapsulates platform-specific I/O multiplexing mechanisms
/// (IOCP/io_uring/epoll/kqueue)
export class io_context {
public:
  virtual ~io_context();

  // Non-copyable or movable (base class)
  io_context(const io_context &) = delete;
  io_context(io_context &&) = delete;
  auto operator=(const io_context &) -> io_context & = delete;
  auto operator=(io_context &&) -> io_context & = delete;

  /// Run event loop, blocks until stopped
  virtual void run() = 0;

  /// Run event loop once (process ready events, then return)
  /// @return Number of events processed
  virtual auto run_one() -> std::size_t = 0;

  /// Poll ready events (non-blocking)
  /// @return Number of events processed
  virtual auto poll() -> std::size_t = 0;

  /// Stop event loop
  virtual void stop() = 0;

  /// Whether stopped
  [[nodiscard]] virtual auto stopped() const noexcept -> bool = 0;

  /// Reset context (can run again after stop)
  virtual void restart() = 0;

  /// Post a coroutine to event loop for execution (thread-safe, lock-free)
  void post(std::coroutine_handle<> h);

  /// Post a callback to event loop for execution (thread-safe, lock-free, zero
  /// coroutine overhead)
  void post(void (*fn)(void *), void *arg, void (*cleanup)(void *) = nullptr);

  /// Post a pre-constructed post_node (caller owns node lifetime, not deleted
  /// after drain)
  void post_node_raw(post_node *node);

protected:
  io_context() = default;

  /// Platform-specific: wake blocked event loop
  virtual void wake() = 0;

  /// Retrieve and execute all posted tasks, called by run loop
  auto drain_post_queue() -> std::size_t;

  /// Releases queued, heap-owned callbacks during context destruction.
  /// Coroutines and raw nodes remain caller-owned and are deliberately not
  /// resumed/destroyed.
  void discard_post_queue() noexcept;

private:
  /// MPSC push: atomic exchange + store next
  void push_node(post_node *node);

  /// Same as push_node, but marked as not released by drain
  void push_node_no_delete(post_node *node);

  std::atomic<post_node *> post_head_{nullptr};
};

/// co_await post_awaitable{ctx} — Switch current coroutine to io_context event
/// loop execution
export struct post_awaitable {
  io_context &ctx;
  auto await_ready() const noexcept -> bool;
  void await_suspend(std::coroutine_handle<> h) noexcept;
  void await_resume() noexcept;
};

/// Create platform default io_context
export [[nodiscard]] auto make_io_context() -> std::unique_ptr<io_context>;

} // namespace cnetmod
