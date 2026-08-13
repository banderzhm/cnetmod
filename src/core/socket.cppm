module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
    #include <WS2tcpip.h>
    #include <WinSock2.h>
#else
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

export module cnetmod.core.socket;

import std;
import cnetmod.core.error;
import cnetmod.core.address;

namespace cnetmod {

// =============================================================================
// Platform Type Aliases
// =============================================================================

#ifdef CNETMOD_PLATFORM_WINDOWS
export using native_handle_t = SOCKET;
export inline constexpr native_handle_t invalid_handle = INVALID_SOCKET;
#else
export using native_handle_t = int;
export inline constexpr native_handle_t invalid_handle = -1;
#endif

// =============================================================================
// Socket Type
// =============================================================================

/// Socket protocol type
export enum class socket_type
{
    stream,   // TCP
    datagram, // UDP
};

// =============================================================================
// Socket Options
// =============================================================================

/// Socket options
export struct socket_options
{
    bool reuse_address = false;
    bool reuse_port = false;
    bool non_blocking = true;
    bool no_delay = false;                        // TCP_NODELAY
    std::optional<bool> ipv6_only = std::nullopt; // IPV6_V6ONLY; nullopt keeps OS default
    int recv_buffer_size = 0;                     // 0 = system default
    int send_buffer_size = 0;                     // 0 = system default
#ifdef CNETMOD_PLATFORM_WINDOWS
    // Bind a UDP socket to a Windows networking processor. SIO_CPU_AFFINITY
    // permits processor-affine sockets to share one server port without
    // relying on SO_REUSEADDR's incompatible delivery semantics.
    std::optional<std::uint16_t> processor_affinity = std::nullopt;
    // Avoid an IOCP packet when an overlapped operation completes inline.
    // Callers must handle the synchronous result before suspending.
    bool skip_completion_on_success = false;
    /// Request a WSA_FLAG_REGISTERED_IO datagram socket. This is a
    /// creation-time choice used by the Windows RIO backend; normal IOCP is
    /// retained as a capability fallback when RIO is unavailable.
    bool registered_io = false;
#endif
};

#ifdef CNETMOD_PLATFORM_WINDOWS
/// Snapshot published by native socket backends. Counters are monotonically
/// increasing and may change while the snapshot is being read.
export struct socket_async_statistics
{
    std::uint64_t receive_completions{};
    std::uint64_t send_completions{};
    std::uint64_t receive_queue_drops{};
    bool registered_io_active{};
    bool registered_io_fallback{};
};

/// Internal lifetime hook used by native asynchronous socket backends. It
/// lets a backend retain in-kernel operations safely across closes without
/// coupling the portable socket module to a particular IO implementation.
export class socket_async_state
{
public:
    virtual ~socket_async_state() = default;
    virtual void on_socket_close() noexcept = 0;

    [[nodiscard]] virtual auto statistics() const noexcept -> socket_async_statistics
    {
        return {};
    }
};
#endif

// =============================================================================
// Socket Class
// =============================================================================

/// Platform-independent socket wrapper
/// Owns the lifetime of native handle (RAII)
export class socket
{
public:
    socket() noexcept = default;
    ~socket();

    // Non-copyable
    socket(const socket&) = delete;
    auto operator=(const socket&) -> socket& = delete;

    // Movable
    socket(socket&& other) noexcept;
    auto operator=(socket&& other) noexcept -> socket&;

    /// Create socket
    [[nodiscard]] static auto create(
        address_family family,
        socket_type type,
        bool registered_io = false) -> std::expected<socket, std::error_code>;

    /// Construct from native handle (takes ownership)
    [[nodiscard]] static auto from_native(native_handle_t handle) noexcept -> socket
    {
        return socket{handle};
    }

    /// Bind address
    [[nodiscard]] auto bind(const endpoint& ep) -> std::expected<void, std::error_code>;

    /// Listen
    [[nodiscard]] auto listen(int backlog = 128) -> std::expected<void, std::error_code>;

    /// Set non-blocking mode
    [[nodiscard]] auto set_non_blocking(bool enabled) -> std::expected<void, std::error_code>;

    /// Apply options
    [[nodiscard]] auto apply_options(const socket_options& opts) -> std::expected<void, std::error_code>;

    /// Join an IPv4/IPv6 multicast group on this datagram socket.
    [[nodiscard]] auto join_multicast_group(
        const ip_address& group,
        std::optional<ip_address> local_address = std::nullopt,
        unsigned int interface_index = 0) -> std::expected<void, std::error_code>;

    /// Leave an IPv4/IPv6 multicast group.
    [[nodiscard]] auto leave_multicast_group(
        const ip_address& group,
        std::optional<ip_address> local_address = std::nullopt,
        unsigned int interface_index = 0) -> std::expected<void, std::error_code>;

    /// Configure outbound multicast hop limit / TTL.
    [[nodiscard]] auto set_multicast_hops(address_family family, int hops)
        -> std::expected<void, std::error_code>;

    /// Configure whether outbound multicast datagrams are looped back locally.
    [[nodiscard]] auto set_multicast_loopback(address_family family, bool enabled)
        -> std::expected<void, std::error_code>;

    /// Get local endpoint (getsockname)
    [[nodiscard]] auto local_endpoint() const -> std::expected<endpoint, std::error_code>;

    /// Get remote endpoint (getpeername)
    [[nodiscard]] auto remote_endpoint() const -> std::expected<endpoint, std::error_code>;

    /// Close socket
    void close() noexcept;

    /// Shutdown socket directions without releasing the handle.
    void shutdown_send() noexcept;
    void shutdown_both() noexcept;

    /// Get native handle
    [[nodiscard]] auto native_handle() const noexcept -> native_handle_t
    {
        return handle_;
    }

    [[nodiscard]] auto family() const noexcept -> address_family
    {
        return family_;
    }

    /// Whether this socket is backed by Windows Registered I/O (RIO).
    ///
    /// This capability query is intentionally available on every platform so
    /// protocol scheduling code can choose an appropriate batch size without
    /// leaking Windows-only preprocessor branches into its hot path. Non-Windows
    /// backends never expose RIO and therefore return false.
    [[nodiscard]] auto registered_io_enabled() const noexcept -> bool
    {
#ifdef CNETMOD_PLATFORM_WINDOWS
        return registered_io_enabled_;
#else
        return false;
#endif
    }

#ifdef CNETMOD_PLATFORM_WINDOWS
    /// Internal IOCP association state.  A socket is permanently bound to one
    /// completion port, so native I/O must not repeat CreateIoCompletionPort
    /// for every read and write on the hot path.
    enum class iocp_association_claim : std::uint8_t
    {
        already_associated,
        claimed,
        different_context,
    };

    [[nodiscard]] auto claim_iocp_association(std::uintptr_t port) noexcept
        -> iocp_association_claim;
    void complete_iocp_association(std::uintptr_t port, bool succeeded) noexcept;

    [[nodiscard]] auto skips_completion_on_success() const noexcept -> bool
    {
        return skip_completion_on_success_;
    }

    /// Internal capability downgrade used when a provider accepts a
    /// registered-I/O socket but does not expose the RIO extension table.
    void disable_registered_io() noexcept
    {
        registered_io_enabled_ = false;
    }

    /// Preserve an explicit RIO request after rebuilding a socket with the
    /// regular overlapped provider. This keeps diagnostics truthful: callers
    /// can distinguish "RIO was not requested" from "RIO was requested but
    /// the provider could not allocate its registered-I/O resources".
    void mark_registered_io_fallback() noexcept
    {
        registered_io_requested_ = true;
        registered_io_enabled_ = false;
    }

    /// Backend-private state. Normal socket users never need this; native
    /// async engines use it solely to coordinate close with kernel requests.
    void set_async_state(std::shared_ptr<socket_async_state> state) noexcept
    {
        async_state_ = std::move(state);
    }

    [[nodiscard]] auto async_state() const noexcept -> const std::shared_ptr<socket_async_state>&
    {
        return async_state_;
    }

    [[nodiscard]] auto async_statistics() const noexcept -> socket_async_statistics
    {
        auto result = async_state_ ? async_state_->statistics() : socket_async_statistics{};
        result.registered_io_active = registered_io_enabled_;
        result.registered_io_fallback = registered_io_requested_ && !registered_io_enabled_;
        return result;
    }
#endif

    /// Release ownership only when no backend owns outstanding asynchronous
    /// work. A live RIO ring retains registered buffers and completion state;
    /// exporting its native socket would let the caller close or reuse that
    /// handle underneath the backend.
    [[nodiscard]] auto try_release() noexcept
        -> std::expected<native_handle_t, std::error_code>
    {
#ifdef CNETMOD_PLATFORM_WINDOWS
        if (async_state_)
            return std::unexpected(make_error_code(errc::operation_in_progress));
#endif
        auto h = handle_;
        handle_ = invalid_handle;
        family_ = address_family::unspecified;
#ifdef CNETMOD_PLATFORM_WINDOWS
        skip_completion_on_success_ = false;
        registered_io_requested_ = false;
        registered_io_enabled_ = false;
#endif
        return h;
    }

    /// Compatibility wrapper for code that uses a sentinel native handle for
    /// failure. New code should use `try_release()` and handle the explicit
    /// in-flight-I/O error instead.
    [[nodiscard]] auto release() noexcept -> native_handle_t
    {
        auto released = try_release();
        if (!released)
            return invalid_handle;
        return *released;
    }

    /// Check if valid
    [[nodiscard]] auto is_open() const noexcept -> bool
    {
        return handle_ != invalid_handle;
    }

    explicit operator bool() const noexcept
    {
        return is_open();
    }

private:
    explicit socket(native_handle_t handle,
        address_family family = address_family::unspecified) noexcept
        : handle_(handle), family_(family) {}

    native_handle_t handle_ = invalid_handle;
    address_family family_ = address_family::unspecified;
#ifdef CNETMOD_PLATFORM_WINDOWS
    bool skip_completion_on_success_{};
    bool registered_io_requested_{};
    bool registered_io_enabled_{};
    std::atomic<std::uintptr_t> iocp_association_{};
    std::shared_ptr<socket_async_state> async_state_;
#endif
};

} // namespace cnetmod
