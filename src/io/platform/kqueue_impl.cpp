module;

#include <cnetmod/config.hpp>

#include <cerrno>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

module cnetmod.io.platform.kqueue;

import std;

namespace cnetmod {

namespace {

    auto make_non_blocking(int descriptor) noexcept -> std::error_code
    {
        const auto flags = ::fcntl(descriptor, F_GETFL, 0);
        if (flags < 0 || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0)
            return std::error_code(errno, std::generic_category());
        return {};
    }

} // namespace

void kqueue_completion::dispatch() const noexcept
{
    if (dispatch_callback)
        dispatch_callback(state);
}

kqueue_context::kqueue_context(std::size_t max_events)
    : events_(max_events)
{
    kqueue_fd_ = ::kqueue();
    if (kqueue_fd_ < 0)
        throw std::system_error(errno, std::generic_category(), "kqueue failed");
    if (::pipe(pipe_fds_) < 0)
    {
        ::close(kqueue_fd_);
        throw std::system_error(errno, std::generic_category(), "pipe failed");
    }
    const auto read_error = make_non_blocking(pipe_fds_[0]);
    const auto write_error = read_error ? std::error_code{}
                                        : make_non_blocking(pipe_fds_[1]);
    if (const auto error = read_error ? read_error : write_error)
    {
        ::close(pipe_fds_[0]);
        ::close(pipe_fds_[1]);
        ::close(kqueue_fd_);
        throw std::system_error(error, "fcntl(O_NONBLOCK) failed");
    }

    struct kevent event{};

    EV_SET(&event, pipe_fds_[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
    if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) < 0)
    {
        ::close(pipe_fds_[0]);
        ::close(pipe_fds_[1]);
        ::close(kqueue_fd_);
        throw std::system_error(errno, std::generic_category(),
            "kevent(pipe) failed");
    }
}

kqueue_context::~kqueue_context()
{
    if (pipe_fds_[0] >= 0)
        ::close(pipe_fds_[0]);
    if (pipe_fds_[1] >= 0)
        ::close(pipe_fds_[1]);
    if (kqueue_fd_ >= 0)
        ::close(kqueue_fd_);
}

void kqueue_context::run()
{
    execution_scope executing{*this};
    while (!stopped_.load(std::memory_order_relaxed))
        run_one_impl(nullptr);
}

auto kqueue_context::run_one() -> std::size_t
{
    execution_scope executing{*this};
    return run_one_impl(nullptr);
}

auto kqueue_context::poll() -> std::size_t
{
    execution_scope executing{*this};
    struct timespec zero{};

    return run_one_impl(&zero);
}

void kqueue_context::stop()
{
    stopped_.store(true, std::memory_order_relaxed);
    const char value = 1;
    (void)::write(pipe_fds_[1], &value, 1);
}

auto kqueue_context::stopped() const noexcept -> bool
{
    return stopped_.load(std::memory_order_relaxed);
}

void kqueue_context::restart()
{
    stopped_.store(false, std::memory_order_relaxed);
}

auto kqueue_context::add_event(int ident, int16_t filter, uint16_t flags,
    void* udata)
    -> std::expected<void, std::error_code>
{
    struct kevent event{};

    EV_SET(&event, static_cast<uintptr_t>(ident), filter, flags, 0, 0, udata);
    if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) < 0)
        return std::unexpected(std::error_code(errno, std::generic_category()));
    return {};
}

auto kqueue_context::delete_event(int ident, int16_t filter)
    -> std::expected<void, std::error_code>
{
    struct kevent event{};

    EV_SET(&event, static_cast<uintptr_t>(ident), filter, EV_DELETE, 0, 0,
        nullptr);
    if (::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) < 0)
        return std::unexpected(std::error_code(errno, std::generic_category()));
    return {};
}

auto kqueue_context::native_handle() const noexcept -> int
{
    return kqueue_fd_;
}

void kqueue_context::wake()
{
    const char value = 1;
    (void)::write(pipe_fds_[1], &value, 1);
}

auto kqueue_context::run_one_impl(struct timespec* timeout) -> std::size_t
{
    const int count = ::kevent(kqueue_fd_, nullptr, 0, events_.data(),
        static_cast<int>(events_.size()), timeout);
    if (count <= 0)
        return 0;
    std::size_t handled = 0;
    bool wake_pending = false;
    for (int i = 0; i < count; ++i)
    {
        auto* udata = events_[i].udata;
        if (!udata)
        {
            wake_pending = true;
            continue;
        }
        static_cast<kqueue_completion*>(udata)->dispatch();
        ++handled;
    }
    // Cancellation removes its kernel registration before posting a resume.
    // Process every event already returned by this kevent() call first so a
    // stale readiness notification can lose the operation's atomic claim
    // while the suspended frame is still alive.
    if (wake_pending)
    {
        char buffer[64];
        while (::read(pipe_fds_[0], buffer, sizeof(buffer)) > 0)
        {
        }
        handled += drain_post_queue();
    }
    return handled;
}

} // namespace cnetmod
