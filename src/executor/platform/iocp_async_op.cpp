module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_IOCP
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
// clang-format off: Winsock must precede Windows.h and extension headers.
    #include <WinSock2.h>
    #include <WS2tcpip.h>
    #include <MSWSock.h>
    #include <Windows.h>
    // clang-format on
    #include <exec/static_thread_pool.hpp>
#endif

module cnetmod.executor.async_op;

#ifdef CNETMOD_HAS_IOCP
import cnetmod.io.platform.iocp;
import cnetmod.executor.pool;
#endif
import cnetmod.core.serial_port;
import cnetmod.core.socket;
import cnetmod.core.log;
import cnetmod.coro.cancel;
import cnetmod.coro.channel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.utils.concurrent_containers.queue;
import cnetmod.utils.concurrent_containers.atomic_hash_map;

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
        if (!r)
        {
            if (r.error().value() == ERROR_INVALID_PARAMETER)
                return {};
            return r;
        }
        return {};
    }

    auto ensure_associated(iocp_context& iocp, socket& sock)
        -> std::expected<void, std::error_code>
    {
        const auto port = reinterpret_cast<std::uintptr_t>(iocp.native_handle());
        switch (sock.claim_iocp_association(port))
        {
        case socket::iocp_association_claim::already_associated:
            return {};
        case socket::iocp_association_claim::different_context:
            return std::unexpected(
                std::make_error_code(std::errc::operation_not_permitted));
        case socket::iocp_association_claim::claimed:
            break;
        }

        auto result = iocp.associate(
            reinterpret_cast<HANDLE>(sock.native_handle()));
        // CreateIoCompletionPort returns ERROR_INVALID_PARAMETER for a handle
        // that is already attached to this port.  Preserve the historical
        // compatibility behaviour while caching that established association.
        const auto succeeded = result.has_value() ||
            result.error().value() == ERROR_INVALID_PARAMETER;
        sock.complete_iocp_association(port, succeeded);
        if (!succeeded)
            return std::unexpected(result.error());
        return {};
    }

    auto load_accept_ex(SOCKET s) -> LPFN_ACCEPTEX
    {
        LPFN_ACCEPTEX fn = nullptr;
        GUID guid = WSAID_ACCEPTEX;
        DWORD bytes = 0;
        ::WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guid, sizeof(guid), &fn, sizeof(fn),
            &bytes, nullptr, nullptr);
        return fn;
    }

    auto load_connect_ex(SOCKET s) -> LPFN_CONNECTEX
    {
        LPFN_CONNECTEX fn = nullptr;
        GUID guid = WSAID_CONNECTEX;
        DWORD bytes = 0;
        ::WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
            &guid, sizeof(guid), &fn, sizeof(fn),
            &bytes, nullptr, nullptr);
        return fn;
    }

    auto get_socket_family(SOCKET s) -> int
    {
        ::sockaddr_storage addr{};
        int addrlen = sizeof(addr);
        if (::getsockname(s, reinterpret_cast<::sockaddr*>(&addr), &addrlen) == 0)
            return addr.ss_family;
        return AF_INET;
    }

    inline auto& file_pool()
    {
        static thread_pool pool;
        return pool;
    }

    auto fill_sockaddr(const endpoint& ep,
        ::sockaddr_storage& storage) noexcept -> int
    {
        std::memset(&storage, 0, sizeof(storage));
        if (ep.address().is_v4())
        {
            auto& sa = reinterpret_cast<::sockaddr_in&>(storage);
            sa.sin_family = AF_INET;
            sa.sin_port = ::htons(ep.port());
            sa.sin_addr = ep.address().to_v4().native();
            return sizeof(::sockaddr_in);
        }
        else
        {
            auto& sa = reinterpret_cast<::sockaddr_in6&>(storage);
            sa.sin6_family = AF_INET6;
            sa.sin6_port = ::htons(ep.port());
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

} // anonymous namespace

namespace {

    // RIO receives must remain posted for the lifetime of a UDP listener. A
    // per-call RIOReceive batch cannot safely cancel the unused requests after
    // the first packet arrives, which is why this is a persistent request ring
    // rather than a cosmetic replacement for the WSARecvFrom drain loop.
    class rio_udp_receive_ring final : public socket_async_state,
                                       public std::enable_shared_from_this<rio_udp_receive_ring>
    {
        enum class request_kind : std::uint8_t
        {
            receive,
            send,
        };

        static constexpr std::size_t address_bytes = sizeof(sockaddr_storage) + 16U;
        static constexpr std::size_t send_depth = 64U;

        struct request_tag
        {
            request_kind kind;
        };

        struct send_batch
        {
            void set_setup_error(int value) noexcept
            {
                int expected{};
                (void)error.compare_exchange_strong(expected, value,
                    std::memory_order_release, std::memory_order_relaxed);
            }

            void complete(int status, bool success) noexcept
            {
                if (success)
                    submitted.fetch_add(1U, std::memory_order_relaxed);
                else
                    set_setup_error(status);
                if (pending.fetch_sub(1U, std::memory_order_acq_rel) == 1U)
                    (void)readiness.try_send({});
            }

            std::atomic<std::size_t> pending{};
            std::atomic<std::size_t> submitted{};
            std::atomic<int> error{};
            channel<std::monostate> readiness{1U};
        };

        // A registered RIO buffer is expensive to create and destroy. QUIC
        // datagrams are bounded and normally fit in this listener's receive
        // payload size, so use a persistent copy-in send pool instead of two
        // RIORegisterBuffer calls for every packet.  Separate slots avoid
        // cache-line contention between independent connection strands.
        struct alignas(64) send_slot
        {
            explicit send_slot(std::size_t datagram_size)
                : storage(datagram_size + address_bytes) {}

            std::vector<std::byte> storage;
            RIO_BUFFERID buffer_id{RIO_INVALID_BUFFERID};
            RIO_BUF payload{};
            RIO_BUF remote{};
            std::atomic<bool> in_use{};
        };

        struct send_operation
        {
            void release_buffers(const RIO_EXTENSION_FUNCTION_TABLE& api) noexcept
            {
                if (fixed_slot != nullptr)
                {
                    fixed_slot->in_use.store(false, std::memory_order_release);
                    fixed_slot = nullptr;
                    return;
                }
                if (payload_id != RIO_INVALID_BUFFERID)
                {
                    api.RIODeregisterBuffer(payload_id);
                    payload_id = RIO_INVALID_BUFFERID;
                }
                if (remote_id != RIO_INVALID_BUFFERID)
                {
                    api.RIODeregisterBuffer(remote_id);
                    remote_id = RIO_INVALID_BUFFERID;
                }
            }

            request_tag tag{request_kind::send};
            std::shared_ptr<send_batch> batch;
            send_slot* fixed_slot{};
            sockaddr_storage remote_storage{};
            RIO_BUFFERID payload_id{RIO_INVALID_BUFFERID};
            RIO_BUFFERID remote_id{RIO_INVALID_BUFFERID};
            RIO_BUF payload{};
            RIO_BUF remote{};
        };

    public:
        static auto create(iocp_context& context, socket& socket,
            std::size_t datagram_size, std::size_t receive_depth)
            -> std::expected<std::shared_ptr<rio_udp_receive_ring>, std::error_code>
        {
            auto ring = std::shared_ptr<rio_udp_receive_ring>{
                new rio_udp_receive_ring(context, socket.native_handle(),
                    datagram_size, receive_depth)};
            if (auto initialized = ring->initialize(); !initialized)
                return std::unexpected(initialized.error());
            return ring;
        }

        ~rio_udp_receive_ring() override
        {
            destroy_native();
        }

        [[nodiscard]] auto receive(std::size_t maximum)
            -> task<std::expected<std::vector<udp_received_datagram>, std::error_code>>
        {
            std::vector<udp_received_datagram> result;
            result.reserve(maximum);
            for (;;)
            {
                while (result.size() < maximum)
                {
                    auto packet = completed_.try_dequeue();
                    if (!packet)
                        break;
                    result.push_back(std::move(*packet));
                }
                if (!result.empty())
                    co_return result;
                if (const auto failure = failed_.load(std::memory_order_acquire);
                    failure != 0)
                    co_return std::unexpected(std::error_code(failure,
                        std::system_category()));
                if (closing_.load(std::memory_order_acquire))
                    co_return std::unexpected(std::make_error_code(std::errc::not_connected));
                const auto ready = co_await readiness_.receive();
                if (!ready)
                    co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            }
        }

        [[nodiscard]] auto receive(std::size_t maximum, cancel_token& token)
            -> task<std::expected<std::vector<udp_received_datagram>, std::error_code>>
        {
            if (token.is_cancelled())
                co_return std::unexpected(make_error_code(errc::operation_aborted));

            std::vector<udp_received_datagram> result;
            result.reserve(maximum);
            for (;;)
            {
                while (result.size() < maximum)
                {
                    auto packet = completed_.try_dequeue();
                    if (!packet)
                        break;
                    result.push_back(std::move(*packet));
                }
                if (!result.empty())
                    co_return result;
                if (const auto failure = failed_.load(std::memory_order_acquire);
                    failure != 0)
                    co_return std::unexpected(std::error_code(failure,
                        std::system_category()));
                if (closing_.load(std::memory_order_acquire))
                    co_return std::unexpected(std::make_error_code(std::errc::not_connected));
                if (token.is_cancelled())
                    co_return std::unexpected(make_error_code(errc::operation_aborted));

                // RIO has no per-request cancellation API for its persistent
                // receive ring. Cancellation ends this caller's wait while
                // the listener-owned receives remain posted and safe.
                token.ctx_ = this;
                token.cancel_fn_ = &cancel_receive_wait;
                token.pending_.store(true, std::memory_order_release);
                const auto ready = co_await readiness_.receive();
                token.pending_.store(false, std::memory_order_release);
                if (token.ctx_ == this)
                {
                    token.cancel_fn_ = nullptr;
                    token.ctx_ = nullptr;
                }
                if (token.is_cancelled())
                    co_return std::unexpected(make_error_code(errc::operation_aborted));
                if (!ready)
                    co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            }
        }

        [[nodiscard]] auto send(std::span<const udp_send_datagram> datagrams)
            -> task<std::expected<std::size_t, std::error_code>>
        {
            if (datagrams.empty())
                co_return std::size_t{};
            if (closing_.load(std::memory_order_acquire))
                co_return std::unexpected(std::make_error_code(std::errc::not_connected));

            auto batch = std::make_shared<send_batch>();
            for (const auto& datagram : datagrams)
            {
                if (datagram.bytes.data == nullptr || datagram.bytes.size == 0U ||
                    datagram.bytes.size > std::numeric_limits<DWORD>::max())
                {
                    batch->set_setup_error(WSAEINVAL);
                    break;
                }
                auto operation = std::make_shared<send_operation>();
                operation->batch = batch;
                if (datagram.bytes.size <= datagram_size_)
                {
                    auto* slot = try_acquire_send_slot();
                    if (slot == nullptr)
                    {
                        // The public batch API explicitly permits a partial
                        // send result. Do not queue an unbounded number of
                        // copies when the RIO request queue is saturated.
                        batch->set_setup_error(WSAENOBUFS);
                        break;
                    }
                    operation->fixed_slot = slot;
                    std::memcpy(slot->storage.data(), datagram.bytes.data,
                        datagram.bytes.size);
                    sockaddr_storage remote_storage{};
                    const auto remote_length = fill_sockaddr(datagram.peer,
                        remote_storage);
                    std::memcpy(slot->storage.data() + datagram_size_,
                        std::addressof(remote_storage), remote_length);
                    operation->payload = slot->payload;
                    operation->payload.Length = static_cast<DWORD>(datagram.bytes.size);
                    operation->remote = slot->remote;
                    operation->remote.Length = static_cast<DWORD>(remote_length);
                }
                else
                {
                    // Preserve generic UDP semantics for datagrams larger
                    // than the persistent QUIC-sized slots. This fallback is
                    // cold and retains the caller-owned buffer until RIO
                    // completion exactly as before.
                    operation->payload_id = api_.RIORegisterBuffer(
                        static_cast<char*>(const_cast<void*>(datagram.bytes.data)),
                        static_cast<DWORD>(datagram.bytes.size));
                    if (operation->payload_id == RIO_INVALID_BUFFERID)
                    {
                        batch->set_setup_error(::WSAGetLastError());
                        break;
                    }
                    const auto remote_length = fill_sockaddr(datagram.peer,
                        operation->remote_storage);
                    operation->remote_id = api_.RIORegisterBuffer(
                        reinterpret_cast<char*>(std::addressof(operation->remote_storage)),
                        static_cast<DWORD>(remote_length));
                    if (operation->remote_id == RIO_INVALID_BUFFERID)
                    {
                        operation->release_buffers(api_);
                        batch->set_setup_error(::WSAGetLastError());
                        break;
                    }
                    operation->payload = RIO_BUF{operation->payload_id, 0U,
                        static_cast<DWORD>(datagram.bytes.size)};
                    operation->remote = RIO_BUF{operation->remote_id, 0U,
                        static_cast<DWORD>(remote_length)};
                }
                const auto operation_key = reinterpret_cast<std::uintptr_t>(operation.get());
                // RIO retains only RequestContext. Keep the actual operation
                // in a lock-free ownership table until its CQ result is
                // consumed; a local vector would be destroyed as soon as
                // this coroutine suspends and leaves a dangling tag.
                if (!inflight_sends_.try_emplace(operation_key, operation))
                {
                    operation->release_buffers(api_);
                    batch->set_setup_error(WSAENOBUFS);
                    break;
                }
                batch->pending.fetch_add(1U, std::memory_order_release);
                send_inflight_.fetch_add(1U, std::memory_order_release);
                if (!api_.RIOSendEx(request_queue_, std::addressof(operation->payload), 1U,
                        nullptr, std::addressof(operation->remote), nullptr, nullptr, 0U,
                        std::addressof(operation->tag)))
                {
                    const auto error = ::WSAGetLastError();
                    send_inflight_.fetch_sub(1U, std::memory_order_acq_rel);
                    (void)inflight_sends_.erase(operation_key);
                    operation->release_buffers(api_);
                    batch->complete(error, false);
                    break;
                }
            }

            if (batch->pending.load(std::memory_order_acquire) != 0U &&
                api_.RIONotify(completion_queue_) != 0)
                batch->set_setup_error(::WSAGetLastError());

            while (batch->pending.load(std::memory_order_acquire) != 0U)
            {
                const auto ready = co_await batch->readiness.receive();
                if (!ready)
                    co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            }
            const auto submitted = batch->submitted.load(std::memory_order_acquire);
            if (submitted != 0U)
                co_return submitted;
            const auto error = batch->error.load(std::memory_order_acquire);
            co_return std::unexpected(make_error_code(from_native_error(
                error != 0 ? error : WSAECONNABORTED)));
        }

        void on_socket_close() noexcept override
        {
            bool expected = false;
            if (!closing_.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                return;
            // Keep callback storage, registered buffers and the RIO CQ alive
            // after socket::close releases its reference. closesocket causes
            // every posted receive to complete; drain() releases this hold
            // only after the final completion has been observed.
            self_keepalive_.store(shared_from_this(), std::memory_order_release);
            readiness_.close();
            if (inflight_.load(std::memory_order_acquire) == 0U &&
                send_inflight_.load(std::memory_order_acquire) == 0U)
                finish_shutdown();
        }

        [[nodiscard]] auto statistics() const noexcept -> socket_async_statistics override
        {
            return {
                receive_completions_.load(std::memory_order_relaxed),
                send_completions_.load(std::memory_order_relaxed),
                receive_queue_drops_.load(std::memory_order_relaxed),
                true,
                false};
        }

    private:
        static void cancel_receive_wait(cancel_token& token) noexcept
        {
            auto* state = static_cast<rio_udp_receive_ring*>(token.ctx_);
            if (state != nullptr)
                (void)state->readiness_.try_send({});
        }

        struct receive_slot
        {
            explicit receive_slot(std::size_t datagram_size)
                : storage(datagram_size + address_bytes) {}

            request_tag tag{request_kind::receive};
            std::vector<std::byte> storage;
            RIO_BUFFERID buffer_id{RIO_INVALID_BUFFERID};
            RIO_BUF data{};
            RIO_BUF remote{};
        };

        rio_udp_receive_ring(iocp_context& context, native_handle_t socket,
            std::size_t datagram_size, std::size_t receive_depth)
            : context_(context), socket_(socket), datagram_size_(datagram_size), completed_(receive_depth * 4U), readiness_(1U)
        {
            slots_.reserve(receive_depth);
            for (std::size_t index{}; index < receive_depth; ++index)
                slots_.emplace_back(datagram_size_);
            send_slots_.reserve(send_depth);
            for (std::size_t index{}; index < send_depth; ++index)
                send_slots_.push_back(std::make_unique<send_slot>(datagram_size_));
            api_.cbSize = sizeof(api_);
            notification_.completion_callback = &on_iocp_notification;
            notification_.completion_context = this;
        }

        [[nodiscard]] auto initialize() -> std::expected<void, std::error_code>
        {
            const auto fail = [](std::string_view stage) -> std::unexpected<std::error_code>
            {
                const auto native_error = ::WSAGetLastError();
                logger::warn{"RIO {} failed: WSA error={}", stage, native_error};
                return std::unexpected(make_error_code(from_native_error(native_error)));
            };
            GUID id = WSAID_MULTIPLE_RIO;
            DWORD bytes{};
            if (::WSAIoctl(socket_, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                    std::addressof(id), sizeof(id), std::addressof(api_), sizeof(api_),
                    std::addressof(bytes), nullptr, nullptr) != 0)
                return fail("extension-table lookup");

            RIO_NOTIFICATION_COMPLETION notification{};
            notification.Type = RIO_IOCP_COMPLETION;
            notification.Iocp.IocpHandle = context_.native_handle();
            notification.Iocp.CompletionKey = nullptr;
            notification.Iocp.Overlapped = std::addressof(notification_);
            completion_queue_ = api_.RIOCreateCompletionQueue(
                static_cast<DWORD>(slots_.size() * 2U), std::addressof(notification));
            if (completion_queue_ == RIO_INVALID_CQ)
                return fail("completion-queue creation");

            request_queue_ = api_.RIOCreateRequestQueue(socket_,
                // RIO reserves kernel bookkeeping for the full declared
                // send depth.  256 fails with WSAENOBUFS on the standard
                // Windows UDP provider before a single request is posted;
                // 64 still covers the receive ring and one UDP batch while
                // preserving API-level partial-send back-pressure.
                static_cast<ULONG>(slots_.size()), 1U, send_depth, 1U,
                completion_queue_, completion_queue_, this);
            if (request_queue_ == RIO_INVALID_RQ)
            {
                destroy_native();
                return fail("request-queue creation");
            }

            for (auto& slot : slots_)
            {
                slot.buffer_id = api_.RIORegisterBuffer(
                    reinterpret_cast<char*>(slot.storage.data()),
                    static_cast<DWORD>(slot.storage.size()));
                if (slot.buffer_id == RIO_INVALID_BUFFERID)
                {
                    destroy_native();
                    return fail("receive-buffer registration");
                }
                slot.data = RIO_BUF{slot.buffer_id, 0U,
                    static_cast<DWORD>(datagram_size_)};
                slot.remote = RIO_BUF{slot.buffer_id,
                    static_cast<DWORD>(datagram_size_),
                    static_cast<DWORD>(address_bytes)};
            }
            for (const auto& slot : send_slots_)
            {
                slot->buffer_id = api_.RIORegisterBuffer(
                    reinterpret_cast<char*>(slot->storage.data()),
                    static_cast<DWORD>(slot->storage.size()));
                if (slot->buffer_id == RIO_INVALID_BUFFERID)
                {
                    destroy_native();
                    return fail("send-buffer registration");
                }
                slot->payload = RIO_BUF{slot->buffer_id, 0U,
                    static_cast<DWORD>(datagram_size_)};
                slot->remote = RIO_BUF{slot->buffer_id,
                    static_cast<DWORD>(datagram_size_),
                    static_cast<DWORD>(address_bytes)};
            }
            bool partial_post_failure{};
            for (auto& slot : slots_)
            {
                if (!post_receive(slot))
                {
                    // Requests already accepted by RIO cannot be cancelled
                    // individually. Keep this state alive and surface the
                    // error through receive(); socket close will drain those
                    // requests before buffers/CQ are reclaimed.
                    partial_post_failure = true;
                    break;
                }
            }
            if (api_.RIONotify(completion_queue_) != 0)
            {
                logger::warn{"RIO initial notification failed: WSA error={}", ::WSAGetLastError()};
                if (inflight_.load(std::memory_order_acquire) != 0U)
                    return {};
                destroy_native();
                return std::unexpected(make_error_code(from_native_error(::WSAGetLastError())));
            }
            if (partial_post_failure && inflight_.load(std::memory_order_acquire) == 0U)
            {
                destroy_native();
                return std::unexpected(std::make_error_code(std::errc::io_error));
            }
            return {};
        }

        [[nodiscard]] auto post_receive(receive_slot& slot) noexcept -> bool
        {
            if (api_.RIOReceiveEx(request_queue_, std::addressof(slot.data), 1U,
                    nullptr, std::addressof(slot.remote), nullptr, nullptr, 0U,
                    std::addressof(slot)) == FALSE)
            {
                const auto error = ::WSAGetLastError();
                logger::warn{"RIO receive submission failed: WSA error={}", error};
                failed_.store(error, std::memory_order_release);
                (void)readiness_.try_send({});
                return false;
            }
            inflight_.fetch_add(1U, std::memory_order_release);
            return true;
        }

        [[nodiscard]] auto try_acquire_send_slot() noexcept -> send_slot*
        {
            for (const auto& slot : send_slots_)
            {
                bool expected = false;
                if (slot->in_use.compare_exchange_strong(expected, true,
                        std::memory_order_acq_rel, std::memory_order_relaxed))
                    return slot.get();
            }
            return nullptr;
        }

        static void on_iocp_notification(iocp_overlapped& notification) noexcept
        {
            auto* state = static_cast<rio_udp_receive_ring*>(notification.completion_context);
            if (state != nullptr)
                state->drain();
        }

        void drain() noexcept
        {
            auto keepalive = shared_from_this();
            if (draining_.test_and_set(std::memory_order_acq_rel))
                return;

            RIORESULT results[64];
            for (;;)
            {
                const auto count = api_.RIODequeueCompletion(completion_queue_, results,
                    static_cast<ULONG>(std::size(results)));
                if (count == 0U)
                    break;
                for (ULONG index{}; index < count; ++index)
                    consume(results[index]);
            }
            draining_.clear(std::memory_order_release);
            if (!closing_.load(std::memory_order_acquire) &&
                api_.RIONotify(completion_queue_) != 0)
            {
                failed_.store(::WSAGetLastError(), std::memory_order_release);
                (void)readiness_.try_send({});
            }
            if (closing_.load(std::memory_order_acquire) &&
                inflight_.load(std::memory_order_acquire) == 0U &&
                send_inflight_.load(std::memory_order_acquire) == 0U)
                finish_shutdown();
        }

        void consume(const RIORESULT& result) noexcept
        {
            auto* tag = reinterpret_cast<request_tag*>(
                static_cast<std::uintptr_t>(result.RequestContext));
            if (tag->kind == request_kind::send)
            {
                const auto key = reinterpret_cast<std::uintptr_t>(tag);
                const auto operation = inflight_sends_.find(key);
                if (!operation)
                {
                    logger::error{"RIO completion referenced an unknown send operation"};
                    return;
                }
                (*operation)->release_buffers(api_);
                (*operation)->batch->complete(result.Status, result.Status == NO_ERROR);
                if (result.Status == NO_ERROR)
                    send_completions_.fetch_add(1U, std::memory_order_relaxed);
                (void)inflight_sends_.erase(key);
                send_inflight_.fetch_sub(1U, std::memory_order_acq_rel);
                if (closing_.load(std::memory_order_acquire) &&
                    inflight_.load(std::memory_order_acquire) == 0U &&
                    send_inflight_.load(std::memory_order_acquire) == 0U)
                    finish_shutdown();
                return;
            }
            auto* slot = reinterpret_cast<receive_slot*>(tag);
            inflight_.fetch_sub(1U, std::memory_order_acq_rel);
            if (result.Status == NO_ERROR && !closing_.load(std::memory_order_acquire))
            {
                receive_completions_.fetch_add(1U, std::memory_order_relaxed);
                udp_received_datagram packet{udp_datagram_buffer{datagram_size_}, {}};
                packet.bytes.assign(slot->storage.begin(), slot->storage.begin() + std::min<std::size_t>(result.BytesTransferred, datagram_size_));
                sockaddr_storage remote{};
                std::memcpy(std::addressof(remote),
                    slot->storage.data() + datagram_size_, sizeof(remote));
                packet.peer = endpoint_from_sockaddr(remote);
                if (completed_.try_enqueue(std::move(packet)))
                    (void)readiness_.try_send({});
                else
                    receive_queue_drops_.fetch_add(1U, std::memory_order_relaxed);
            }
            if (!closing_.load(std::memory_order_acquire))
                (void)post_receive(*slot);
        }

        void finish_shutdown() noexcept
        {
            bool expected = false;
            if (!native_destroyed_.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                return;
            destroy_native();
            self_keepalive_.store({}, std::memory_order_release);
        }

        void destroy_native() noexcept
        {
            if (completion_queue_ == RIO_INVALID_CQ)
                return;
            for (auto& slot : slots_)
            {
                if (slot.buffer_id != RIO_INVALID_BUFFERID)
                {
                    api_.RIODeregisterBuffer(slot.buffer_id);
                    slot.buffer_id = RIO_INVALID_BUFFERID;
                }
            }
            for (const auto& slot : send_slots_)
            {
                if (slot->buffer_id != RIO_INVALID_BUFFERID)
                {
                    api_.RIODeregisterBuffer(slot->buffer_id);
                    slot->buffer_id = RIO_INVALID_BUFFERID;
                }
            }
            api_.RIOCloseCompletionQueue(completion_queue_);
            completion_queue_ = RIO_INVALID_CQ;
            request_queue_ = RIO_INVALID_RQ;
        }

        iocp_context& context_;
        native_handle_t socket_;
        std::size_t datagram_size_{};
        RIO_EXTENSION_FUNCTION_TABLE api_{};
        RIO_CQ completion_queue_{RIO_INVALID_CQ};
        RIO_RQ request_queue_{RIO_INVALID_RQ};
        iocp_overlapped notification_{};
        std::vector<receive_slot> slots_;
        std::vector<std::unique_ptr<send_slot>> send_slots_;
        concurrent_containers::bounded_mpmc_queue<udp_received_datagram> completed_;
        concurrent_containers::atomic_hash_map<std::uintptr_t,
            std::shared_ptr<send_operation>>
            inflight_sends_{512U};
        channel<std::monostate> readiness_;
        std::atomic<std::size_t> inflight_{};
        std::atomic<std::size_t> send_inflight_{};
        std::atomic<std::uint64_t> receive_completions_{};
        std::atomic<std::uint64_t> send_completions_{};
        std::atomic<std::uint64_t> receive_queue_drops_{};
        std::atomic<int> failed_{};
        std::atomic<bool> closing_{};
        std::atomic<bool> native_destroyed_{};
        std::atomic_flag draining_{};
        std::atomic<std::shared_ptr<rio_udp_receive_ring>> self_keepalive_{};
    };

    auto get_rio_receive_ring(iocp_context& context, socket& socket,
        std::size_t datagram_size, std::size_t receive_depth)
        -> std::shared_ptr<rio_udp_receive_ring>
    {
        if (!socket.registered_io_enabled())
            return {};
        if (const auto existing = socket.async_state())
        {
            if (const auto ring = std::dynamic_pointer_cast<rio_udp_receive_ring>(existing))
                return ring;
            socket.disable_registered_io();
            return {};
        }
        auto ring = rio_udp_receive_ring::create(context, socket, datagram_size,
            receive_depth);
        if (!ring)
        {
            logger::warn{"RIO setup unavailable; falling back to IOCP UDP: error={}",
                ring.error().value()};
            socket.disable_registered_io();
            return {};
        }
        socket.set_async_state(*ring);
        return *ring;
    }

} // namespace

// =============================================================================
// IOCP Suspend Awaiter
// =============================================================================

static void iocp_cancel_fn(cancel_token& token) noexcept;

struct iocp_sendto_on_suspend
{
    iocp_overlapped& ov;
    io_context& resume_context;
    native_handle_t socket_handle;
    WSABUF& buffer;
    const ::sockaddr_storage& destination;
    int destination_length;
    DWORD& bytes_sent;
    bool skip_completion_on_success;

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> bool
    {
        ov.coroutine = coroutine;
        ov.resume_context = std::addressof(resume_context);
        const int status = ::WSASendTo(socket_handle, &buffer, 1, &bytes_sent, 0,
            reinterpret_cast<const ::sockaddr*>(&destination), destination_length,
            &ov, nullptr);
        if (status == 0 && skip_completion_on_success)
        {
            ov.bytes_transferred = bytes_sent;
            return false;
        }
        if (status == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                ov.error = make_error_code(from_native_error(error));
                return false;
            }
        }
        return true;
    }

    void await_resume() noexcept {}
};

// Every overlapped operation must publish its continuation before it is
// submitted to the kernel. An IOCP worker may otherwise consume an immediate
// completion in the gap before the continuation is published, leaving
// the coroutine parked forever.  `submit` returns true only when the
// operation completed synchronously without an IOCP notification (or failed
// synchronously after storing `ov.error`).
template <typename Submit>
struct iocp_submit_suspend
{
    iocp_overlapped& ov;
    bool skip_completion_on_success;
    cancel_token* token;
    void* io_handle;
    Submit submit;

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> bool
    {
        ov.coroutine = coroutine;
        if (token)
        {
            token->io_handle_ = io_handle;
            token->overlapped_ = static_cast<LPOVERLAPPED>(&ov);
            token->cancel_fn_ = &iocp_cancel_fn;
            token->pending_.store(true, std::memory_order_release);
        }

        if (submit(skip_completion_on_success))
        {
            if (token)
                token->pending_.store(false, std::memory_order_relaxed);
            return false;
        }

        // Cancellation can win between the caller's initial check and I/O
        // submission.  Cancel the newly submitted operation in that case.
        if (token && token->is_cancelled())
            iocp_cancel_fn(*token);
        return true;
    }

    void await_resume() noexcept
    {
        if (token)
            token->pending_.store(false, std::memory_order_relaxed);
    }
};

template <typename Submit>
iocp_submit_suspend(iocp_overlapped&, bool, cancel_token*, void*, Submit)
    -> iocp_submit_suspend<Submit>;

// Register the continuation before submitting the receive to Winsock.  An
// overlapped receive may complete on another IOCP worker immediately after
// WSARecvFrom returns.  Submitting first and assigning `ov.coroutine` in a
// later awaiter loses that completion (and leaves the caller suspended
// forever) under Release timing.
struct iocp_recvfrom_suspend
{
    iocp_overlapped& ov;
    native_handle_t socket_handle;
    WSABUF& buffer;
    DWORD& flags;
    ::sockaddr_storage& sender;
    INT& sender_length;
    DWORD& bytes_received;
    bool skip_completion_on_success;
    cancel_token* token{};

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> bool
    {
        ov.coroutine = coroutine;
        if (token)
        {
            token->io_handle_ = reinterpret_cast<void*>(socket_handle);
            token->overlapped_ = static_cast<LPOVERLAPPED>(&ov);
            token->cancel_fn_ = &iocp_cancel_fn;
            token->pending_.store(true, std::memory_order_release);
        }

        const int status = ::WSARecvFrom(socket_handle, &buffer, 1,
            &bytes_received, &flags, reinterpret_cast<::sockaddr*>(&sender),
            &sender_length, &ov, nullptr);
        if (status == 0 && skip_completion_on_success)
        {
            ov.bytes_transferred = bytes_received;
            if (token)
                token->pending_.store(false, std::memory_order_relaxed);
            return false;
        }
        if (status == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                ov.error = make_error_code(from_native_error(error));
                if (token)
                    token->pending_.store(false, std::memory_order_relaxed);
                return false;
            }
        }

        if (token && token->is_cancelled())
            iocp_cancel_fn(*token);
        return true;
    }

    void await_resume() noexcept
    {
        if (token)
            token->pending_.store(false, std::memory_order_relaxed);
    }
};

// UDP sends have the same completion-before-continuation race as receives.
// Keep the operation submission inside the awaiter after the coroutine has
// been published to IOCP.
struct iocp_sendto_suspend
{
    iocp_overlapped& ov;
    native_handle_t socket_handle;
    WSABUF& buffer;
    ::sockaddr_storage& destination;
    int destination_length;
    DWORD& bytes_sent;
    bool skip_completion_on_success;
    cancel_token* token{};

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> bool
    {
        ov.coroutine = coroutine;
        if (token)
        {
            token->io_handle_ = reinterpret_cast<void*>(socket_handle);
            token->overlapped_ = static_cast<LPOVERLAPPED>(&ov);
            token->cancel_fn_ = &iocp_cancel_fn;
            token->pending_.store(true, std::memory_order_release);
        }

        const int status = ::WSASendTo(socket_handle, &buffer, 1, &bytes_sent,
            0, reinterpret_cast<const ::sockaddr*>(&destination),
            destination_length, &ov, nullptr);
        if (status == 0 && skip_completion_on_success)
        {
            ov.bytes_transferred = bytes_sent;
            if (token)
                token->pending_.store(false, std::memory_order_relaxed);
            return false;
        }
        if (status == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            if (error != WSA_IO_PENDING)
            {
                ov.error = make_error_code(from_native_error(error));
                if (token)
                    token->pending_.store(false, std::memory_order_relaxed);
                return false;
            }
        }

        if (token && token->is_cancelled())
            iocp_cancel_fn(*token);
        return true;
    }

    void await_resume() noexcept
    {
        if (token)
            token->pending_.store(false, std::memory_order_relaxed);
    }
};

// =============================================================================
// IOCP Cancel Version Suspend Awaiter
// =============================================================================

/// cancel_fn_: Call CancelIoEx to cancel specified OVERLAPPED operation
static void iocp_cancel_fn(cancel_token& token) noexcept
{
    ::CancelIoEx(static_cast<HANDLE>(token.io_handle_),
        static_cast<LPOVERLAPPED>(token.overlapped_));
}

// Timer-queue callbacks can run as soon as CreateTimerQueueTimer returns.
// Publish the coroutine before creating the timer, otherwise a 1ms timer can
// post an IOCP completion before a continuation has been registered.
struct timer_queue_state
{
    iocp_overlapped ov;
    iocp_context* context{};
    HANDLE timer{};
    std::atomic<bool> completed{false};
};

static void iocp_timer_cancel_fn(cancel_token& token) noexcept
{
    auto* state = static_cast<timer_queue_state*>(token.ctx_);
    if (state && !state->completed.exchange(true, std::memory_order_acq_rel))
        state->context->post_completion(&state->ov);
}

struct iocp_timer_suspend
{
    timer_queue_state& state;
    DWORD milliseconds;
    cancel_token* token{};

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> bool
    {
        state.ov.coroutine = coroutine;
        const auto callback = [](PVOID parameter, BOOLEAN /*fired*/) noexcept
        {
            auto* timer_state = static_cast<timer_queue_state*>(parameter);
            if (!timer_state->completed.exchange(true, std::memory_order_acq_rel))
                timer_state->context->post_completion(&timer_state->ov);
        };
        if (!::CreateTimerQueueTimer(&state.timer, nullptr, callback, &state,
                milliseconds, 0, WT_EXECUTEONLYONCE))
        {
            state.ov.error = std::error_code(static_cast<int>(::GetLastError()),
                std::system_category());
            return false;
        }
        if (token)
        {
            token->pending_.store(true, std::memory_order_release);
            token->ctx_ = &state;
            token->cancel_fn_ = &iocp_timer_cancel_fn;
            if (token->is_cancelled())
                iocp_timer_cancel_fn(*token);
        }
        return true;
    }

    void await_resume() noexcept
    {
        if (token)
        {
            token->pending_.store(false, std::memory_order_relaxed);
            token->cancel_fn_ = nullptr;
            token->ctx_ = nullptr;
        }
    }
};

// =============================================================================
// Async Network Operations — IOCP
// =============================================================================

auto async_accept(io_context& ctx, socket& listener)
    -> task<std::expected<socket, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp, listener);
        !r)
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

    co_await iocp_submit_suspend{ov, listener.skips_completion_on_success(), nullptr,
        reinterpret_cast<void*>(listener.native_handle()), [&](bool skip_completion) noexcept
        {
            const BOOL ok = accept_ex(listener.native_handle(), accept_sock->native_handle(),
                output_buf, 0, addr_len, addr_len, &bytes, &ov);
            if (ok)
            {
                if (skip_completion)
                    ov.bytes_transferred = bytes;
                return skip_completion;
            }
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);

    SOCKET ls = listener.native_handle();
    ::setsockopt(accept_sock->native_handle(), SOL_SOCKET,
        SO_UPDATE_ACCEPT_CONTEXT,
        reinterpret_cast<const char*>(&ls), sizeof(ls));

    co_return std::move(*accept_sock);
}

auto async_accept(io_context& ctx, socket& listener, cancel_token& token)
    -> task<std::expected<socket, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp, listener);
        !r)
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

    co_await iocp_submit_suspend{ov, listener.skips_completion_on_success(),
        std::addressof(token), reinterpret_cast<void*>(listener.native_handle()),
        [&](bool skip_completion) noexcept
        {
            const BOOL ok = accept_ex(listener.native_handle(), accept_sock->native_handle(),
                output_buf, 0, addr_len, addr_len, &bytes, &ov);
            if (ok)
            {
                if (skip_completion)
                    ov.bytes_transferred = bytes;
                return skip_completion;
            }
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);

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

    if (auto r = ensure_associated(iocp, sock);
        !r)
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
    if (ep.address().is_v4())
    {
        auto& sa = reinterpret_cast<::sockaddr_in&>(dest);
        sa.sin_family = AF_INET;
        sa.sin_port = ::htons(ep.port());
        sa.sin_addr = ep.address().to_v4().native();
        dest_len = sizeof(::sockaddr_in);
    }
    else
    {
        auto& sa = reinterpret_cast<::sockaddr_in6&>(dest);
        sa.sin6_family = AF_INET6;
        sa.sin6_port = ::htons(ep.port());
        sa.sin6_addr = ep.address().to_v6().native();
        dest_len = sizeof(::sockaddr_in6);
    }

    iocp_overlapped ov;

    co_await iocp_submit_suspend{ov, sock.skips_completion_on_success(), nullptr,
        reinterpret_cast<void*>(sock.native_handle()), [&](bool skip_completion) noexcept
        {
            const BOOL ok = connect_ex(sock.native_handle(),
                reinterpret_cast<const ::sockaddr*>(&dest), dest_len, nullptr, 0, nullptr, &ov);
            if (ok)
                return skip_completion;
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);

    ::setsockopt(sock.native_handle(), SOL_SOCKET,
        SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);

    co_return std::expected<void, std::error_code>{};
}

auto async_connect(io_context& ctx, socket& sock, const endpoint& ep,
    cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp, sock);
        !r)
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
    if (ep.address().is_v4())
    {
        auto& sa = reinterpret_cast<::sockaddr_in&>(dest);
        sa.sin_family = AF_INET;
        sa.sin_port = ::htons(ep.port());
        sa.sin_addr = ep.address().to_v4().native();
        dest_len = sizeof(::sockaddr_in);
    }
    else
    {
        auto& sa = reinterpret_cast<::sockaddr_in6&>(dest);
        sa.sin6_family = AF_INET6;
        sa.sin6_port = ::htons(ep.port());
        sa.sin6_addr = ep.address().to_v6().native();
        dest_len = sizeof(::sockaddr_in6);
    }

    iocp_overlapped ov;

    co_await iocp_submit_suspend{ov, sock.skips_completion_on_success(),
        std::addressof(token), reinterpret_cast<void*>(sock.native_handle()),
        [&](bool skip_completion) noexcept
        {
            const BOOL ok = connect_ex(sock.native_handle(),
                reinterpret_cast<const ::sockaddr*>(&dest), dest_len, nullptr, 0, nullptr, &ov);
            if (ok)
                return skip_completion;
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);

    ::setsockopt(sock.native_handle(), SOL_SOCKET,
        SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);

    co_return std::expected<void, std::error_code>{};
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp, sock);
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    DWORD bytes_received{};
    iocp_overlapped ov;

    co_await iocp_submit_suspend{ov, sock.skips_completion_on_success(), nullptr,
        reinterpret_cast<void*>(sock.native_handle()), [&](bool skip_completion) noexcept
        {
            const int status = ::WSARecv(sock.native_handle(), &wsabuf, 1,
                &bytes_received, &flags, &ov, nullptr);
            if (status == 0)
            {
                if (skip_completion)
                    ov.bytes_transferred = bytes_received;
                return skip_completion;
            }
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_read(io_context& ctx, socket& sock, mutable_buffer buf,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, sock);
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    DWORD bytes_received{};
    iocp_overlapped ov;

    co_await iocp_submit_suspend{ov, sock.skips_completion_on_success(),
        std::addressof(token), reinterpret_cast<void*>(sock.native_handle()),
        [&](bool skip_completion) noexcept
        {
            const int status = ::WSARecv(sock.native_handle(), &wsabuf, 1,
                &bytes_received, &flags, &ov, nullptr);
            if (status == 0)
            {
                if (skip_completion)
                    ov.bytes_transferred = bytes_received;
                return skip_completion;
            }
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp, sock);
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD bytes_sent{};
    iocp_overlapped ov;

    co_await iocp_submit_suspend{ov, sock.skips_completion_on_success(), nullptr,
        reinterpret_cast<void*>(sock.native_handle()), [&](bool skip_completion) noexcept
        {
            const int status = ::WSASend(sock.native_handle(), &wsabuf, 1,
                &bytes_sent, 0, &ov, nullptr);
            if (status == 0)
            {
                if (skip_completion)
                    ov.bytes_transferred = bytes_sent;
                return skip_completion;
            }
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_write(io_context& ctx, socket& sock, const_buffer buf,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, sock);
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD bytes_sent{};
    iocp_overlapped ov;

    co_await iocp_submit_suspend{ov, sock.skips_completion_on_success(),
        std::addressof(token), reinterpret_cast<void*>(sock.native_handle()),
        [&](bool skip_completion) noexcept
        {
            const int status = ::WSASend(sock.native_handle(), &wsabuf, 1,
                &bytes_sent, 0, &ov, nullptr);
            if (status == 0)
            {
                if (skip_completion)
                    ov.bytes_transferred = bytes_sent;
                return skip_completion;
            }
            const int error = ::WSAGetLastError();
            if (error == WSA_IO_PENDING)
                return false;
            ov.error = make_error_code(from_native_error(error));
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

// =============================================================================
// Async File Operations — IOCP
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

auto async_file_remove(io_context& ctx, const std::filesystem::path& path)
    -> task<std::expected<void, std::error_code>>
{
    co_await pool_post_awaitable{file_pool()};
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (!error && std::filesystem::is_directory(status))
        error = std::make_error_code(std::errc::is_a_directory);
    if (!error)
        (void)std::filesystem::remove(path, error);
    co_await post_awaitable{ctx};
    if (error == std::errc::no_such_file_or_directory)
        error.clear();
    if (error)
        co_return std::unexpected(error);
    co_return std::expected<void, std::error_code>{};
}

auto async_file_remove(io_context& ctx, const std::filesystem::path& path,
    cancel_token& token) -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    auto result = co_await async_file_remove(ctx, path);
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
    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, f.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;
    ov.set_offset(offset);

    co_await iocp_submit_suspend{ov, false, nullptr,
        static_cast<void*>(f.native_handle()), [&](bool) noexcept
        {
            if (::ReadFile(f.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

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

    co_await iocp_submit_suspend{ov, false, std::addressof(token),
        static_cast<void*>(f.native_handle()), [&](bool) noexcept
        {
            if (::ReadFile(f.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);
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

    co_await iocp_submit_suspend{ov, false, nullptr,
        static_cast<void*>(f.native_handle()), [&](bool) noexcept
        {
            if (::WriteFile(f.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);
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

    co_await iocp_submit_suspend{ov, false, std::addressof(token),
        static_cast<void*>(f.native_handle()), [&](bool) noexcept
        {
            if (::WriteFile(f.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_file_flush(io_context& ctx, file& f)
    -> task<std::expected<void, std::error_code>>
{
    (void)ctx;
    BOOL ok = ::FlushFileBuffers(f.native_handle());
    if (!ok)
    {
        DWORD err = ::GetLastError();
        co_return std::unexpected(
            std::error_code(static_cast<int>(err), std::system_category()));
    }
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

auto async_send_file(io_context& ctx, socket& sock, file& source,
    std::uint64_t offset, std::uint64_t byte_count)
    -> task<std::expected<std::uint64_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);
    auto associated = ensure_associated(
        iocp, reinterpret_cast<HANDLE>(sock.native_handle()));
    if (!associated)
        co_return std::unexpected(associated.error());

    std::uint64_t transferred = 0;
    while (transferred < byte_count)
    {
        const auto chunk = static_cast<DWORD>(std::min<std::uint64_t>(
            byte_count - transferred,
            std::numeric_limits<DWORD>::max()));
        iocp_overlapped ov;
        ov.set_offset(offset + transferred);

        co_await iocp_submit_suspend{ov, false, nullptr,
            reinterpret_cast<void*>(sock.native_handle()), [&](bool) noexcept
            {
                if (::TransmitFile(sock.native_handle(), source.native_handle(), chunk, 0,
                        &ov, nullptr, 0))
                    return false;
                const auto error = ::WSAGetLastError();
                if (error == WSA_IO_PENDING)
                    return false;
                ov.error = make_error_code(from_native_error(error));
                return true;
            }};
        if (ov.error)
            co_return std::unexpected(ov.error);
        if (ov.bytes_transferred == 0)
            break;
        transferred += ov.bytes_transferred;
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

    auto& iocp = static_cast<iocp_context&>(ctx);
    auto associated = ensure_associated(
        iocp, reinterpret_cast<HANDLE>(sock.native_handle()));
    if (!associated)
        co_return std::unexpected(associated.error());

    std::uint64_t transferred = 0;
    while (transferred < byte_count)
    {
        const auto chunk = static_cast<DWORD>(std::min<std::uint64_t>(
            byte_count - transferred,
            std::numeric_limits<DWORD>::max()));
        iocp_overlapped ov;
        ov.set_offset(offset + transferred);

        co_await iocp_submit_suspend{ov, false, std::addressof(token),
            reinterpret_cast<void*>(sock.native_handle()), [&](bool) noexcept
            {
                if (::TransmitFile(sock.native_handle(), source.native_handle(), chunk, 0,
                        &ov, nullptr, 0))
                    return false;
                const auto error = ::WSAGetLastError();
                if (error == WSA_IO_PENDING)
                    return false;
                ov.error = make_error_code(from_native_error(error));
                return true;
            }};
        if (token.is_cancelled())
            co_return std::unexpected(
                make_error_code(errc::operation_aborted));
        if (ov.error)
            co_return std::unexpected(ov.error);
        if (ov.bytes_transferred == 0)
            break;
        transferred += ov.bytes_transferred;
    }
    co_return transferred;
}

// =============================================================================
// Async Timer — IOCP (CreateTimerQueueTimer + PostQueuedCompletionStatus)
// =============================================================================

auto async_timer_wait(io_context& ctx,
    std::chrono::steady_clock::duration duration)
    -> task<std::expected<void, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    timer_queue_state timer_state{};
    timer_state.context = &iocp;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    if (ms <= 0)
        ms = 1;

    co_await iocp_timer_suspend{timer_state, static_cast<DWORD>(ms)};
    if (timer_state.ov.error)
        co_return std::unexpected(timer_state.ov.error);

    ::DeleteTimerQueueTimer(nullptr, timer_state.timer, INVALID_HANDLE_VALUE);

    co_return std::expected<void, std::error_code>{};
}

auto async_timer_wait(io_context& ctx,
    std::chrono::steady_clock::duration duration,
    cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    timer_queue_state timer_state{};
    timer_state.context = &iocp;

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    if (ms <= 0)
        ms = 1;

    co_await iocp_timer_suspend{timer_state, static_cast<DWORD>(ms),
        std::addressof(token)};
    if (timer_state.ov.error)
        co_return std::unexpected(timer_state.ov.error);

    ::DeleteTimerQueueTimer(nullptr, timer_state.timer, INVALID_HANDLE_VALUE);

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

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

    co_await iocp_submit_suspend{ov, false, nullptr,
        static_cast<void*>(port.native_handle()), [&](bool) noexcept
        {
            if (::ReadFile(port.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

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

    co_await iocp_submit_suspend{ov, false, std::addressof(token),
        static_cast<void*>(port.native_handle()), [&](bool) noexcept
        {
            if (::ReadFile(port.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_serial_write(io_context& ctx, serial_port& port, const_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto r = ensure_associated(iocp, port.native_handle()); !r)
        co_return std::unexpected(r.error());

    iocp_overlapped ov;

    co_await iocp_submit_suspend{ov, false, nullptr,
        static_cast<void*>(port.native_handle()), [&](bool) noexcept
        {
            if (::WriteFile(port.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};
    if (ov.error)
        co_return std::unexpected(ov.error);
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

    co_await iocp_submit_suspend{ov, false, std::addressof(token),
        static_cast<void*>(port.native_handle()), [&](bool) noexcept
        {
            if (::WriteFile(port.native_handle(), buf.data,
                    static_cast<DWORD>(buf.size), nullptr, &ov))
                return false;
            const DWORD error = ::GetLastError();
            if (error == ERROR_IO_PENDING)
                return false;
            ov.error = std::error_code(static_cast<int>(error), std::system_category());
            return true;
        }};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
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
            reinterpret_cast<HANDLE>(sock.native_handle()));
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    iocp_overlapped ov;
    ::sockaddr_storage from_addr{};
    INT from_len = sizeof(from_addr);
    DWORD bytes_received{};

    co_await iocp_recvfrom_suspend{ov, sock.native_handle(), wsabuf, flags,
        from_addr, from_len, bytes_received, sock.skips_completion_on_success()};
    if (ov.error)
        co_return std::unexpected(ov.error);

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_recvfrom(io_context& ctx, socket& sock,
    mutable_buffer buf, endpoint& peer,
    cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    auto& iocp = static_cast<iocp_context&>(ctx);

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle()));
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = static_cast<char*>(buf.data);
    wsabuf.len = static_cast<ULONG>(buf.size);
    DWORD flags = 0;
    iocp_overlapped ov;
    ::sockaddr_storage from_addr{};
    INT from_len = sizeof(from_addr);
    DWORD bytes_received{};

    co_await iocp_recvfrom_suspend{ov, sock.native_handle(), wsabuf, flags,
        from_addr, from_len, bytes_received, sock.skips_completion_on_success(),
        std::addressof(token)};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);

    peer = endpoint_from_sockaddr(from_addr);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_sendto(io_context& ctx, socket& sock,
    const_buffer buf, const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(ctx);

    // HTTP/3 normally emits one congestion-controlled QUIC packet at a time.
    // Route those packets through the persistent RIO completion ring as well:
    // independent connection strands can then submit concurrently and one CQ
    // callback drains their completions in a batch. The cancellable overload
    // below intentionally remains IOCP-based because RIO has no per-request
    // cancellation primitive with equivalent semantics.
    if (sock.registered_io_enabled() && buf.data != nullptr && buf.size != 0U)
    {
        if (auto ring = get_rio_receive_ring(iocp, sock,
                std::max<std::size_t>(buf.size, 1U), 64U))
        {
            const std::array datagrams{udp_send_datagram{buf, peer}};
            auto sent = co_await ring->send(datagrams);
            if (sent && *sent == 1U)
                co_return buf.size;

            // A provider can support a persistent RIO receive queue yet
            // reject dynamic send-buffer registration or RIOSendEx. This is
            // observable on some Windows UDP providers during the Retry
            // exchange of a shared HTTP/3 listener. No datagram was accepted
            // when this one-element batch fails, so retire only the send
            // acceleration and retry through the established IOCP path.
            // Keep the receive ring alive: it owns posted kernel receives and
            // must drain them safely when the socket closes.
            logger::warn{"RIO UDP send unavailable; falling back to IOCP: error={}",
                sent ? std::make_error_code(std::errc::resource_unavailable_try_again).value()
                     : sent.error().value()};
            sock.disable_registered_io();
        }
    }

    if (auto r = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle()));
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    iocp_overlapped ov;
    ::sockaddr_storage dest{};
    int dest_len = fill_sockaddr(peer, dest);
    DWORD bytes_sent{};

    co_await iocp_sendto_suspend{ov, sock.native_handle(), wsabuf, dest,
        dest_len, bytes_sent, sock.skips_completion_on_success()};
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_sendto_on(io_context& socket_context, io_context& resume_context,
    socket& sock, const_buffer buf, const endpoint& peer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto& iocp = static_cast<iocp_context&>(socket_context);
    if (auto result = ensure_associated(iocp,
            reinterpret_cast<HANDLE>(sock.native_handle()));
        !result)
        co_return std::unexpected(result.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    iocp_overlapped ov;
    ::sockaddr_storage destination{};
    const int destination_length = fill_sockaddr(peer, destination);
    DWORD bytes_sent{};

    co_await iocp_sendto_on_suspend{ov, resume_context, sock.native_handle(),
        wsabuf, destination, destination_length, bytes_sent,
        sock.skips_completion_on_success()};
    if (ov.error)
        co_return std::unexpected(ov.error);
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
            reinterpret_cast<HANDLE>(sock.native_handle()));
        !r)
        co_return std::unexpected(r.error());

    WSABUF wsabuf{};
    wsabuf.buf = const_cast<char*>(static_cast<const char*>(buf.data));
    wsabuf.len = static_cast<ULONG>(buf.size);
    iocp_overlapped ov;
    ::sockaddr_storage dest{};
    int dest_len = fill_sockaddr(peer, dest);
    DWORD bytes_sent{};

    co_await iocp_sendto_suspend{ov, sock.native_handle(), wsabuf, dest,
        dest_len, bytes_sent, sock.skips_completion_on_success(),
        std::addressof(token)};

    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    if (ov.error)
        co_return std::unexpected(ov.error);
    co_return static_cast<std::size_t>(ov.bytes_transferred);
}

auto async_recvfrom_batch(io_context& ctx, socket& sock,
    std::size_t max_datagrams, std::size_t max_datagram_size)
    -> task<std::expected<std::vector<udp_received_datagram>, std::error_code>>
{
    if (max_datagrams == 0U || max_datagram_size == 0U)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    auto& iocp = static_cast<iocp_context&>(ctx);
    // A registered socket uses a persistent RIO request ring. Its completion
    // queue is drained in batches by the IOCP adapter above; an unavailable
    // provider disables only this acceleration and falls through to the
    // established overlapped implementation below.
    if (auto ring = get_rio_receive_ring(iocp, sock, max_datagram_size,
            std::clamp<std::size_t>(max_datagrams * 2U, 64U, 256U)))
        co_return co_await ring->receive(max_datagrams);

    std::vector<udp_received_datagram> result;
    result.reserve(max_datagrams);
    udp_received_datagram first{udp_datagram_buffer{max_datagram_size}, {}};
    auto received = co_await async_recvfrom(ctx, sock,
        mutable_buffer{first.bytes.data(), first.bytes.size()}, first.peer);
    if (!received)
        co_return std::unexpected(received.error());
    first.bytes.resize(*received);
    result.push_back(std::move(first));

    // The first WSARecvFrom completion establishes readiness.  Drain only
    // immediately available datagrams; subsequent batches return to IOCP so
    // the listener never blocks an I/O worker on an empty socket.
    while (result.size() < max_datagrams)
    {
        // An IOCP socket is normally blocking for synchronous Winsock calls.
        // Calling WSARecvFrom with a null OVERLAPPED after the first packet
        // can therefore block the entire listener while it waits to fill the
        // rest of the batch. Probe readiness first: this preserves the batch
        // fast path without turning a short UDP burst into a receive deadlock.
        fd_set readable{};
        FD_ZERO(&readable);
        FD_SET(sock.native_handle(), &readable);
        TIMEVAL immediate{};
        const auto ready = ::select(0, &readable, nullptr, nullptr, &immediate);
        if (ready == SOCKET_ERROR)
            co_return std::unexpected(make_error_code(from_native_error(::WSAGetLastError())));
        if (ready == 0)
            break;
        udp_received_datagram next{udp_datagram_buffer{max_datagram_size}, {}};
        WSABUF buffer{static_cast<ULONG>(next.bytes.size()),
            static_cast<CHAR*>(static_cast<void*>(next.bytes.data()))};
        DWORD bytes{};
        DWORD flags{};
        ::sockaddr_storage sender{};
        INT sender_length = sizeof(sender);
        const int status = ::WSARecvFrom(sock.native_handle(), &buffer, 1, &bytes, &flags,
            reinterpret_cast<::sockaddr*>(&sender), &sender_length, nullptr, nullptr);
        if (status == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
                break;
            co_return std::unexpected(make_error_code(from_native_error(error)));
        }
        next.bytes.resize(bytes);
        next.peer = endpoint_from_sockaddr(sender);
        result.push_back(std::move(next));
    }
    co_return result;
}

auto async_sendto_batch(io_context& ctx, socket& sock,
    std::span<const udp_send_datagram> datagrams)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (datagrams.empty())
        co_return std::size_t{};
    if (sock.registered_io_enabled())
    {
        std::size_t maximum_size{};
        for (const auto& datagram : datagrams)
            maximum_size = std::max(maximum_size, datagram.bytes.size);
        auto& iocp = static_cast<iocp_context&>(ctx);
        if (auto ring = get_rio_receive_ring(iocp, sock,
                std::max<std::size_t>(maximum_size, 1U), 64U))
        {
            // One RIO request queue accepts a bounded number of outstanding
            // sends. A partial result is already part of this API's UDP
            // backpressure contract, so callers can continue with the next
            // batch without hidden heap queues.
            co_return co_await ring->send(datagrams.first(
                std::min<std::size_t>(datagrams.size(), 256U)));
        }
    }

    std::size_t submitted{};
    // Each item is submitted with the existing IOCP overlapped operation.
    // This maintains completion-driven backpressure and avoids assigning a
    // mutable destination sockaddr to an overlapped request after its frame
    // has gone out of scope.
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

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (auto ring = get_rio_receive_ring(iocp, sock, max_datagram_size,
            std::clamp<std::size_t>(max_datagrams * 2U, 64U, 256U)))
        co_return co_await ring->receive(max_datagrams, token);

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
        fd_set readable{};
        FD_ZERO(&readable);
        FD_SET(sock.native_handle(), &readable);
        TIMEVAL immediate{};
        const auto ready = ::select(0, &readable, nullptr, nullptr, &immediate);
        if (ready == SOCKET_ERROR)
            co_return std::unexpected(make_error_code(from_native_error(::WSAGetLastError())));
        if (ready == 0)
            break;
        udp_received_datagram next{udp_datagram_buffer{max_datagram_size}, {}};
        WSABUF buffer{static_cast<ULONG>(next.bytes.size()),
            static_cast<CHAR*>(static_cast<void*>(next.bytes.data()))};
        DWORD bytes{};
        DWORD flags{};
        ::sockaddr_storage sender{};
        INT sender_length = sizeof(sender);
        const int status = ::WSARecvFrom(sock.native_handle(), &buffer, 1, &bytes, &flags,
            reinterpret_cast<::sockaddr*>(&sender), &sender_length, nullptr, nullptr);
        if (status == SOCKET_ERROR)
        {
            const int error = ::WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
                break;
            co_return std::unexpected(make_error_code(from_native_error(error)));
        }
        next.bytes.resize(bytes);
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
        // Preserve precise cancellation: RIO lacks an equivalent per-send
        // cancel primitive, so the token-aware API intentionally uses the
        // cancellable IOCP operation rather than a non-interruptible RIO send.
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

auto prepare_async_datagram_io(io_context& ctx, socket& sock,
    std::size_t max_datagram_size, std::size_t receive_depth)
    -> std::expected<void, std::error_code>
{
    if (!sock.registered_io_enabled())
        return {};
    if (max_datagram_size == 0U || receive_depth == 0U)
        return std::unexpected(make_error_code(errc::invalid_argument));

    auto& iocp = static_cast<iocp_context&>(ctx);
    if (get_rio_receive_ring(iocp, sock, max_datagram_size, receive_depth))
        return {};

    // `get_rio_receive_ring` disables registered I/O on this handle when its
    // provider cannot create the ring. Its owner must replace the handle with
    // a plain overlapped UDP socket before issuing normal async operations.
    return std::unexpected(make_error_code(errc::operation_not_supported));
}

#endif // CNETMOD_HAS_IOCP

} // namespace cnetmod
