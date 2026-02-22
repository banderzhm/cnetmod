module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IOCP
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#endif

module cnetmod.executor.async_op;

#ifdef CNETMOD_HAS_IOCP
import cnetmod.io.platform.iocp;
#endif
import cnetmod.core.serial_port;
import cnetmod.coro.cancel;

namespace cnetmod {

#ifdef CNETMOD_HAS_IOCP

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

/// Associate handle to IOCP (duplicate association silently ignored)
auto ensure_associated(iocp_context& iocp, HANDLE handle)
    -> std::expected<void, std::error_code>
{
    auto r = iocp.associate(handle);
    if (!r) {
        if (r.error().value() == ERROR_INVALID_PARAMETER)
            return {};
        return r;
    }
    return {};
}

auto load_accept_ex(SOCKET s) -> LPFN_ACCEPTEX {
    LPFN_ACCEPTEX fn = nullptr;
    GUID guid = WSAID_ACCEPTEX;
    DWORD bytes = 0;
    ::WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guid, sizeof(guid), &fn, sizeof(fn),
        &bytes, nullptr, nullptr);
    return fn;
}

auto load_connect_ex(SOCKET s) -> LPFN_CONNECTEX {
    LPFN_CONNECTEX fn = nullptr;
    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    ::WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guid, sizeof(guid), &fn, sizeof(fn),
        &bytes, nullptr, nullptr);
    return fn;
}

auto get_socket_family(SOCKET s) -> int {
    ::sockaddr_storage addr{};
    int addrlen = sizeof(addr);
    if (::getsockname(s, reinterpret_cast<::sockaddr*>(&addr), &addrlen) == 0)
        return addr.ss_family;
    return AF_INET;
}

auto fill_sockaddr(const endpoint& ep,
                   ::sockaddr_storage& storage) noexcept -> int {
    std::memset(&storage, 0, sizeof(storage));
    if (ep.address().is_v4()) {
        auto& sa = reinterpret_cast<::sockaddr_in&>(storage);
        sa.sin_family = AF_INET;
        sa.sin_port = ::htons(ep.port());
        sa.sin_addr = ep.address().to_v4().native();
        return sizeof(::sockaddr_in);
    } else {
        auto& sa = reinterpret_cast<::sockaddr_in6&>(storage);
        sa.sin6_family = AF_INET6;
        sa.sin6_port = ::htons(ep.port());
        sa.sin6_addr = ep.address().to_v6().native();
        return sizeof(::sockaddr_in6);
    }
}

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
// IOCP Suspend Awaiter
// =============================================================================

struct iocp_suspend {
    iocp_overlapped& ov;
    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { ov.coroutine = h; }
    void await_resume() noexcept {}
};

// =============================================================================
// IOCP Cancel Version Suspend Awaiter
// =============================================================================

/// cancel_fn_: Call CancelIoEx to cancel specified OVERLAPPED operation
static void iocp_cancel_fn(cancel_token& token) noexcept {
    ::CancelIoEx(static_cast<HANDLE>(token.io_handle_),
                 static_cast<LPOVERLAPPED>(token.overlapped_));
}

/// IOCP suspend awaiter with cancel support
/// Writes cancel info to cancel_token on await_suspend
struct iocp_cancel_suspend {
    iocp_overlapped& ov;
    cancel_token& token;
    void* io_handle;  // HANDLE (socket converted to HANDLE or file HANDLE)

    auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<> h) noexcept {
        ov.coroutine = h;
        token.io_handle_ = io_handle;
        token.overlapped_ = static_cast<LPOVERLAPPED>(&ov);
        token.cancel_fn_ = &iocp_cancel_fn;
        token.pending_.store(true, std::memory_order_release);
        // If already cancelled before setting pending, trigger cancel immediately
        if (token.is_cancelled())
            iocp_cancel_fn(token);
    }

    void await_resume() noexcept {
        token.pending_.store(false, std::memory_order_relaxed);
    }
};

// =============================================================================
// Async Network Operations — IOCP
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(listener.native_handle())); !r)
        co_return std::unexpected(r.error());

    auto accept_ex = load_accept_ex(listener.native_handle());
    if (!accept_ex)
        co_return std::unexpected(make_error_code(errc::operation_not_supported));

    int af = get_socket_family(listener.native_handle());
    auto family = (af == AF_INET6) ? address_family::ipv6 : address_family::ipv4;
    auto accept_sock = socket::create(family, socket_type::stream);
    if (!accept_sock)
        co_return std::unexpected(accept_sock.error());

    constexpr DWORD addr_len = sizeof(::sockaddr_in6) + 16;
    char output_buf[addr_len * 2]{};
    DWORD bytes = 0;
    iocp_overlapped ov;

    BOOL ok = accept_ex(
        listener.native_handle(), accept_sock->native_handle(),
        output_buf, 0, addr_len, addr_len, &bytes, &ov);

    if (!ok) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);

    SOCKET ls = listener.native_handle();
    ::setsockopt(accept_sock->native_handle(), SOL_SOCKET,
        SO_UPDATE_ACCEPT_CONTEXT,
        reinterpret_cast<const char*>(&ls), sizeof(ls));

    co_return std::move(*accept_sock);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep)
    -> task<std::expected<void, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    // ConnectEx requires socket to be already bound
    ::sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0;
    ::bind(sock.native_handle(),
        reinterpret_cast<const ::sockaddr*>(&bind_addr), sizeof(bind_addr));

    auto connect_ex = load_connect_ex(sock.native_handle());
    if (!connect_ex)
        co_return std::unexpected(make_error_code(errc::operation_not_supported));

    ::sockaddr_storage dest{};
    int dest_len = 0;
    if (ep.address().is_v4()) {
        auto& sa = reinterpret_cast<::sockaddr_in&>(dest);
        sa.sin_family = AF_INET;
        sa.sin_port = ::htons(ep.port());
        sa.sin_addr = ep.address().to_v4().native();
        dest_len = sizeof(::sockaddr_in);
    } else {
        auto& sa = reinterpret_cast<::sockaddr_in6&>(dest);
        sa.sin6_family = AF_INET6;
        sa.sin6_port = ::htons(ep.port());
        sa.sin6_addr = ep.address().to_v6().native();
        dest_len = sizeof(::sockaddr_in6);
    }

    iocp_overlapped ov;

    BOOL ok = connect_ex(sock.native_handle(),
        reinterpret_cast<const ::sockaddr*>(&dest), dest_len,
        nullptr, 0, nullptr, &ov);

    if (!ok) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);

    ::setsockopt(sock.native_handle(), SOL_SOCKET,
        SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    iocp_overlapped ov;

    int ret = ::WSARecv(sock.native_handle(), &wsabuf, 1,
                        nullptr, &flags, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    iocp_overlapped ov;

    int ret = ::WSASend(sock.native_handle(), &wsabuf, 1,
                        nullptr, 0, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

// =============================================================================
// Async File Operations — IOCP
// =============================================================================

auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                     std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, f.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;
    ov.set_offset(offset);

    BOOL ok = ::ReadFile(f.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                      std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, f.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;
    ov.set_offset(offset);

    BOOL ok = ::WriteFile(f.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>
{
    (void)ctx;
    BOOL ok = ::FlushFileBuffers(f.native_handle());
    if (!ok) {
        DWORD err = ::GetLastError();
        co_return std::unexpected(
            std::error_code(static_cast<int>(err), std::system_category()));
    }
    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Async Timer — IOCP (CreateTimerQueueTimer + PostQueuedCompletionStatus)
// =============================================================================

auto async_timer_wait(io_context& ctx,
                      std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    struct timer_ctx {
        iocp_overlapped ov;
        iocp_context* iocp_ptr;
    };

    timer_ctx tc{};
    tc.iocp_ptr = &iocp;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    if (ms <= 0) ms = 1;

    HANDLE timer = nullptr;
    auto callback = [](PVOID param, BOOLEAN /*fired*/) {
        auto* p = static_cast<timer_ctx*>(param);
        p->iocp_ptr->post_completion(&p->ov);
    };

    if (!::CreateTimerQueueTimer(&timer, nullptr, callback, &tc,
            static_cast<DWORD>(ms), 0, WT_EXECUTEONLYONCE)) {
        co_return std::unexpected(
            std::error_code(static_cast<int>(::GetLastError()),
                            std::system_category()));
    }

    co_await iocp_suspend{tc.ov};

    ::DeleteTimerQueueTimer(nullptr, timer, INVALID_HANDLE_VALUE);

    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Async Serial Port Operations — IOCP
// =============================================================================

auto async_serial_read(io_context& ctx, serial_port& port, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, port.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;
    // Serial port has no offset concept, keep offset at 0

    BOOL ok = ::ReadFile(port.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_serial_write(io_context& ctx, serial_port& port, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, port.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;

    BOOL ok = ::WriteFile(port.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

// =============================================================================
// Cancellable Version — Async Network Operations
// =============================================================================

auto async_accept(io_context& ctx, socket& listener, cancel_token& token)
    -> task<std::expected<socket, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(listener.native_handle())); !r)
        co_return std::unexpected(r.error());

    auto accept_ex = load_accept_ex(listener.native_handle());
    if (!accept_ex)
        co_return std::unexpected(make_error_code(errc::operation_not_supported));

    int af = get_socket_family(listener.native_handle());
    auto family = (af == AF_INET6) ? address_family::ipv6 : address_family::ipv4;
    auto accept_sock = socket::create(family, socket_type::stream);
    if (!accept_sock)
        co_return std::unexpected(accept_sock.error());

    constexpr DWORD addr_len = sizeof(::sockaddr_in6) + 16;
    char output_buf[addr_len * 2]{};
    DWORD bytes = 0;
    iocp_overlapped ov;

    BOOL ok = accept_ex(
        listener.native_handle(), accept_sock->native_handle(),
        output_buf, 0, addr_len, addr_len, &bytes, &ov);

    if (!ok) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_cancel_suspend{ov, token,
        reinterpret_cast<void*>(listener.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);

    SOCKET ls = listener.native_handle();
    ::setsockopt(accept_sock->native_handle(), SOL_SOCKET,
        SO_UPDATE_ACCEPT_CONTEXT,
        reinterpret_cast<const char*>(&ls), sizeof(ls));

    co_return std::move(*accept_sock);
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
                   cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    ::sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0;
    ::bind(sock.native_handle(),
        reinterpret_cast<const ::sockaddr*>(&bind_addr), sizeof(bind_addr));

    auto connect_ex = load_connect_ex(sock.native_handle());
    if (!connect_ex)
        co_return std::unexpected(make_error_code(errc::operation_not_supported));

    ::sockaddr_storage dest{};
    int dest_len = 0;
    if (ep.address().is_v4()) {
        auto& sa = reinterpret_cast<::sockaddr_in&>(dest);
        sa.sin_family = AF_INET;
        sa.sin_port = ::htons(ep.port());
        sa.sin_addr = ep.address().to_v4().native();
        dest_len = sizeof(::sockaddr_in);
    } else {
        auto& sa = reinterpret_cast<::sockaddr_in6&>(dest);
        sa.sin6_family = AF_INET6;
        sa.sin6_port = ::htons(ep.port());
        sa.sin6_addr = ep.address().to_v6().native();
        dest_len = sizeof(::sockaddr_in6);
    }

    iocp_overlapped ov;

    BOOL ok = connect_ex(sock.native_handle(),
        reinterpret_cast<const ::sockaddr*>(&dest), dest_len,
        nullptr, 0, nullptr, &ov);

    if (!ok) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_cancel_suspend{ov, token,
        reinterpret_cast<void*>(sock.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);

    ::setsockopt(sock.native_handle(), SOL_SOCKET,
        SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
                cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    iocp_overlapped ov;

    int ret = ::WSARecv(sock.native_handle(), &wsabuf, 1,
                        nullptr, &flags, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_cancel_suspend{ov, token,
        reinterpret_cast<void*>(sock.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf,
                 cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    iocp_overlapped ov;

    int ret = ::WSASend(sock.native_handle(), &wsabuf, 1,
                        nullptr, 0, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_cancel_suspend{ov, token,
        reinterpret_cast<void*>(sock.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

// =============================================================================
// Cancellable Version — Async File Operations
// =============================================================================

auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
                     std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, f.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;
    ov.set_offset(offset);

    BOOL ok = ::ReadFile(f.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_cancel_suspend{ov, token,
        static_cast<void*>(f.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
                      std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, f.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;
    ov.set_offset(offset);

    BOOL ok = ::WriteFile(f.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_cancel_suspend{ov, token,
        static_cast<void*>(f.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
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

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, port.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;

    BOOL ok = ::ReadFile(port.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_cancel_suspend{ov, token,
        static_cast<void*>(port.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_serial_write(io_context& ctx, serial_port& port, const_buffer buf,
                        cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, port.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;

    BOOL ok = ::WriteFile(port.native_handle(), buf.data,
        static_cast<DWORD>(buf.size), nullptr, &ov);
    if (!ok) {
        DWORD err = ::GetLastError();
        if (err != ERROR_IO_PENDING)
            co_return std::unexpected(
                std::error_code(static_cast<int>(err), std::system_category()));
    }

    co_await iocp_cancel_suspend{ov, token,
        static_cast<void*>(port.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
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

    auto& iocp = static_cast<iocp_context&>(ctx);

    struct timer_ctx {
        iocp_overlapped ov;
        iocp_context* iocp_ptr;
    };

    timer_ctx tc{};
    tc.iocp_ptr = &iocp;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    if (ms <= 0) ms = 1;

    HANDLE timer = nullptr;
    auto callback = [](PVOID param, BOOLEAN /*fired*/) {
        auto* p = static_cast<timer_ctx*>(param);
        p->iocp_ptr->post_completion(&p->ov);
    };

    if (!::CreateTimerQueueTimer(&timer, nullptr, callback, &tc,
            static_cast<DWORD>(ms), 0, WT_EXECUTEONLYONCE)) {
        co_return std::unexpected(
            std::error_code(static_cast<int>(::GetLastError()),
                            std::system_category()));
    }

    // Timer completes via post_completion, cannot use CancelIoEx
    // Set cancel_fn_ to nullptr, cancel only sets flag
    token.pending_.store(true, std::memory_order_release);
    co_await iocp_suspend{tc.ov};
    token.pending_.store(false, std::memory_order_relaxed);

    ::DeleteTimerQueueTimer(nullptr, timer, INVALID_HANDLE_VALUE);

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Async UDP I/O — IOCP
// =============================================================================

auto async_recvfrom(io_context& ctx, socket& sock,
                    mutable_buffer buf, endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    iocp_overlapped ov;
    ::sockaddr_storage from_addr{};
    INT from_len = sizeof(from_addr);

    int ret = ::WSARecvFrom(sock.native_handle(), &wsabuf, 1,
                            nullptr, &flags,
                            reinterpret_cast<::sockaddr*>(&from_addr),
                            &from_len, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_sendto(io_context& ctx, socket& sock,
                  const_buffer buf, const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    iocp_overlapped ov;
    ::sockaddr_storage dest{};
    int dest_len = fill_sockaddr(peer, dest);

    int ret = ::WSASendTo(sock.native_handle(), &wsabuf, 1,
                          nullptr, 0,
                          reinterpret_cast<const ::sockaddr*>(&dest),
                          dest_len, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_suspend{ov};
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
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

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    iocp_overlapped ov;
    ::sockaddr_storage from_addr{};
    INT from_len = sizeof(from_addr);

    int ret = ::WSARecvFrom(sock.native_handle(), &wsabuf, 1,
                            nullptr, &flags,
                            reinterpret_cast<::sockaddr*>(&from_addr),
                            &from_len, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_cancel_suspend{ov, token,
        reinterpret_cast<void*>(sock.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_sendto(io_context& ctx, socket& sock,
                  const_buffer buf, const endpoint& peer,
                  cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle())); !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    iocp_overlapped ov;
    ::sockaddr_storage dest{};
    int dest_len = fill_sockaddr(peer, dest);

    int ret = ::WSASendTo(sock.native_handle(), &wsabuf, 1,
                          nullptr, 0,
                          reinterpret_cast<const ::sockaddr*>(&dest),
                          dest_len, &ov, nullptr);
    if (ret == SOCKET_ERROR) {
        int err = ::WSAGetLastError();
        if (err != WSA_IO_PENDING)
            co_return std::unexpected(make_error_code(from_native_error(err)));
    }

    co_await iocp_cancel_suspend{ov, token,
        reinterpret_cast<void*>(sock.native_handle())};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error) co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

#endif // CNETMOD_HAS_IOCP

} // namespace cnetmod
