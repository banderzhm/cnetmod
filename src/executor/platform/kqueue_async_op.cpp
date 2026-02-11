module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_KQUEUE

#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>

#endif

module cnetmod.executor.async_op;

#ifdef CNETMOD_HAS_KQUEUE
import cnetmod.io.platform.kqueue;
#endif
import cnetmod.coro.cancel;

namespace cnetmod {

#ifdef CNETMOD_HAS_KQUEUE

// =============================================================================
// kqueue awaiter — readiness 通知
// =============================================================================

/// 注册事件到 kqueue，就绪时恢复协程
/// 使用 EV_ONESHOT 确保单次触发
struct kqueue_awaiter {
    kqueue_context& ctx;
    int fd;
    int16_t filter;                // EVFILT_READ 或 EVFILT_WRITE
    std::error_code sync_error{};

    auto await_ready() const noexcept -> bool { return false; }

    auto await_suspend(std::coroutine_handle<> h) noexcept -> bool {
        auto r = ctx.add_event(fd, filter, EV_ADD | EV_ONESHOT,
                               reinterpret_cast<void*>(h.address()));
        if (!r) {
            sync_error = r.error();
            return false;
        }
        return true;
    }

    void await_resume() noexcept {}
};

// =============================================================================
// 辅助
// =============================================================================

namespace {

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
// kqueue cancel 版 awaiter
// =============================================================================

/// cancel_fn_：从 kqueue 删除事件，然后 post 协程恢复
static void kqueue_cancel_fn(cancel_token& token) noexcept {
    auto* kq = static_cast<kqueue_context*>(token.ctx_);
    kq->delete_event(token.fd_, token.filter_);
    if (token.coroutine_)
        kq->post(token.coroutine_);
}

/// 带取消支持的 kqueue awaiter
struct kqueue_cancel_awaiter {
    kqueue_context& ctx;
    int fd;
    int16_t filter;
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

        token.ctx_ = &ctx;
        token.fd_ = fd;
        token.filter_ = filter;
        token.coroutine_ = h;
        token.cancel_fn_ = &kqueue_cancel_fn;

        auto r = ctx.add_event(fd, filter, EV_ADD | EV_ONESHOT,
                               reinterpret_cast<void*>(h.address()));
        if (!r) {
            sync_error = r.error();
            return false;
        }

        token.pending_.store(true, std::memory_order_release);

        if (token.is_cancelled()) {
            token.pending_.store(false, std::memory_order_relaxed);
            ctx.delete_event(fd, filter);
            sync_error = make_error_code(errc::operation_aborted);
            return false;
        }

        return true;
    }

    void await_resume() noexcept {
        token.pending_.store(false, std::memory_order_relaxed);
    }
};

} // anonymous namespace

// =============================================================================
// 异步网络操作 — kqueue (readiness-based)
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);

    // 等待监听 socket 可读
    kqueue_awaiter aw{kq, static_cast<int>(listener.native_handle()), EVFILT_READ};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    int fd = ::accept(static_cast<int>(listener.native_handle()), nullptr, nullptr);
    if (fd < 0)
        co_return std::unexpected(last_error());

    // 设置非阻塞
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    co_return socket::from_native(fd);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep)
    -> task<std::expected<void, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(ep, dest);

    int ret = ::connect(static_cast<int>(sock.native_handle()),
                        reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    if (ret == 0)
        co_return std::expected<void, std::error_code>{};

    if (errno != EINPROGRESS)
        co_return std::unexpected(last_error());

    // 等待 socket 可写（连接完成）
    kqueue_awaiter aw{kq, static_cast<int>(sock.native_handle()), EVFILT_WRITE};
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
    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_awaiter aw{kq, static_cast<int>(sock.native_handle()), EVFILT_READ};
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
    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_awaiter aw{kq, static_cast<int>(sock.native_handle()), EVFILT_WRITE};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ssize_t n = ::send(static_cast<int>(sock.native_handle()),
                       buf.data, buf.size, 0);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// 异步文件操作 — kqueue (同步包装)
// kqueue 不支持常规文件异步 I/O，使用 pread/pwrite 同步执行
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
// 异步串口操作 — kqueue (同步 read/write，串口 fd 支持 kqueue 事件)
// =============================================================================

auto async_serial_read(io_context& ctx, serial_port& port, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_awaiter aw{kq, static_cast<int>(port.native_handle()), EVFILT_READ};
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
    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_awaiter aw{kq, static_cast<int>(port.native_handle()), EVFILT_WRITE};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    ssize_t n = ::write(static_cast<int>(port.native_handle()), buf.data, buf.size);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// 异步定时器 — kqueue (EVFILT_TIMER + EV_ONESHOT)
// =============================================================================

auto async_timer_wait(io_context& ctx,
                      std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    if (ms <= 0) ms = 1;

    // 用全局递增 ID 作为 EVFILT_TIMER 的 ident
    static std::atomic<int> next_id{1000000};
    int timer_id = next_id.fetch_add(1, std::memory_order_relaxed);

    // 直接通过 native kqueue fd 注册 EVFILT_TIMER + EV_ONESHOT
    struct kqueue_timer_awaiter {
        int kq_fd;
        int id;
        intptr_t timeout_ms;
        std::error_code sync_error{};

        auto await_ready() const noexcept -> bool { return false; }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool {
            struct kevent ev{};
            EV_SET(&ev, static_cast<uintptr_t>(id), EVFILT_TIMER,
                   EV_ADD | EV_ONESHOT, 0, timeout_ms,
                   reinterpret_cast<void*>(h.address()));
            if (::kevent(kq_fd, &ev, 1, nullptr, 0, nullptr) < 0) {
                sync_error = std::error_code(errno, std::generic_category());
                return false;
            }
            return true;
        }

        void await_resume() noexcept {}
    };

    kqueue_timer_awaiter aw{kq.native_handle(), timer_id, static_cast<intptr_t>(ms)};
    co_await aw;

    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// 可取消版本 — 异步网络操作
// =============================================================================

auto async_accept(io_context& ctx, socket& listener, cancel_token& token)
    -> task<std::expected<socket, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_cancel_awaiter aw{kq, static_cast<int>(listener.native_handle()),
                             EVFILT_READ, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    int fd = ::accept(static_cast<int>(listener.native_handle()), nullptr, nullptr);
    if (fd < 0)
        co_return std::unexpected(last_error());

    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    co_return socket::from_native(fd);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
                   cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(ep, dest);

    int ret = ::connect(static_cast<int>(sock.native_handle()),
                        reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    if (ret == 0)
        co_return std::expected<void, std::error_code>{};

    if (errno != EINPROGRESS)
        co_return std::unexpected(last_error());

    kqueue_cancel_awaiter aw{kq, static_cast<int>(sock.native_handle()),
                             EVFILT_WRITE, token};
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

    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_cancel_awaiter aw{kq, static_cast<int>(sock.native_handle()),
                             EVFILT_READ, token};
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

    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_cancel_awaiter aw{kq, static_cast<int>(sock.native_handle()),
                             EVFILT_WRITE, token};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    ssize_t n = ::send(static_cast<int>(sock.native_handle()),
                       buf.data, buf.size, 0);
    if (n < 0)
        co_return std::unexpected(last_error());

    co_return static_cast<std::size_t>(n);
}

// =============================================================================
// 可取消版本 — 异步文件操作（同步包装，cancel 仅前置检查）
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
// 可取消版本 — 异步串口操作
// =============================================================================

auto async_serial_read(io_context& ctx, serial_port& port, mutable_buffer buf,
                       cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_cancel_awaiter aw{kq, static_cast<int>(port.native_handle()),
                             EVFILT_READ, token};
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

    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_cancel_awaiter aw{kq, static_cast<int>(port.native_handle()),
                             EVFILT_WRITE, token};
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
// 可取消版本 — 异步定时器
// =============================================================================

auto async_timer_wait(io_context& ctx,
                      std::chrono::steady_clock::duration duration,
                      cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    if (ms <= 0) ms = 1;

    static std::atomic<int> next_id{1000000};
    int timer_id = next_id.fetch_add(1, std::memory_order_relaxed);

    // kqueue EVFILT_TIMER 不能用 cancel_awaiter（ident 是 timer_id 而非 fd）
    // 用原始 kqueue_timer_awaiter + 手动检查 token
    struct kqueue_timer_cancel_awaiter {
        int kq_fd;
        int id;
        intptr_t timeout_ms;
        cancel_token& tk;
        std::error_code sync_error{};

        auto await_ready() const noexcept -> bool {
            return tk.is_cancelled();
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool {
            if (tk.is_cancelled()) {
                sync_error = make_error_code(errc::operation_aborted);
                return false;
            }

            struct kevent ev{};
            EV_SET(&ev, static_cast<uintptr_t>(id), EVFILT_TIMER,
                   EV_ADD | EV_ONESHOT, 0, timeout_ms,
                   reinterpret_cast<void*>(h.address()));
            if (::kevent(kq_fd, &ev, 1, nullptr, 0, nullptr) < 0) {
                sync_error = std::error_code(errno, std::generic_category());
                return false;
            }

            tk.pending_.store(true, std::memory_order_release);
            return true;
        }

        void await_resume() noexcept {
            tk.pending_.store(false, std::memory_order_relaxed);
        }
    };

    kqueue_timer_cancel_awaiter aw{kq.native_handle(), timer_id,
                                   static_cast<intptr_t>(ms), token};
    co_await aw;

    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    co_return std::expected<void, std::error_code>{};
}

#endif // CNETMOD_HAS_KQUEUE

} // namespace cnetmod
