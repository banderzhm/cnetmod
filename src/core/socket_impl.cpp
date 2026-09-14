module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
// clang-format off: Winsock declarations must precede extension headers.
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <MSWSock.h>
// clang-format on
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

module cnetmod.core.socket;

import std;
import cnetmod.core.error;

namespace cnetmod {

// =============================================================================
// Helpers: address_family -> AF_xxx / socket_type -> SOCK_xxx
// =============================================================================

namespace {

    auto to_native_family(address_family family) noexcept -> int
    {
        switch (family)
        {
        case address_family::ipv4:
            return AF_INET;
        case address_family::ipv6:
            return AF_INET6;
        case address_family::unspecified:
            return AF_UNSPEC;
        }
        return AF_UNSPEC;
    }

    auto to_native_socktype(socket_type type) noexcept -> int
    {
        switch (type)
        {
        case socket_type::stream:
            return SOCK_STREAM;
        case socket_type::datagram:
            return SOCK_DGRAM;
        }
        return SOCK_STREAM;
    }

    auto last_error() noexcept -> int
    {
#ifdef CNETMOD_PLATFORM_WINDOWS
        return ::WSAGetLastError();
#else
        return errno;
#endif
    }

    void suppress_sigpipe([[maybe_unused]] native_handle_t handle) noexcept
    {
#ifdef CNETMOD_PLATFORM_MACOS
        const int enabled = 1;
        (void)::setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE,
            &enabled, static_cast<socklen_t>(sizeof(enabled)));
#endif
    }

    /// Fill sockaddr_storage, return length
    auto fill_sockaddr(const endpoint& ep, ::sockaddr_storage& storage) noexcept -> int
    {
        std::memset(&storage, 0, sizeof(storage));
        if (ep.address().is_v4())
        {
            auto& sa = reinterpret_cast<::sockaddr_in&>(storage);
            sa.sin_family = AF_INET;
            sa.sin_port = htons(ep.port());
            sa.sin_addr = ep.address().to_v4().native();
            return static_cast<int>(sizeof(::sockaddr_in));
        }
        else
        {
            auto& sa = reinterpret_cast<::sockaddr_in6&>(storage);
            sa.sin6_family = AF_INET6;
            sa.sin6_port = htons(ep.port());
            sa.sin6_addr = ep.address().to_v6().native();
            return static_cast<int>(sizeof(::sockaddr_in6));
        }
    }

} // anonymous namespace

// =============================================================================
// Lifecycle
// =============================================================================

socket::~socket()
{
    close();
}

auto socket::from_native(native_handle_t handle) noexcept -> socket
{
    suppress_sigpipe(handle);
    return socket{handle};
}

socket::socket(socket&& other) noexcept
#ifdef CNETMOD_PLATFORM_WINDOWS
    : handle_(other.handle_), family_(other.family_), skip_completion_on_success_(other.skip_completion_on_success_), registered_io_requested_(other.registered_io_requested_), registered_io_enabled_(other.registered_io_enabled_), iocp_association_(other.iocp_association_.load(std::memory_order_relaxed)), async_state_(std::move(other.async_state_))
#else
    : handle_(other.handle_), family_(other.family_)
#endif
{
    other.handle_ = invalid_handle;
    other.family_ = address_family::unspecified;
#ifdef CNETMOD_PLATFORM_WINDOWS
    other.skip_completion_on_success_ = false;
    other.registered_io_requested_ = false;
    other.registered_io_enabled_ = false;
    other.iocp_association_.store(0, std::memory_order_relaxed);
#endif
}

auto socket::operator=(socket&& other) noexcept -> socket&
{
    if (this != &other)
    {
        close();
        handle_ = other.handle_;
        family_ = other.family_;
#ifdef CNETMOD_PLATFORM_WINDOWS
        skip_completion_on_success_ = other.skip_completion_on_success_;
        registered_io_requested_ = other.registered_io_requested_;
        registered_io_enabled_ = other.registered_io_enabled_;
        iocp_association_.store(
            other.iocp_association_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        async_state_ = std::move(other.async_state_);
#endif
        other.handle_ = invalid_handle;
        other.family_ = address_family::unspecified;
#ifdef CNETMOD_PLATFORM_WINDOWS
        other.skip_completion_on_success_ = false;
        other.registered_io_requested_ = false;
        other.registered_io_enabled_ = false;
        other.iocp_association_.store(0, std::memory_order_relaxed);
#endif
    }
    return *this;
}

#ifdef CNETMOD_PLATFORM_WINDOWS

auto socket::claim_iocp_association(std::uintptr_t port) noexcept
    -> iocp_association_claim
{
    // The pending marker is never a Windows HANDLE. It lets another coroutine
    // wait for the single CreateIoCompletionPort call instead of racing it.
    constexpr std::uintptr_t pending = 1;
    for (;;)
    {
        auto observed = iocp_association_.load(std::memory_order_acquire);
        if (observed == port)
            return iocp_association_claim::already_associated;
        if (observed == 0)
        {
            if (iocp_association_.compare_exchange_weak(observed, pending,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                return iocp_association_claim::claimed;
            continue;
        }
        if (observed == pending)
        {
            iocp_association_.wait(pending, std::memory_order_relaxed);
            continue;
        }
        return iocp_association_claim::different_context;
    }
}

void socket::complete_iocp_association(std::uintptr_t port, bool succeeded) noexcept
{
    iocp_association_.store(succeeded ? port : 0, std::memory_order_release);
    iocp_association_.notify_all();
}

#endif

// =============================================================================
// Creation
// =============================================================================

auto socket::create(address_family family, socket_type type, bool registered_io)
    -> std::expected<socket, std::error_code>
{
    int af = to_native_family(family);
    int st = to_native_socktype(type);
    int proto = (type == socket_type::stream) ? IPPROTO_TCP : IPPROTO_UDP;

#ifdef CNETMOD_PLATFORM_WINDOWS
    // WSA_FLAG_OVERLAPPED allows handle to be associated with IOCP. RIO is a
    // creation-time opt-in and is valid only for datagram sockets.
    DWORD flags = WSA_FLAG_OVERLAPPED;
    if (registered_io && type == socket_type::datagram)
        flags |= WSA_FLAG_REGISTERED_IO;
    SOCKET fd = ::WSASocketW(af, st, proto, nullptr, 0, flags);
    bool registered_socket = registered_io && type == socket_type::datagram;
    // RIO is optional on older Windows providers. The public socket contract
    // must still open a normal overlapped UDP socket rather than make HTTP/3
    // startup dependent on one acceleration capability.
    if (fd == INVALID_SOCKET && registered_socket)
    {
        fd = ::WSASocketW(af, st, proto, nullptr, 0, WSA_FLAG_OVERLAPPED);
        registered_socket = false;
    }
    if (fd == INVALID_SOCKET)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#else
    int fd = ::socket(af, st, proto);
    if (fd < 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#endif

    socket result{fd, family};
    suppress_sigpipe(fd);
#ifdef CNETMOD_PLATFORM_WINDOWS
    result.registered_io_requested_ = registered_io && type == socket_type::datagram;
    result.registered_io_enabled_ = registered_socket;
#else
    (void)registered_io;
#endif
    return result;
}

// =============================================================================
// bind / listen
// =============================================================================

auto socket::bind(const endpoint& ep) -> std::expected<void, std::error_code>
{
    ::sockaddr_storage storage{};
    int len = fill_sockaddr(ep, storage);

    if (::bind(handle_, reinterpret_cast<const ::sockaddr*>(&storage), len) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));

    return {};
}

auto socket::listen(int backlog) -> std::expected<void, std::error_code>
{
    if (::listen(handle_, backlog) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
    return {};
}

// =============================================================================
// Options
// =============================================================================

auto socket::set_non_blocking(bool enabled) -> std::expected<void, std::error_code>
{
#ifdef CNETMOD_PLATFORM_WINDOWS
    u_long mode = enabled ? 1 : 0;
    if (::ioctlsocket(handle_, FIONBIO, &mode) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#else
    int flags = ::fcntl(handle_, F_GETFL, 0);
    if (flags < 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
    flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(handle_, F_SETFL, flags) < 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));
#endif
    return {};
}

auto socket::apply_options(const socket_options& opts)
    -> std::expected<void, std::error_code>
{
#ifdef CNETMOD_PLATFORM_WINDOWS
    if (opts.processor_affinity)
    {
        // SIO_CPU_AFFINITY is a Windows networking-stack extension used by
        // high-throughput UDP servers to create per-processor sockets for one
        // local port. Older SDKs do not always publish the symbolic constant.
        constexpr DWORD sio_cpu_affinity = _WSAIOW(IOC_VENDOR, 21);
        DWORD bytes_returned{};
        auto processor = *opts.processor_affinity;
        if (::WSAIoctl(handle_, sio_cpu_affinity, &processor, sizeof(processor),
                nullptr, 0, &bytes_returned, nullptr, nullptr) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }
    if (opts.skip_completion_on_success)
    {
        if (!::SetFileCompletionNotificationModes(
                reinterpret_cast<HANDLE>(handle_),
                FILE_SKIP_COMPLETION_PORT_ON_SUCCESS | FILE_SKIP_SET_EVENT_ON_HANDLE))
            return std::unexpected(make_error_code(from_native_error(last_error())));
        skip_completion_on_success_ = true;
    }
#endif

    // SO_REUSEADDR
    if (opts.reuse_address)
    {
        int val = 1;
        if (::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR,
                reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    // Port-sharing socket groups let UDP protocols shard one listening
    // endpoint across independent event loops. POSIX exposes SO_REUSEPORT.
    // Windows server sockets use SIO_CPU_AFFINITY instead; SO_REUSEADDR has
    // different ownership and delivery semantics and must not emulate it.
#ifdef CNETMOD_PLATFORM_WINDOWS
    (void)opts.reuse_port;
#else
    if (opts.reuse_port)
    {
        int val = 1;
        if (::setsockopt(handle_, SOL_SOCKET, SO_REUSEPORT,
                &val, sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }
#endif

    // TCP_NODELAY
    if (opts.no_delay)
    {
        int val = 1;
        if (::setsockopt(handle_, IPPROTO_TCP, TCP_NODELAY,
                reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    // IPV6_V6ONLY: explicit dual-stack control for IPv6 listeners/sockets.
    if (opts.ipv6_only.has_value() && family_ == address_family::ipv6)
    {
        int val = *opts.ipv6_only ? 1 : 0;
        if (::setsockopt(handle_, IPPROTO_IPV6, IPV6_V6ONLY,
                reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    // RIO sockets are submitted exclusively through registered-I/O calls.
    // Winsock rejects FIONBIO on these handles with WSAEOPNOTSUPP, while
    // overlapped and RIO submissions themselves never block a worker thread.
    // A normal socket (including a provider fallback) keeps the established
    // non-blocking configuration.
    if (opts.non_blocking
#ifdef CNETMOD_PLATFORM_WINDOWS
        && !registered_io_enabled_
#endif
    )
    {
        if (auto r = set_non_blocking(true); !r)
            return r;
    }

    // Receive buffer
    if (opts.recv_buffer_size > 0)
    {
        int val = opts.recv_buffer_size;
        if (::setsockopt(handle_, SOL_SOCKET, SO_RCVBUF,
                reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    // Send buffer
    if (opts.send_buffer_size > 0)
    {
        int val = opts.send_buffer_size;
        if (::setsockopt(handle_, SOL_SOCKET, SO_SNDBUF,
                reinterpret_cast<const char*>(&val), sizeof(val)) != 0)
            return std::unexpected(make_error_code(from_native_error(last_error())));
    }

    return {};
}

auto socket::join_multicast_group(const ip_address& group,
    std::optional<ip_address> local_address,
    unsigned int interface_index)
    -> std::expected<void, std::error_code>
{
    if (group.is_v4())
    {
        ::ip_mreq mreq{};
        mreq.imr_multiaddr = group.to_v4().native();
        mreq.imr_interface = local_address && local_address->is_v4()
            ? local_address->to_v4().native()
            : ipv4_address::any().native();
        if (::setsockopt(handle_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0)
        {
            return std::unexpected(make_error_code(from_native_error(last_error())));
        }
        return {};
    }

    ::ipv6_mreq mreq6{};
    mreq6.ipv6mr_multiaddr = group.to_v6().native();
    mreq6.ipv6mr_interface = interface_index;
    if (::setsockopt(handle_, IPPROTO_IPV6, IPV6_JOIN_GROUP,
            reinterpret_cast<const char*>(&mreq6), sizeof(mreq6)) != 0)
    {
        return std::unexpected(make_error_code(from_native_error(last_error())));
    }
    return {};
}

auto socket::leave_multicast_group(const ip_address& group,
    std::optional<ip_address> local_address,
    unsigned int interface_index)
    -> std::expected<void, std::error_code>
{
    if (group.is_v4())
    {
        ::ip_mreq mreq{};
        mreq.imr_multiaddr = group.to_v4().native();
        mreq.imr_interface = local_address && local_address->is_v4()
            ? local_address->to_v4().native()
            : ipv4_address::any().native();
        if (::setsockopt(handle_, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                reinterpret_cast<const char*>(&mreq), sizeof(mreq)) != 0)
        {
            return std::unexpected(make_error_code(from_native_error(last_error())));
        }
        return {};
    }

    ::ipv6_mreq mreq6{};
    mreq6.ipv6mr_multiaddr = group.to_v6().native();
    mreq6.ipv6mr_interface = interface_index;
    if (::setsockopt(handle_, IPPROTO_IPV6, IPV6_LEAVE_GROUP,
            reinterpret_cast<const char*>(&mreq6), sizeof(mreq6)) != 0)
    {
        return std::unexpected(make_error_code(from_native_error(last_error())));
    }
    return {};
}

auto socket::set_multicast_hops(address_family family, int hops)
    -> std::expected<void, std::error_code>
{
    if (family == address_family::ipv4)
    {
#ifdef CNETMOD_PLATFORM_WINDOWS
        DWORD value = static_cast<DWORD>(std::max(hops, 0));
#else
        unsigned char value = static_cast<unsigned char>(std::clamp(hops, 0, 255));
#endif
        if (::setsockopt(handle_, IPPROTO_IP, IP_MULTICAST_TTL,
                reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
        {
            return std::unexpected(make_error_code(from_native_error(last_error())));
        }
        return {};
    }

    int value = std::max(hops, 0);
    if (::setsockopt(handle_, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
            reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
    {
        return std::unexpected(make_error_code(from_native_error(last_error())));
    }
    return {};
}

auto socket::set_multicast_loopback(address_family family, bool enabled)
    -> std::expected<void, std::error_code>
{
    if (family == address_family::ipv4)
    {
#ifdef CNETMOD_PLATFORM_WINDOWS
        DWORD value = enabled ? 1u : 0u;
#else
        unsigned char value = enabled ? 1u : 0u;
#endif
        if (::setsockopt(handle_, IPPROTO_IP, IP_MULTICAST_LOOP,
                reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
        {
            return std::unexpected(make_error_code(from_native_error(last_error())));
        }
        return {};
    }

    int value = enabled ? 1 : 0;
    if (::setsockopt(handle_, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
            reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
    {
        return std::unexpected(make_error_code(from_native_error(last_error())));
    }
    return {};
}

// =============================================================================
// local_endpoint
// =============================================================================

auto socket::local_endpoint() const -> std::expected<endpoint, std::error_code>
{
    ::sockaddr_storage storage{};
#ifdef CNETMOD_PLATFORM_WINDOWS
    int len = sizeof(storage);
#else
    ::socklen_t len = sizeof(storage);
#endif
    if (::getsockname(handle_,
            reinterpret_cast<::sockaddr*>(&storage), &len) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));

    if (storage.ss_family == AF_INET)
    {
        auto& sa = reinterpret_cast<const ::sockaddr_in&>(storage);
        char buf[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        auto a = ipv4_address::from_string(buf);
        return endpoint{ip_address{a.value_or(ipv4_address{})}, ntohs(sa.sin_port)};
    }
    else
    {
        auto& sa = reinterpret_cast<const ::sockaddr_in6&>(storage);
        char buf[INET6_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET6, &sa.sin6_addr, buf, sizeof(buf));
        auto a = ipv6_address::from_string(buf);
        return endpoint{ip_address{a.value_or(ipv6_address{})}, ntohs(sa.sin6_port)};
    }
}

// =============================================================================
// remote_endpoint
// =============================================================================

auto socket::remote_endpoint() const -> std::expected<endpoint, std::error_code>
{
    ::sockaddr_storage storage{};
#ifdef CNETMOD_PLATFORM_WINDOWS
    int len = sizeof(storage);
#else
    ::socklen_t len = sizeof(storage);
#endif

    if (::getpeername(handle_,
            reinterpret_cast<::sockaddr*>(&storage), &len) != 0)
        return std::unexpected(make_error_code(from_native_error(last_error())));

    if (storage.ss_family == AF_INET)
    {
        auto& sa = reinterpret_cast<const ::sockaddr_in&>(storage);
        char buf[INET_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
        auto a = ipv4_address::from_string(buf);
        return endpoint{ip_address{a.value_or(ipv4_address{})}, ntohs(sa.sin_port)};
    }
    else
    {
        auto& sa = reinterpret_cast<const ::sockaddr_in6&>(storage);
        char buf[INET6_ADDRSTRLEN]{};
        ::inet_ntop(AF_INET6, &sa.sin6_addr, buf, sizeof(buf));
        auto a = ipv6_address::from_string(buf);
        return endpoint{ip_address{a.value_or(ipv6_address{})}, ntohs(sa.sin6_port)};
    }
}

// =============================================================================
// Close
// =============================================================================

void socket::shutdown_send() noexcept
{
    if (handle_ == invalid_handle)
        return;
#ifdef CNETMOD_PLATFORM_WINDOWS
    ::shutdown(handle_, SD_SEND);
#else
    ::shutdown(handle_, SHUT_WR);
#endif
}

void socket::shutdown_both() noexcept
{
    if (handle_ == invalid_handle)
        return;
#ifdef CNETMOD_PLATFORM_WINDOWS
    ::shutdown(handle_, SD_BOTH);
#else
    ::shutdown(handle_, SHUT_RDWR);
#endif
}

void socket::close() noexcept
{
    if (handle_ == invalid_handle)
        return;
#ifdef CNETMOD_PLATFORM_WINDOWS
    if (async_state_)
        async_state_->on_socket_close();
    ::closesocket(handle_);
#else
    ::close(handle_);
#endif
    handle_ = invalid_handle;
#ifdef CNETMOD_PLATFORM_WINDOWS
    skip_completion_on_success_ = false;
    registered_io_requested_ = false;
    registered_io_enabled_ = false;
    async_state_.reset();
#endif
}

} // namespace cnetmod
