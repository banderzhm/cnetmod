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

    auto method_path_key(std::optional<http_method> method, std::string_view path)
        -> std::string
    {
        return std::string(method ? method_to_string(*method) : "*") + " " +
            std::string(path);
    }

    auto method_bucket_key(std::optional<http_method> method,
        std::string_view first) -> std::string
    {
        return method_path_key(method, first);
    }
} // namespace detail

auto route_params::get(std::string_view key) const noexcept
    -> std::string_view
{
    auto it = named.find(std::string(key));
    return it == named.end() ? std::string_view{} : std::string_view{it->second};
}

request_context::request_context(io_context& c, socket& s,
    const request_parser& p, response& r,
    route_params x)
    : ctx_(c), sock_(s), resp_(r), params_(std::move(x)), headers_ptr_(&p.headers()), method_(p.method()), body_(p.body())
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

auto request_context::cancellation_token() noexcept -> cnetmod::cancel_token&
{
    return cancellation_;
}

request_context::operation_registration::operation_registration(request_context& request,
    cnetmod::cancel_token& cancellation) noexcept
    : token(cancellation)
{
    if (request.operations_cancelled_.load(std::memory_order_acquire))
    {
        token.cancel();
        return;
    }
    concurrent_containers::exclusive_latch_guard lock{request.operations_latch_};
    if (request.operations_cancelled_.load(std::memory_order_acquire))
    {
        token.cancel();
        return;
    }
    owner = &request;
    next = owner->operations_;
    if (next)
        next->previous = this;
    owner->operations_ = this;
}

request_context::operation_registration::~operation_registration()
{
    if (!owner)
        return;
    concurrent_containers::exclusive_latch_guard lock{owner->operations_latch_};
    if (previous)
        previous->next = next;
    else
        owner->operations_ = next;
    if (next)
        next->previous = previous;
}

void request_context::cancel_pending_operations() noexcept
{
    if (operations_cancelled_.exchange(true, std::memory_order_acq_rel))
        return;
    cancellation_.cancel();
    concurrent_containers::exclusive_latch_guard lock{operations_latch_};
    for (auto* operation = operations_; operation; operation = operation->next)
        operation->token.cancel();
}

auto request_context::trace_id() const noexcept -> std::string_view
{
    return trace_id_;
}

void request_context::set_trace_id(std::string value)
{
    trace_id_ = std::move(value);
}

auto request_context::trace_span_id() const noexcept -> std::string_view
{
    return trace_span_id_;
}

auto request_context::trace_flags() const noexcept -> std::uint8_t
{
    return trace_flags_;
}

auto request_context::trace_state() const noexcept -> std::string_view
{
    return trace_state_;
}

void request_context::set_trace_context(std::string trace_id, std::string span_id,
    std::uint8_t flags, std::string state)
{
    trace_id_ = std::move(trace_id);
    trace_span_id_ = std::move(span_id);
    trace_flags_ = flags;
    trace_state_ = std::move(state);
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

auto request_context::headers() const noexcept -> const header_map&
{
    return *headers_ptr_;
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
    auto it = headers_ptr_->find(key);
    return it == headers_ptr_->end() ? std::string_view{}
                                     : std::string_view{it->second};
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
    return params_.matched.get();
}

auto request_context::params() const noexcept -> const route_params&
{
    return params_;
}

auto request_context::scope() noexcept -> request_scope&
{
    return scope_;
}

auto request_context::scope() const noexcept -> const request_scope&
{
    return scope_;
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
    if (sse_state_ == sse_stream_state::open)
        co_return true;
    if (sse_state_ != sse_stream_state::not_started)
        co_return false;
    resp_.set_status(s);
    sse::prepare(resp_, sse::response_options{.status_code = s});
    sse_chunked_ = resp_.version() == http_version::http_1_1;
    if (sse_chunked_)
        resp_.set_header("Transfer-Encoding", "chunked");
    else
        resp_.set_header("Connection", "close");
    auto h = resp_.serialize();
    sse_state_ = sse_stream_state::committing;
    if (!(co_await write_sse_bytes(h)))
    {
        sse_state_ = sse_stream_state::failed;
        co_return false;
    }
    sse_state_ = sse_stream_state::open;
    co_return true;
}

auto request_context::sse_started() const noexcept -> bool
{
    return sse_state_ != sse_stream_state::not_started;
}

auto request_context::sse_state() const noexcept -> sse_stream_state
{
    return sse_state_;
}

void request_context::configure_sse(sse_stream_options options) noexcept
{
    sse_deadline_ = deadline::after(options.max_duration);
    sse_write_timeout_ = options.write_timeout;
    deadline_ = sse_deadline_;
}

void request_context::set_stream_writer(stream_write_fn writer)
{
    stream_writer_ = std::move(writer);
}

void request_context::expire_sse() noexcept
{
    if (sse_state_ != sse_stream_state::closed)
        sse_state_ = sse_stream_state::failed;
    cancel_pending_operations();
    sock_.close();
}

auto request_context::write_sse_bytes(std::string_view bytes) -> task<bool>
{
    if (sse_deadline_.expired())
    {
        expire_sse();
        co_return false;
    }
    cancel_token write_cancellation;
    operation_registration registration{*this, write_cancellation};
    const auto write_deadline = sse_deadline_.constrain(
        deadline::after(sse_write_timeout_));
    auto operation = stream_writer_
        ? stream_writer_(bytes, write_cancellation)
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
    if (sse_chunked_)
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
    if (sse_chunked_ && !(co_await write_sse_bytes("0\r\n\r\n")))
        co_return false;
    sse_state_ = sse_stream_state::closed;
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
    if (form_cache_)
        return &*form_cache_;
    auto ct = get_header("Content-Type");
    if (ct.empty())
        return std::unexpected(make_error_code(http_errc::invalid_multipart));
    auto r = http::parse_form(ct, body());
    if (!r)
        return std::unexpected(r.error());
    form_cache_ = std::move(*r);
    return &*form_cache_;
}

auto request_context::resp() noexcept -> response&
{
    return resp_;
}

auto request_context::io_ctx() noexcept -> io_context&
{
    return ctx_;
}

auto request_context::raw_socket() noexcept -> socket&
{
    return sock_;
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
    std::optional<request_body_stream_options> request_stream) -> router&
{
    if (!f)
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
    described->pattern = canonical;
    if (const auto* name = metadata.find<endpoint_name>())
        described->name = name->value;
    described->metadata = std::move(metadata);
    entries_.push_back({m, std::move(segs), canonical, std::move(f),
        request_stream, {}, order, std::move(described)});
    auto& e = entries_.back();
    e.score = detail::route_specificity(e.segments, !m, order);
    if (stat)
    {
        exact_index_[detail::method_path_key(m, canonical)].push_back(idx);
        static_exact_indices_.push_back(idx);
    }
    else if (first)
        first_literal_index_[detail::method_bucket_key(m, *first)].push_back(idx);
    else
        generic_indices_.push_back(idx);
    return *this;
}

auto router::find_exact(http_method m, std::string_view p) const
    -> const route_entry*
{
    const route_entry* best = nullptr;
    // Static routes dominate ordinary HTTP service workloads. For a small
    // route table, compare the original request view directly and avoid
    // allocating a canonical path plus composite hash key per request.
    if (static_exact_indices_.size() <= 8U)
    {
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
    auto find = [&](std::optional<http_method> x)
    {
        auto it = exact_index_.find(detail::method_path_key(x, p));
        if (it == exact_index_.end())
            return;
        for (auto i : it->second)
        {
            auto& e = entries_[i];
            if (!best || detail::better_score(e.score, best->score))
                best = &e;
        }
    };
    find(m);
    find({});
    return best;
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

auto router::match(http_method m, std::string_view p) const
    -> std::optional<match_result>
{
    const auto matched = [](const route_entry& entry, route_params params)
    {
        params.matched = entry.described;
        return match_result{entry.handler, std::move(params),
            entry.request_stream};
    };
    if (auto x = find_exact(m, p))
        return matched(*x, {});
    auto parts = detail::split_path(p);
    auto canonical = detail::canonical_from_parts(parts);
    if (auto x = find_exact(m, canonical))
        return matched(*x, {});
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
        auto it = first_literal_index_.find(detail::method_bucket_key(x, first));
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
    auto x = string_to_method(m);
    return x ? match(*x, p) : std::nullopt;
}
} // namespace cnetmod::http
