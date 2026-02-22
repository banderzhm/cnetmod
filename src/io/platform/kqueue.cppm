module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_KQUEUE

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#include <cerrno>

export module cnetmod.io.platform.kqueue;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

// =============================================================================
// kqueue Context implementation
// =============================================================================

/// macOS/BSD kqueue implementation (readiness-based)
export class kqueue_context : public io_context {
public:
    explicit kqueue_context(std::size_t max_events = 128)
        : events_(max_events)
    {
        kqueue_fd_ = ::kqueue();
        if (kqueue_fd_ < 0)
            throw std::system_error(
                errno, std::generic_category(), "kqueue failed");

        // Create pipe for stop signal
        if (::pipe(pipe_fds_) < 0) {
            ::close(kqueue_fd_);
            throw std::system_error(
                errno, std::generic_category(), "pipe failed");
        }

        // Register pipe read end to kqueue
        struct kevent ev{};
        EV_SET(&ev, pipe_fds_[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
        if (::kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr) < 0) {
            ::close(pipe_fds_[0]);
            ::close(pipe_fds_[1]);
            ::close(kqueue_fd_);
            throw std::system_error(
                errno, std::generic_category(), "kevent(pipe) failed");
        }
    }

    ~kqueue_context() override {
        if (pipe_fds_[0] >= 0) ::close(pipe_fds_[0]);
        if (pipe_fds_[1] >= 0) ::close(pipe_fds_[1]);
        if (kqueue_fd_ >= 0) ::close(kqueue_fd_);
    }

    void run() override {
        while (!stopped_.load(std::memory_order_relaxed)) {
            run_one_impl(nullptr);  // No timeout
        }
    }

    auto run_one() -> std::size_t override {
        return run_one_impl(nullptr);
    }

    auto poll() -> std::size_t override {
        struct timespec zero{};
        return run_one_impl(&zero);
    }

    void stop() override {
        stopped_.store(true, std::memory_order_relaxed);
        char c = 1;
        ::write(pipe_fds_[1], &c, 1);
    }

    [[nodiscard]] auto stopped() const noexcept -> bool override {
        return stopped_.load(std::memory_order_relaxed);
    }

    void restart() override {
        stopped_.store(false, std::memory_order_relaxed);
        // Drain pipe
        char buf[64];
        while (::read(pipe_fds_[0], buf, sizeof(buf)) > 0) {}
    }

    // kqueue event registration interface (for async_op layer use)

    [[nodiscard]] auto add_event(int ident, int16_t filter, uint16_t flags, void* udata)
        -> std::expected<void, std::error_code>
    {
        struct kevent ev{};
        EV_SET(&ev, static_cast<uintptr_t>(ident), filter, flags, 0, 0, udata);
        if (::kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr) < 0)
            return std::unexpected(
                std::error_code(errno, std::generic_category()));
        return {};
    }

    [[nodiscard]] auto delete_event(int ident, int16_t filter)
        -> std::expected<void, std::error_code>
    {
        struct kevent ev{};
        EV_SET(&ev, static_cast<uintptr_t>(ident), filter, EV_DELETE, 0, 0, nullptr);
        if (::kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr) < 0)
            return std::unexpected(
                std::error_code(errno, std::generic_category()));
        return {};
    }

    /// Returns underlying kqueue fd, for operations like async_timer_wait that need extended parameters
    [[nodiscard]] auto native_handle() const noexcept -> int {
        return kqueue_fd_;
    }

protected:
    void wake() override {
        char c = 1;
        ::write(pipe_fds_[1], &c, 1);
    }

private:
    auto run_one_impl(struct timespec* timeout) -> std::size_t {
        int n = ::kevent(kqueue_fd_, nullptr, 0,
                        events_.data(), static_cast<int>(events_.size()),
                        timeout);
        if (n <= 0) return 0;

        std::size_t handled = 0;
        for (int i = 0; i < n; ++i) {
            auto* udata = events_[i].udata;
            if (udata == nullptr) {
                // Pipe (stop or post wake) — drain
                char buf[64];
                ::read(pipe_fds_[0], buf, sizeof(buf));
                handled += drain_post_queue();
                continue;
            }
            auto h = std::coroutine_handle<>::from_address(udata);
            h.resume();
            ++handled;
        }
        return handled;
    }

    int kqueue_fd_ = -1;
    int pipe_fds_[2] = {-1, -1};
    std::vector<struct kevent> events_;
    std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_KQUEUE
