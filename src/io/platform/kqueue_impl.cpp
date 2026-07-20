module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_KQUEUE

#include <cerrno>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

module cnetmod.io.platform.kqueue;

import std;

namespace cnetmod {

kqueue_context::kqueue_context(std::size_t max_events) : events_(max_events) {
  kqueue_fd_ = ::kqueue();
  if (kqueue_fd_ < 0)
    throw std::system_error(errno, std::generic_category(), "kqueue failed");
  if (::pipe(pipe_fds_) < 0) {
    ::close(kqueue_fd_);
    throw std::system_error(errno, std::generic_category(), "pipe failed");
  }
  struct kevent event{};
  EV_SET(&event, pipe_fds_[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
  if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) < 0) {
    ::close(pipe_fds_[0]);
    ::close(pipe_fds_[1]);
    ::close(kqueue_fd_);
    throw std::system_error(errno, std::generic_category(),
                            "kevent(pipe) failed");
  }
}

kqueue_context::~kqueue_context() {
  if (pipe_fds_[0] >= 0)
    ::close(pipe_fds_[0]);
  if (pipe_fds_[1] >= 0)
    ::close(pipe_fds_[1]);
  if (kqueue_fd_ >= 0)
    ::close(kqueue_fd_);
}

void kqueue_context::run() {
  while (!stopped_.load(std::memory_order_relaxed))
    run_one_impl(nullptr);
}

auto kqueue_context::run_one() -> std::size_t { return run_one_impl(nullptr); }

auto kqueue_context::poll() -> std::size_t {
  struct timespec zero{};
  return run_one_impl(&zero);
}

void kqueue_context::stop() {
  stopped_.store(true, std::memory_order_relaxed);
  const char value = 1;
  (void)::write(pipe_fds_[1], &value, 1);
}

auto kqueue_context::stopped() const noexcept -> bool {
  return stopped_.load(std::memory_order_relaxed);
}

void kqueue_context::restart() {
  stopped_.store(false, std::memory_order_relaxed);
  char buffer[64];
  while (::read(pipe_fds_[0], buffer, sizeof(buffer)) > 0) {
  }
}

auto kqueue_context::add_event(int ident, int16_t filter, uint16_t flags,
                               void *udata)
    -> std::expected<void, std::error_code> {
  struct kevent event{};
  EV_SET(&event, static_cast<uintptr_t>(ident), filter, flags, 0, 0, udata);
  if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) < 0)
    return std::unexpected(std::error_code(errno, std::generic_category()));
  return {};
}

auto kqueue_context::delete_event(int ident, int16_t filter)
    -> std::expected<void, std::error_code> {
  struct kevent event{};
  EV_SET(&event, static_cast<uintptr_t>(ident), filter, EV_DELETE, 0, 0,
         nullptr);
  if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) < 0)
    return std::unexpected(std::error_code(errno, std::generic_category()));
  return {};
}

auto kqueue_context::native_handle() const noexcept -> int {
  return kqueue_fd_;
}

void kqueue_context::wake() {
  const char value = 1;
  (void)::write(pipe_fds_[1], &value, 1);
}

auto kqueue_context::run_one_impl(struct timespec *timeout) -> std::size_t {
  const int count = ::kevent(kqueue_fd_, nullptr, 0, events_.data(),
                             static_cast<int>(events_.size()), timeout);
  if (count <= 0)
    return 0;
  std::size_t handled = 0;
  for (int i = 0; i < count; ++i) {
    auto *udata = events_[i].udata;
    if (!udata) {
      char buffer[64];
      (void)::read(pipe_fds_[0], buffer, sizeof(buffer));
      handled += drain_post_queue();
      continue;
    }
    std::coroutine_handle<>::from_address(udata).resume();
    ++handled;
  }
  return handled;
}

} // namespace cnetmod

#endif // CNETMOD_HAS_KQUEUE
