module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_EPOLL

#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

module cnetmod.io.platform.epoll;

import std;

namespace cnetmod {

epoll_context::epoll_context(std::size_t max_events) : events_(max_events) {
  epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ < 0)
    throw std::system_error(errno, std::generic_category(),
                            "epoll_create1 failed");
  event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (event_fd_ < 0) {
    ::close(epoll_fd_);
    throw std::system_error(errno, std::generic_category(), "eventfd failed");
  }
  ::epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.ptr = nullptr;
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev) < 0) {
    ::close(event_fd_);
    ::close(epoll_fd_);
    throw std::system_error(errno, std::generic_category(),
                            "epoll_ctl(eventfd) failed");
  }
}

epoll_context::~epoll_context() {
  if (event_fd_ >= 0)
    ::close(event_fd_);
  if (epoll_fd_ >= 0)
    ::close(epoll_fd_);
}

void epoll_context::run() {
  while (!stopped_.load(std::memory_order_relaxed))
    run_one_impl(-1);
}

auto epoll_context::run_one() -> std::size_t { return run_one_impl(-1); }
auto epoll_context::poll() -> std::size_t { return run_one_impl(0); }

void epoll_context::stop() {
  stopped_.store(true, std::memory_order_relaxed);
  const std::uint64_t value = 1;
  (void)::write(event_fd_, &value, sizeof(value));
}

auto epoll_context::stopped() const noexcept -> bool {
  return stopped_.load(std::memory_order_relaxed);
}

void epoll_context::restart() {
  stopped_.store(false, std::memory_order_relaxed);
  std::uint64_t value = 0;
  (void)::read(event_fd_, &value, sizeof(value));
}

auto epoll_context::add(int fd, uint32_t events, void *user_data)
    -> std::expected<void, std::error_code> {
  ::epoll_event ev{};
  ev.events = events;
  ev.data.ptr = user_data;
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
    if (errno != EEXIST || ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
      return std::unexpected(std::error_code(errno, std::generic_category()));
  }
  return {};
}

auto epoll_context::modify(int fd, uint32_t events, void *user_data)
    -> std::expected<void, std::error_code> {
  ::epoll_event ev{};
  ev.events = events;
  ev.data.ptr = user_data;
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
    return std::unexpected(std::error_code(errno, std::generic_category()));
  return {};
}

auto epoll_context::remove(int fd) -> std::expected<void, std::error_code> {
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0)
    return std::unexpected(std::error_code(errno, std::generic_category()));
  return {};
}

void epoll_context::wake() {
  const std::uint64_t value = 1;
  (void)::write(event_fd_, &value, sizeof(value));
}

auto epoll_context::run_one_impl(int timeout_ms) -> std::size_t {
  const int count = ::epoll_wait(epoll_fd_, events_.data(),
                                 static_cast<int>(events_.size()), timeout_ms);
  if (count <= 0)
    return 0;
  std::size_t handled = 0;
  for (int i = 0; i < count; ++i) {
    auto *ptr = events_[i].data.ptr;
    if (!ptr) {
      std::uint64_t value = 0;
      (void)::read(event_fd_, &value, sizeof(value));
      handled += drain_post_queue();
      continue;
    }
    std::coroutine_handle<>::from_address(ptr).resume();
    ++handled;
  }
  return handled;
}

} // namespace cnetmod

#endif // CNETMOD_HAS_EPOLL
