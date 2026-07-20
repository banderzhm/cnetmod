module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_KQUEUE

#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>

export module cnetmod.io.platform.kqueue;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

export class kqueue_context : public io_context {
public:
  explicit kqueue_context(std::size_t max_events = 128);
  ~kqueue_context() override;
  void run() override;
  [[nodiscard]] auto run_one() -> std::size_t override;
  [[nodiscard]] auto poll() -> std::size_t override;
  void stop() override;
  [[nodiscard]] auto stopped() const noexcept -> bool override;
  void restart() override;
  [[nodiscard]] auto add_event(int ident, int16_t filter, uint16_t flags,
                               void *udata)
      -> std::expected<void, std::error_code>;
  [[nodiscard]] auto delete_event(int ident, int16_t filter)
      -> std::expected<void, std::error_code>;
  [[nodiscard]] auto native_handle() const noexcept -> int;

protected:
  void wake() override;

private:
  auto run_one_impl(struct timespec *timeout) -> std::size_t;

  int kqueue_fd_ = -1;
  int pipe_fds_[2] = {-1, -1};
  std::vector<struct kevent> events_;
  std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_KQUEUE
