module;

#include <cnetmod/config.hpp>

#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

module cnetmod.io.platform.epoll;

import std;

namespace cnetmod {

epoll_context::epoll_context(std::size_t max_events)
    : events_(max_events)
{
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0)
        throw std::system_error(errno, std::generic_category(),
            "epoll_create1 failed");
    event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd_ < 0)
    {
        ::close(epoll_fd_);
        throw std::system_error(errno, std::generic_category(), "eventfd failed");
    }
    ::epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.ptr = nullptr;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, event_fd_, &ev) < 0)
    {
        ::close(event_fd_);
        ::close(epoll_fd_);
        throw std::system_error(errno, std::generic_category(),
            "epoll_ctl(eventfd) failed");
    }
}

epoll_context::~epoll_context()
{
    if (event_fd_ >= 0)
        ::close(event_fd_);
    if (epoll_fd_ >= 0)
        ::close(epoll_fd_);
}

void epoll_context::run()
{
    while (!stopped_.load(std::memory_order_relaxed))
        run_one_impl(-1);
}

auto epoll_context::run_one() -> std::size_t
{
    return run_one_impl(-1);
}

auto epoll_context::poll() -> std::size_t
{
    return run_one_impl(0);
}

void epoll_context::stop()
{
    stopped_.store(true, std::memory_order_relaxed);
    const std::uint64_t value = 1;
    (void)::write(event_fd_, &value, sizeof(value));
}

auto epoll_context::stopped() const noexcept -> bool
{
    return stopped_.load(std::memory_order_relaxed);
}

void epoll_context::restart()
{
    stopped_.store(false, std::memory_order_relaxed);
    std::uint64_t value = 0;
    (void)::read(event_fd_, &value, sizeof(value));
}

auto epoll_context::arm(readiness_registration& registration)
    -> std::expected<void, std::error_code>
{
    ::epoll_event event{};
    event.events = registration.events;
    event.data.ptr = std::addressof(registration);
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, registration.fd, &event) < 0)
    {
        if (errno != EEXIST ||
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, registration.fd, &event) < 0)
            return std::unexpected(std::error_code(errno, std::generic_category()));
    }
    return {};
}

auto epoll_context::disarm_or_rearm(readiness_registration& registration)
    -> std::expected<void, std::error_code>
{
    const auto directions = registration.events & (EPOLLIN | EPOLLOUT);
    if (directions != 0U)
    {
        ::epoll_event event{};
        event.events = registration.events;
        event.data.ptr = std::addressof(registration);
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, registration.fd, &event) < 0)
            return std::unexpected(std::error_code(errno, std::generic_category()));
        return {};
    }
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, registration.fd, nullptr) < 0 &&
        errno != ENOENT)
        return std::unexpected(std::error_code(errno, std::generic_category()));
    return {};
}

auto epoll_context::add(int fd, uint32_t events, void* user_data, void (*ready)(void*) noexcept)
    -> std::expected<void, std::error_code>
{
    const auto directions = events & (EPOLLIN | EPOLLOUT);
    if (directions == 0U || (directions != EPOLLIN && directions != EPOLLOUT))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    auto entry = registrations_.find(fd);
    const bool inserted = entry == registrations_.end();
    if (inserted)
    {
        try
        {
            // Publish only a fully owned registration. Allocation failures must
            // neither leave a null slot nor escape a noexcept I/O awaiter.
            auto registration = std::make_unique<readiness_registration>(
                readiness_registration{.fd = fd});
            entry = registrations_.try_emplace(fd, std::move(registration)).first;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
        }
    }
    auto& registration = *entry->second;
    auto* const previous_waiter = directions == EPOLLIN
        ? registration.read_waiter
        : registration.write_waiter;
    const auto previous_events = registration.events;
    const auto previous_ready = directions == EPOLLIN ? registration.read_ready : registration.write_ready;
    if (directions == EPOLLIN)
    {
        registration.read_waiter = user_data;
        registration.read_ready = ready;
    }
    else
    {
        registration.write_waiter = user_data;
        registration.write_ready = ready;
    }
    registration.events |= directions | (events & ~(EPOLLIN | EPOLLOUT));

    if (auto armed = arm(registration); !armed)
    {
        if (directions == EPOLLIN)
        {
            registration.read_waiter = previous_waiter;
            registration.read_ready = previous_ready;
        }
        else
        {
            registration.write_waiter = previous_waiter;
            registration.write_ready = previous_ready;
        }
        registration.events = previous_events;
        if (inserted)
            registrations_.erase(entry);
        return std::unexpected(armed.error());
    }
    return {};
}

auto epoll_context::modify(int fd, uint32_t events, void* user_data)
    -> std::expected<void, std::error_code>
{
    return add(fd, events, user_data);
}

auto epoll_context::remove(int fd) -> std::expected<void, std::error_code>
{
    const auto found = registrations_.find(fd);
    if (found == registrations_.end())
        return {};
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0 && errno != ENOENT)
        return std::unexpected(std::error_code(errno, std::generic_category()));
    retire_registration(fd);
    return {};
}

auto epoll_context::remove(int fd, uint32_t events, void* user_data)
    -> std::expected<void, std::error_code>
{
    const auto directions = events & (EPOLLIN | EPOLLOUT);
    const auto found = registrations_.find(fd);
    if (found == registrations_.end())
        return {};
    auto& registration = *found->second;
    if ((directions & EPOLLIN) != 0U && registration.read_waiter == user_data)
    {
        registration.read_waiter = nullptr;
        registration.events &= ~EPOLLIN;
    }
    if ((directions & EPOLLOUT) != 0U && registration.write_waiter == user_data)
    {
        registration.write_waiter = nullptr;
        registration.events &= ~EPOLLOUT;
    }
    if (auto rearmed = disarm_or_rearm(registration); !rearmed)
        return std::unexpected(rearmed.error());
    if ((registration.events & (EPOLLIN | EPOLLOUT)) == 0U)
        retire_registration(fd);
    return {};
}

void epoll_context::wake()
{
    const std::uint64_t value = 1;
    (void)::write(event_fd_, &value, sizeof(value));
}

void epoll_context::retire_registration(int fd) noexcept
{
    const auto found = registrations_.find(fd);
    if (found == registrations_.end())
        return;
    if (dispatching_)
    {
        auto removed = std::move(found->second);
        removed->retired_next = std::move(retired_);
        retired_ = std::move(removed);
    }
    registrations_.erase(found);
}

void epoll_context::release_retired() noexcept
{
    while (retired_)
    {
        auto removed = std::move(retired_);
        retired_ = std::move(removed->retired_next);
    }
}

auto epoll_context::run_one_impl(int timeout_ms) -> std::size_t
{
    const int count = ::epoll_wait(epoll_fd_, events_.data(),
        static_cast<int>(events_.size()), timeout_ms);
    if (count <= 0)
        return 0;
    dispatching_ = true;

    struct batch_guard
    {
        epoll_context& context;

        ~batch_guard()
        {
            context.dispatching_ = false;
            context.release_retired();
        }
    } guard{*this};

    std::size_t handled = 0;
    for (int i = 0; i < count; ++i)
    {
        auto* raw = events_[i].data.ptr;
        if (!raw)
        {
            std::uint64_t value = 0;
            (void)::read(event_fd_, &value, sizeof(value));
            handled += drain_post_queue();
            continue;
        }
        auto* registration = static_cast<readiness_registration*>(raw);
        const auto found = registrations_.find(registration->fd);
        if (found == registrations_.end() || found->second.get() != registration)
            continue;

        const auto ready_events = events_[i].events;
        const bool terminal = (ready_events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U;
        void* read_waiter = nullptr;
        void* write_waiter = nullptr;
        const auto read_ready = registration->read_ready;
        const auto write_ready = registration->write_ready;
        if (terminal || (ready_events & EPOLLIN) != 0U)
        {
            read_waiter = registration->read_waiter;
            registration->read_waiter = nullptr;
            registration->events &= ~EPOLLIN;
        }
        if (terminal || (ready_events & EPOLLOUT) != 0U)
        {
            write_waiter = registration->write_waiter;
            registration->write_waiter = nullptr;
            registration->events &= ~EPOLLOUT;
        }

        (void)disarm_or_rearm(*registration);
        if ((registration->events & (EPOLLIN | EPOLLOUT)) == 0U)
            retire_registration(registration->fd);
        if (read_waiter)
        {
            if (read_ready)
                read_ready(read_waiter);
            else
                std::coroutine_handle<>::from_address(read_waiter).resume();
            ++handled;
        }
        if (write_waiter && write_waiter != read_waiter)
        {
            if (write_ready)
                write_ready(write_waiter);
            else
                std::coroutine_handle<>::from_address(write_waiter).resume();
            ++handled;
        }
    }
    return handled;
}

} // namespace cnetmod
