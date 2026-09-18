module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:router;

import std;
import cnetmod.protocol.http.semantics;
import :parser;
import :request;
import :response;
import :multipart;
import :sse;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.utils.flat_map;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;

namespace cnetmod::http {
/**
 * @brief Describes the lifecycle of a Server-Sent Events response stream.
 */
export enum class sse_stream_state
{
    not_started,
    committing,
    open,
    failed,
    closed
};

/**
 * @brief Bounds the lifetime and socket write latency of one SSE response.
 */
export struct sse_stream_options
{
    std::chrono::milliseconds max_duration{120000};
    std::chrono::milliseconds write_timeout{5000};
};

export class request_context;
export class sse_stream;
export using sse_handler_fn =
    std::function<task<void>(request_context&, sse_stream&)>;

export struct route_params
{
    cnetmod::flat_map<std::string, std::string, std::less<>> named;
    std::string wildcard;
    [[nodiscard]] auto get(std::string_view key) const noexcept
        -> std::string_view;
};

export class request_context
{
public:
    request_context(io_context& ctx, socket& sock, const request_parser& parser,
        response& resp, route_params params);
    request_context(io_context& ctx, socket& sock, std::string_view method,
        std::string_view uri, const header_map& headers,
        std::string_view body, response& resp, route_params params,
        std::shared_ptr<request_body_stream> body_stream = {});
    [[nodiscard]] auto method() const noexcept -> std::string_view;
    [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method>;
    [[nodiscard]] auto path() const noexcept -> std::string_view;
    [[nodiscard]] auto query_string() const noexcept -> std::string_view;
    [[nodiscard]] auto uri() const noexcept -> std::string_view;
    [[nodiscard]] auto headers() const noexcept -> const header_map&;
    [[nodiscard]] auto body() const -> std::string_view;
    [[nodiscard]] auto has_body_stream() const noexcept -> bool;
    [[nodiscard]] auto receive_body_chunk()
        -> task<std::optional<request_body_chunk>>;
    [[nodiscard]] auto read_full_body() -> task<std::string_view>;
    [[nodiscard]] auto body_stream_error() const noexcept -> std::error_code;
    [[nodiscard]] auto received_body_bytes() const noexcept -> std::size_t;
    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view;
    [[nodiscard]] auto param(std::string_view name) const noexcept
        -> std::string_view;
    [[nodiscard]] auto wildcard() const noexcept -> std::string_view;
    [[nodiscard]] auto params() const noexcept -> const route_params&;
    void text(int status_code, std::string_view text_body);
    void json(int status_code, std::string_view json_body);
    void html(int status_code, std::string_view html_body);
    void redirect(std::string_view location, int code = 302);
    void not_found();
    /**
     * Returns true after SSE header delivery has been attempted.
     *
     * A true result is conservative: callers must not fall back to a regular
     * HTTP response because part of the streaming header may already be on the
     * wire, including when the stream state is failed.
     */
    [[nodiscard]] auto sse_started() const noexcept -> bool;
    /**
     * Returns the current Server-Sent Events stream state.
     */
    [[nodiscard]] auto sse_state() const noexcept -> sse_stream_state;
    auto sse_begin(int status_code = status::ok) -> task<bool>;
    auto sse_send(std::string_view data, std::string_view event = {})
        -> task<bool>;
    auto sse_json(std::string_view json_payload, std::string_view event = {})
        -> task<bool>;
    auto sse_comment(std::string_view comment) -> task<bool>;
    auto sse_heartbeat() -> task<bool>;
    auto sse_done() -> task<bool>;

    /**
     * @brief Runs a dynamically selected SSE response with bounded lifetime.
     *
     * Call this only after authentication, parameter validation, and resource
     * lookup have selected streaming for the current request. Before this call,
     * the handler may still produce an ordinary HTTP error response. During the
     * call, the stream handler and total-duration watchdog are joined through
     * structured concurrency; no background watchdog outlives the request.
     */
    auto with_sse(sse_handler_fn handler,
        sse_stream_options options = {}) -> task<void>;
    [[nodiscard]] auto parse_form()
        -> std::expected<const form_data*, std::error_code>;
    [[nodiscard]] auto resp() noexcept -> response&;
    [[nodiscard]] auto io_ctx() noexcept -> io_context&;
    [[nodiscard]] auto raw_socket() noexcept -> socket&;
    /// Request-scoped budget. Nested services should constrain and forward it
    /// instead of starting independent relative timeouts.
    [[nodiscard]] auto request_deadline() const noexcept -> const cnetmod::deadline&;
    void set_deadline(cnetmod::deadline value) noexcept;
    [[nodiscard]] auto cancellation_token() noexcept -> cnetmod::cancel_token&;

    /**
     * @brief Cancels direct and registered downstream operations of this request.
     *
     * Terminal for the request. The request must outlive all child operations.
     */
    void cancel_pending_operations() noexcept;

    /**
     * @brief Runs an owned factory with an independent request-linked token.
     *
     * Cancelled completion returns to the event loop before unlinking its
     * registration, including exceptional completion resumed by cancellation.
     */
    template <class Factory>
    auto with_deadline(Factory operation)
        -> decltype(cnetmod::with_deadline(std::declval<io_context&>(),
            std::declval<cnetmod::deadline>(), std::declval<Factory&>()))
    {
        cnetmod::cancel_token token;
        operation_registration registration{*this, token};
        using operation_type = std::invoke_result_t<Factory&, cnetmod::cancel_token&>;
        using result_type = decltype(std::declval<operation_type&>().handle().promise().result());
        std::optional<result_type> result;
        std::exception_ptr failure;
        try
        {
            result.emplace(co_await cnetmod::with_deadline(ctx_, deadline_,
                std::invoke(operation, token), token));
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (token.is_cancelled())
            co_await cnetmod::post_awaitable{ctx_};
        if (failure)
            std::rethrow_exception(failure);
        co_return std::move(*result);
    }

    [[nodiscard]] auto trace_id() const noexcept -> std::string_view;
    void set_trace_id(std::string value);
    /// W3C Trace Context fields. They are populated by the optional tracing
    /// middleware and remain empty for applications that do not opt in.
    [[nodiscard]] auto trace_span_id() const noexcept -> std::string_view;
    [[nodiscard]] auto trace_flags() const noexcept -> std::uint8_t;
    [[nodiscard]] auto trace_state() const noexcept -> std::string_view;
    void set_trace_context(std::string trace_id, std::string span_id,
        std::uint8_t flags, std::string state = {});
    [[nodiscard]] auto client_address() const -> std::string;

private:
    friend class sse_stream;
    friend class router;
    void configure_sse(sse_stream_options options) noexcept;
    void expire_sse() noexcept;
    auto write_sse_bytes(std::string_view bytes) -> task<bool>;
    auto write_sse_frame(std::string frame) -> task<bool>;
    void drain_available_body_chunks() const;
    void init_path_query(std::string_view uri);
    io_context& ctx_;
    socket& sock_;
    response& resp_;
    route_params params_;
    const header_map* headers_ptr_{};
    std::string_view method_;
    mutable std::string body_storage_;
    mutable std::string_view body_;
    std::shared_ptr<request_body_stream> body_stream_;
    std::string_view uri_;
    std::string_view path_;
    std::string_view query_;
    std::optional<form_data> form_cache_;
    mutable bool body_stream_drained_ = false;
    sse_stream_state sse_state_ = sse_stream_state::not_started;
    deadline sse_deadline_{};
    std::chrono::milliseconds sse_write_timeout_{5000};
    cnetmod::deadline deadline_{};
    cnetmod::cancel_token cancellation_;

    struct operation_registration
    {
        request_context* owner{};
        cnetmod::cancel_token& token;
        operation_registration* previous{};
        operation_registration* next{};
        operation_registration(request_context& request, cnetmod::cancel_token& cancellation) noexcept;
        ~operation_registration();
        operation_registration(const operation_registration&) = delete;
        auto operator=(const operation_registration&) -> operation_registration& = delete;
    };

    concurrent_containers::atomic_rw_latch operations_latch_;
    operation_registration* operations_{};
    std::atomic<bool> operations_cancelled_{false};
    std::string trace_id_;
    std::string trace_span_id_;
    std::string trace_state_;
    std::uint8_t trace_flags_{};
};

/**
 * @brief Writes one Server-Sent Events response through a request context.
 *
 * The stream does not own the request context. It centralizes the conservative
 * commit state, lazy stream start, named event delivery, heartbeat frames, and
 * terminal frame. Payload serialization remains an application concern. Use
 * request_context::with_sse() when SSE is selected dynamically so the stream
 * also receives a structured total-duration watchdog.
 */
export class sse_stream
{
public:
    /**
     * @brief Binds the stream to a request context for the handler lifetime.
     */
    explicit sse_stream(request_context& context,
        sse_stream_options options = {}) noexcept;

    sse_stream(const sse_stream&) = delete;
    auto operator=(const sse_stream&) -> sse_stream& = delete;

    /**
     * @brief Reports whether SSE header delivery has been attempted.
     *
     * Once true, callers must not fall back to a regular HTTP response, even
     * when the header or first frame failed to reach the peer.
     */
    [[nodiscard]] auto started() const noexcept -> bool;

    /**
     * @brief Returns the current response stream state.
     */
    [[nodiscard]] auto state() const noexcept -> sse_stream_state;

    /**
     * @brief Commits the SSE response headers if the stream has not started.
     */
    auto begin(int status_code = status::ok) -> task<bool>;

    /**
     * @brief Sends one data frame, optionally with a named event.
     */
    auto send(std::string_view payload, std::string_view event = {})
        -> task<bool>;

    /**
     * @brief Sends one SSE comment frame.
     */
    auto comment(std::string_view value) -> task<bool>;

    /**
     * @brief Sends the standard heartbeat comment frame.
     */
    auto heartbeat() -> task<bool>;

    /**
     * @brief Sends the terminal frame and closes this logical stream.
     */
    auto finish() -> task<bool>;

    /**
     * @brief Creates a callback suitable for incremental producer output.
     *
     * The callback is request-scoped and must not outlive this stream or its
     * route handler. Use the Application task supervisor for longer-lived
     * producers and stop them before the request completes.
     */
    [[nodiscard]] auto callback(std::string event = {})
        -> std::function<task<bool>(std::string_view)>;

private:
    request_context* context_;
};

export using handler_fn = std::function<task<void>(request_context&)>;
export using next_fn = std::function<task<void>()>;
export using middleware_fn =
    std::function<task<void>(request_context&, next_fn)>;

namespace detail {
    enum class segment_kind
    {
        exact,
        param,
        wildcard
    };

    struct segment
    {
        segment_kind kind;
        std::string value;
    };

    struct route_score
    {
        int wildcard_count = 0;
        int param_count = 0;
        int literal_count = 0;
        int segment_count = 0;
        int method_cost = 0;
        std::uint64_t order = 0;
    };
} // namespace detail

export struct match_result
{
    handler_fn handler;
    route_params params;
    std::optional<request_body_stream_options> request_stream;
};

export class router
{
public:
    router() = default;
    auto get(std::string_view pattern, handler_fn fn) -> router&;
    auto post(std::string_view pattern, handler_fn fn) -> router&;
    auto put(std::string_view pattern, handler_fn fn) -> router&;
    auto del(std::string_view pattern, handler_fn fn) -> router&;
    auto patch(std::string_view pattern, handler_fn fn) -> router&;
    auto any(std::string_view pattern, handler_fn fn) -> router&;

    /**
     * @brief Registers a POST route whose body is delivered incrementally.
     */
    auto stream_post(std::string_view pattern, handler_fn fn,
        request_body_stream_options options = {}) -> router&;

    /**
     * @brief Registers a PUT route whose body is delivered incrementally.
     */
    auto stream_put(std::string_view pattern, handler_fn fn,
        request_body_stream_options options = {}) -> router&;

    /**
     * @brief Registers a PATCH route whose body is delivered incrementally.
     */
    auto stream_patch(std::string_view pattern, handler_fn fn,
        request_body_stream_options options = {}) -> router&;

    /**
     * @brief Registers a GET endpoint with a request-bound SSE stream.
     */
    auto sse_get(std::string_view pattern, sse_handler_fn fn,
        std::optional<sse_stream_options> options = std::nullopt) -> router&;

    /**
     * @brief Registers a POST endpoint with a request-bound SSE stream.
     */
    auto sse_post(std::string_view pattern, sse_handler_fn fn,
        std::optional<sse_stream_options> options = std::nullopt) -> router&;

    /**
     * @brief Sets the options inherited by subsequently registered SSE routes.
     */
    auto sse_defaults(sse_stream_options options) -> router&;
    [[nodiscard]] auto match(http_method method, std::string_view path) const
        -> std::optional<match_result>;
    [[nodiscard]] auto match(std::string_view method, std::string_view path) const
        -> std::optional<match_result>;

private:
    struct route_entry
    {
        std::optional<http_method> method;
        std::vector<detail::segment> segments;
        std::string canonical_path;
        handler_fn handler;
        std::optional<request_body_stream_options> request_stream;
        detail::route_score score;
        std::uint64_t order = 0;
    };

    auto add(http_method method, std::string_view pattern, handler_fn fn)
        -> router&;
    auto add_sse(http_method method, std::string_view pattern,
        sse_handler_fn fn, std::optional<sse_stream_options> options) -> router&;
    auto add_route(std::optional<http_method> method, std::string_view pattern,
        handler_fn fn,
        std::optional<request_body_stream_options> request_stream = {}) -> router&;
    [[nodiscard]] auto find_exact(http_method method,
        std::string_view canonical_path) const
        -> const route_entry*;
    static auto try_match(const std::vector<detail::segment>& segs,
        const std::vector<std::string_view>& parts,
        route_params& out) -> bool;
    std::vector<route_entry> entries_;
    sse_stream_options sse_defaults_{};
    std::vector<std::size_t> static_exact_indices_;
    std::unordered_map<std::string, std::vector<std::size_t>> exact_index_;
    std::unordered_map<std::string, std::vector<std::size_t>>
        first_literal_index_;
    std::vector<std::size_t> generic_indices_;
    std::uint64_t next_order_ = 0;
};
} // namespace cnetmod::http
