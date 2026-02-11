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

namespace cnetmod {

#ifdef CNETMOD_HAS_IOCP

// =============================================================================
// 辅助
// =============================================================================

namespace {

/// 关联句柄到 IOCP（重复关联静默忽略）
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

} // anonymous namespace

// =============================================================================
// IOCP 挂起 awaiter
// =============================================================================

struct iocp_suspend {
    iocp_overlapped& ov;
    auto await_ready() const noexcept -> bool { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept { ov.coroutine = h; }
    void await_resume() noexcept {}
};

// =============================================================================
// 异步网络操作 — IOCP
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

    // ConnectEx 要求 socket 已绑定
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
// 异步文件操作 — IOCP
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
// 异步定时器 — IOCP (CreateTimerQueueTimer + PostQueuedCompletionStatus)
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

#endif // CNETMOD_HAS_IOCP

} // namespace cnetmod
