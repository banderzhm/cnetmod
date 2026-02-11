module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_EPOLL

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>

export module cnetmod.io.platform.epoll;

import std;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.io.io_operation;

namespace cnetmod {

// =============================================================================
// epoll Context 实现
// =============================================================================

/// Linux epoll 实现（readiness-based）
export class epoll_context : public io_context {
public:
    explicit epoll_context(std::size_t max_events = 128)
        : events_(max_events)
    {
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0)
            throw std::system_error(
                errno, std::generic_category(), "epoll_create1 failed");

        event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (event_fd_ < 0) {
            ::close(epoll_fd_);
            throw std::system_error(
                errno, std::generic_category(), "eventfd failed");
        }

        // 注册 eventfd 到 epoll（用于 stop 唤醒）
        ::epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.ptr = nullptr;  // nullptr = stop 信号
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev) < 0) {
            ::close(event_fd_);
            ::close(epoll_fd_);
            throw std::system_error(
                errno, std::generic_category(), "epoll_ctl(eventfd) failed");
        }
    }

    ~epoll_context() override {
        if (event_fd_ >= 0) ::close(event_fd_);
        if (epoll_fd_ >= 0) ::close(epoll_fd_);
    }

    void run() override {
        while (!stopped_.load(std::memory_order_relaxed)) {
            run_one_impl(-1);
        }
    }

    auto run_one() -> std::size_t override {
        return run_one_impl(-1);
    }

    auto poll() -> std::size_t override {
        return run_one_impl(0);
    }

    void stop() override {
        stopped_.store(true, std::memory_order_relaxed);
        std::uint64_t val = 1;
        ::write(event_fd_, &val, sizeof(val));
    }

    [[nodiscard]] auto stopped() const noexcept -> bool override {
        return stopped_.load(std::memory_order_relaxed);
    }

    void restart() override {
        stopped_.store(false, std::memory_order_relaxed);
        // 排空 eventfd
        std::uint64_t val = 0;
        ::read(event_fd_, &val, sizeof(val));
    }

    // epoll 事件注册接口（供 async_op 层使用）

    [[nodiscard]] auto add(int fd, uint32_t events, void* user_data)
        -> std::expected<void, std::error_code>
    {
        ::epoll_event ev{};
        ev.events = events;
        ev.data.ptr = user_data;
        int op = EPOLL_CTL_ADD;
        if (::epoll_ctl(epoll_fd_, op, fd, &ev) < 0) {
            if (errno == EEXIST) {
                // 已存在，改为 MOD
                if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
                    return std::unexpected(
                        std::error_code(errno, std::generic_category()));
            } else {
                return std::unexpected(
                    std::error_code(errno, std::generic_category()));
            }
        }
        return {};
    }

    [[nodiscard]] auto modify(int fd, uint32_t events, void* user_data)
        -> std::expected<void, std::error_code>
    {
        ::epoll_event ev{};
        ev.events = events;
        ev.data.ptr = user_data;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0)
            return std::unexpected(
                std::error_code(errno, std::generic_category()));
        return {};
    }

    [[nodiscard]] auto remove(int fd)
        -> std::expected<void, std::error_code>
    {
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0)
            return std::unexpected(
                std::error_code(errno, std::generic_category()));
        return {};
    }

protected:
    void wake() override {
        std::uint64_t val = 1;
        ::write(event_fd_, &val, sizeof(val));
    }

private:
    auto run_one_impl(int timeout_ms) -> std::size_t {
        int n = ::epoll_wait(epoll_fd_, events_.data(),
                            static_cast<int>(events_.size()), timeout_ms);
        if (n <= 0) return 0;

        std::size_t handled = 0;
        for (int i = 0; i < n; ++i) {
            auto* ptr = events_[i].data.ptr;
            if (ptr == nullptr) {
                // eventfd（stop 或 post 唤醒）— 排空
                std::uint64_t val = 0;
                ::read(event_fd_, &val, sizeof(val));
                handled += drain_post_queue();
                continue;
            }
            // user_data 是 coroutine_handle<>::address()
            auto h = std::coroutine_handle<>::from_address(ptr);
            h.resume();
            ++handled;
        }
        return handled;
    }

    int epoll_fd_ = -1;
    int event_fd_ = -1;
    std::vector<::epoll_event> events_;
    std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_EPOLL
