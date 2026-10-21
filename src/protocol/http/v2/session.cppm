export module cnetmod.protocol.http.v2.session;

import std;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.task_group;
import cnetmod.coro.mutex;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.http.v2.frame;
import cnetmod.protocol.http.v2.settings;
import cnetmod.protocol.http.v2.header_compression;
import cnetmod.protocol.http.v2.stream;

export namespace cnetmod::http::v2 {
struct server_request
{
    std::uint32_t stream_id{};
    std::vector<header_field> headers;
    std::vector<std::byte> body;
    std::shared_ptr<cnetmod::http::request_body_stream> body_stream;
};

/// Flyweight metadata with compile-time static-storage and const qualification.
/// Unlike a borrowed span, this factory cannot accept a request-local array.
class static_header_fields
{
public:
    constexpr static_header_fields() noexcept = default;

    template <auto& Fields>
    [[nodiscard]] static constexpr auto from() noexcept -> static_header_fields
    {
        static_assert(std::is_const_v<std::remove_reference_t<decltype(Fields)>>,
            "static HTTP/2 header fields must be immutable");
        return static_header_fields{std::span<const header_field>{Fields}};
    }

    [[nodiscard]] constexpr auto fields() const noexcept -> std::span<const header_field>
    {
        return fields_;
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool
    {
        return fields_.empty();
    }

private:
    constexpr explicit static_header_fields(std::span<const header_field> fields) noexcept
        : fields_(fields) {}

    std::span<const header_field> fields_;
};

struct server_response
{
    std::uint32_t status = 200;
    std::vector<header_field> headers;
    std::vector<header_field> trailers;
    std::vector<std::byte> body;
    std::string body_text;
    std::shared_ptr<const std::string> shared_body;
    std::shared_ptr<cnetmod::http::response_body_source> body_source;
    static_header_fields static_headers;
    static_header_fields static_trailers;
    /// Complete HPACK block for a fixed response shape. When present, the
    /// fields above are ignored and no per-response encoding is performed.
    std::shared_ptr<const preencoded_header_block> preencoded_headers;
    bool automatic_content_length = true;
};

using server_handler =
    std::function<cnetmod::task<server_response>(server_request)>;
using streaming_server_handler = std::function<cnetmod::task<server_response>(
    server_request, cnetmod::cancel_token&)>;
/// Server integration variant that borrows request storage for the duration of
/// the awaited call, allowing connection-local header capacity reuse.
using borrowed_streaming_server_handler =
    std::function<cnetmod::task<server_response>(
        server_request&, cnetmod::cancel_token&)>;
using sync_server_handler = std::function<std::optional<server_response>(
    server_request&, cnetmod::cancel_token&)>;
using buffered_request_filter = std::function<bool(std::span<const header_field>)>;
using transport_reader =
    std::function<cnetmod::task<std::expected<std::size_t, std::error_code>>(
        cnetmod::mutable_buffer)>;
using transport_writer =
    std::function<cnetmod::task<std::expected<void, std::error_code>>(
        cnetmod::const_buffer)>;
/// Optional vectored transport. When installed, large shared response bodies
/// are framed by reference and handed to the transport together with the
/// owned frame bytes instead of being copied into the outbound buffer first.
using transport_gather_writer =
    std::function<cnetmod::task<std::expected<void, std::error_code>>(
        std::span<const cnetmod::const_buffer>)>;

/// RFC 9113 connection state machine.  It is transport-neutral at the frame
/// layer and owns all stream and HPACK state on the connection's io_context.
class session
{
public:
    session(cnetmod::io_context& context, cnetmod::socket& socket,
        server_handler handler = {}, transport_reader reader = {},
        transport_writer writer = {});
    session(cnetmod::io_context& context, cnetmod::socket& socket,
        streaming_server_handler handler,
        cnetmod::http::request_body_stream_options body_options = {},
        transport_reader reader = {}, transport_writer writer = {});
    session(cnetmod::io_context& context, cnetmod::socket& socket,
        borrowed_streaming_server_handler handler,
        cnetmod::http::request_body_stream_options body_options = {},
        transport_reader reader = {}, transport_writer writer = {},
        sync_server_handler sync_handler = {},
        bool buffer_complete_requests = false,
        buffered_request_filter buffer_filter = {});

    /// Install a vectored writer used whenever borrowed body segments are
    /// queued. Call before run(); the regular writer remains the fallback.
    void set_gather_writer(transport_gather_writer writer);

    [[nodiscard]] auto run(std::span<const std::byte> initial = {})
        -> cnetmod::task<void>;
    [[nodiscard]] auto receive(std::span<const std::byte> bytes)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto take_outbound() -> std::vector<std::byte>;
    [[nodiscard]] auto peer_settings() const noexcept -> const settings&;
    [[nodiscard]] auto local_settings() noexcept -> settings&;

private:
    struct ready_request
    {
        std::uint32_t stream_id{};
        std::vector<header_field> headers;
        bool detached = false;
    };

    [[nodiscard]] auto process_frame(frame_header header,
        std::span<const std::byte> payload, bool complete_body_follows = false)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto
    validate_headers(std::span<const header_field> fields) const
        -> std::error_code;
    [[nodiscard]] auto dispatch_ready() -> cnetmod::task<void>;
    void start_streaming_request(std::uint32_t stream_id);
    auto start_response_write(std::uint32_t stream_id, server_response response) -> bool;
    [[nodiscard]] auto write_response(std::uint32_t stream_id,
        server_response response, cnetmod::cancel_token& cancellation)
        -> cnetmod::task<std::expected<void, std::error_code>>;
    void queue_response(std::uint32_t stream_id, server_response response,
        bool detached = false);
    auto queue_response_data(std::uint32_t stream_id, server_response& response,
        std::size_t& offset, bool detached = false) -> bool;
    void drain_pending_responses();
    auto queue_response_trailers(std::uint32_t stream_id, const server_response& response)
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto flush_outbound()
        -> cnetmod::task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto flush_batched_outbound()
        -> cnetmod::task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto write_outbound_locked()
        -> cnetmod::task<std::expected<void, std::error_code>>;
    void schedule_flow_credit(std::uint32_t stream_id, std::size_t bytes);
    [[nodiscard]] auto return_flow_credit()
        -> cnetmod::task<std::expected<void, std::error_code>>;
    void close_streaming_bodies() noexcept;
    [[nodiscard]] auto shutdown_streaming()
        -> cnetmod::task<void>;
    void queue_connection_error(std::uint32_t code);
    void queue_frame(frame_header header, std::span<const std::byte> payload);
    /// Queue a DATA frame whose payload stays in `owner`'s storage.
    void queue_borrowed_frame(frame_header header,
        std::span<const std::byte> payload,
        const std::shared_ptr<const std::string>& owner);
    /// Owned plus borrowed bytes waiting for the next transport write.
    [[nodiscard]] auto pending_outbound_bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto acquire_request_headers()
        -> std::vector<header_field>;
    void release_request_headers(std::vector<header_field> headers) noexcept;

    cnetmod::io_context* context_{};
    cnetmod::socket* socket_{};
    transport_reader reader_;
    transport_writer writer_;
    settings local_{};
    settings peer_{};
    header_compression decoder_{};
    header_compression encoder_{};
    std::pmr::unsynchronized_pool_resource stream_pool_;
    std::pmr::unordered_map<std::uint32_t, stream> streams_;

    struct pending_response
    {
        server_response response;
        std::size_t offset{};
    };

    std::unordered_map<std::uint32_t, pending_response> pending_responses_;
    std::vector<ready_request> ready_requests_;
    server_handler handler_;
    streaming_server_handler streaming_handler_;
    borrowed_streaming_server_handler borrowed_streaming_handler_;
    sync_server_handler sync_handler_;
    bool buffer_complete_requests_ = false;
    buffered_request_filter buffer_filter_;
    cnetmod::http::request_body_stream_options body_options_{};
    std::unique_ptr<cnetmod::task_group> background_;
    cnetmod::async_mutex writer_mutex_;
    std::vector<std::byte> input_;
    std::vector<std::byte> outbound_;
    std::vector<std::byte> write_buffer_;
    /// A borrowed payload transmitted after outbound_[0, offset). The owner
    /// keeps the bytes alive until the write that carries them completes.
    struct borrowed_segment
    {
        std::size_t offset{};
        std::span<const std::byte> data;
        std::shared_ptr<const std::string> owner;
    };
    std::vector<borrowed_segment> outbound_borrowed_;
    std::vector<borrowed_segment> write_borrowed_;
    std::size_t outbound_borrowed_bytes_ = 0;
    std::vector<cnetmod::const_buffer> write_parts_;
    transport_gather_writer gather_writer_;
    bool writer_active_ = false;
    std::vector<std::byte> header_block_;
    std::vector<header_field> response_headers_;
    std::vector<header_field> response_trailers_;
    std::vector<std::byte> encoded_headers_;
    std::vector<std::vector<header_field>> request_header_pool_;
    std::vector<std::pair<std::uint32_t, std::size_t>> pending_flow_credit_;
    cnetmod::concurrent_containers::atomic_rw_latch flow_credit_latch_;
    bool flow_credit_scheduled_ = false;
    std::uint32_t continuation_stream_id_ = 0;
    bool continuation_end_stream_ = false;
    std::uint32_t last_peer_stream_id_ = 0;
    std::int32_t receive_window_ = 65'535;
    std::int32_t send_window_ = 65'535;
    bool received_settings_ = false;
    bool goaway_received_ = false;
    bool received_preface_ = false;
};
} // namespace cnetmod::http::v2
