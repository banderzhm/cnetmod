module cnetmod.protocol.http;

import std;
import :router;
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
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.executor.async_op;

namespace cnetmod::http {
namespace detail {
    auto parse_pattern(std::string_view pattern) -> std::vector<segment>
    {
        std::vector<segment> out;
        if (pattern.starts_with('/'))
            pattern.remove_prefix(1);
        while (!pattern.empty())
        {
            auto slash = pattern.find('/');
            auto part =
                slash == std::string_view::npos ? pattern : pattern.substr(0, slash);
            if (part.starts_with(':'))
                out.push_back({segment_kind::param, std::string(part.substr(1))});
            else if (part.starts_with('*'))
            {
                out.push_back({segment_kind::wildcard,
                    std::string(part.size() > 1 ? part.substr(1) : "path")});
                break;
            }
            else
                out.push_back({segment_kind::exact, std::string(part)});
            if (slash == std::string_view::npos)
                break;
            pattern.remove_prefix(slash + 1);
        }
        return out;
    }

    auto split_path(std::string_view path) -> std::vector<std::string_view>
    {
        std::vector<std::string_view> out;
        if (path.starts_with('/'))
            path.remove_prefix(1);
        while (!path.empty())
        {
            auto slash = path.find('/');
            if (slash == std::string_view::npos)
            {
                out.push_back(path);
                break;
            }
            if (slash)
                out.push_back(path.substr(0, slash));
            path.remove_prefix(slash + 1);
        }
        return out;
    }

    auto route_specificity(const std::vector<segment>& segs, bool any,
        std::uint64_t order) -> route_score
    {
        route_score r;
        r.segment_count = static_cast<int>(segs.size());
        r.method_cost = any;
        r.order = order;
        for (auto& s : segs)
        {
            if (s.kind == segment_kind::exact)
                ++r.literal_count;
            else if (s.kind == segment_kind::param)
                ++r.param_count;
            else
                ++r.wildcard_count;
        }
        return r;
    }

    auto better_score(const route_score& a, const route_score& b) noexcept -> bool
    {
        if (a.wildcard_count != b.wildcard_count)
            return a.wildcard_count < b.wildcard_count;
        if (a.param_count != b.param_count)
            return a.param_count < b.param_count;
        if (a.literal_count != b.literal_count)
            return a.literal_count > b.literal_count;
        if (a.segment_count != b.segment_count)
            return a.segment_count > b.segment_count;
        if (a.method_cost != b.method_cost)
            return a.method_cost < b.method_cost;
        return a.order < b.order;
    }

    auto is_static_route(const std::vector<segment>& segs) noexcept -> bool
    {
        return std::ranges::all_of(
            segs, [](const segment& s)
            {
                return s.kind == segment_kind::exact;
            });
    }

    auto first_literal_segment(const std::vector<segment>& segs)
        -> std::optional<std::string>
    {
        if (!segs.empty() && segs.front().kind == segment_kind::exact)
            return segs.front().value;
        return {};
    }

    auto canonical_from_segments(const std::vector<segment>& segs) -> std::string
    {
        if (segs.empty())
            return "/";
        std::string out;
        for (auto& s : segs)
        {
            out.push_back('/');
            out += s.value;
        }
        return out;
    }

    auto canonical_from_parts(const std::vector<std::string_view>& parts)
        -> std::string
    {
        if (parts.empty())
            return "/";
        std::string out;
        for (auto p : parts)
        {
            out.push_back('/');
            out += p;
        }
        return out;
    }

    auto is_canonical_path(std::string_view path) noexcept -> bool
    {
        return path == "/" || (!path.empty() && path.front() == '/' &&
            path.back() != '/' && path.find("//") == std::string_view::npos);
    }

} // namespace detail

auto router::literal_bucket_hash::operator()(
    const literal_bucket_key& key) const noexcept -> std::size_t
{
    return (*this)(literal_bucket_view{key.method, key.first});
}

auto router::literal_bucket_hash::operator()(
    literal_bucket_view key) const noexcept -> std::size_t
{
    const auto method = key.method
        ? static_cast<std::size_t>(*key.method) + 1U
        : 0U;
    const auto path = std::hash<std::string_view>{}(key.first);
    return path ^ (method + 0x9e3779b9U + (path << 6U) + (path >> 2U));
}

auto router::literal_bucket_equal::operator()(const literal_bucket_key& left,
    const literal_bucket_key& right) const noexcept -> bool
{
    return left.method == right.method && left.first == right.first;
}

auto router::literal_bucket_equal::operator()(const literal_bucket_key& left,
    literal_bucket_view right) const noexcept -> bool
{
    return left.method == right.method && left.first == right.first;
}

auto router::literal_bucket_equal::operator()(literal_bucket_view left,
    const literal_bucket_key& right) const noexcept -> bool
{
    return right.method == left.method && right.first == left.first;
}

auto route_params::get(std::string_view key) const noexcept
    -> std::string_view
{
    auto it = named.find(std::string(key));
    return it == named.end() ? std::string_view{} : std::string_view{it->second};
}

request_context::request_context(io_context& c, socket& s,
    const request_parser& p, response& r,
    route_params x, std::shared_ptr<request_body_stream> stream)
    : ctx_(c), sock_(s), resp_(r), params_(std::move(x)), parser_(&p),
      method_(p.method()), body_(p.body()), body_stream_(std::move(stream))
{
    init_path_query(p.uri());
}

request_context::request_context(io_context& c, socket& s, std::string_view m,
    std::string_view u, const header_map& h,
    std::string_view b, response& r,
    route_params x,
    std::shared_ptr<request_body_stream> stream)
    : ctx_(c), sock_(s), resp_(r), params_(std::move(x)), headers_ptr_(&h), method_(m), body_storage_(b), body_(body_storage_), body_stream_(std::move(stream))
{
    init_path_query(u);
}

request_context::request_context(io_context& c, socket& s, std::string_view m,
    std::string_view u, std::span<const request_header_view> h,
    std::string_view b, response& r, route_params x,
    std::shared_ptr<request_body_stream> stream)
    : ctx_(c), sock_(s), resp_(r), params_(std::move(x)), header_views_(h),
      method_(m), body_storage_(b), body_(body_storage_),
      body_stream_(std::move(stream))
{
    init_path_query(u);
}

auto request_context::method() const noexcept -> std::string_view
{
    return method_;
}

auto request_context::request_deadline() const noexcept -> const cnetmod::deadline&
{
    return deadline_;
}

void request_context::set_deadline(cnetmod::deadline value) noexcept
{
    deadline_ = deadline_.constrain(value);
}

auto request_context::extended() -> extended_state&
{
    if (!extended_)
        extended_ = std::make_unique<extended_state>();
    return *extended_;
}

auto request_context::extended_if() noexcept -> extended_state*
{
    return extended_.get();
}

auto request_context::extended_if() const noexcept -> const extended_state*
{
    return extended_.get();
}

auto request_context::cancellation_token() -> cnetmod::cancel_token&
{
    return extended().cancellation;
}

request_context::operation_registration::operation_registration(request_context& request,
    cnetmod::cancel_token& cancellation) noexcept
    : token(cancellation)
{
    auto& state = request.extended();
    if (state.operations_cancelled.load(std::memory_order_acquire))
    {
        token.cancel();
        return;
    }
    concurrent_containers::exclusive_latch_guard lock{state.operations_latch};
    if (state.operations_cancelled.load(std::memory_order_acquire))
    {
        token.cancel();
        return;
    }
    owner = &request;
    next = state.operations;
    if (next)
        next->previous = this;
    state.operations = this;
}

request_context::operation_registration::~operation_registration()
{
    if (!owner)
        return;
    auto& state = *owner->extended_;
    concurrent_containers::exclusive_latch_guard lock{state.operations_latch};
    if (previous)
        previous->next = next;
    else
        state.operations = next;
    if (next)
        next->previous = previous;
}

void request_context::cancel_pending_operations() noexcept
{
    auto& state = extended();
    if (state.operations_cancelled.exchange(true, std::memory_order_acq_rel))
        return;
    state.cancellation.cancel();
    concurrent_containers::exclusive_latch_guard lock{state.operations_latch};
    for (auto* operation = state.operations; operation; operation = operation->next)
        operation->token.cancel();
}

auto request_context::trace_id() const noexcept -> std::string_view
{
    const auto* state = extended_if();
    return state ? std::string_view{state->trace_id} : std::string_view{};
}

void request_context::set_trace_id(std::string value)
{
    extended().trace_id = std::move(value);
}

auto request_context::trace_span_id() const noexcept -> std::string_view
{
    const auto* state = extended_if();
    return state ? std::string_view{state->trace_span_id} : std::string_view{};
}

auto request_context::trace_flags() const noexcept -> std::uint8_t
{
    const auto* state = extended_if();
    return state ? state->trace_flags : std::uint8_t{};
}

auto request_context::trace_state() const noexcept -> std::string_view
{
    const auto* state = extended_if();
    return state ? std::string_view{state->trace_state} : std::string_view{};
}

void request_context::set_trace_context(std::string trace_id, std::string span_id,
    std::uint8_t flags, std::string state)
{
    auto& extended_state = extended();
    extended_state.trace_id = std::move(trace_id);
    extended_state.trace_span_id = std::move(span_id);
    extended_state.trace_flags = flags;
    extended_state.trace_state = std::move(state);
}

auto request_context::client_address() const -> std::string
{
    auto peer = sock_.remote_endpoint();
    return peer ? peer->to_string() : std::string{};
}

auto request_context::method_enum() const noexcept
    -> std::optional<http_method>
{
    return string_to_method(method_);
}

auto request_context::path() const noexcept -> std::string_view
{
    return path_;
}

auto request_context::query_string() const noexcept -> std::string_view
{
    return query_;
}

auto request_context::uri() const noexcept -> std::string_view
{
    return uri_;
}

auto request_context::borrowed_header_views() const noexcept
    -> std::span<const request_header_view>
{
    return header_views_;
}

auto request_context::headers() const -> const header_map&
{
    if (parser_)
        return parser_->headers();
    if (headers_ptr_)
        return *headers_ptr_;
    if (!header_views_materialized_)
    {
        materialized_headers_.reserve(header_views_.size());
        for (const auto header : header_views_)
            materialized_headers_[std::string{header.name}] = header.value;
        header_views_materialized_ = true;
    }
    return materialized_headers_;
}

auto request_context::body() const -> std::string_view
{
    drain_available_body_chunks();
    return body_;
}

auto request_context::has_body_stream() const noexcept -> bool
{
    return body_stream_ != nullptr;
}

auto request_context::receive_body_chunk()
    -> task<std::optional<request_body_chunk>>
{
    if (!body_stream_)
        co_return std::nullopt;
    co_return co_await body_stream_->receive();
}

auto request_context::read_full_body() -> task<std::string_view>
{
    if (!body_stream_ || body_stream_drained_)
    {
        drain_available_body_chunks();
        co_return body_;
    }
    drain_available_body_chunks();
    while (auto c = co_await body_stream_->receive())
        body_storage_.append(reinterpret_cast<const char*>(c->data()), c->size());
    body_ = body_storage_;
    body_stream_drained_ = true;
    co_return body_;
}

auto request_context::get_header(std::string_view key) const
    -> std::string_view
{
    if (parser_)
        return parser_->get_header(key);
    if (headers_ptr_)
    {
        auto it = headers_ptr_->find(key);
        return it == headers_ptr_->end() ? std::string_view{}
                                         : std::string_view{it->second};
    }
    const case_insensitive_less less;
    for (const auto header : header_views_)
        if (header.name.size() == key.size() && (header.name == key || (!less(header.name, key) && !less(key, header.name))))
            return header.value;
    return {};
}

auto request_context::param(std::string_view n) const noexcept
    -> std::string_view
{
    return params_.get(n);
}

auto request_context::wildcard() const noexcept -> std::string_view
{
    return params_.wildcard;
}

auto request_context::endpoint() const noexcept -> const http::endpoint*
{
    return params_.matched;
}

auto request_context::params() const noexcept -> const route_params&
{
    return params_;
}

auto request_context::scope() -> request_scope&
{
    return extended().scope;
}

auto request_context::scope() const noexcept -> const request_scope&
{
    static const request_scope empty;
    const auto* state = extended_if();
    return state ? state->scope : empty;
}

void request_context::text(int s, std::string_view b)
{
    resp_.set_status(s);
    resp_.set_header("Content-Type", "text/plain; charset=utf-8");
    resp_.set_body(std::string(b));
}

void request_context::json(int s, std::string_view b)
{
    resp_.set_status(s);
    resp_.set_header("Content-Type", "application/json; charset=utf-8");
    resp_.set_body(std::string(b));
}

void request_context::json_owned(int s, std::string b)
{
    resp_.set_status(s);
    resp_.set_header("Content-Type", "application/json; charset=utf-8");
    resp_.set_body(std::move(b));
}

void request_context::html(int s, std::string_view b)
{
    resp_.set_status(s);
    resp_.set_header("Content-Type", "text/html; charset=utf-8");
    resp_.set_body(std::string(b));
}

void request_context::redirect(std::string_view l, int c)
{
    resp_.set_status(c);
    resp_.set_header("Location", l);
}

void request_context::not_found()
{
    text(status::not_found, "404 Not Found");
}

auto request_context::sse_begin(int s) -> task<bool>
{
    auto& state = extended();
    if (state.sse_state == sse_stream_state::open)
        co_return true;
    if (state.sse_state != sse_stream_state::not_started)
        co_return false;
    resp_.set_status(s);
    sse::prepare(resp_, sse::response_options{.status_code = s});
    state.sse_chunked = resp_.version() == http_version::http_1_1;
    if (state.sse_chunked)
        resp_.set_header("Transfer-Encoding", "chunked");
    else
        resp_.set_header("Connection", "close");
    auto h = resp_.serialize();
    state.sse_state = sse_stream_state::committing;
    if (!(co_await write_sse_bytes(h)))
    {
        state.sse_state = sse_stream_state::failed;
        co_return false;
    }
    state.sse_state = sse_stream_state::open;
    co_return true;
}

auto request_context::sse_started() const noexcept -> bool
{
    const auto* state = extended_if();
    return state && state->sse_state != sse_stream_state::not_started;
}

auto request_context::sse_state() const noexcept -> sse_stream_state
{
    const auto* state = extended_if();
    return state ? state->sse_state : sse_stream_state::not_started;
}

void request_context::configure_sse(sse_stream_options options)
{
    auto& state = extended();
    state.sse_deadline = deadline::after(options.max_duration);
    state.sse_write_timeout = options.write_timeout;
    deadline_ = state.sse_deadline;
}

void request_context::set_stream_writer(stream_write_fn writer)
{
    extended().stream_writer = std::move(writer);
}

void request_context::expire_sse() noexcept
{
    auto& state = extended();
    if (state.sse_state != sse_stream_state::closed)
        state.sse_state = sse_stream_state::failed;
    cancel_pending_operations();
    sock_.close();
}

auto request_context::write_sse_bytes(std::string_view bytes) -> task<bool>
{
    auto& state = extended();
    if (state.sse_deadline.expired())
    {
        expire_sse();
        co_return false;
    }
    cancel_token write_cancellation;
    operation_registration registration{*this, write_cancellation};
    const auto write_deadline = state.sse_deadline.constrain(
        deadline::after(state.sse_write_timeout));
    auto operation = state.stream_writer
        ? state.stream_writer(bytes, write_cancellation)
        : async_write_all(ctx_, sock_,
              const_buffer{bytes.data(), bytes.size()}, write_cancellation);
    auto written = co_await cnetmod::with_deadline(ctx_, write_deadline,
        std::move(operation), write_cancellation);
    if (!written)
    {
        expire_sse();
        co_return false;
    }
    co_return true;
}

auto request_context::write_sse_frame(std::string frame) -> task<bool>
{
    if (!(co_await sse_begin()))
        co_return false;
    if (extended().sse_chunked)
    {
        auto chunk = std::format("{:x}\r\n", frame.size());
        chunk += frame;
        chunk += "\r\n";
        co_return co_await write_sse_bytes(chunk);
    }
    co_return co_await write_sse_bytes(frame);
}

auto request_context::sse_send(std::string_view d, std::string_view e)
    -> task<bool>
{
    co_return co_await write_sse_frame(sse::data(d, e));
}

auto request_context::sse_json(std::string_view j, std::string_view e)
    -> task<bool>
{
    co_return co_await sse_send(j, e);
}

auto request_context::sse_comment(std::string_view value) -> task<bool>
{
    co_return co_await write_sse_frame(sse::comment(value));
}

auto request_context::sse_heartbeat() -> task<bool>
{
    co_return co_await write_sse_frame(sse::heartbeat());
}

auto request_context::sse_done() -> task<bool>
{
    if (!(co_await write_sse_frame(sse::done())))
        co_return false;
    auto& state = extended();
    if (state.sse_chunked && !(co_await write_sse_bytes("0\r\n\r\n")))
        co_return false;
    state.sse_state = sse_stream_state::closed;
    co_return true;
}

auto request_context::with_sse(sse_handler_fn handler,
    sse_stream_options options) -> task<void>
{
    if (!handler)
        throw std::invalid_argument("SSE handler must not be empty");
    if (options.max_duration <= std::chrono::milliseconds::zero() ||
        options.write_timeout <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("SSE timeouts must be positive");

    sse_stream stream{*this, options};
    cancel_token watchdog_cancellation;
    auto invoke_handler = [&]() -> task<void>
    {
        try
        {
            co_await handler(*this, stream);
        }
        catch (...)
        {
            watchdog_cancellation.cancel();
            throw;
        }
        watchdog_cancellation.cancel();
    };
    auto watchdog = [&]() -> task<void>
    {
        const auto waited = co_await async_timer_wait(ctx_,
            options.max_duration, watchdog_cancellation);
        if (waited)
            expire_sse();
    };
    co_await when_all(invoke_handler(), watchdog());
}

auto request_context::parse_form()
    -> std::expected<const form_data*, std::error_code>
{
    if (const auto* state = extended_if(); state && state->form_cache)
        return &*state->form_cache;
    auto ct = get_header("Content-Type");
    if (ct.empty())
        return std::unexpected(make_error_code(http_errc::invalid_multipart));
    auto r = http::parse_form(ct, body());
    if (!r)
        return std::unexpected(r.error());
    auto& cache = extended().form_cache;
    cache = std::move(*r);
    return &*cache;
}

auto request_context::resp() noexcept -> response&
{
    return resp_;
}

auto request_context::io_ctx() noexcept -> io_context&
{
    return ctx_;
}

void router::on_unmatched(unmatched_handler_fn handler)
{
    unmatched_ = std::move(handler);
}

auto router::unmatched_handler() const -> unmatched_handler_fn
{
    return unmatched_;
}

auto request_context::raw_socket() noexcept -> socket&
{
    return sock_;
}

void request_context::upgrade_connection(connection_upgrade_handler handler)
{
    if (!handler)
        throw std::invalid_argument("connection upgrade handler must not be empty");
    if (!connection_upgrade_enabled_ || body_stream_)
        throw std::logic_error("this request transport cannot upgrade connections");
    extended().connection_upgrade = std::move(handler);
}

auto request_context::can_upgrade_connection() const noexcept -> bool
{
    return connection_upgrade_enabled_ && !body_stream_;
}

void request_context::enable_connection_upgrade() noexcept
{
    connection_upgrade_enabled_ = true;
}

auto request_context::take_connection_upgrade()
    -> connection_upgrade_handler
{
    auto* state = extended_if();
    return state ? std::move(state->connection_upgrade)
                 : connection_upgrade_handler{};
}

auto request_context::peer_address() const -> std::string
{
    auto remote = sock_.remote_endpoint();
    return remote ? remote->address().to_string() : std::string{};
}

void request_context::drain_available_body_chunks() const
{
    if (!body_stream_ || body_stream_drained_)
        return;
    while (auto c = body_stream_->try_receive())
        body_storage_.append(reinterpret_cast<const char*>(c->data()), c->size());
    body_ = body_storage_;
    if (body_stream_->is_closed())
        body_stream_drained_ = true;
}

void request_context::init_path_query(std::string_view u)
{
    uri_ = u;
    auto q = u.find('?');
    path_ = u.substr(0, q);
    if (q != std::string_view::npos)
        query_ = u.substr(q + 1);
}

auto router::get(std::string_view p, handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add(http_method::GET, p, std::move(f), std::move(metadata));
}

auto router::post(std::string_view p, handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add(http_method::POST, p, std::move(f), std::move(metadata));
}

auto router::put(std::string_view p, handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add(http_method::PUT, p, std::move(f), std::move(metadata));
}

auto router::del(std::string_view p, handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add(http_method::DELETE_, p, std::move(f), std::move(metadata));
}

auto router::patch(std::string_view p, handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add(http_method::PATCH, p, std::move(f), std::move(metadata));
}

auto router::any(std::string_view p, handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_route({}, p, std::move(f), std::move(metadata));
}

auto router::get_sync(std::string_view p, sync_handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_sync(http_method::GET, p, std::move(f), std::move(metadata));
}

auto router::post_sync(std::string_view p, sync_handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_sync(http_method::POST, p, std::move(f), std::move(metadata));
}

auto router::put_sync(std::string_view p, sync_handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_sync(http_method::PUT, p, std::move(f), std::move(metadata));
}

auto router::del_sync(std::string_view p, sync_handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_sync(http_method::DELETE_, p, std::move(f), std::move(metadata));
}

auto router::patch_sync(std::string_view p, sync_handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_sync(http_method::PATCH, p, std::move(f), std::move(metadata));
}

auto router::any_sync(std::string_view p, sync_handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_sync(std::nullopt, p, std::move(f), std::move(metadata));
}

auto request_context::body_stream_error() const noexcept -> std::error_code
{
    return body_stream_ ? body_stream_->error() : std::error_code{};
}

auto request_context::received_body_bytes() const noexcept -> std::size_t
{
    return body_stream_ ? body_stream_->received_bytes() : body_.size();
}

auto router::stream_post(std::string_view p, handler_fn f,
    request_body_stream_options options, endpoint_metadata metadata) -> router&
{
    return add_route(http_method::POST, p, std::move(f), std::move(metadata),
        options);
}

auto router::stream_put(std::string_view p, handler_fn f,
    request_body_stream_options options, endpoint_metadata metadata) -> router&
{
    return add_route(http_method::PUT, p, std::move(f), std::move(metadata),
        options);
}

auto router::stream_patch(std::string_view p, handler_fn f,
    request_body_stream_options options, endpoint_metadata metadata) -> router&
{
    return add_route(http_method::PATCH, p, std::move(f), std::move(metadata),
        options);
}

auto router::sse_get(std::string_view p, sse_handler_fn f,
    std::optional<sse_stream_options> options, endpoint_metadata metadata)
    -> router&
{
    return add_sse(http_method::GET, p, std::move(f), options,
        std::move(metadata));
}

auto router::sse_post(std::string_view p, sse_handler_fn f,
    std::optional<sse_stream_options> options, endpoint_metadata metadata)
    -> router&
{
    return add_sse(http_method::POST, p, std::move(f), options,
        std::move(metadata));
}

auto router::sse_defaults(sse_stream_options options) -> router&
{
    if (options.max_duration <= std::chrono::milliseconds::zero() ||
        options.write_timeout <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("SSE timeouts must be positive");
    sse_defaults_ = options;
    return *this;
}

auto router::add(http_method m, std::string_view p, handler_fn f,
    endpoint_metadata metadata) -> router&
{
    return add_route(m, p, std::move(f), std::move(metadata));
}

auto router::add_sync(std::optional<http_method> method, std::string_view path,
    sync_handler_fn handler, endpoint_metadata metadata) -> router&
{
    if (!handler)
        throw std::invalid_argument("sync route handler must not be empty");
    return add_route(method, path, {}, std::move(metadata), {},
        std::move(handler));
}

auto router::add_sse(http_method m, std::string_view p, sse_handler_fn f,
    std::optional<sse_stream_options> options, endpoint_metadata metadata)
    -> router&
{
    if (!f)
        throw std::invalid_argument("SSE route handler must not be empty");
    const auto selected = options.value_or(sse_defaults_);
    if (selected.max_duration <= std::chrono::milliseconds::zero() ||
        selected.write_timeout <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("SSE timeouts must be positive");
    return add(m, p, [handler = std::move(f), selected](request_context& request) -> task<void>
        {
            co_await request.with_sse(handler, selected);
        },
        std::move(metadata));
}

auto router::add_route(std::optional<http_method> m, std::string_view p,
    handler_fn f, endpoint_metadata metadata,
    std::optional<request_body_stream_options> request_stream,
    sync_handler_fn sync_handler) -> router&
{
    if (!f && !sync_handler)
        throw std::invalid_argument("route handler must not be empty");
    if (request_stream &&
        (request_stream->max_bytes == 0 || request_stream->chunk_capacity == 0))
        throw std::invalid_argument(
            "request body stream limits must be positive");
    auto segs = detail::parse_pattern(p);
    auto order = next_order_++;
    auto idx = entries_.size();
    auto stat = detail::is_static_route(segs);
    auto first = detail::first_literal_segment(segs);
    auto canonical = detail::canonical_from_segments(segs);
    auto described = std::make_shared<endpoint>();
    described->method = m;
    described->pattern = std::string{p};
    if (const auto* name = metadata.find<endpoint_name>())
        described->name = name->value;
    described->metadata = std::move(metadata);
    entries_.push_back({m, std::move(segs), canonical, std::move(f),
        std::move(sync_handler), request_stream, {}, order,
        std::move(described)});
    auto& e = entries_.back();
    e.score = detail::route_specificity(e.segments, !m, order);
    if (stat)
    {
        auto& bucket = exact_index_[canonical];
        if (m)
        {
            auto& slot = bucket.methods[static_cast<std::size_t>(*m)];
            if (slot == no_route_index)
                slot = idx;
        }
        else if (bucket.any == no_route_index)
            bucket.any = idx;
        static_exact_indices_.push_back(idx);
    }
    else if (first)
        first_literal_index_[literal_bucket_key{m, *first}].push_back(idx);
    else
        generic_indices_.push_back(idx);
    return *this;
}

auto router::find_exact(http_method m, std::string_view p) const
    -> const route_entry*
{
    // Static routes dominate ordinary HTTP service workloads. For a small
    // route table, compare the original request view directly and avoid
    // allocating a canonical path plus composite hash key per request.
    if (static_exact_indices_.size() <= 8U)
    {
        const route_entry* best = nullptr;
        for (const auto index : static_exact_indices_)
        {
            const auto& entry = entries_[index];
            if ((!entry.method || *entry.method == m) &&
                entry.canonical_path == p &&
                (!best || detail::better_score(entry.score, best->score)))
                best = &entry;
        }
        return best;
    }
    const auto it = exact_index_.find(p);
    if (it == exact_index_.end())
        return nullptr;
    const auto method_index = it->second.methods[static_cast<std::size_t>(m)];
    if (method_index != no_route_index)
        return &entries_[method_index];
    return it->second.any != no_route_index
        ? &entries_[it->second.any]
        : nullptr;
}

auto router::try_match(const std::vector<detail::segment>& s,
    const std::vector<std::string_view>& p, route_params& o)
    -> bool
{
    std::size_t pi = 0;
    for (auto& x : s)
    {
        if (x.kind == detail::segment_kind::wildcard)
        {
            std::string r;
            for (; pi < p.size(); ++pi)
            {
                if (!r.empty())
                    r += '/';
                r += p[pi];
            }
            o.wildcard = std::move(r);
            o.named[x.value] = o.wildcard;
            return true;
        }
        if (pi >= p.size())
            return false;
        if (x.kind == detail::segment_kind::exact)
        {
            if (p[pi] != x.value)
                return false;
        }
        else
            o.named[x.value] = std::string(p[pi]);
        ++pi;
    }
    return pi == p.size();
}

auto router::match_borrowed(http_method m, std::string_view p) const
    -> std::optional<borrowed_match_result>
{
    const auto matched = [](const route_entry& entry, route_params params)
    {
        params.matched = entry.described.get();
        return borrowed_match_result{&entry, std::move(params)};
    };
    if (auto x = find_exact(m, p))
        return matched(*x, {});
    auto parts = detail::split_path(p);
    if (!detail::is_canonical_path(p))
    {
        auto canonical = detail::canonical_from_parts(parts);
        if (auto x = find_exact(m, canonical))
            return matched(*x, {});
    }
    const route_entry* best = nullptr;
    route_params bp;
    auto consider = [&](std::size_t i)
    {
        auto& e = entries_[i];
        route_params x;
        if (try_match(e.segments, parts, x) &&
            (!best || detail::better_score(e.score, best->score)))
        {
            best = &e;
            bp = std::move(x);
        }
    };
    auto bucket = [&](std::optional<http_method> x, std::string_view first)
    {
        auto it = first_literal_index_.find(literal_bucket_view{x, first});
        if (it != first_literal_index_.end())
            for (auto i : it->second)
                consider(i);
    };
    if (!parts.empty())
    {
        bucket(m, parts.front());
        bucket({}, parts.front());
    }
    for (auto i : generic_indices_)
    {
        auto& e = entries_[i];
        if (!e.method || *e.method == m)
            consider(i);
    }
    if (best)
        return matched(*best, std::move(bp));
    return {};
}

auto router::match_borrowed(std::string_view m, std::string_view p) const
    -> std::optional<borrowed_match_result>
{
    auto x = string_to_method(m);
    return x ? match_borrowed(*x, p) : std::nullopt;
}

auto router::match(http_method m, std::string_view p) const
    -> std::optional<match_result>
{
    auto matched = match_borrowed(m, p);
    if (!matched)
        return {};
    const auto& route = *matched->route;
    return match_result{route.handler, route.sync_handler,
        std::move(matched->params), route.request_stream};
}

auto router::allowed_methods(std::string_view p) const
    -> std::vector<http_method>
{
    const auto parts = detail::split_path(p);
    std::vector<http_method> result;
    result.reserve(4);
    for (const auto& entry : entries_)
    {
        if (!entry.method)
            continue;
        route_params ignored;
        if (!try_match(entry.segments, parts, ignored) ||
            std::ranges::find(result, *entry.method) != result.end())
            continue;
        result.push_back(*entry.method);
    }
    return result;
}

auto router::endpoints() const -> std::vector<std::shared_ptr<const endpoint>>
{
    std::vector<std::shared_ptr<const endpoint>> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_)
        result.push_back(entry.described);
    return result;
}

auto router::match(std::string_view m, std::string_view p) const
    -> std::optional<match_result>
{
    auto matched = match_borrowed(m, p);
    if (!matched)
        return {};
    const auto& route = *matched->route;
    return match_result{route.handler, route.sync_handler,
        std::move(matched->params), route.request_stream};
}
} // namespace cnetmod::http
