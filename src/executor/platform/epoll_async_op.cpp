module;

#include <cnetmod/config.hpp>

// epoll is only used as async_op backend when epoll is available but io_uring is not
// When io_uring is available, io_uring is preferred
#if defined(CNETMOD_HAS_EPOLL) && !defined(CNETMOD_HAS_IO_URING)

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

#endif

module cnetmod.executor.async_op;

#if defined(CNETMOD_HAS_EPOLL) && !defined(CNETMOD_HAS_IO_URING)
import cnetmod.io.platform.epoll;
#endif
import cnetmod.coro.cancel;

namespace cnetmod {

#if defined(CNETMOD_HAS_EPOLL) && !defined(CNETMOD_HAS_IO_URING)

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

// =============================================================================
// epoll awaiter — Readiness Notification
// =============================================================================

/// Register fd to epoll, resume coroutine when ready
/// Use EPOLLONESHOT to ensure single trigger
struct epoll_awaiter {
    epoll_context& ctx;
    int fd;
    uint32_t events;               // EPOLLIN or EPOLLOUT
    std::error_code sync_error{};  // Synchronous error on registration failure

    auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> h) noexcept -> bool {
        auto r = ctx.add(fd, events | EPOLLONESHOT,
                         reinterpret_cast<void*>(h.address()));
        if (!r) {
            sync_error = r.error();
            return false;  // Registration failed, don't suspend
        }
        return true;
    }

    void await_resume() noexcept {}
};

auto last_error() noexcept -> std::error_code {
    return make_error_code(from_native_error(errno));
}

auto get_socket_family(int fd) -> int {
    ::sockaddr_storage addr{};
    ::socklen_t addrlen = sizeof(addr);
    if (::getsockname(fd, reinterpret_cast<::sockaddr*>(&addr), &addrlen) == 0)
        return addr.ss_family;
    return AF_INET;
}

auto fill_sockaddr(const endpoint& ep, ::sockaddr_storage& storage) noexcept -> ::socklen_t {
    std::memset(&storage, 0, sizeof(storage));
    if (ep.address().is_v4()) {
        auto& sa = reinterpret_cast<::sockaddr_in&>(storage);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(ep.port());
        sa.sin_addr = ep.address().to_v4().native();
        return sizeof(::sockaddr_in);
    } else {
        auto& sa = reinterpret_cast<::sockaddr_in6&>(storage);
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(ep.port());
        sa.sin6_addr = ep.address().to_v6().native();
        return sizeof(::sockaddr_in6);
    }
}

// =============================================================================
// epoll Cancel Version Awaiter
// =============================================================================

/// cancel_fn_: Remove fd from epoll, then post coroutine resume
static void epoll_cancel_fn(cancel_token& token) noexcept {
    auto* ep = static_cast<epoll_context*>(token.ctx_);
    ep->remove(token.fd_);
    if (token.coroutine_)
        ep->post(token.coroutine_);
}

/// epoll awaiter with cancel support
/// Register fd to epoll, resume coroutine on ready or cancel
struct epoll_cancel_awaiter {
    epoll_context& ctx;
    int fd;
    uint32_t events;
    cancel_token& token;
    std::error_code sync_error{};

    auto await_ready() const noexcept -> bool {
        return token.is_cancelled();
    }

    auto await_suspend(std::coroutine_handle<> h) noexcept -> bool {
        if (token.is_cancelled()) {
            sync_error = make_error_code(errc::operation_aborted);
            return false;
        }

        // Write cancel info
        token.ctx_ = &ctx;
        token.fd_ = fd;
        token.coroutine_ = h;
        token.cancel_fn_ = &epoll_cancel_fn;

        auto r = ctx.add(fd, events | EPOLLONESHOT,
                         reinterpret_cast<void*>(h.address()));
        if (!r) {
            sync_error = r.error();
            return false;
        }

        token.pending_.store(true, std::memory_order_release);

        // Double check: check cancelled again after setting pending
        if (token.is_cancelled()) {
            token.pending_.store(false, std::memory_order_relaxed);
            ctx.remove(fd);
            sync_error = make_error_code(errc::operation_aborted);
            return false;
        }

        return true;
    }

    void await_resume() noexcept {
        token.pending_.store(false, std::memory_order_relaxed);
    }
};

auto endpoint_from_sockaddr(const ::sockaddr_storage& sa) noexcept -> endpoint {
    if (sa.ss_family == AF_INET6) {
        const auto& sin6 = reinterpret_cast<const ::sockaddr_in6&>(sa);
        return endpoint{ipv6_address::from_native(sin6.sin6_addr),
                        ::ntohs(sin6.sin6_port)};
    }
    const auto& sin = reinterpret_cast<const ::sockaddr_in&>(sa);
    const auto* b = reinterpret_cast<const std::uint8_t*>(&sin.sin_addr);
    return endpoint{ipv4_address(b[0], b[1], b[2], b[3]),
                    ::ntohs(sin.sin_port)};
}

} // anonymous namespace

// =============================================================================
// Async Network Operations — epoll (readiness-based)
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    // Wait for listening socket to be readable (new connection ready)
    epoll_awaiter aw{epoll, static_cast<int>(listener.native_handle()), EPOLLIN};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    // Execute accept4 after ready
    int fd = ::accept4(static_cast<int>(listener.native_handle()),
                       nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        co_return std::unexpected(last_error());

    co_return socket::from_native(fd);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep)
    -> task<std::expected<void, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(ep, dest);

    int ret = ::connect(static_cast<int>(sock.native_handle()),
                        reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    if (ret == 0)
        co_return std::expected<void, std::error_code>{};

    if (errno != EINPROGRESS)
        co_return std::unexpected(last_error());

    // Wait for socket to be writable (connection complete)
    epoll_awaiter aw{epoll, static_cast<int>(sock.native_handle()), EPOLLOUT};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    // Check connection result
    int so_error = 0;
    ::socklen_t len = sizeof(so_error);
    ::getsockopt(static_cast<int>(sock.native_handle()),
                 SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0)
        co_return std::unexpected(make_error_code(from_native_error(so_error)));

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    // Wait for socket to be readable
    epoll_awaiter aw{epoll, static_cast<int>(sock.native_handle()), EPOLLIN};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ssize_t n = ::recv(static_cast<int>(sock.native_handle()),
                       buf.data, buf.size, 0);
    if (n < 0)
        co_return std::unexpected(last_error());
    if (n == 0)
        co_return std::unexpected(make_error_code(errc::end_of_file));

    co_return static_cast<std::size_t>(n);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    // Wait for socket to be writable
    epoll_awaiter aw{epoll, static_cast<int>(sock.native_handle()), EPOLLOUT};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ssize_t n = ::send(static_cast<int>(sock.native_handle()),
                       buf.data, buf.size, MSG_NOSIGNAL);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// Async File Operations — epoll (synchronous wrapper)
// epoll does not support async I/O for regular files, using pread/pwrite synchronously
// =============================================================================

auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                     std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    (void)ctx;
    ssize_t n = ::pread(static_cast<int>(f.native_handle()),
                        buf.data, buf.size, static_cast<off_t>(offset));
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                      std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    (void)ctx;
    ssize_t n = ::pwrite(static_cast<int>(f.native_handle()),
                         buf.data, buf.size, static_cast<off_t>(offset));
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>
{
    (void)ctx;
    if (::fsync(static_cast<int>(f.native_handle())) != 0)
        co_return std::unexpected(last_error());

    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Async Serial Port Operations — epoll (serial fd supports epoll events)
// =============================================================================

auto async_serial_read(io_context& ctx, serial_port& port, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_awaiter aw{epoll, static_cast<int>(port.native_handle()), EPOLLIN};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ssize_t n = ::read(static_cast<int>(port.native_handle()), buf.data, buf.size);
    if (n < 0)
        co_return std::unexpected(last_error());
    if (n == 0)
        co_return std::unexpected(make_error_code(errc::end_of_file));

    co_return static_cast<std::size_t>(n);
}

auto async_serial_write(io_context& ctx, serial_port& port, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_awaiter aw{epoll, static_cast<int>(port.native_handle()), EPOLLOUT};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ssize_t n = ::write(static_cast<int>(port.native_handle()), buf.data, buf.size);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// Async Timer — epoll (timerfd)
// =============================================================================

auto async_timer_wait(io_context& ctx,
                      std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>
{
    auto& ep = static_cast<epoll_context&>(ctx);

    int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0)
        co_return std::unexpected(last_error());

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    struct ::itimerspec its{};
    its.it_value.tv_sec = static_cast<time_t>(ns / 1000000000LL);
    its.it_value.tv_nsec = static_cast<long>(ns % 1000000000LL);
    if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0)
        its.it_value.tv_nsec = 1;  // Minimum 1ns

    if (::timerfd_settime(tfd, 0, &its, nullptr) < 0) {
        auto ec = last_error();
        ::close(tfd);
        co_return std::unexpected(ec);
    }

    epoll_awaiter aw{ep, tfd, EPOLLIN};
    co_await aw;

    // Read timerfd to clear timer
    std::uint64_t exp = 0;
    ::read(tfd, &exp, sizeof(exp));
    ::close(tfd);

    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Cancellable Version — Async Network Operations
// =============================================================================

auto async_accept(io_context& ctx, socket& listener, cancel_token& token)
    -> task<std::expected<socket, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_cancel_awaiter aw{epoll, static_cast<int>(listener.native_handle()),
                            EPOLLIN, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    int fd = ::accept4(static_cast<int>(listener.native_handle()),
                       nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        co_return std::unexpected(last_error());

    co_return socket::from_native(fd);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
                   cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(ep, dest);

    int ret = ::connect(static_cast<int>(sock.native_handle()),
                        reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    if (ret == 0)
        co_return std::expected<void, std::error_code>{};

    if (errno != EINPROGRESS)
        co_return std::unexpected(last_error());

    epoll_cancel_awaiter aw{epoll, static_cast<int>(sock.native_handle()),
                            EPOLLOUT, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    int so_error = 0;
    ::socklen_t len = sizeof(so_error);
    ::getsockopt(static_cast<int>(sock.native_handle()),
                 SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0)
        co_return std::unexpected(make_error_code(from_native_error(so_error)));

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
                cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_cancel_awaiter aw{epoll, static_cast<int>(sock.native_handle()),
                            EPOLLIN, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    ssize_t n = ::recv(static_cast<int>(sock.native_handle()),
                       buf.data, buf.size, 0);
    if (n < 0)
        co_return std::unexpected(last_error());
    if (n == 0)
        co_return std::unexpected(make_error_code(errc::end_of_file));

    co_return static_cast<std::size_t>(n);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf,
                 cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_cancel_awaiter aw{epoll, static_cast<int>(sock.native_handle()),
                            EPOLLOUT, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    ssize_t n = ::send(static_cast<int>(sock.native_handle()),
                       buf.data, buf.size, MSG_NOSIGNAL);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// Cancellable Version — Async File Operations (synchronous wrapper, cancel only pre-checks)
// =============================================================================

auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                     std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    (void)ctx;
    ssize_t n = ::pread(static_cast<int>(f.native_handle()),
                        buf.data, buf.size, static_cast<off_t>(offset));
    if (n < 0)
        co_return std::unexpected(last_error());
    co_return static_cast<std::size_t>(n);
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                      std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    (void)ctx;
    ssize_t n = ::pwrite(static_cast<int>(f.native_handle()),
                         buf.data, buf.size, static_cast<off_t>(offset));
    if (n < 0)
        co_return std::unexpected(last_error());
    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// Cancellable Version — Async Serial Port Operations
// =============================================================================

auto async_serial_read(io_context& ctx, serial_port& port, mutable_buffer buf,
                       cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_cancel_awaiter aw{epoll, static_cast<int>(port.native_handle()),
                            EPOLLIN, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    ssize_t n = ::read(static_cast<int>(port.native_handle()), buf.data, buf.size);
    if (n < 0)
        co_return std::unexpected(last_error());
    if (n == 0)
        co_return std::unexpected(make_error_code(errc::end_of_file));

    co_return static_cast<std::size_t>(n);
}

auto async_serial_write(io_context& ctx, serial_port& port, const_buffer buf,
                        cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_cancel_awaiter aw{epoll, static_cast<int>(port.native_handle()),
                            EPOLLOUT, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    ssize_t n = ::write(static_cast<int>(port.native_handle()), buf.data, buf.size);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// Cancellable Version — Async Timer
// =============================================================================

auto async_timer_wait(io_context& ctx,
                      std::chrono::steady_clock::duration duration,
                      cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& ep = static_cast<epoll_context&>(ctx);

    int tfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (tfd < 0)
        co_return std::unexpected(last_error());

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    struct ::itimerspec its{};
    its.it_value.tv_sec = static_cast<time_t>(ns / 1000000000LL);
    its.it_value.tv_nsec = static_cast<long>(ns % 1000000000LL);
    if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0)
        its.it_value.tv_nsec = 1;

    if (::timerfd_settime(tfd, 0, &its, nullptr) < 0) {
        auto ec = last_error();
        ::close(tfd);
        co_return std::unexpected(ec);
    }

    epoll_cancel_awaiter aw{ep, tfd, EPOLLIN, token};
    co_await aw;

    std::uint64_t exp = 0;
    ::read(tfd, &exp, sizeof(exp));
    ::close(tfd);

    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Async UDP I/O — epoll
// =============================================================================

auto async_recvfrom(io_context& ctx, socket& sock,
                    mutable_buffer buf, endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_awaiter aw{epoll, static_cast<int>(sock.native_handle()), EPOLLIN};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ::sockaddr_storage from_addr{};
    ::socklen_t from_len = sizeof(from_addr);
    ssize_t n = ::recvfrom(static_cast<int>(sock.native_handle()),
                           buf.data, buf.size, 0,
                           reinterpret_cast<::sockaddr*>(&from_addr), &from_len);
    if (n < 0)
        co_return std::unexpected(last_error());

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(n);
}

auto async_sendto(io_context& ctx, socket& sock,
                  const_buffer buf, const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_awaiter aw{epoll, static_cast<int>(sock.native_handle()), EPOLLOUT};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(peer, dest);
    ssize_t n = ::sendto(static_cast<int>(sock.native_handle()),
                         buf.data, buf.size, MSG_NOSIGNAL,
                         reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// Cancellable Version — Async UDP I/O
// =============================================================================

auto async_recvfrom(io_context& ctx, socket& sock,
                    mutable_buffer buf, endpoint& peer,
                    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_cancel_awaiter aw{epoll, static_cast<int>(sock.native_handle()),
                            EPOLLIN, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    ::sockaddr_storage from_addr{};
    ::socklen_t from_len = sizeof(from_addr);
    ssize_t n = ::recvfrom(static_cast<int>(sock.native_handle()),
                           buf.data, buf.size, 0,
                           reinterpret_cast<::sockaddr*>(&from_addr), &from_len);
    if (n < 0)
        co_return std::unexpected(last_error());

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(n);
}

auto async_sendto(io_context& ctx, socket& sock,
                  const_buffer buf, const endpoint& peer,
                  cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& epoll = static_cast<epoll_context&>(ctx);

    epoll_cancel_awaiter aw{epoll, static_cast<int>(sock.native_handle()),
                            EPOLLOUT, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(peer, dest);
    ssize_t n = ::sendto(static_cast<int>(sock.native_handle()),
                         buf.data, buf.size, MSG_NOSIGNAL,
                         reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

#endif // CNETMOD_HAS_EPOLL && !CNETMOD_HAS_IO_URING

} // namespace cnetmod
