module;

#include <cnetmod/config.hpp>

// epoll 仅在有 epoll 且无 io_uring 时作为 async_op 后端
// 当 io_uring 可用时，优先使用 io_uring
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

namespace cnetmod {

#if defined(CNETMOD_HAS_EPOLL) && !defined(CNETMOD_HAS_IO_URING)

// =============================================================================
// 辅助
// =============================================================================

namespace {

// =============================================================================
// epoll awaiter — readiness 通知
// =============================================================================

/// 注册 fd 到 epoll，就绪时恢复协程
/// 使用 EPOLLONESHOT 确保单次触发
struct epoll_awaiter {
    epoll_context& ctx;
    int fd;
    uint32_t events;               // EPOLLIN 或 EPOLLOUT
    std::error_code sync_error{};  // 注册失败时的同步错误

    auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> h) noexcept -> bool {
        auto r = ctx.add(fd, events | EPOLLONESHOT,
                         reinterpret_cast<void*>(h.address()));
        if (!r) {
            sync_error = r.error();
            return false;  // 注册失败，不挂起
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

} // anonymous namespace

// =============================================================================
// 异步网络操作 — epoll (readiness-based)
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& epoll = static_cast<epoll_context&>(ctx);

    // 等待监听 socket 可读（有新连接就绪）
    epoll_awaiter aw{epoll, static_cast<int>(listener.native_handle()), EPOLLIN};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    // 就绪后执行 accept4
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

    // 等待 socket 可写（连接完成）
    epoll_awaiter aw{epoll, static_cast<int>(sock.native_handle()), EPOLLOUT};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    // 检查连接结果
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

    // 等待 socket 可读
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

    // 等待 socket 可写
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
// 异步文件操作 — epoll (同步包装)
// epoll 不支持常规文件异步 I/O，使用 pread/pwrite 同步执行
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
// 异步定时器 — epoll (timerfd)
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
        its.it_value.tv_nsec = 1;  // 最小 1ns

    if (::timerfd_settime(tfd, 0, &its, nullptr) < 0) {
        auto ec = last_error();
        ::close(tfd);
        co_return std::unexpected(ec);
    }

    epoll_awaiter aw{ep, tfd, EPOLLIN};
    co_await aw;

    // 读取 timerfd 清除定时器
    std::uint64_t exp = 0;
    ::read(tfd, &exp, sizeof(exp));
    ::close(tfd);

    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    co_return std::expected<void, std::error_code>{};
}

#endif // CNETMOD_HAS_EPOLL && !CNETMOD_HAS_IO_URING

} // namespace cnetmod
