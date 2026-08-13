module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

export module cnetmod.protocol.http.v3.session;

import std;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.coro.channel;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.wait_group;
import cnetmod.coro.spawn;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.frame;
import cnetmod.protocol.http.v3.qpack;
import cnetmod.utils.flat_map;

namespace cnetmod::http::v3 {

using quic::quic_connection;
using quic::stream_id;

export struct http3_request
{
    http_method method{http_method::GET};
    std::string path{"/"};
    std::string scheme{"https"};
    std::string host;
    std::uint16_t port{443};
    /// Extended CONNECT protocol token (for example "webtransport").
    /// Empty means an ordinary HTTP request.
    std::string protocol;
    http_version version{http_version::http_3};
    header_map headers;
    /// Optional RFC 9218 priority requested for this exchange.  A client
    /// emits PRIORITY_UPDATE after opening the request stream; a server
    /// handler can adjust the response later with update_priority().
    std::optional<http_priority> priority;
    /// Transport-assigned request stream. This is populated for incoming
    /// server requests and must be treated as read-only by applications.
    std::optional<stream_id> request_stream;
    std::string body;
    /// Server-side pull stream for request DATA frames. It is populated only
    /// for the explicit streaming server handler, allowing that handler to
    /// consume a large upload without buffering the complete request in
    /// `body`. Client-created requests leave this empty; use `body_source` to
    /// upload.
    std::shared_ptr<request_body_stream> body_stream;
    /// Optional pull-based body producer.  It is consumed once and sends one
    /// or more HTTP/3 DATA frames with cancellation/back-pressure.
    std::shared_ptr<request_body_source> body_source;
    header_map trailers;
};

/// Build an RFC 9220 extended CONNECT request. Sending it still requires the
/// peer's SETTINGS_ENABLE_CONNECT_PROTOCOL and SETTINGS_H3_DATAGRAM gates.
export [[nodiscard]] auto make_webtransport_connect_request(
    std::string host, std::string path = "/") -> http3_request;

/// RFC WebTransport over HTTP/3 uses the client-initiated bidirectional
/// Extended CONNECT stream ID as its session ID.  Child bidirectional streams
/// start with that ID; child unidirectional streams additionally start with
/// their WebTransport stream-type marker (0x54).
export inline constexpr std::uint64_t webtransport_unidirectional_stream_type = 0x54U;
/// Draft wire marker emitted by aioquic for bidirectional WebTransport child
/// streams. RFC 9220 uses only the session ID; decoding accepts both forms so
/// an RFC 9220 endpoint can interoperate with deployed aioquic releases.
export inline constexpr std::uint64_t webtransport_legacy_bidirectional_stream_type = 0x41U;

export [[nodiscard]] auto encode_webtransport_bidirectional_stream_preface(
    stream_id session_id) -> std::expected<std::vector<std::byte>, std::error_code>;
export [[nodiscard]] auto encode_webtransport_unidirectional_stream_preface(
    stream_id session_id) -> std::expected<std::vector<std::byte>, std::error_code>;
/// Decodes a child stream preface. `unidirectional` selects the wire form.
/// The returned second value is the number of bytes consumed from `bytes`.
export [[nodiscard]] auto decode_webtransport_stream_preface(byte_view bytes,
    bool unidirectional) -> std::expected<std::pair<stream_id, std::size_t>, std::error_code>;

export struct http3_response;

/// One RFC 9114 server-push candidate.  The server sends `request` in a
/// PUSH_PROMISE on the parent request stream and delivers `response` on its
/// own server-initiated unidirectional push stream.  A null response is
/// invalid and causes only that parent request to fail; it never results in a
/// partially encoded push stream.
export struct http3_push
{
    http3_request request;
    std::shared_ptr<http3_response> response;
    std::optional<http_priority> priority;
};

export struct http3_response
{
    int status{status::ok};
    http_version version{http_version::http_3};
    header_map headers;
    std::string body;
    /// Explicit opt-in streaming response body.  It is populated only for
    /// `streaming_client_response_handler`; the legacy response API continues
    /// to collect the complete body in `body`.
    std::shared_ptr<request_body_stream> body_stream;
    /// Optional pull-based response body producer. It is explicit so the
    /// legacy `body` response remains allocation-free for small payloads.
    /// `body` and `body_source` must not be used together.
    std::shared_ptr<response_body_source> body_source;
    header_map trailers;
    /// Optional resources promised before this response's final HEADERS.
    /// Server push is deliberately explicit and disabled unless the client
    /// has advertised a MAX_PUSH_ID limit.
    std::vector<http3_push> pushes;
};

/// The peer-provided WebTransport close details from a
/// CLOSE_WEBTRANSPORT_SESSION capsule.
export struct webtransport_close_info
{
    std::uint64_t application_error_code{};
    std::string reason;
};

struct webtransport_session_state;

/// A live WebTransport-over-HTTP/3 session.  It owns no transport; the
/// originating HTTP/3 client/session must outlive it.  Child streams opened
/// through this type are automatically prefixed with the session ID.
export class webtransport_session
{
public:
    [[nodiscard]] auto id() const noexcept -> stream_id;
    [[nodiscard]] auto is_open() const noexcept -> bool;
    [[nodiscard]] auto send_datagram(byte_view payload)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto open_bidirectional_stream()
        -> task<std::expected<stream_id, std::error_code>>;
    [[nodiscard]] auto open_unidirectional_stream()
        -> task<std::expected<stream_id, std::error_code>>;
    /// Send application bytes on a child stream owned by this session.
    [[nodiscard]] auto send_stream(stream_id stream, byte_view payload,
        bool finish = false) -> task<std::expected<void, std::error_code>>;
    /// Read application bytes from a child stream owned by this session.
    /// A zero result denotes the peer FIN.
    [[nodiscard]] auto receive_stream(stream_id stream, mutable_buffer output)
        -> task<std::expected<std::size_t, std::error_code>>;
    /// Wait for a peer-created WebTransport child stream.  The WebTransport
    /// preface has already been consumed when this returns.
    [[nodiscard]] auto accept_stream()
        -> task<std::expected<stream_id, std::error_code>>;
    /// Wait for the next HTTP Datagram belonging to this session.  It returns
    /// only the payload; the HTTP Datagram context ID is the session ID.
    [[nodiscard]] auto receive_datagram()
        -> task<std::expected<std::vector<std::byte>, std::error_code>>;
    /// Wait for a peer CLOSE_WEBTRANSPORT_SESSION capsule. A bare FIN has no
    /// close details and completes with not_connected.
    [[nodiscard]] auto wait_for_close()
        -> task<std::expected<webtransport_close_info, std::error_code>>;
    /// Gracefully ends the CONNECT stream.  A non-empty reason or non-zero
    /// application error sends the RFC WebTransport close capsule before FIN;
    /// it never closes the shared QUIC connection or unrelated HTTP/3 requests.
    [[nodiscard]] auto close(std::uint64_t application_error_code = 0,
        std::string_view reason = {}) -> task<std::expected<void, std::error_code>>;

private:
    friend class http3_client_session;
    friend class http3_server_session;
    webtransport_session(quic_connection& connection, stream_id session_id) noexcept;

    explicit webtransport_session(std::shared_ptr<webtransport_session_state> state) noexcept
        : state_(std::move(state)) {}

    [[nodiscard]] auto state() const noexcept -> const std::shared_ptr<webtransport_session_state>&
    {
        return state_;
    }

    std::shared_ptr<webtransport_session_state> state_;
};

export using server_request_handler =
    std::function<std::error_code(http3_request&, http3_response&)>;
/// Coroutine request handler for dynamic HTTP/3 endpoints. The token is
/// cancelled when the peer abandons the request stream or the connection is
/// closed; handlers must propagate it to downstream I/O.
export using async_server_request_handler = std::function<
    task<std::expected<void, std::error_code>>(http3_request&, http3_response&,
        cnetmod::cancel_token&)>;
/// Explicit opt-in coroutine handler for streaming request bodies.  Unlike
/// async_server_request_handler, this handler is invoked after HEADERS and may
/// consume DATA incrementally from `body`.  Existing async handlers retain the
/// legacy complete-body semantics.
export using streaming_server_request_handler = std::function<
    task<std::expected<void, std::error_code>>(http3_request&, http3_response&,
        request_body_stream&, cnetmod::cancel_token&)>;
/// Coroutine handler for an accepted WebTransport extended-CONNECT request.
/// The server sends the successful CONNECT response before invoking it.  The
/// supplied session is scoped to this CONNECT stream; closing it leaves other
/// HTTP/3 requests on the QUIC connection untouched.
export using async_webtransport_handler = std::function<
    task<std::expected<void, std::error_code>>(http3_request&, webtransport_session&,
        cnetmod::cancel_token&)>;

/// Handlers for a complete HTTP/3 listener.  WebTransport is an extension of
/// HTTP/3, so normal requests and Extended CONNECT share one endpoint.
export struct http3_server_handlers
{
    async_server_request_handler request;
    async_webtransport_handler webtransport;
};

export using client_request_handler =
    std::function<task<std::expected<http3_response, std::error_code>>(const http3_request&)>;
/// Explicit opt-in response streaming callback.  The callback is invoked
/// after response HEADERS and consumes DATA incrementally from `body`.  The
/// legacy `send_request` APIs retain complete-body semantics.
export using streaming_client_response_handler = std::function<
    task<std::expected<void, std::error_code>>(http3_response&, request_body_stream&,
        cnetmod::cancel_token&)>;
/// Runs for one accepted server push. The supplied promise and response are
/// complete, independently validated HTTP/3 messages. Returning an error
/// cancels only that push stream and leaves the parent request usable.
export using server_push_handler = std::function<
    task<std::expected<void, std::error_code>>(http3_request,
        http3_response)>;

/// A PUSH_PROMISE is delivered before the associated push stream body.  A
/// callback error rejects that resource and makes the client send RFC 9114
/// CANCEL_PUSH; it never fails the parent response.
export struct server_push_promise
{
    std::uint64_t push_id{};
    http3_request request;
};

export using server_push_promise_handler = std::function<
    task<std::expected<void, std::error_code>>(server_push_promise)>;

export struct http3_settings
{
    std::uint64_t max_header_list_size{};
    std::uint64_t qpack_max_table_capacity{};
    std::uint64_t qpack_blocked_streams{};
    /// Advertise SETTINGS_H3_DATAGRAM (RFC 9297). QUIC transport parameters
    /// still provide the independent maximum-size authorization.
    bool enable_datagram{};
    bool enable_connect_protocol{};
    bool enable_webtransport{};
    std::uint64_t webtransport_max_sessions{};
};

export class http3_server_session
{
public:
    http3_server_session(quic_connection& conn, server_request_handler handler);
    http3_server_session(quic_connection& conn, async_server_request_handler handler);
    http3_server_session(quic_connection& conn, streaming_server_request_handler handler);
    http3_server_session(quic_connection& conn, async_webtransport_handler handler);
    http3_server_session(quic_connection& conn, http3_server_handlers handlers);
    /// Sets the SETTINGS frame emitted by this server connection before
    /// `run()`.  A non-zero QPACK capacity is explicit opt-in so existing
    /// deployments keep the zero-risk static-table default.
    auto configure_local_settings(http3_settings settings) noexcept -> void;
    auto run() -> task<void>;
    auto close() -> task<void>;
    auto send_goaway(stream_id last_stream) -> task<void>;
    /// Publish an RFC 9218 PRIORITY_UPDATE for a client-initiated request
    /// stream and immediately apply it to locally queued response data.
    [[nodiscard]] auto update_priority(stream_id request_stream,
        http_priority priority) -> task<std::expected<void, std::error_code>>;
    /// Validates bytes from one peer-initiated unidirectional stream.  The
    /// transport supplies a complete stream prefix and calls this again as it
    /// grows; protocol violations are reported to the caller for connection
    /// close handling.
    [[nodiscard]] auto process_peer_unidirectional_stream(stream_id id,
        byte_view bytes) -> task<std::expected<void, std::error_code>>;
    /// Internal peer-stream classifier used by the HTTP/3 transport loop.
    /// Returns true after the preface was routed to a WebTransport session.
    [[nodiscard]] auto route_webtransport_stream(stream_id id, byte_view bytes,
        bool unidirectional) -> std::expected<bool, std::error_code>;
    [[nodiscard]] auto get_active_streams_count() const noexcept -> std::size_t;
    [[nodiscard]] auto datagrams_enabled() const noexcept -> bool;
    /// True once both endpoints negotiated Extended CONNECT and HTTP Datagrams.
    /// This is the protocol gate required before opening a WebTransport session.
    [[nodiscard]] auto webtransport_enabled() const noexcept -> bool;
    [[nodiscard]] auto send_datagram(std::uint64_t context_id, byte_view payload)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto receive_datagram()
        -> task<std::expected<std::pair<std::uint64_t, std::vector<std::byte>>, std::error_code>>;

private:
    auto service_peer_stream(stream_id id) -> task<void>;
    quic_connection& conn_;
    server_request_handler handler_;
    async_server_request_handler async_handler_;
    streaming_server_request_handler streaming_handler_;
    async_webtransport_handler webtransport_handler_;
    qpack_encoder encoder_;
    qpack_decoder decoder_;
    // A server session services request streams concurrently. QPACK state is
    // connection-scoped, so decode/flush and encode/flush/send must each be
    // serialized while application handlers remain fully concurrent.
    cnetmod::async_mutex request_header_mutex_;
    cnetmod::async_mutex response_header_mutex_;
    cnetmod::async_mutex control_stream_mutex_;
    cnetmod::async_mutex push_cancellation_mutex_;
    cnetmod::flat_map<stream_id, http_priority> published_priorities_;
    http3_settings local_settings_{};
    std::optional<stream_id> control_stream_;
    std::optional<stream_id> qpack_encoder_stream_;
    std::optional<stream_id> qpack_decoder_stream_;
    bool control_stream_sent_{};
    bool closing_{};
    std::size_t active_streams_{};
    bool peer_control_stream_seen_{};
    bool peer_settings_seen_{};
    bool peer_datagram_enabled_{};
    bool peer_connect_protocol_enabled_{};
    bool peer_webtransport_enabled_{};
    bool peer_qpack_encoder_stream_seen_{};
    bool peer_qpack_decoder_stream_seen_{};
    std::optional<std::uint64_t> peer_max_push_id_;
    std::uint64_t next_push_id_{};
    cnetmod::flat_map<std::uint64_t, std::shared_ptr<cnetmod::cancel_token>>
        active_push_cancellations_;
    cnetmod::flat_map<std::uint64_t, bool> peer_cancelled_pushes_;
    bool received_goaway_{};
    std::uint64_t goaway_stream_id_{std::numeric_limits<std::uint64_t>::max()};
    cnetmod::flat_map<stream_id, std::uint64_t> peer_unidirectional_stream_types_;
    cnetmod::flat_map<stream_id, std::size_t> peer_unidirectional_stream_bytes_;
    // QPACK header blocks may arrive before their encoder-stream inserts.
    // The decoder completes those blocks asynchronously; retain the decoded
    // fields and wake the owning request coroutine without re-decoding (and
    // therefore without issuing a duplicate Header Acknowledgement).
    channel<std::monostate> qpack_progress_{1024};
    cnetmod::flat_map<stream_id, std::deque<std::vector<header_field>>>
        completed_headers_;
    cnetmod::flat_map<stream_id, std::shared_ptr<webtransport_session_state>>
        webtransport_sessions_;
    channel<std::pair<std::uint64_t, std::vector<std::byte>>> http_datagrams_{256};
    // A peer may open WebTransport child streams in the same packet as the
    // CONNECT request.  Child handlers wait on this one-shot registration
    // signal until the CONNECT coroutine publishes the session state.
    channel<std::monostate> webtransport_registration_{4096};
    std::atomic<std::size_t> pending_webtransport_streams_{};
    bool datagram_dispatcher_started_{};
    async_wait_group peer_streams_;

    auto dispatch_datagrams() -> task<void>;
    auto apply_push_cancellations(const std::vector<std::uint64_t>& ids)
        -> task<void>;
    auto register_push_cancellation(std::uint64_t id,
        const std::shared_ptr<cnetmod::cancel_token>& token) -> task<void>;
    auto release_push_cancellation(std::uint64_t id) -> task<void>;
    auto send_pushes(stream_id parent_stream,
        const std::vector<http3_push>& pushes)
        -> task<std::expected<void, std::error_code>>;
    /// Continues an already-promised streaming push after the parent response
    /// has been released.  This separation is required for RFC 9114
    /// CANCEL_PUSH: a client must have an opportunity to receive the promise
    /// and cancel before an application body producer is awaited.
    auto send_push_body(std::uint64_t push_id, stream_id stream,
        std::shared_ptr<http3_response> response,
        std::optional<std::uint64_t> expected_length,
        std::shared_ptr<cnetmod::cancel_token> token) -> task<void>;
};

export class http3_client_session
{
public:
    http3_client_session(quic_connection& conn, client_request_handler handler);
    auto configure_local_settings(http3_settings settings) noexcept -> void;
    /// Advertise the largest Push ID this client accepts and install the
    /// completion callback. Passing nullopt keeps server push disabled.
    auto configure_server_push(std::optional<std::uint64_t> max_push_id,
        server_push_handler handler = {},
        server_push_promise_handler promise_handler = {}) -> void;
    auto connect() -> task<std::expected<void, std::error_code>>;
    auto close() -> task<void>;
    auto close_all() -> task<void>;
    auto send_request(const http3_request& req) -> task<std::expected<http3_response, std::error_code>>;
    auto send_request(const http3_request& req, cnetmod::cancel_token& token)
        -> task<std::expected<http3_response, std::error_code>>;
    auto send_request_streaming(const http3_request& req,
        streaming_client_response_handler handler)
        -> task<std::expected<http3_response, std::error_code>>;
    /// Publish an RFC 9218 PRIORITY_UPDATE for an already-open request stream.
    /// Most client code supplies http3_request::priority instead, which keeps
    /// the request and its initial scheduling policy together.
    [[nodiscard]] auto update_priority(stream_id request_stream,
        http_priority priority) -> task<std::expected<void, std::error_code>>;
    auto send_request_streaming(const http3_request& req,
        streaming_client_response_handler handler, cnetmod::cancel_token& token)
        -> task<std::expected<http3_response, std::error_code>>;
    /// Open an RFC WebTransport extended-CONNECT session.  `request` must be
    /// built with make_webtransport_connect_request() and has no body/trailers.
    [[nodiscard]] auto connect_webtransport(const http3_request& request)
        -> task<std::expected<webtransport_session, std::error_code>>;
    [[nodiscard]] auto process_peer_unidirectional_stream(stream_id id,
        byte_view bytes) -> task<std::expected<void, std::error_code>>;
    /// Reports that the mandatory peer control SETTINGS frame has been
    /// consumed and its QPACK limits applied to this connection.
    [[nodiscard]] auto peer_settings_received() const noexcept -> bool;
    /// Internal peer-stream classifier used by the HTTP/3 transport loop.
    /// Returns true after the preface was routed to a WebTransport session.
    [[nodiscard]] auto route_webtransport_stream(stream_id id, byte_view bytes,
        bool unidirectional) -> std::expected<bool, std::error_code>;
    [[nodiscard]] auto accepting_requests() const noexcept -> bool;
    [[nodiscard]] auto datagrams_enabled() const noexcept -> bool;
    /// True once both endpoints negotiated Extended CONNECT and HTTP Datagrams.
    /// This is the protocol gate required before opening a WebTransport session.
    [[nodiscard]] auto webtransport_enabled() const noexcept -> bool;
    [[nodiscard]] auto send_datagram(std::uint64_t context_id, byte_view payload)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto receive_datagram()
        -> task<std::expected<std::pair<std::uint64_t, std::vector<std::byte>>, std::error_code>>;
    /// Called by the connection's peer-stream consumer after the FIN of a
    /// server-initiated unidirectional stream. It consumes an RFC 9114 push
    /// stream and dispatches the registered push handler.
    [[nodiscard]] auto consume_server_push_stream(stream_id id, byte_view bytes)
        -> task<std::expected<void, std::error_code>>;
    /// Sends RFC 9114 CANCEL_PUSH for an announced Push ID. It is idempotent;
    /// local delivery of the cancelled resource is suppressed as well.
    [[nodiscard]] auto cancel_server_push(std::uint64_t push_id)
        -> task<std::expected<void, std::error_code>>;

private:
    quic_connection& conn_;
    client_request_handler handler_;
    qpack_encoder encoder_;
    qpack_decoder decoder_;
    // QPACK encoder state and its instruction stream are shared by all
    // request streams on a connection. Keep encode + flush + HEADERS send
    // atomic so concurrent batch requests cannot interleave them.
    cnetmod::async_mutex request_header_mutex_;
    // Serialize request submission while shared QUIC/QPACK writer state is
    // being updated. The gate is released after request FIN, so independent
    // response streams can be consumed concurrently by the batch API.
    cnetmod::async_mutex request_mutex_;
    // QPACK state is connection-scoped.  This coroutine gate serializes
    // short state transitions without ever blocking an I/O worker thread.
    cnetmod::async_mutex qpack_mutex_;
    cnetmod::async_mutex control_stream_mutex_;
    cnetmod::flat_map<stream_id, http_priority> published_priorities_;
    http3_settings settings_;
    std::optional<stream_id> control_stream_;
    std::optional<stream_id> qpack_encoder_stream_;
    std::optional<stream_id> qpack_decoder_stream_;
    bool control_stream_sent_{};
    bool received_goaway_{};
    std::uint64_t goaway_stream_id_{std::numeric_limits<std::uint64_t>::max()};
    bool peer_control_stream_seen_{};
    bool peer_settings_seen_{};
    bool peer_datagram_enabled_{};
    bool peer_connect_protocol_enabled_{};
    bool peer_webtransport_enabled_{};
    bool peer_qpack_encoder_stream_seen_{};
    bool peer_qpack_decoder_stream_seen_{};
    std::optional<std::uint64_t> peer_max_push_id_;
    std::optional<std::uint64_t> local_max_push_id_;
    server_push_handler push_handler_;
    server_push_promise_handler push_promise_handler_;
    cnetmod::flat_map<std::uint64_t, http3_request> promised_pushes_;
    cnetmod::flat_map<std::uint64_t, bool> notified_pushes_;
    cnetmod::flat_map<std::uint64_t, bool> cancelled_pushes_;
    channel<std::monostate> push_promise_progress_{1024};
    cnetmod::flat_map<stream_id, std::uint64_t> peer_unidirectional_stream_types_;
    cnetmod::flat_map<stream_id, std::size_t> peer_unidirectional_stream_bytes_;
    channel<std::monostate> qpack_progress_{1024};
    cnetmod::flat_map<stream_id,
        std::deque<std::vector<header_field>>>
        completed_headers_;
    cnetmod::flat_map<stream_id, std::shared_ptr<webtransport_session_state>>
        webtransport_sessions_;
    channel<std::pair<std::uint64_t, std::vector<std::byte>>> http_datagrams_{256};
    bool datagram_dispatcher_started_{};

    auto dispatch_datagrams() -> task<void>;
    auto dispatch_server_push_promises(stream_id parent_stream) -> task<void>;
};

export auto make_http3_server_session(quic_connection& conn, server_request_handler handler)
    -> std::unique_ptr<http3_server_session>;
export auto make_http3_server_session(quic_connection& conn,
    async_server_request_handler handler) -> std::unique_ptr<http3_server_session>;
export auto make_http3_server_session(quic_connection& conn,
    streaming_server_request_handler handler) -> std::unique_ptr<http3_server_session>;
export auto make_http3_server_session(quic_connection& conn,
    async_webtransport_handler handler) -> std::unique_ptr<http3_server_session>;
export auto make_http3_server_session(quic_connection& conn, http3_server_handlers handlers)
    -> std::unique_ptr<http3_server_session>;
export auto make_http3_client_session(quic_connection& conn, client_request_handler handler)
    -> std::unique_ptr<http3_client_session>;
} // namespace cnetmod::http::v3
    #endif
#endif
