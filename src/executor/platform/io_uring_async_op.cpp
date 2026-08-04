module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IO_URING

    #include <arpa/inet.h>
    #include <cerrno>
    #include <cstdlib>
    #include <exec/static_thread_pool.hpp>
    #include <fcntl.h>
    #include <liburing.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/uio.h>
    #include <unistd.h>

#endif

module cnetmod.executor.async_op;

#ifdef CNETMOD_HAS_IO_URING
import std;
import cnetmod.io.platform.io_uring;
import cnetmod.executor.pool;
#endif
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod {

#ifdef CNETMOD_HAS_IO_URING

// =============================================================================
// io_uring Suspend Awaiter
// =============================================================================

/// Suspend coroutine, wait for io_uring CQE completion
/// Event loop extracts uring_overlapped* from CQE user_data,
/// writes result and resumes coroutine
struct uring_suspend
{
    uring_overlapped& ov;

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
        ov.coroutine = h;
    }

    void await_resume() noexcept {}
};

// =============================================================================
// io_uring Cancel Version Suspend Awaiter
// =============================================================================

/// cancel_fn_: Submit IORING_OP_ASYNC_CANCEL to cancel operation
static void uring_cancel_fn(cancel_token& token) noexcept
{
    auto* uring = static_cast<io_uring_context*>(token.ctx_);
    auto* sqe = uring->get_sqe();
    if (sqe)
    {
        ::io_uring_prep_cancel(sqe, token.overlapped_, 0);
        ::io_uring_sqe_set_data(sqe, nullptr); // Cancel itself needs no callback
        (void)uring->submit();
    }
}

/// io_uring suspend awaiter with cancel support
struct uring_cancel_suspend
{
    uring_overlapped& ov;
    cancel_token& token;
    io_uring_context* ctx;

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept
    {
        ov.coroutine = h;
        token.ctx_ = ctx;
        token.overlapped_ = &ov;
        token.cancel_fn_ = &uring_cancel_fn;
        token.pending_.store(true, std::memory_order_release);
        if (token.is_cancelled())
            uring_cancel_fn(token);
    }

    void await_resume() noexcept
    {
        token.pending_.store(false, std::memory_order_relaxed);
    }
};

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

    auto fill_sockaddr(const endpoint& ep, ::sockaddr_storage& storage) noexcept -> ::socklen_t
    {
        std::memset(&storage, 0, sizeof(storage));
        if (ep.address().is_v4())
        {
            auto& sa = reinterpret_cast<::sockaddr_in&>(storage);
            sa.sin_family = AF_INET;
            sa.sin_port = htons(ep.port());
            sa.sin_addr = ep.address().to_v4().native();
            return sizeof(::sockaddr_in);
        }
        else
        {
            auto& sa = reinterpret_cast<::sockaddr_in6&>(storage);
            sa.sin6_family = AF_INET6;
            sa.sin6_port = htons(ep.port());
            sa.sin6_addr = ep.address().to_v6().native();
            return sizeof(::sockaddr_in6);
        }
    }

    auto endpoint_from_sockaddr(const ::sockaddr_storage& sa) noexcept -> endpoint
    {
        if (sa.ss_family == AF_INET6)
        {
            const auto& sin6 = reinterpret_cast<const ::sockaddr_in6&>(sa);
            return endpoint{ipv6_address::from_native(sin6.sin6_addr),
                ::ntohs(sin6.sin6_port)};
        }
        const auto& sin = reinterpret_cast<const ::sockaddr_in&>(sa);
        const auto* b = reinterpret_cast<const std::uint8_t*>(&sin.sin_addr);
        return endpoint{ipv4_address(b[0], b[1], b[2], b[3]),
            ::ntohs(sin.sin_port)};
    }

    inline auto& file_pool()
    {
        static thread_pool pool;
        return pool;
    }

    auto one_shot_async_accept(io_uring_context& uring, socket& listener)
        -> task<std::expected<socket, std::error_code>>
    {
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

    // liburing 2.0+ exposes these flags.  Older distributions retain the
    // single-shot path below at compile time.
    #if defined(IORING_ACCEPT_MULTISHOT) && defined(IORING_CQE_F_MORE)

    struct multishot_accept_state
    {
        io_uring_context* context{};
        std::uint64_t context_id{};
        int listener_fd = -1;
        uring_overlapped operation{};
        std::deque<int> accepted_fds;
        uring_overlapped* waiter{};
        bool active = false;
        bool unsupported = false;
    };

    using multishot_accept_key = std::pair<std::uint64_t, int>;

    auto multishot_accept_states()
        -> std::map<multishot_accept_key, std::unique_ptr<multishot_accept_state>>&
    {
        // io_uring submissions and completion delivery are already serialized by
        // io_uring_context.  The state is retained for the listener lifetime so a
        // persistent SQE never points at a coroutine-local object.
        static std::map<multishot_accept_key, std::unique_ptr<multishot_accept_state>> states;
        return states;
    }

    auto multishot_accept_enabled() noexcept -> bool
    {
        static const bool enabled = []
        {
            const auto* value = std::getenv("CNETMOD_DISABLE_MULTISHOT_ACCEPT");
            return value == nullptr || value[0] == '\0' || value[0] == '0';
        }();
        return enabled;
    }

    void on_multishot_accept(uring_overlapped& operation, int32_t result,
        std::uint32_t flags)
    {
        auto& state = *static_cast<multishot_accept_state*>(operation.completion_context);
        state.active = (flags & IORING_CQE_F_MORE) != 0;

        // A kernel that predates multishot accept accepts the SQE but completes it
        // with EINVAL/EOPNOTSUPP.  Mark this listener once, then let the current
        // caller transparently reissue its request as a single-shot accept.
        if (result == -EINVAL || result == -EOPNOTSUPP)
            state.unsupported = true;

        if (state.waiter && state.waiter->coroutine)
        {
            auto* waiter = std::exchange(state.waiter, nullptr);
            waiter->result = result;
            waiter->coroutine.resume();
        }
        else if (result >= 0)
        {
            state.accepted_fds.push_back(result);
        }

        // A terminal operation can no longer produce CQEs.  Drop its state once
        // there are no accepted descriptors to hand out.  Unsupported listeners
        // are retained so older kernels fall back only once per listener.
        if (!state.active && !state.unsupported && state.accepted_fds.empty() &&
            !state.waiter)
        {
            multishot_accept_states().erase({state.context_id, state.listener_fd});
        }
    }

    auto start_multishot_accept(multishot_accept_state& state)
        -> std::expected<void, std::error_code>
    {
        auto* sqe = state.context->prepare_sqe();
        if (!sqe)
            return std::unexpected(make_error_code(errc::no_buffer_space));

        state.operation.completion = &on_multishot_accept;
        state.operation.completion_context = &state;
        ::io_uring_prep_multishot_accept(sqe, state.listener_fd, nullptr, nullptr,
            SOCK_NONBLOCK | SOCK_CLOEXEC);
        ::io_uring_sqe_set_data(sqe, &state.operation);
        state.active = true;

        if (auto r = state.context->flush(); !r)
        {
            state.active = false;
            return std::unexpected(r.error());
        }
        return {};
    }

    #endif

} // anonymous namespace

// =============================================================================
// Async Network Operations — io_uring (completion-based)
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    #if defined(IORING_ACCEPT_MULTISHOT) && defined(IORING_CQE_F_MORE)
    const auto key = multishot_accept_key{uring.instance_id(),
        static_cast<int>(listener.native_handle())};
    auto& states = multishot_accept_states();
    auto [it, inserted] = states.try_emplace(key);
    if (inserted)
    {
        it->second = std::make_unique<multishot_accept_state>();
        it->second->context = &uring;
        it->second->context_id = key.first;
        it->second->listener_fd = key.second;
    }
    auto& state = *it->second;

    if (multishot_accept_enabled() && !state.unsupported)
    {
        if (!state.accepted_fds.empty())
        {
            const auto fd = state.accepted_fds.front();
            state.accepted_fds.pop_front();
            co_return socket::from_native(fd);
        }

        // A shared multishot SQE has a single waiter.  A concurrent caller
        // retains the established one-shot semantics rather than stealing a
        // completion intended for the first caller.
        if (!state.waiter)
        {
            if (!state.active)
            {
                if (auto r = start_multishot_accept(state); !r)
                    co_return std::unexpected(r.error());
            }

            uring_overlapped waiter;
            state.waiter = &waiter;
            co_await uring_suspend{waiter};

            if (waiter.result >= 0)
                co_return socket::from_native(waiter.result);
            if (!state.unsupported)
                co_return std::unexpected(
                    make_error_code(from_native_error(-waiter.result)));
        }
    }
    #endif

    co_return co_await one_shot_async_accept(uring, listener);
}

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

auto async_wait_readable(io_context& ctx, socket& sock)
    -> task<std::expected<void, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);
    uring_overlapped ov;
    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));
    ::io_uring_prep_poll_add(sqe, static_cast<int>(sock.native_handle()), POLLIN);
    ::io_uring_sqe_set_data(sqe, &ov);
    if (auto result = uring.flush(); !result)
        co_return std::unexpected(result.error());
    co_await uring_suspend{ov};
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));
    co_return std::expected<void, std::error_code>{};
}

auto async_wait_writable(io_context& ctx, socket& sock)
    -> task<std::expected<void, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);
    uring_overlapped ov;
    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));
    ::io_uring_prep_poll_add(sqe, static_cast<int>(sock.native_handle()), POLLOUT);
    ::io_uring_sqe_set_data(sqe, &ov);
    if (auto result = uring.flush(); !result)
        co_return std::unexpected(result.error());
    co_await uring_suspend{ov};
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));
    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Async File Operations — io_uring (native async read/write; open/stat offloaded)
// =============================================================================

auto async_file_open(io_context& ctx,
    const std::filesystem::path& path,
    open_mode mode)
    -> task<std::expected<file, std::error_code>>
{
    co_await pool_post_awaitable{file_pool()};
    auto result = file::open(path, mode);
    co_await post_awaitable{ctx};
    co_return result;
}

auto async_file_open(io_context& ctx,
    const std::filesystem::path& path,
    open_mode mode,
    cancel_token& token)
    -> task<std::expected<file, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto result = co_await async_file_open(ctx, path, mode);
    if (token.is_cancelled())
    {
        if (result)
            (void)co_await async_file_close(ctx, *result);
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    }
    co_return result;
}

auto async_file_stat(io_context& ctx,
    const std::filesystem::path& path)
    -> task<std::expected<file_stat, std::error_code>>
{
    co_await pool_post_awaitable{file_pool()};
    auto result = file::stat(path);
    co_await post_awaitable{ctx};
    co_return result;
}

auto async_file_stat(io_context& ctx,
    const std::filesystem::path& path,
    cancel_token& token)
    -> task<std::expected<file_stat, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto result = co_await async_file_stat(ctx, path);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return result;
}

auto async_file_close(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>
{
    auto fd = static_cast<int>(f.native_handle());
    if (fd == invalid_file_handle)
        co_return std::expected<void, std::error_code>{};

    auto& uring = static_cast<io_uring_context&>(ctx);
    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_close(sqe, fd);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    // Mark handle closed without double-closing
    (void)f.release();

    co_return std::expected<void, std::error_code>{};
}

auto async_file_close(io_context& ctx, file& f, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto result = co_await async_file_close(ctx, f);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return result;
}

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

namespace {

    struct file_batch_state
    {
        std::vector<file_io_result>* results = nullptr;
        std::coroutine_handle<> waiter{};
        std::size_t remaining = 0;
    };

    struct file_batch_operation
    {
        uring_overlapped operation{};
        file_batch_state* state = nullptr;
        std::size_t result_index = 0;
    };

    void complete_file_batch_operation(uring_overlapped& operation,
        int32_t result, std::uint32_t)
    {
        auto& item = *static_cast<file_batch_operation*>(
            operation.completion_context);
        if (result < 0)
        {
            (*item.state->results)[item.result_index] = std::unexpected(
                make_error_code(from_native_error(-result)));
        }
        else
        {
            (*item.state->results)[item.result_index] =
                static_cast<std::size_t>(result);
        }

        if (--item.state->remaining == 0 && item.state->waiter)
            item.state->waiter.resume();
    }

    struct file_batch_awaiter
    {
        file_batch_state& state;

        [[nodiscard]] auto await_ready() const noexcept -> bool
        {
            return state.remaining == 0;
        }

        auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> bool
        {
            state.waiter = coroutine;
            return state.remaining != 0;
        }

        void await_resume() const noexcept {}
    };

    template <typename Request, typename FileSelector, typename Prepare>
    auto submit_file_batch(io_context& ctx, std::span<const Request> requests,
        FileSelector select_file, Prepare prepare)
        -> task<std::vector<file_io_result>>
    {
        std::vector<file_io_result> results(requests.size());
        std::vector<std::size_t> valid_indices;
        valid_indices.reserve(requests.size());

        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            if (select_file(requests[index]))
            {
                valid_indices.push_back(index);
            }
            else
            {
                results[index] = std::unexpected(
                    make_error_code(errc::invalid_argument));
            }
        }

        auto& uring = static_cast<io_uring_context&>(ctx);
        std::size_t cursor = 0;
        while (cursor < valid_indices.size())
        {
            auto available = ::io_uring_sq_space_left(uring.native_ring());
            if (available == 0)
            {
                auto flushed = uring.flush_pending();
                if (!flushed)
                {
                    for (; cursor < valid_indices.size(); ++cursor)
                        results[valid_indices[cursor]] =
                            std::unexpected(flushed.error());
                    break;
                }
                available = ::io_uring_sq_space_left(uring.native_ring());
            }
            if (available == 0)
            {
                auto error = make_error_code(errc::no_buffer_space);
                for (; cursor < valid_indices.size(); ++cursor)
                    results[valid_indices[cursor]] = std::unexpected(error);
                break;
            }

            const auto chunk_size = std::min<std::size_t>(
                available, valid_indices.size() - cursor);
            file_batch_state state{
                .results = &results,
                .remaining = chunk_size,
            };
            std::vector<file_batch_operation> operations(chunk_size);

            for (std::size_t item_index = 0; item_index < chunk_size;
                ++item_index)
            {
                const auto request_index = valid_indices[cursor + item_index];
                auto& item = operations[item_index];
                item.state = &state;
                item.result_index = request_index;
                item.operation.completion = &complete_file_batch_operation;
                item.operation.completion_context = &item;

                auto* sqe = uring.prepare_sqe();
                prepare(sqe, requests[request_index]);
                ::io_uring_sqe_set_data(sqe, &item.operation);
            }

            auto submitted = uring.flush_pending();
            if (!submitted)
            {
                for (std::size_t item_index = 0; item_index < chunk_size;
                    ++item_index)
                {
                    results[valid_indices[cursor + item_index]] =
                        std::unexpected(submitted.error());
                }
                cursor += chunk_size;
                continue;
            }

            co_await file_batch_awaiter{state};
            cursor += chunk_size;
        }

        co_return results;
    }

} // namespace

auto async_file_read_batch(
    io_context& ctx, std::span<const file_read_request> requests)
    -> task<std::vector<file_io_result>>
{
    co_return co_await submit_file_batch(
        ctx, requests,
        [](const file_read_request& request)
        {
            return request.source;
        },
        [](io_uring_sqe* sqe, const file_read_request& request)
        {
            ::io_uring_prep_read(
                sqe, static_cast<int>(request.source->native_handle()),
                request.destination.data,
                static_cast<unsigned>(request.destination.size),
                request.offset);
        });
}

auto async_file_write_batch(
    io_context& ctx, std::span<const file_write_request> requests)
    -> task<std::vector<file_io_result>>
{
    co_return co_await submit_file_batch(
        ctx, requests,
        [](const file_write_request& request)
        {
            return request.destination;
        },
        [](io_uring_sqe* sqe, const file_write_request& request)
        {
            ::io_uring_prep_write(
                sqe, static_cast<int>(request.destination->native_handle()),
                request.source.data,
                static_cast<unsigned>(request.source.size), request.offset);
        });
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

auto async_file_flush(io_context& ctx, file& f, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto result = co_await async_file_flush(ctx, f);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return result;
}

namespace {

    struct splice_pipe
    {
        int read_fd = -1;
        int write_fd = -1;

        splice_pipe() noexcept
        {
            int descriptors[2] = {-1, -1};
            if (::pipe2(descriptors, O_CLOEXEC) == 0)
            {
                read_fd = descriptors[0];
                write_fd = descriptors[1];
            }
        }

        ~splice_pipe()
        {
            if (read_fd >= 0)
                ::close(read_fd);
            if (write_fd >= 0)
                ::close(write_fd);
        }

        splice_pipe(const splice_pipe&) = delete;
        auto operator=(const splice_pipe&) -> splice_pipe& = delete;

        [[nodiscard]] auto valid() const noexcept -> bool
        {
            return read_fd >= 0 && write_fd >= 0;
        }
    };

    auto submit_splice(io_uring_context& uring, int source_fd,
        std::int64_t source_offset, int destination_fd,
        std::int64_t destination_offset, unsigned length,
        unsigned flags, cancel_token* token)
        -> task<std::expected<int32_t, std::error_code>>
    {
        uring_overlapped operation;
        auto* sqe = uring.prepare_sqe();
        if (!sqe)
            co_return std::unexpected(make_error_code(errc::no_buffer_space));

        ::io_uring_prep_splice(sqe, source_fd, source_offset, destination_fd,
            destination_offset, length, flags);
        ::io_uring_sqe_set_data(sqe, &operation);

        auto submitted = uring.flush_pending();
        if (!submitted)
            co_return std::unexpected(submitted.error());

        if (token)
        {
            co_await uring_cancel_suspend{operation, *token, &uring};
            if (token->is_cancelled())
                co_return std::unexpected(
                    make_error_code(errc::operation_aborted));
        }
        else
        {
            co_await uring_suspend{operation};
        }
        co_return operation.result;
    }

    auto async_send_file_splice(io_context& ctx, socket& sock, file& source,
        std::uint64_t offset, std::uint64_t byte_count,
        cancel_token* token)
        -> task<std::expected<std::uint64_t, std::error_code>>
    {
        if (token && token->is_cancelled())
            co_return std::unexpected(make_error_code(errc::operation_aborted));

        splice_pipe pipe;
        if (!pipe.valid())
            co_return std::unexpected(
                make_error_code(from_native_error(errno)));

        auto& uring = static_cast<io_uring_context&>(ctx);
        constexpr std::uint64_t splice_chunk_size = 64 * 1024;
        std::uint64_t transferred = 0;

        while (transferred < byte_count)
        {
            const auto length = static_cast<unsigned>(std::min<std::uint64_t>(
                byte_count - transferred, splice_chunk_size));
            // The two splice operations deliberately complete in two stages.
            // A linked pair cannot safely use the first CQE's short-read result as
            // the second SQE's length and may otherwise wait forever on the pipe.
            auto staged_result = co_await submit_splice(
                uring, static_cast<int>(source.native_handle()),
                static_cast<std::int64_t>(offset + transferred), pipe.write_fd,
                -1, length, SPLICE_F_MOVE | SPLICE_F_MORE, token);
            if (!staged_result)
                co_return std::unexpected(staged_result.error());
            if (*staged_result < 0)
                co_return std::unexpected(
                    make_error_code(from_native_error(-*staged_result)));
            if (*staged_result == 0)
                break;

            const auto staged = static_cast<std::uint64_t>(*staged_result);
            std::uint64_t sent = 0;
            while (sent < staged)
            {
                const auto remaining = static_cast<unsigned>(staged - sent);
                auto drained = co_await submit_splice(
                    uring, pipe.read_fd, -1,
                    static_cast<int>(sock.native_handle()), -1, remaining,
                    SPLICE_F_MOVE | SPLICE_F_MORE, token);
                if (!drained)
                    co_return std::unexpected(drained.error());
                if (*drained < 0)
                    co_return std::unexpected(
                        make_error_code(from_native_error(-*drained)));
                if (*drained == 0)
                    co_return std::unexpected(make_error_code(errc::broken_pipe));
                sent += static_cast<std::uint64_t>(*drained);
            }
            transferred += staged;
        }
        co_return transferred;
    }

} // namespace

auto async_send_file(io_context& ctx, socket& sock, file& source,
    std::uint64_t offset, std::uint64_t byte_count)
    -> task<std::expected<std::uint64_t, std::error_code>>
{
    co_return co_await async_send_file_splice(
        ctx, sock, source, offset, byte_count, nullptr);
}

auto async_send_file(io_context& ctx, socket& sock, file& source,
    std::uint64_t offset, std::uint64_t byte_count,
    cancel_token& token)
    -> task<std::expected<std::uint64_t, std::error_code>>
{
    co_return co_await async_send_file_splice(
        ctx, sock, source, offset, byte_count, &token);
}

// =============================================================================
// Async Serial Port Operations — io_uring
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
// Async Timer — io_uring (IORING_OP_TIMEOUT)
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

// =============================================================================
// Async UDP I/O — io_uring
// =============================================================================

auto async_recvfrom(io_context& ctx, socket& sock,
    mutable_buffer buf, endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    ::sockaddr_storage from_addr{};

    struct ::iovec iov{};

    iov.iov_base = buf.data;
    iov.iov_len = buf.size;

    struct ::msghdr msg{};

    msg.msg_name = &from_addr;
    msg.msg_namelen = sizeof(from_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_recvmsg(sqe, static_cast<int>(sock.native_handle()), &msg, 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_suspend{ov};

    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(ov.result);
}

auto async_recvfrom(io_context& ctx, socket& sock,
    mutable_buffer buf, endpoint& peer,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    ::sockaddr_storage from_addr{};

    struct ::iovec iov{};

    iov.iov_base = buf.data;
    iov.iov_len = buf.size;

    struct ::msghdr msg{};

    msg.msg_name = &from_addr;
    msg.msg_namelen = sizeof(from_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    uring_overlapped ov;

    auto* sqe = uring.prepare_sqe();
    if (!sqe)
        co_return std::unexpected(make_error_code(errc::no_buffer_space));

    ::io_uring_prep_recvmsg(sqe, static_cast<int>(sock.native_handle()), &msg, 0);
    ::io_uring_sqe_set_data(sqe, &ov);

    if (auto r = uring.flush(); !r)
        co_return std::unexpected(r.error());

    co_await uring_cancel_suspend{ov, token, &uring};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.result < 0)
        co_return std::unexpected(make_error_code(from_native_error(-ov.result)));

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(ov.result);
}

auto async_sendto(io_context& ctx, socket& sock,
    const_buffer buf, const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& uring = static_cast<io_uring_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(peer, dest);

    struct ::iovec iov{};

    iov.iov_base = const_cast<void*>(buf.data);
    iov.iov_len = buf.size;

    struct ::msghdr msg{};

    msg.msg_name = &dest;
    msg.msg_namelen = dest_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    for (;;)
    {
        uring_overlapped ov;
        auto* sqe = uring.prepare_sqe();
        if (!sqe)
            co_return std::unexpected(make_error_code(errc::no_buffer_space));
        ::io_uring_prep_sendmsg(sqe, static_cast<int>(sock.native_handle()), &msg, MSG_NOSIGNAL);
        ::io_uring_sqe_set_data(sqe, &ov);
        if (auto r = uring.flush(); !r)
            co_return std::unexpected(r.error());
        co_await uring_suspend{ov};
        if (ov.result >= 0)
            co_return static_cast<std::size_t>(ov.result);
        const auto error = make_error_code(from_native_error(-ov.result));
        if (error != std::make_error_code(std::errc::operation_would_block) &&
            error != std::make_error_code(std::errc::resource_unavailable_try_again))
            co_return std::unexpected(error);
        // io_uring SENDMSG can complete EAGAIN on a non-blocking UDP socket.
        // Park on POLLOUT before re-submitting to avoid a CQE-driven busy loop.
        auto writable = co_await async_wait_writable(ctx, sock);
        if (!writable)
            co_return std::unexpected(writable.error());
    }
}

auto async_sendto(io_context& ctx, socket& sock,
    const_buffer buf, const endpoint& peer,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& uring = static_cast<io_uring_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(peer, dest);

    struct ::iovec iov{};

    iov.iov_base = const_cast<void*>(buf.data);
    iov.iov_len = buf.size;

    struct ::msghdr msg{};

    msg.msg_name = &dest;
    msg.msg_namelen = dest_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    for (;;)
    {
        uring_overlapped ov;
        auto* sqe = uring.prepare_sqe();
        if (!sqe)
            co_return std::unexpected(make_error_code(errc::no_buffer_space));
        ::io_uring_prep_sendmsg(sqe, static_cast<int>(sock.native_handle()), &msg, MSG_NOSIGNAL);
        ::io_uring_sqe_set_data(sqe, &ov);
        if (auto r = uring.flush(); !r)
            co_return std::unexpected(r.error());
        co_await uring_cancel_suspend{ov, token, &uring};
        if (token.is_cancelled())
            co_return std::unexpected(make_error_code(errc::operation_aborted));
        if (ov.result >= 0)
            co_return static_cast<std::size_t>(ov.result);
        const auto error = make_error_code(from_native_error(-ov.result));
        if (error != std::make_error_code(std::errc::operation_would_block) &&
            error != std::make_error_code(std::errc::resource_unavailable_try_again))
            co_return std::unexpected(error);
        auto writable = co_await async_wait_writable(ctx, sock);
        if (!writable)
            co_return std::unexpected(writable.error());
    }
}

auto async_recvfrom_batch(io_context& ctx, socket& sock,
    std::size_t max_datagrams, std::size_t max_datagram_size)
    -> task<std::expected<std::vector<udp_received_datagram>, std::error_code>>
{
    if (max_datagrams == 0U || max_datagram_size == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    std::vector<udp_received_datagram> result;
    result.reserve(max_datagrams);

    // Wait through io_uring for the first packet.  Once it completes the
    // socket is readable, so recvmmsg drains the rest without another event
    // loop round-trip.
    udp_received_datagram first{std::vector<std::byte>(max_datagram_size), {}};
    auto received = co_await async_recvfrom(ctx, sock,
        mutable_buffer{first.bytes.data(), first.bytes.size()}, first.peer);
    if (!received)
        co_return std::unexpected(received.error());
    first.bytes.resize(*received);
    result.push_back(std::move(first));

    const auto remaining = max_datagrams - result.size();
    if (remaining == 0U)
        co_return result;

    std::vector<std::vector<std::byte>> storage(remaining,
        std::vector<std::byte>(max_datagram_size));
    std::vector<::iovec> iovecs(remaining);
    std::vector<::sockaddr_storage> peers(remaining);
    std::vector<::mmsghdr> messages(remaining);
    for (std::size_t i = 0; i < remaining; ++i)
    {
        iovecs[i] = {storage[i].data(), storage[i].size()};
        messages[i].msg_hdr.msg_name = &peers[i];
        messages[i].msg_hdr.msg_namelen = sizeof(peers[i]);
        messages[i].msg_hdr.msg_iov = &iovecs[i];
        messages[i].msg_hdr.msg_iovlen = 1;
    }
    const int count = ::recvmmsg(static_cast<int>(sock.native_handle()), messages.data(),
        static_cast<unsigned int>(messages.size()), MSG_DONTWAIT, nullptr);
    if (count < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            co_return result;
        co_return std::unexpected(make_error_code(from_native_error(errno)));
    }
    for (int i = 0; i < count; ++i)
    {
        storage[static_cast<std::size_t>(i)].resize(messages[static_cast<std::size_t>(i)].msg_len);
        result.push_back({std::move(storage[static_cast<std::size_t>(i)]),
            endpoint_from_sockaddr(peers[static_cast<std::size_t>(i)])});
    }
    co_return result;
}

auto async_sendto_batch(io_context& ctx, socket& sock,
    std::span<const udp_send_datagram> datagrams)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (datagrams.empty())
        co_return std::size_t{0};

    // Submit one message through io_uring to provide proper readiness and
    // backpressure semantics, then sendmmsg the already-ready remainder.
    auto first = co_await async_sendto(ctx, sock, datagrams.front().bytes, datagrams.front().peer);
    if (!first)
        co_return std::unexpected(first.error());
    if (datagrams.size() == 1U)
        co_return std::size_t{1};

    const auto remaining = datagrams.subspan(1);
    std::vector<::iovec> iovecs(remaining.size());
    std::vector<::sockaddr_storage> peers(remaining.size());
    std::vector<::mmsghdr> messages(remaining.size());
    for (std::size_t i = 0; i < remaining.size(); ++i)
    {
        iovecs[i] = {const_cast<void*>(remaining[i].bytes.data), remaining[i].bytes.size};
        messages[i].msg_hdr.msg_name = &peers[i];
        messages[i].msg_hdr.msg_namelen = fill_sockaddr(remaining[i].peer, peers[i]);
        messages[i].msg_hdr.msg_iov = &iovecs[i];
        messages[i].msg_hdr.msg_iovlen = 1;
    }
    const int count = ::sendmmsg(static_cast<int>(sock.native_handle()), messages.data(),
        static_cast<unsigned int>(messages.size()), MSG_NOSIGNAL);
    if (count < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            co_return std::size_t{1};
        co_return std::unexpected(make_error_code(from_native_error(errno)));
    }
    co_return 1U + static_cast<std::size_t>(count);
}

#endif // CNETMOD_HAS_IO_URING

} // namespace cnetmod
