module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING

#include <liburing.h>

export module cnetmod.io.platform.io_uring;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

export struct uring_overlapped {
  std::coroutine_handle<> coroutine{};
  int32_t result = 0;
  std::uint16_t buffer_id = 0;
  void (*completion)(uring_overlapped &, int32_t, std::uint32_t) = nullptr;
  void *completion_context = nullptr;
};

/// Linux io_uring event loop.  The interface keeps the state layout only;
/// platform and syscall logic lives in io_uring_impl.cpp.
export class io_uring_context : public io_context {
public:
  explicit io_uring_context(unsigned queue_depth = 256);
  ~io_uring_context() override;

  void run() override;
  [[nodiscard]] auto run_one() -> std::size_t override;
  [[nodiscard]] auto poll() -> std::size_t override;
  void stop() override;
  [[nodiscard]] auto stopped() const noexcept -> bool override;
  [[nodiscard]] auto instance_id() const noexcept -> std::uint64_t;
  [[nodiscard]] auto native_ring() noexcept -> ::io_uring *;
  void restart() override;
  [[nodiscard]] auto get_sqe() -> ::io_uring_sqe *;
  [[nodiscard]] auto prepare_sqe() -> ::io_uring_sqe *;
  [[nodiscard]] auto flush() -> std::expected<int, std::error_code>;
  [[nodiscard]] auto flush_pending() -> std::expected<int, std::error_code>;
  [[nodiscard]] auto submit_batch_size() const noexcept -> unsigned;
  [[nodiscard]] auto submit() -> std::expected<int, std::error_code>;

protected:
  void wake() override;

private:
  static constexpr unsigned max_cqe_batch_ = 64;
  static constexpr unsigned default_cqe_tick_limit_ = 256;
  static constexpr unsigned max_cqe_tick_limit_ = 4096;
  static constexpr unsigned default_submit_batch_size_ = 1;
  static constexpr unsigned max_submit_batch_size_ = 128;

  static auto submit_batch_size_from_env() noexcept -> unsigned;
  static auto env_enabled(const char *name) noexcept -> bool;
  static auto cqe_tick_limit_from_env() noexcept -> unsigned;
  [[nodiscard]] auto submit_pending() -> std::expected<int, std::error_code>;
  auto run_one_impl(bool blocking) -> std::size_t;
  void submit_wake_read();

  ::io_uring ring_{};
  std::atomic<bool> stopped_{false};
  bool initialized_ = false;
  int pipe_fds_[2] = {-1, -1};
  char pipe_buf_ = 0;
  uring_overlapped wake_ov_{};
  unsigned pending_sqes_ = 0;
  unsigned submit_batch_size_ = default_submit_batch_size_;
  unsigned cqe_tick_limit_ = default_cqe_tick_limit_;
  inline static std::atomic<std::uint64_t> next_instance_id_{0};
  std::uint64_t instance_id_ = 0;
};

} // namespace cnetmod

#endif // CNETMOD_HAS_IO_URING
