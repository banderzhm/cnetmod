module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING

#include <liburing.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>

#endif

module cnetmod.executor.async_op;

#ifdef CNETMOD_HAS_IO_URING
import cnetmod.io.platform.io_uring;
#endif
import cnetmod.coro.cancel;

namespace cnetmod {

#ifdef CNETMOD_HAS_IO_URING

// =============================================================================
// io_uring 挂起 awaiter
// =============================================================================

/// 挂起协程，等待 io_uring CQE 完成
/// 事件循环从 CQE user_data 取出 uring_overlapped*，
/// 写入 result 后恢复协程
struct uring_suspend {
    uring_overlapped& ov;
    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { ov.coroutine = h; }
    void await_resume() noexcept {}
};

// =============================================================================
// io_uring cancel 版挂起 awaiter
// =============================================================================

/// cancel_fn_：提交 IORING_OP_ASYNC_CANCEL 取消操作
static void uring_cancel_fn(cancel_token& token) noexcept {
    auto* uring = static_cast<io_uring_context*>(token.ctx_);
    auto* sqe = uring->get_sqe();
    if (sqe) {
        ::io_uring_prep_cancel(sqe, token.overlapped_, 0);
        ::io_uring_sqe_set_data(sqe, nullptr);  // cancel 自身无需回调
        (void)uring->submit();
    }
}

/// 带取消支持的 io_uring 挂起 awaiter
struct uring_cancel_suspend {
    uring_overlapped& ov;
    cancel_token& token;
    io_uring_context* ctx;

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        ov.coroutine = h;
        token.ctx_ = ctx;
        token.overlapped_ = &ov;
        token.cancel_fn_ = &uring_cancel_fn;
        token.pending_.store(true, std::memory_order_release);
        if (token.is_cancelled())
            uring_cancel_fn(token);
    }

    void await_resume() noexcept {
        token.pending_.store(false, std::memory_order_relaxed);
    }
};

// =============================================================================
// 辅助
// =============================================================================

namespace {

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
// 异步网络操作 — io_uring (completion-based)
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_accept(sqe, static_cast<int>(listener.native_handle()),
                           nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return socket::from_native(ov.result);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep)
    -> task<std::expected<void, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(ep, dest);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_connect(sqe, static_cast<int>(sock.native_handle()),
                            reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_recv(sqe, static_cast<int>(sock.native_handle()),
                         buf.data, buf.size, 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));
    if (ov.result == 0)
        co_return std::unexpected(make_error_code(errc::end_of_file));

    co_return static_cast<std::size_t>(ov.result);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_send(sqe, static_cast<int>(sock.native_handle()),
                         buf.data, buf.size, MSG_NOSIGNAL);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

// =============================================================================
// 异步文件操作 — io_uring (原生异步 file I/O)
// =============================================================================

auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                     std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_read(sqe, static_cast<int>(f.native_handle()),
                         buf.data, static_cast<unsigned>(buf.size), offset);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                      std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_write(sqe, static_cast<int>(f.native_handle()),
                          buf.data, static_cast<unsigned>(buf.size), offset);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_fsync(sqe, static_cast<int>(f.native_handle()), 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// 异步串口操作 — io_uring
// =============================================================================

auto async_serial_read(io_context& ctx, serial_port& port, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_read(sqe, static_cast<int>(port.native_handle()),
                         buf.data, static_cast<unsigned>(buf.size), 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

auto async_serial_write(io_context& ctx, serial_port& port, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_write(sqe, static_cast<int>(port.native_handle()),
                          buf.data, static_cast<unsigned>(buf.size), 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

// =============================================================================
// 异步定时器 — io_uring (IORING_OP_TIMEOUT)
// =============================================================================

auto async_timer_wait(io_context& ctx,
                      std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    __kernel_timespec ts{};
    ts.tv_sec = static_cast<__kernel_time64_t>(ns / 1000000000LL);
    ts.tv_nsec = static_cast<long long>(ns % 1000000000LL);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_timeout(sqe, &ts, 0, 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    // -ETIME is expected for normal timeout completion
    if (ov.result == -ETIME || ov.result == 0)
        co_return std::expected<void, std::error_code>{};
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

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

    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_accept(sqe, static_cast<int>(listener.native_handle()),
                           nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return socket::from_native(ov.result);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
                   cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(ep, dest);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_connect(sqe, static_cast<int>(sock.native_handle()),
                            reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
                cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_recv(sqe, static_cast<int>(sock.native_handle()),
                         buf.data, buf.size, 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));
    if (ov.result == 0)
        co_return std::unexpected(make_error_code(errc::end_of_file));

    co_return static_cast<std::size_t>(ov.result);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf,
                 cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_send(sqe, static_cast<int>(sock.native_handle()),
                         buf.data, buf.size, MSG_NOSIGNAL);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

// =============================================================================
// 可取消版本 — 异步文件操作
// =============================================================================

auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                     std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_read(sqe, static_cast<int>(f.native_handle()),
                         buf.data, static_cast<unsigned>(buf.size), offset);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                      std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_write(sqe, static_cast<int>(f.native_handle()),
                          buf.data, static_cast<unsigned>(buf.size), offset);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
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

    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_read(sqe, static_cast<int>(port.native_handle()),
                         buf.data, static_cast<unsigned>(buf.size), 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
}

auto async_serial_write(io_context& ctx, serial_port& port, const_buffer buf,
                        cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_write(sqe, static_cast<int>(port.native_handle()),
                          buf.data, static_cast<unsigned>(buf.size), 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return static_cast<std::size_t>(ov.result);
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

    auto& uring = static_cast<io_uring_context&>(ctx);

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    __kernel_timespec ts{};
    ts.tv_sec = static_cast<__kernel_time64_t>(ns / 1000000000LL);
    ts.tv_nsec = static_cast<long long>(ns % 1000000000LL);

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_timeout(sqe, &ts, 0, 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result == -ETIME || ov.result == 0)
        co_return std::expected<void, std::error_code>{};
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    co_return std::expected<void, std::error_code>{};
}

#endif // CNETMOD_HAS_IO_URING

} // namespace cnetmod