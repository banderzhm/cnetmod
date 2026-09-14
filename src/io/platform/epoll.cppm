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

export class epoll_context : public io_context
{
public:
    explicit epoll_context(std::size_t max_events = 128);
    ~epoll_context() override;
    void run() override;
    [[nodiscard]] auto run_one() -> std::size_t override;
    [[nodiscard]] auto poll() -> std::size_t override;
    void stop() override;
    [[nodiscard]] auto stopped() const noexcept -> bool override;
    void restart() override;
    [[nodiscard]] auto add(int fd, uint32_t events, void* user_data, void (*ready)(void*) noexcept = nullptr)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto modify(int fd, uint32_t events, void* user_data)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto remove(int fd) -> std::expected<void, std::error_code>;
    /// Remove only the matching directional readiness waiter.  A UDP socket
    /// commonly has an always-pending EPOLLIN receive and an occasional
    /// EPOLLOUT send waiter; cancelling one must not discard the other.
    [[nodiscard]] auto remove(int fd, uint32_t events, void* user_data)
        -> std::expected<void, std::error_code>;

protected:
    void wake() override;

private:
    struct readiness_registration
    {
        int fd{};
        uint32_t events{};
        void* read_waiter{};
        void* write_waiter{};
        void (*read_ready)(void*) noexcept = nullptr;
        void (*write_ready)(void*) noexcept = nullptr;
        std::unique_ptr<readiness_registration> retired_next;
    };

    [[nodiscard]] auto arm(readiness_registration& registration)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto disarm_or_rearm(readiness_registration& registration)
        -> std::expected<void, std::error_code>;

    auto run_one_impl(int timeout_ms) -> std::size_t;

    /**
     * Keeps removed registrations alive while a fetched event batch references them.
     */
    void retire_registration(int fd) noexcept;
    void release_retired() noexcept;

    int epoll_fd_ = -1;
    int event_fd_ = -1;
    std::vector<::epoll_event> events_;
    std::unordered_map<int, std::unique_ptr<readiness_registration>> registrations_;
    std::unique_ptr<readiness_registration> retired_;
    bool dispatching_ = false;
    std::atomic<bool> stopped_{false};
};

} // namespace cnetmod

#endif // CNETMOD_HAS_EPOLL
