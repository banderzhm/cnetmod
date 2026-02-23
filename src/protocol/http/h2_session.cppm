module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_NGHTTP2
#define NGHTTP2_NO_SSIZE_T
#include <nghttp2/nghttp2.h>
#endif

export module cnetmod.protocol.http:h2_session;

import std;
import :types;
import :h2_types;
import :h2_stream;
import :stream_io;
import :response;
import :router;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.channel;

namespace cnetmod::http {

#ifdef CNETMOD_HAS_NGHTTP2

// =============================================================================
// http2_session — Production-Grade Coroutine-Driven HTTP/2 Server Session
// =============================================================================
// Manages a single HTTP/2 connection with true multiplexed stream concurrency.
// Uses nghttp2 mem API (no I/O callbacks — all socket I/O is coroutine-driven).
//
// Architecture:
//   1. recv_loop: co_await async_read() → nghttp2_session_mem_recv2()
//      - nghttp2 callbacks fire synchronously: accumulate headers/body in h2_stream
//      - When request complete: spawn() handler coroutine concurrently
//   2. Handler coroutines run concurrently via spawn(), execute router + middleware,
//      then send completed response through channel<submit_item> back to session loop
//   3. response_consumer_loop: reads from channel, calls submit_response + flush
//      - This is the ONLY coroutine that touches nghttp2 submit/send after init
//   4. GOAWAY graceful shutdown: submit GOAWAY frame, drain in-flight streams
//
// Thread-safety invariant:
//   All nghttp2_session_* calls happen on the same io_context (single-threaded).
//   The channel serializes handler→session communication without locks.

export class http2_session {
public:
    /// @param io  The stream_io wrapping the accepted connection
    /// @param router  The router for matching handlers
    /// @param middlewares  Middleware chain
    /// @param settings  HTTP/2 settings
    /// @param date_fn  Optional function returning cached Date header string
    http2_session(stream_io& io, const router& rtr,
                  const std::vector<middleware_fn>& mws,
                  const h2_settings& settings = {},
                  std::function<std::string()> date_fn = {})
        : io_(io), router_(rtr), middlewares_(mws), settings_(settings)
        , date_fn_(std::move(date_fn))
        , submit_ch_(settings.max_concurrent_streams)
    {}

    ~http2_session() {
        if (session_) {
            nghttp2_session_del(session_);
            session_ = nullptr;
        }
    }

    // Non-copyable, non-movable (session_ has callbacks pointing to this)
    http2_session(const http2_session&) = delete;
    auto operator=(const http2_session&) -> http2_session& = delete;
    http2_session(http2_session&&) = delete;
    auto operator=(http2_session&&) -> http2_session& = delete;

    // =========================================================================
    // Public API
    // =========================================================================

    /// Run the HTTP/2 session (blocking coroutine, returns when connection closes)
    /// @param initial_data  Pre-read data (e.g. cleartext h2c client preface)
    /// @param initial_len   Length of initial_data
    auto run(const std::byte* initial_data = nullptr,
             std::size_t initial_len = 0) -> task<void> {
        // Initialize nghttp2 session
        if (!init_session()) {
            co_return;
        }

        // Send server connection preface (SETTINGS frame)
        if (!submit_settings()) {
            co_return;
        }

        // Initial flush to send SETTINGS
        auto fr = co_await flush();
        if (!fr) co_return;

        // Feed pre-read data (cleartext h2c: client preface + possibly SETTINGS)
        if (initial_data && initial_len > 0) {
            auto consumed = nghttp2_session_mem_recv2(
                session_,
                reinterpret_cast<const uint8_t*>(initial_data),
                initial_len);
            if (consumed < 0) {
                co_await send_goaway(h2_errc::protocol_error);
                co_return;
            }

            // Spawn handlers for any requests completed in initial data
            spawn_pending_handlers();

            auto f = co_await flush();
            if (!f) co_return;
        }

        // Start response consumer loop (reads from channel, submits to nghttp2)
        spawn(io_.io_ctx(), response_consumer_loop());

        // Main recv loop
        std::array<std::byte, 16384> buf{};
        while (nghttp2_session_want_read(session_) ||
               nghttp2_session_want_write(session_)) {

            if (nghttp2_session_want_read(session_)) {
                auto rd = co_await io_.async_read(
                    mutable_buffer{buf.data(), buf.size()});
                if (!rd || *rd == 0) break;

                auto consumed = nghttp2_session_mem_recv2(
                    session_,
                    reinterpret_cast<const uint8_t*>(buf.data()),
                    *rd);
                if (consumed < 0) {
                    // Protocol error — send GOAWAY and drain
                    co_await send_goaway(h2_errc::protocol_error);
                    break;
                }
            }

            // Spawn handler coroutines for newly completed requests
            spawn_pending_handlers();

            // Flush any pending outbound frames (SETTINGS ACK, PING, WINDOW_UPDATE)
            auto f = co_await flush();
            if (!f) break;

            // If shutting down and no more active streams, we're done
            if (shutting_down_ && active_stream_count_ == 0) break;
        }

        // Close channel to signal consumer loop to exit
        submit_ch_.close();
        io_.close();
    }

    /// Initiate graceful shutdown — sends GOAWAY, drains in-flight streams
    auto shutdown() -> task<void> {
        if (shutting_down_) co_return;
        co_await send_goaway(h2_errc::no_error);
    }

private:
    // =========================================================================
    // submit_item — Response data sent from handler coroutines to session loop
    // =========================================================================

    struct submit_item {
        std::int32_t stream_id;
        int          status_code;
        header_map   headers;
        std::string  body;
    };

    // =========================================================================
    // Header Normalization (RFC 9113 §8.2)
    // =========================================================================

    /// Convert header map to HTTP/2 compliant lowercase, filtering hop-by-hop headers
    static auto normalize_headers(const header_map& src) -> header_map {
        // Hop-by-hop headers forbidden in HTTP/2 (RFC 9113 §8.2.2)
        static constexpr std::string_view hop_by_hop[] = {
            "connection", "keep-alive", "proxy-connection",
            "transfer-encoding", "upgrade",
        };

        header_map out;
        for (auto& [k, v] : src) {
            std::string lower_k(k);
            std::ranges::transform(lower_k, lower_k.begin(),
                [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });

            // Filter hop-by-hop headers
            bool skip = false;
            for (auto hbh : hop_by_hop) {
                if (lower_k == hbh) { skip = true; break; }
            }
            if (skip) continue;

            out[std::move(lower_k)] = v;
        }
        return out;
    }

    // =========================================================================
    // nghttp2 Session Initialization
    // =========================================================================

    auto init_session() -> bool {
        nghttp2_session_callbacks* callbacks = nullptr;
        nghttp2_session_callbacks_new(&callbacks);

        nghttp2_session_callbacks_set_on_begin_headers_callback(
            callbacks, on_begin_headers);
        nghttp2_session_callbacks_set_on_header_callback(
            callbacks, on_header);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
            callbacks, on_data_chunk_recv);
        nghttp2_session_callbacks_set_on_frame_recv_callback(
            callbacks, on_frame_recv);
        nghttp2_session_callbacks_set_on_stream_close_callback(
            callbacks, on_stream_close);

        // --- nghttp2 options ---
        nghttp2_option* opt = nullptr;
        nghttp2_option_new(&opt);

        // Skip client magic validation (already checked in server.cppm for h2c)
        nghttp2_option_set_no_recv_client_magic(opt, 1);

        // Manual WINDOW_UPDATE: batch consume calls to reduce frame overhead
        nghttp2_option_set_no_auto_window_update(opt, 1);

        int rv = nghttp2_session_server_new2(&session_, callbacks, this, opt);
        nghttp2_option_del(opt);
        nghttp2_session_callbacks_del(callbacks);

        return rv == 0;
    }

    auto submit_settings() -> bool {
        nghttp2_settings_entry iv[] = {
            {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
             settings_.max_concurrent_streams},
            {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE,
             settings_.initial_window_size},
            {NGHTTP2_SETTINGS_MAX_FRAME_SIZE,
             settings_.max_frame_size},
            {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
             settings_.max_header_list_size},
            {NGHTTP2_SETTINGS_ENABLE_PUSH,
             settings_.enable_push ? 1u : 0u},
        };

        int rv = nghttp2_submit_settings(
            session_, NGHTTP2_FLAG_NONE,
            iv, sizeof(iv) / sizeof(iv[0]));
        if (rv != 0) return false;

        // Enlarge connection-level receive window (stream_id=0)
        // This is separate from per-stream initial_window_size in SETTINGS.
        if (settings_.connection_window_size > NGHTTP2_INITIAL_WINDOW_SIZE) {
            nghttp2_session_set_local_window_size(
                session_, NGHTTP2_FLAG_NONE, 0,
                static_cast<int32_t>(settings_.connection_window_size));
        }

        return true;
    }

    // =========================================================================
    // Flush — Send all pending nghttp2 frames to the network
    // =========================================================================

    auto flush() -> task<std::expected<void, std::error_code>> {
        // Write coalescing: accumulate all pending frames into a single buffer,
        // then issue one write syscall instead of one per mem_send2 chunk.
        flush_buf_.clear();
        for (;;) {
            const uint8_t* data = nullptr;
            auto len = nghttp2_session_mem_send2(session_, &data);
            if (len < 0) {
                co_return std::unexpected(
                    make_error_code(h2_errc::internal_error));
            }
            if (len == 0) break;

            auto sz = static_cast<std::size_t>(len);
            flush_buf_.append(reinterpret_cast<const char*>(data), sz);
        }
        if (!flush_buf_.empty()) {
            auto wr = co_await io_.async_write_all(
                flush_buf_.data(), flush_buf_.size());
            if (!wr) co_return std::unexpected(wr.error());
        }
        co_return {};
    }

    // =========================================================================
    // GOAWAY — Graceful Shutdown
    // =========================================================================

    auto send_goaway(h2_errc error = h2_errc::no_error)
        -> task<void>
    {
        if (!session_) co_return;
        shutting_down_ = true;

        auto last_sid = nghttp2_session_get_last_proc_stream_id(session_);
        nghttp2_submit_goaway(session_, NGHTTP2_FLAG_NONE,
            last_sid, static_cast<uint32_t>(error), nullptr, 0);

        (void)co_await flush();
    }

    // =========================================================================
    // Spawn Pending Handlers — Fire-and-forget concurrent handler coroutines
    // =========================================================================

    void spawn_pending_handlers() {
        if (pending_handlers_.empty()) return;

        auto pending = std::move(pending_handlers_);
        pending_handlers_.clear();

        for (auto sid : pending) {
            spawn(io_.io_ctx(), handler_coro(sid));
        }
    }

    // =========================================================================
    // Handler Coroutine — Runs concurrently, sends result via channel
    // =========================================================================
    // Spawned as fire-and-forget for each complete request.
    // Copies all needed data from h2_stream at spawn time (stream may be
    // closed/erased by nghttp2 before handler completes).
    // Does NOT touch nghttp2 — sends result through submit channel.

    auto handler_coro(std::int32_t stream_id) -> task<void> {
        // --- Snapshot request data from h2_stream (must happen synchronously) ---
        auto it = streams_.find(stream_id);
        if (it == streams_.end()) co_return;

        auto& stream = it->second;
        auto method_sv = stream.method();
        auto method_e  = stream.method_enum();
        auto path_sv   = stream.path();
        auto full_path = std::string(stream.full_path());
        auto headers   = stream.headers();    // copy
        auto body      = std::string(stream.body());

        // --- Invalid method → 400 (no need to run router) ---
        if (!method_e) {
            submit_item item;
            item.stream_id   = stream_id;
            item.status_code = status::bad_request;
            item.headers["content-type"] = "text/plain; charset=utf-8";
            item.body = "400 Bad Request";
            co_await submit_ch_.send(std::move(item));
            co_return;
        }

        // --- Route matching ---
        auto mr = router_.match(*method_e, path_sv);

        response resp(status::ok);
        resp.set_header("server", "cnetmod");
        if (date_fn_) {
            resp.set_header("date", date_fn_());
        }

        route_params rp;
        handler_fn handler;

        if (mr) {
            handler = std::move(mr->handler);
            rp = std::move(mr->params);
        } else {
            handler = [](request_context& ctx) -> task<void> {
                ctx.not_found();
                co_return;
            };
        }

        // --- Execute middleware + handler ---
        // request_context references local copies (all on coroutine frame, stable)
        request_context rctx(io_.io_ctx(), io_.raw_socket(),
                             method_sv, full_path,
                             headers, body,
                             resp, std::move(rp));

        co_await execute_chain(rctx, handler, 0);

        // --- Build submit_item with normalized headers ---
        submit_item item;
        item.stream_id   = stream_id;
        item.status_code = resp.status_code();
        item.headers     = normalize_headers(resp.headers());
        item.body        = std::string(resp.body());

        co_await submit_ch_.send(std::move(item));
    }

    // =========================================================================
    // Response Consumer Loop — Reads from channel, submits to nghttp2
    // =========================================================================
    // Runs as a separate spawned coroutine on the same io_context.
    // This is the ONLY code path that calls nghttp2 submit/send after init.

    auto response_consumer_loop() -> task<void> {
        while (true) {
            auto item_opt = co_await submit_ch_.receive();
            if (!item_opt) break;  // Channel closed

            // Process first item
            process_submit_item(*item_opt);

            // Response batching: drain all immediately available items
            // from the channel before flushing, reducing syscall count.
            while (auto more = submit_ch_.try_receive()) {
                process_submit_item(*more);
            }

            // Single flush for all batched responses
            auto f = co_await flush();
            if (!f) break;
        }
    }

    /// Process a single submit_item: populate h2_stream response and submit to nghttp2
    void process_submit_item(submit_item& item) {
        auto it = streams_.find(item.stream_id);
        if (it == streams_.end()) return;  // Stream already closed by peer

        auto& stream = it->second;
        stream.resp().status_code = item.status_code;
        stream.resp().headers     = std::move(item.headers);
        stream.resp().body        = std::move(item.body);
        stream.resp().body_offset = 0;

        submit_response(stream);
        stream.mark_response_ready();
    }

    // =========================================================================
    // Submit HTTP/2 Response for a Stream
    // =========================================================================

    void submit_response(h2_stream& stream) {
        auto& resp = stream.resp();
        if (resp.sent) return;
        resp.sent = true;

        // Thread-local reusable nva vector: avoids per-response heap allocation.
        // Safe in multi-core mode because each worker thread has its own TLS.
        // Only response_consumer_loop calls this (single coroutine per session),
        // so no concurrent access within the same thread_local instance.
        thread_local std::vector<nghttp2_nv> nva;
        nva.clear();
        nva.reserve(resp.headers.size() + 2);

        // :status pseudo-header
        std::string status_str = std::to_string(resp.status_code);
        nva.push_back({
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":status")),
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(status_str.data())),
            7, status_str.size(),
            NGHTTP2_NV_FLAG_NO_COPY_NAME
        });

        // Regular headers (already lowercase from normalize_headers)
        for (auto& [k, v] : resp.headers) {
            nva.push_back({
                const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(k.data())),
                const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(v.data())),
                k.size(), v.size(),
                NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE
            });
        }

        if (!resp.body.empty()) {
            // Add content-length if not already present
            if (resp.headers.find("content-length") == resp.headers.end()) {
                resp.headers["content-length"] = std::to_string(resp.body.size());
                auto& cl = resp.headers["content-length"];
                nva.push_back({
                    const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>("content-length")),
                    const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(cl.data())),
                    14, cl.size(),
                    NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE
                });
            }

            // Set up data provider for response body
            nghttp2_data_provider2 prd;
            prd.source.ptr = &stream;
            prd.read_callback = data_source_read;

            nghttp2_submit_response2(
                session_, stream.stream_id(),
                nva.data(), nva.size(), &prd);
        } else {
            nghttp2_submit_response2(
                session_, stream.stream_id(),
                nva.data(), nva.size(), nullptr);
        }
    }

    // =========================================================================
    // Streaming Data Source — Chunked DATA frame delivery with flow control
    // =========================================================================
    // nghttp2 calls this callback repeatedly (up to max_frame_size per call)
    // until EOF is signaled. This allows proper HTTP/2 flow control windows
    // to take effect for large response bodies.

    static auto data_source_read(
        nghttp2_session* /*session*/,
        int32_t /*stream_id*/,
        uint8_t* buf,
        size_t length,
        uint32_t* data_flags,
        nghttp2_data_source* source,
        void* /*user_data*/) -> nghttp2_ssize
    {
        auto* stream = static_cast<h2_stream*>(source->ptr);
        auto& resp = stream->resp();
        auto& body = resp.body;
        auto& offset = resp.body_offset;

        auto remaining = body.size() - offset;
        auto to_copy = std::min(length, remaining);
        std::memcpy(buf, body.data() + offset, to_copy);
        offset += to_copy;

        // Signal EOF only when entire body has been consumed
        if (offset >= body.size()) {
            *data_flags |= NGHTTP2_DATA_FLAG_EOF;
        }

        return static_cast<nghttp2_ssize>(to_copy);
    }

    // =========================================================================
    // Middleware Chain Execution (same pattern as http::server)
    // =========================================================================

    auto execute_chain(request_context& ctx, handler_fn& handler,
                       std::size_t idx) -> task<void>
    {
        if (idx >= middlewares_.size()) {
            co_await handler(ctx);
            co_return;
        }
        next_fn next = [this, &ctx, &handler, idx]() -> task<void> {
            return execute_chain(ctx, handler, idx + 1);
        };
        co_await middlewares_[idx](ctx, next);
    }

    // =========================================================================
    // h2_request_adapter — Bridges h2_stream to request_parser-like interface
    // =========================================================================

public:
    class h2_request_adapter {
    public:
        explicit h2_request_adapter(const h2_stream& s) noexcept : stream_(s) {}

        [[nodiscard]] auto method() const noexcept -> std::string_view {
            return stream_.method();
        }
        [[nodiscard]] auto method_enum() const noexcept
            -> std::optional<http_method> {
            return stream_.method_enum();
        }
        [[nodiscard]] auto uri() const noexcept -> std::string_view {
            return stream_.full_path();
        }
        [[nodiscard]] auto version() const noexcept -> http_version {
            return http_version::http_2;
        }
        [[nodiscard]] auto headers() const noexcept -> const header_map& {
            return stream_.headers();
        }
        [[nodiscard]] auto body() const noexcept -> std::string_view {
            return stream_.body();
        }
        [[nodiscard]] auto get_header(std::string_view key) const
            -> std::string_view {
            return stream_.get_header(key);
        }
        [[nodiscard]] auto ready() const noexcept -> bool {
            return stream_.request_complete();
        }

    private:
        const h2_stream& stream_;
    };

private:
    // =========================================================================
    // nghttp2 Callbacks (static, user_data = this)
    // =========================================================================

    static auto on_begin_headers(
        nghttp2_session* /*session*/,
        const nghttp2_frame* frame,
        void* user_data) -> int
    {
        auto* self = static_cast<http2_session*>(user_data);
        if (frame->hd.type != NGHTTP2_HEADERS ||
            frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }

        auto stream_id = frame->hd.stream_id;
        self->streams_.emplace(stream_id, h2_stream{stream_id});
        self->streams_.at(stream_id).set_state(h2_stream_state::open);
        ++self->active_stream_count_;

        // Track highest stream ID for GOAWAY
        if (stream_id > self->last_stream_id_) {
            self->last_stream_id_ = stream_id;
        }

        return 0;
    }

    static auto on_header(
        nghttp2_session* /*session*/,
        const nghttp2_frame* frame,
        const uint8_t* name, size_t namelen,
        const uint8_t* value, size_t valuelen,
        uint8_t /*flags*/,
        void* user_data) -> int
    {
        auto* self = static_cast<http2_session*>(user_data);
        if (frame->hd.type != NGHTTP2_HEADERS ||
            frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }

        auto it = self->streams_.find(frame->hd.stream_id);
        if (it == self->streams_.end()) return 0;

        it->second.add_header(
            std::string_view(reinterpret_cast<const char*>(name), namelen),
            std::string_view(reinterpret_cast<const char*>(value), valuelen));

        return 0;
    }

    static auto on_data_chunk_recv(
        nghttp2_session* session,
        uint8_t /*flags*/,
        int32_t stream_id,
        const uint8_t* data, size_t len,
        void* user_data) -> int
    {
        auto* self = static_cast<http2_session*>(user_data);
        auto it = self->streams_.find(stream_id);
        if (it != self->streams_.end()) {
            it->second.append_body(data, len);
        }

        // Manual flow control: consume received data for both connection and stream.
        // nghttp2 will batch WINDOW_UPDATE frames when the consumed amount reaches
        // the threshold, avoiding per-chunk WINDOW_UPDATE overhead.
        nghttp2_session_consume(session, stream_id, len);

        return 0;
    }

    static auto on_frame_recv(
        nghttp2_session* /*session*/,
        const nghttp2_frame* frame,
        void* user_data) -> int
    {
        auto* self = static_cast<http2_session*>(user_data);

        switch (frame->hd.type) {
        case NGHTTP2_HEADERS: {
            if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) break;

            // Check if END_STREAM is set (no body)
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                auto it = self->streams_.find(frame->hd.stream_id);
                if (it != self->streams_.end()) {
                    it->second.mark_request_complete();
                    it->second.set_state(h2_stream_state::half_closed_remote);
                    self->pending_handlers_.push_back(frame->hd.stream_id);
                }
            }
            break;
        }
        case NGHTTP2_DATA: {
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                auto it = self->streams_.find(frame->hd.stream_id);
                if (it != self->streams_.end()) {
                    it->second.mark_request_complete();
                    it->second.set_state(h2_stream_state::half_closed_remote);
                    self->pending_handlers_.push_back(frame->hd.stream_id);
                }
            }
            break;
        }
        default:
            break;
        }

        return 0;
    }

    static auto on_stream_close(
        nghttp2_session* /*session*/,
        int32_t stream_id,
        uint32_t /*error_code*/,
        void* user_data) -> int
    {
        auto* self = static_cast<http2_session*>(user_data);
        self->streams_.erase(stream_id);
        if (self->active_stream_count_ > 0) {
            --self->active_stream_count_;
        }
        return 0;
    }

    // =========================================================================
    // Member Data
    // =========================================================================

    stream_io& io_;
    const router& router_;
    const std::vector<middleware_fn>& middlewares_;
    h2_settings settings_;
    std::function<std::string()> date_fn_;  // Cached Date header provider

    nghttp2_session* session_ = nullptr;
    std::unordered_map<std::int32_t, h2_stream> streams_;
    std::vector<std::int32_t> pending_handlers_;  // Streams needing handler spawn

    // Channel for handler→session response delivery
    channel<submit_item> submit_ch_;

    // Write coalescing buffer — accumulates mem_send2 chunks for single syscall
    std::string flush_buf_;

    // Graceful shutdown state
    bool shutting_down_ = false;
    std::size_t active_stream_count_ = 0;
    std::int32_t last_stream_id_ = 0;
};

#endif // CNETMOD_HAS_NGHTTP2

} // namespace cnetmod::http
