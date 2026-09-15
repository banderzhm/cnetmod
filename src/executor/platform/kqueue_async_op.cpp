module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_KQUEUE

    #include <arpa/inet.h>
    #include <cerrno>
    #include <exec/static_thread_pool.hpp>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/event.h>
    #if defined(__linux__)
        #include <sys/sendfile.h>
        #include <sys/timerfd.h>
    #endif
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <unistd.h>

#endif

module cnetmod.executor.async_op;

#ifdef CNETMOD_HAS_KQUEUE
import cnetmod.io.platform.kqueue;
import cnetmod.executor.pool;
#endif
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod {

#ifdef CNETMOD_HAS_KQUEUE

// =============================================================================
// kqueue awaiter — readiness notification
// =============================================================================

/// Register event to kqueue, resume coroutine when ready
/// Use EV_ONESHOT to ensure single trigger
struct kqueue_awaiter
{
    kqueue_context& ctx;
    int fd;
    int16_t filter; // EVFILT_READ or EVFILT_WRITE
    std::error_code sync_error{};
    kqueue_completion completion{};

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
    {
        completion = {h.address(), [](void* address) noexcept
            {
                std::coroutine_handle<>::from_address(address).resume();
            }};
        auto r = ctx.add_event(fd, filter, EV_ADD | EV_ONESHOT,
            &completion);
        if (!r)
        {
            sync_error = r.error();
            return false;
        }
        return true;
    }

    void await_resume() noexcept {}
};

// =============================================================================
// Helper functions
// =============================================================================

namespace {

#if defined(MSG_NOSIGNAL)
    constexpr int socket_no_signal_flag = MSG_NOSIGNAL;
#else
    constexpr int socket_no_signal_flag = 0;
#endif

#if defined(MSG_DONTWAIT)
    constexpr int socket_non_blocking_flag = MSG_DONTWAIT;
#else
    constexpr int socket_non_blocking_flag = 0;
#endif

    constexpr int socket_receive_flags = socket_non_blocking_flag;
    constexpr int socket_send_flags =
        socket_no_signal_flag | socket_non_blocking_flag;

    auto last_error() noexcept -> std::error_code
    {
        return make_error_code(from_native_error(errno));
    }

    inline auto& file_pool()
    {
        static thread_pool pool;
        return pool;
    }

    auto get_socket_family(int fd) -> int
    {
        ::sockaddr_storage addr{};
        ::socklen_t addrlen = sizeof(addr);
        if (::getsockname(fd, reinterpret_cast<::sockaddr*>(&addr), &addrlen) == 0)
            return addr.ss_family;
        return AF_INET;
    }

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

    auto completed_connect_error(int descriptor) noexcept -> std::error_code
    {
        int socket_error = 0;
        ::socklen_t error_length = sizeof(socket_error);
        if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &socket_error,
                &error_length) < 0)
            return last_error();
        if (socket_error != 0)
            return make_error_code(from_native_error(socket_error));

        ::sockaddr_storage peer{};
        ::socklen_t peer_length = sizeof(peer);
        if (::getpeername(descriptor,
                reinterpret_cast<::sockaddr*>(&peer), &peer_length) < 0)
            return last_error();
        return {};
    }

    struct send_file_attempt
    {
        off_t transferred{};
        int result{};
    };

    auto send_file_once(int source, int destination, off_t offset,
        off_t requested) noexcept -> send_file_attempt
    {
#if defined(__linux__)
        auto position = offset;
        const auto result = ::sendfile(destination, source, &position,
            static_cast<std::size_t>(requested));
        return {result > 0 ? static_cast<off_t>(result) : 0,
            result < 0 ? -1 : 0};
#else
        auto transferred = requested;
        const auto result = ::sendfile(source, destination, offset,
            &transferred, nullptr, 0);
        return {transferred, result};
#endif
    }

    // =============================================================================
    // kqueue awaiter with cancellation support
    // =============================================================================

    static void kqueue_cancel_fn(void* operation) noexcept;

    /// kqueue awaiter with cancellation support
    struct kqueue_cancel_awaiter
    {
        kqueue_context& ctx;
        int fd;
        int16_t filter;
        cancel_token& token;
        std::error_code sync_error{};
        kqueue_completion completion{};

        /**
         * The suspended frame owns cancellation queue storage until dispatch.
         * Cancellation must not allocate inside a noexcept callback.
         */
        post_node cancellation_post{};
        std::coroutine_handle<> continuation{};

        static void ready(void* operation) noexcept
        {
            auto& awaiter = *static_cast<kqueue_cancel_awaiter*>(operation);
            if (awaiter.token.complete_callback(&awaiter))
                awaiter.continuation.resume();
        }

        static void cancelled(void* operation) noexcept
        {
            auto& awaiter = *static_cast<kqueue_cancel_awaiter*>(operation);
            (void)awaiter.ctx.delete_event(awaiter.fd, awaiter.filter);
            awaiter.continuation.resume();
        }

        auto await_ready() const noexcept -> bool
        {
            return token.is_cancelled();
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
        {
            if (token.is_cancelled())
            {
                sync_error = make_error_code(errc::operation_aborted);
                return false;
            }

            continuation = h;
            completion = {this, &ready};

            auto r = ctx.add_event(fd, filter, EV_ADD | EV_ONESHOT,
                &completion);
            if (!r)
            {
                sync_error = r.error();
                return false;
            }

            if (!token.register_callback(this, &kqueue_cancel_fn))
            {
                (void)ctx.delete_event(fd, filter);
                sync_error = make_error_code(errc::operation_aborted);
                return false;
            }
            return true;
        }

        void await_resume() noexcept
        {
            token.finish_callback(this);
        }
    };

    /**
     * @brief Removes readiness and queues cancellation without allocating.
     */
    static void kqueue_cancel_fn(void* operation) noexcept
    {
        auto* awaiter = static_cast<kqueue_cancel_awaiter*>(operation);
        awaiter->cancellation_post.callback = &kqueue_cancel_awaiter::cancelled;
        awaiter->cancellation_post.callback_arg = awaiter;
        awaiter->ctx.post_node_raw(&awaiter->cancellation_post);
    }

    static void kqueue_timer_cancel_fn(void* operation) noexcept;

    struct kqueue_timer_cancel_awaiter
    {
        kqueue_context& ctx;
        std::uintptr_t id{};
        intptr_t timeout_ms;
        cancel_token& token;
        std::error_code sync_error{};
        post_node cancellation_post{};
        kqueue_completion completion{};
        std::coroutine_handle<> continuation{};

        static void ready(void* operation) noexcept
        {
            auto& awaiter = *static_cast<kqueue_timer_cancel_awaiter*>(operation);
            if (awaiter.token.complete_callback(&awaiter))
                awaiter.continuation.resume();
        }

        static void cancelled(void* operation) noexcept
        {
            auto& awaiter = *static_cast<kqueue_timer_cancel_awaiter*>(operation);
            (void)awaiter.ctx.delete_event(awaiter.id, EVFILT_TIMER);
            awaiter.continuation.resume();
        }

        auto await_ready() const noexcept -> bool
        {
            return token.is_cancelled();
        }

        auto await_suspend(std::coroutine_handle<> handle) noexcept -> bool
        {
            if (token.is_cancelled())
            {
                sync_error = make_error_code(errc::operation_aborted);
                return false;
            }

            continuation = handle;
            completion = {this, &ready};
            id = reinterpret_cast<std::uintptr_t>(this);

            struct kevent event{};
            EV_SET(&event, static_cast<uintptr_t>(id), EVFILT_TIMER,
                EV_ADD | EV_ONESHOT, 0, timeout_ms,
                &completion);
            if (::kevent(ctx.native_handle(), &event, 1, nullptr, 0, nullptr) < 0)
            {
                sync_error = std::error_code(errno, std::generic_category());
                return false;
            }

            if (!token.register_callback(this, &kqueue_timer_cancel_fn))
            {
                (void)ctx.delete_event(id, EVFILT_TIMER);
                sync_error = make_error_code(errc::operation_aborted);
                return false;
            }
            return true;
        }

        void await_resume() noexcept
        {
            token.finish_callback(this);
        }
    };

    /**
     * @brief Removes a timer and resumes its coroutine through the owner queue.
     */
    static void kqueue_timer_cancel_fn(void* operation) noexcept
    {
        auto* awaiter = static_cast<kqueue_timer_cancel_awaiter*>(operation);
        awaiter->cancellation_post.callback =
            &kqueue_timer_cancel_awaiter::cancelled;
        awaiter->cancellation_post.callback_arg = awaiter;
        awaiter->ctx.post_node_raw(&awaiter->cancellation_post);
    }

    auto endpoint_from_sockaddr(const ::sockaddr_storage& sa) noexcept -> endpoint
    {
        if (sa.ss_family == AF_INET6)
        {
            const auto& sin6 = reinterpret_cast<const ::sockaddr_in6&>(sa);
            return endpoint{ipv6_address::from_native(sin6.sin6_addr),
                ntohs(sin6.sin6_port)};
        }
        const auto& sin = reinterpret_cast<const ::sockaddr_in&>(sa);
        const auto* b = reinterpret_cast<const std::uint8_t*>(&sin.sin_addr);
        return endpoint{ipv4_address(b[0], b[1], b[2], b[3]),
            ntohs(sin.sin_port)};
    }

} // anonymous namespace

// =============================================================================
// Async Network Operations — kqueue (readiness-based)
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);

    // Wait for listening socket to be readable
    kqueue_awaiter aw{kq, static_cast<int>(listener.native_handle()), EVFILT_READ};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    int fd = ::accept(static_cast<int>(listener.native_handle()), nullptr, nullptr);
    if (fd < 0)
        co_return std::unexpected(last_error());

    // Set non-blocking
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    co_return socket::from_native(fd);
}

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

    // Wait for socket to be writable (connection complete)
    kqueue_awaiter aw{kq, static_cast<int>(sock.native_handle()), EVFILT_WRITE};
    co_await aw;
    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    if (const auto error =
            completed_connect_error(static_cast<int>(sock.native_handle())))
        co_return std::unexpected(error);

    co_return std::expected<void, std::error_code>{};
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

    if (const auto error =
            completed_connect_error(static_cast<int>(sock.native_handle())))
        co_return std::unexpected(error);

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);
    const auto descriptor = static_cast<int>(sock.native_handle());
    while (true)
    {
        const auto received = ::recv(
            descriptor, buf.data, buf.size, socket_receive_flags);
        if (received > 0)
            co_return static_cast<std::size_t>(received);
        if (received == 0)
            co_return std::unexpected(make_error_code(errc::end_of_file));
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            co_return std::unexpected(last_error());

        kqueue_awaiter awaiter{kq, descriptor, EVFILT_READ};
        co_await awaiter;
        if (awaiter.sync_error)
            co_return std::unexpected(awaiter.sync_error);
    }
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);
    const auto descriptor = static_cast<int>(sock.native_handle());
    while (true)
    {
        if (token.is_cancelled())
            co_return std::unexpected(make_error_code(errc::operation_aborted));
        const auto received = ::recv(
            descriptor, buf.data, buf.size, socket_receive_flags);
        if (received > 0)
            co_return static_cast<std::size_t>(received);
        if (received == 0)
            co_return std::unexpected(make_error_code(errc::end_of_file));
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            co_return std::unexpected(last_error());

        kqueue_cancel_awaiter awaiter{kq, descriptor, EVFILT_READ, token};
        co_await awaiter;
        if (awaiter.sync_error)
            co_return std::unexpected(awaiter.sync_error);
    }
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);
    const auto descriptor = static_cast<int>(sock.native_handle());
    while (true)
    {
        const auto sent = ::send(
            descriptor, buf.data, buf.size, socket_send_flags);
        if (sent >= 0)
            co_return static_cast<std::size_t>(sent);
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            co_return std::unexpected(last_error());

        kqueue_awaiter awaiter{kq, descriptor, EVFILT_WRITE};
        co_await awaiter;
        if (awaiter.sync_error)
            co_return std::unexpected(awaiter.sync_error);
    }
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);
    const auto descriptor = static_cast<int>(sock.native_handle());
    while (true)
    {
        if (token.is_cancelled())
            co_return std::unexpected(make_error_code(errc::operation_aborted));
        const auto sent = ::send(
            descriptor, buf.data, buf.size, socket_send_flags);
        if (sent >= 0)
            co_return static_cast<std::size_t>(sent);
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            co_return std::unexpected(last_error());

        kqueue_cancel_awaiter awaiter{kq, descriptor, EVFILT_WRITE, token};
        co_await awaiter;
        if (awaiter.sync_error)
            co_return std::unexpected(awaiter.sync_error);
    }
}

auto async_wait_readable(io_context& ctx, socket& sock)
    -> task<std::expected<void, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);
    kqueue_awaiter awaiter{
        kq, static_cast<int>(sock.native_handle()), EVFILT_READ};
    co_await awaiter;
    if (awaiter.sync_error)
        co_return std::unexpected(awaiter.sync_error);
    co_return std::expected<void, std::error_code>{};
}

auto async_wait_readable(io_context& ctx, socket& sock, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);
    kqueue_cancel_awaiter awaiter{
        kq, static_cast<int>(sock.native_handle()), EVFILT_READ, token};
    co_await awaiter;
    if (awaiter.sync_error)
        co_return std::unexpected(awaiter.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return std::expected<void, std::error_code>{};
}

auto async_wait_writable(io_context& ctx, socket& sock)
    -> task<std::expected<void, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);
    kqueue_awaiter awaiter{
        kq, static_cast<int>(sock.native_handle()), EVFILT_WRITE};
    co_await awaiter;
    if (awaiter.sync_error)
        co_return std::unexpected(awaiter.sync_error);
    co_return std::expected<void, std::error_code>{};
}

auto async_wait_writable(io_context& ctx, socket& sock, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);
    kqueue_cancel_awaiter awaiter{
        kq, static_cast<int>(sock.native_handle()), EVFILT_WRITE, token};
    co_await awaiter;
    if (awaiter.sync_error)
        co_return std::unexpected(awaiter.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return std::expected<void, std::error_code>{};
}

// =============================================================================
// Async File Operations — kqueue (thread-pool offload)
// kqueue does not support async I/O for regular files, offload pread/pwrite/fsync to thread pool
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
    co_await pool_post_awaitable{file_pool()};
    f.close();
    co_await post_awaitable{ctx};
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
    co_await pool_post_awaitable{file_pool()};
    auto result = [&]() -> std::expected<std::size_t, std::error_code>
    {
        ssize_t n = ::pread(static_cast<int>(f.native_handle()),
            buf.data, buf.size, static_cast<off_t>(offset));
        if (n < 0)
            return std::unexpected(last_error());
        return static_cast<std::size_t>(n);
    }();
    co_await post_awaitable{ctx};
    co_return result;
}

auto async_file_read(io_context& ctx, file& f, mutable_buffer buf,
    std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_await pool_post_awaitable{file_pool()};
    auto result = [&]() -> std::expected<std::size_t, std::error_code>
    {
        ssize_t n = ::pread(static_cast<int>(f.native_handle()),
            buf.data, buf.size, static_cast<off_t>(offset));
        if (n < 0)
            return std::unexpected(last_error());
        return static_cast<std::size_t>(n);
    }();
    co_await post_awaitable{ctx};
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return result;
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
    std::uint64_t offset)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_await pool_post_awaitable{file_pool()};
    auto result = [&]() -> std::expected<std::size_t, std::error_code>
    {
        ssize_t n = ::pwrite(static_cast<int>(f.native_handle()),
            buf.data, buf.size, static_cast<off_t>(offset));
        if (n < 0)
            return std::unexpected(last_error());
        return static_cast<std::size_t>(n);
    }();
    co_await post_awaitable{ctx};
    co_return result;
}

auto async_file_write(io_context& ctx, file& f, const_buffer buf,
    std::uint64_t offset, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_await pool_post_awaitable{file_pool()};
    auto result = [&]() -> std::expected<std::size_t, std::error_code>
    {
        ssize_t n = ::pwrite(static_cast<int>(f.native_handle()),
            buf.data, buf.size, static_cast<off_t>(offset));
        if (n < 0)
            return std::unexpected(last_error());
        return static_cast<std::size_t>(n);
    }();
    co_await post_awaitable{ctx};
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return result;
}

auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>
{
    co_await pool_post_awaitable{file_pool()};
    auto result = [&]() -> std::expected<void, std::error_code>
    {
        if (::fsync(static_cast<int>(f.native_handle())) != 0)
            return std::unexpected(last_error());
        return std::expected<void, std::error_code>{};
    }();
    co_await post_awaitable{ctx};
    co_return result;
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

auto async_send_file(io_context& ctx, socket& sock, file& source,
    std::uint64_t offset, std::uint64_t byte_count)
    -> task<std::expected<std::uint64_t, std::error_code>>
{
    auto& kqueue = static_cast<kqueue_context&>(ctx);
    std::uint64_t transferred = 0;
    while (transferred < byte_count)
    {
        const auto requested = static_cast<off_t>(std::min<std::uint64_t>(
            byte_count - transferred,
            static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())));
        const auto attempt = send_file_once(
            static_cast<int>(source.native_handle()),
            static_cast<int>(sock.native_handle()),
            static_cast<off_t>(offset + transferred), requested);
        if (attempt.transferred > 0)
            transferred += static_cast<std::uint64_t>(attempt.transferred);
        if (attempt.result == 0)
        {
            if (attempt.transferred == 0)
                break;
            continue;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            kqueue_awaiter writable{
                kqueue, static_cast<int>(sock.native_handle()), EVFILT_WRITE};
            co_await writable;
            if (writable.sync_error)
                co_return std::unexpected(writable.sync_error);
            continue;
        }
        co_return std::unexpected(last_error());
    }
    co_return transferred;
}

auto async_send_file(io_context& ctx, socket& sock, file& source,
    std::uint64_t offset, std::uint64_t byte_count,
    cancel_token& token)
    -> task<std::expected<std::uint64_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kqueue = static_cast<kqueue_context&>(ctx);
    std::uint64_t transferred = 0;
    while (transferred < byte_count)
    {
        const auto requested = static_cast<off_t>(std::min<std::uint64_t>(
            byte_count - transferred,
            static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())));
        const auto attempt = send_file_once(
            static_cast<int>(source.native_handle()),
            static_cast<int>(sock.native_handle()),
            static_cast<off_t>(offset + transferred), requested);
        if (attempt.transferred > 0)
            transferred += static_cast<std::uint64_t>(attempt.transferred);
        if (attempt.result == 0)
        {
            if (attempt.transferred == 0)
                break;
            continue;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            kqueue_cancel_awaiter writable{
                kqueue, static_cast<int>(sock.native_handle()), EVFILT_WRITE,
                token};
            co_await writable;
            if (token.is_cancelled())
                co_return std::unexpected(
                    make_error_code(errc::operation_aborted));
            if (writable.sync_error)
                co_return std::unexpected(writable.sync_error);
            continue;
        }
        co_return std::unexpected(last_error());
    }
    co_return transferred;
}

// =============================================================================
// Async Serial Port Operations — kqueue (synchronous read/write, serial fd supports kqueue events)
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
// Async Timer — kqueue (EVFILT_TIMER + EV_ONESHOT)
// =============================================================================

auto async_timer_wait(io_context& ctx,
    std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);

#if defined(__linux__)
    const int timer = ::timerfd_create(
        CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer < 0)
        co_return std::unexpected(last_error());

    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    struct ::itimerspec specification{};
    specification.it_value.tv_sec = static_cast<time_t>(ns / 1000000000LL);
    specification.it_value.tv_nsec = static_cast<long>(ns % 1000000000LL);
    if (specification.it_value.tv_sec == 0 &&
        specification.it_value.tv_nsec == 0)
        specification.it_value.tv_nsec = 1;
    if (::timerfd_settime(timer, 0, &specification, nullptr) < 0)
    {
        const auto error = last_error();
        ::close(timer);
        co_return std::unexpected(error);
    }

    kqueue_awaiter awaiter{kq, timer, EVFILT_READ};
    co_await awaiter;
    std::uint64_t expirations{};
    (void)::read(timer, &expirations, sizeof(expirations));
    ::close(timer);
    if (awaiter.sync_error)
        co_return std::unexpected(awaiter.sync_error);
    co_return std::expected<void, std::error_code>{};
#else

    auto ms = std::chrono::ceil<std::chrono::milliseconds>(duration).count();
    if (ms <= 0)
        ms = 1;

    // Register EVFILT_TIMER + EV_ONESHOT directly via native kqueue fd
    struct kqueue_timer_awaiter
    {
        int kq_fd;
        intptr_t timeout_ms;
        std::error_code sync_error{};
        kqueue_completion completion{};

        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        auto await_suspend(std::coroutine_handle<> h) noexcept -> bool
        {
            struct kevent ev{};
            const auto id = reinterpret_cast<std::uintptr_t>(this);

            completion = {h.address(), [](void* address) noexcept
                {
                    std::coroutine_handle<>::from_address(address).resume();
                }};

            EV_SET(&ev, static_cast<uintptr_t>(id), EVFILT_TIMER,
                EV_ADD | EV_ONESHOT, 0, timeout_ms,
                &completion);
            if (::kevent(kq_fd, &ev, 1, nullptr, 0, nullptr) < 0)
            {
                sync_error = std::error_code(errno, std::generic_category());
                return false;
            }
            return true;
        }

        void await_resume() noexcept {}
    };

    kqueue_timer_awaiter aw{
        kq.native_handle(), static_cast<intptr_t>(ms)};
    co_await aw;

    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);

    co_return std::expected<void, std::error_code>{};
#endif
}

auto async_timer_wait(io_context& ctx,
    std::chrono::steady_clock::duration duration,
    cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);

#if defined(__linux__)
    const int timer = ::timerfd_create(
        CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer < 0)
        co_return std::unexpected(last_error());

    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    struct ::itimerspec specification{};
    specification.it_value.tv_sec = static_cast<time_t>(ns / 1000000000LL);
    specification.it_value.tv_nsec = static_cast<long>(ns % 1000000000LL);
    if (specification.it_value.tv_sec == 0 &&
        specification.it_value.tv_nsec == 0)
        specification.it_value.tv_nsec = 1;
    if (::timerfd_settime(timer, 0, &specification, nullptr) < 0)
    {
        const auto error = last_error();
        ::close(timer);
        co_return std::unexpected(error);
    }

    kqueue_cancel_awaiter awaiter{kq, timer, EVFILT_READ, token};
    co_await awaiter;
    std::uint64_t expirations{};
    (void)::read(timer, &expirations, sizeof(expirations));
    ::close(timer);
    if (awaiter.sync_error)
        co_return std::unexpected(awaiter.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    co_return std::expected<void, std::error_code>{};
#else

    auto ms = std::chrono::ceil<std::chrono::milliseconds>(duration).count();
    if (ms <= 0)
        ms = 1;

    kqueue_timer_cancel_awaiter aw{
        kq, {}, static_cast<intptr_t>(ms), token};
    co_await aw;

    if (aw.sync_error)
        co_return std::unexpected(aw.sync_error);
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    co_return std::expected<void, std::error_code>{};
#endif
}

// =============================================================================
// Async UDP I/O — kqueue
// =============================================================================

auto async_recvfrom(io_context& ctx, socket& sock,
    mutable_buffer buf, endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& kq = static_cast<kqueue_context&>(ctx);

    kqueue_awaiter aw{kq, static_cast<int>(sock.native_handle()), EVFILT_READ};
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

auto async_recvfrom(io_context& ctx, socket& sock,
    mutable_buffer buf, endpoint& peer,
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
    auto& kq = static_cast<kqueue_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(peer, dest);
    for (;;)
    {
        kqueue_awaiter aw{kq, static_cast<int>(sock.native_handle()), EVFILT_WRITE};
        co_await aw;
        if (aw.sync_error)
            co_return std::unexpected(aw.sync_error);
        const ssize_t n = ::sendto(static_cast<int>(sock.native_handle()),
            buf.data, buf.size, socket_send_flags,
            reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
        if (n >= 0)
            co_return static_cast<std::size_t>(n);
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            co_return std::unexpected(last_error());
    }
}

auto async_sendto(io_context& ctx, socket& sock,
    const_buffer buf, const endpoint& peer,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& kq = static_cast<kqueue_context&>(ctx);

    ::sockaddr_storage dest{};
    ::socklen_t dest_len = fill_sockaddr(peer, dest);
    for (;;)
    {
        kqueue_cancel_awaiter aw{kq, static_cast<int>(sock.native_handle()),
            EVFILT_WRITE, token};
        co_await aw;
        if (aw.sync_error)
            co_return std::unexpected(aw.sync_error);
        if (token.is_cancelled())
            co_return std::unexpected(make_error_code(errc::operation_aborted));
        const ssize_t n = ::sendto(static_cast<int>(sock.native_handle()),
            buf.data, buf.size, socket_send_flags,
            reinterpret_cast<const ::sockaddr*>(&dest), dest_len);
        if (n >= 0)
            co_return static_cast<std::size_t>(n);
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            co_return std::unexpected(last_error());
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
    udp_received_datagram first{udp_datagram_buffer{max_datagram_size}, {}};
    auto received = co_await async_recvfrom(ctx, sock,
        mutable_buffer{first.bytes.data(), first.bytes.size()}, first.peer);
    if (!received)
        co_return std::unexpected(received.error());
    first.bytes.resize(*received);
    result.push_back(std::move(first));

    // kqueue reports readiness rather than a packet count.  Drain the
    // non-blocking socket to a bounded budget before registering the next
    // EVFILT_READ notification.
    while (result.size() < max_datagrams)
    {
        udp_received_datagram next{udp_datagram_buffer{max_datagram_size}, {}};
        ::sockaddr_storage sender{};
        ::socklen_t sender_length = sizeof(sender);
        const auto count = ::recvfrom(static_cast<int>(sock.native_handle()), next.bytes.data(),
            next.bytes.size(), 0, reinterpret_cast<::sockaddr*>(&sender), &sender_length);
        if (count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            co_return std::unexpected(last_error());
        }
        next.bytes.resize(static_cast<std::size_t>(count));
        next.peer = endpoint_from_sockaddr(sender);
        result.push_back(std::move(next));
    }
    co_return result;
}

auto async_sendto_batch(io_context& ctx, socket& sock,
    std::span<const udp_send_datagram> datagrams)
    -> task<std::expected<std::size_t, std::error_code>>
{
    std::size_t submitted{};
    for (const auto& datagram : datagrams)
    {
        auto sent = co_await async_sendto(ctx, sock, datagram.bytes, datagram.peer);
        if (!sent)
        {
            if (submitted != 0U)
                co_return submitted;
            co_return std::unexpected(sent.error());
        }
        ++submitted;
    }
    co_return submitted;
}

auto async_recvfrom_batch(io_context& ctx, socket& sock,
    std::size_t max_datagrams, std::size_t max_datagram_size, cancel_token& token)
    -> task<std::expected<std::vector<udp_received_datagram>, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (max_datagrams == 0U || max_datagram_size == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    std::vector<udp_received_datagram> result;
    result.reserve(max_datagrams);
    udp_received_datagram first{udp_datagram_buffer{max_datagram_size}, {}};
    auto received = co_await async_recvfrom(ctx, sock,
        mutable_buffer{first.bytes.data(), first.bytes.size()}, first.peer, token);
    if (!received)
        co_return std::unexpected(received.error());
    first.bytes.resize(*received);
    result.push_back(std::move(first));

    while (result.size() < max_datagrams && !token.is_cancelled())
    {
        udp_received_datagram next{udp_datagram_buffer{max_datagram_size}, {}};
        ::sockaddr_storage sender{};
        ::socklen_t sender_length = sizeof(sender);
        const auto count = ::recvfrom(static_cast<int>(sock.native_handle()), next.bytes.data(),
            next.bytes.size(), 0, reinterpret_cast<::sockaddr*>(&sender), &sender_length);
        if (count < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            co_return std::unexpected(last_error());
        }
        next.bytes.resize(static_cast<std::size_t>(count));
        next.peer = endpoint_from_sockaddr(sender);
        result.push_back(std::move(next));
    }
    co_return result;
}

auto async_sendto_batch(io_context& ctx, socket& sock,
    std::span<const udp_send_datagram> datagrams, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    std::size_t submitted{};
    for (const auto& datagram : datagrams)
    {
        if (token.is_cancelled())
        {
            if (submitted != 0U)
                co_return submitted;
            co_return std::unexpected(make_error_code(errc::operation_aborted));
        }
        auto sent = co_await async_sendto(ctx, sock, datagram.bytes, datagram.peer, token);
        if (!sent)
        {
            if (submitted != 0U)
                co_return submitted;
            co_return std::unexpected(sent.error());
        }
        ++submitted;
    }
    co_return submitted;
}

#endif // CNETMOD_HAS_KQUEUE

} // namespace cnetmod
