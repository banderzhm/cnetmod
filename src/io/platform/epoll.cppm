module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_EPOLL

#include <sys/epoll.h>

export module cnetmod.io.platform.epoll;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

export class epoll_context : public io_context {
public:
  explicit epoll_context(std::size_t max_events = 128);
  ~epoll_context() override;
  void run() override;
  [[nodiscard]] auto run_one() -> std::size_t override;
  [[nodiscard]] auto poll() -> std::size_t override;
  void stop() override;
  [[nodiscard]] auto stopped() const noexcept -> bool override;
  void restart() override;
  [[nodiscard]] auto add(int fd, uint32_t events, void *user_data)
      -> std::expected<void, std::error_code>;
  [[nodiscard]] auto modify(int fd, uint32_t events, void *user_data)
      -> std::expected<void, std::error_code>;
  [[nodiscard]] auto remove(int fd) -> std::expected<void, std::error_code>;

protected:
  void wake() override;

private:
  auto run_one_impl(int timeout_ms) -> std::size_t;

  int epoll_fd_ = -1;
  int event_fd_ = -1;
  std::vector<::epoll_event> events_;
  std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_EPOLL
