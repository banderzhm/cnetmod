module cnetmod.protocol.redis;

import std;
import cnetmod.instrumentation.operation_scope;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.operation_result;
import cnetmod.instrumentation.tracing;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :client;

namespace cnetmod::redis {

namespace {

    /**
     * @brief Invalidates a command stream unless one complete exchange commits.
     */
    class command_exchange_guard
    {
    public:
        explicit command_exchange_guard(client& owner) noexcept
            : owner_(owner)
        {
        }

        ~command_exchange_guard()
        {
            if (!committed_)
                owner_.close();
        }

        void commit() noexcept
        {
            committed_ = true;
        }

    private:
        client& owner_;
        bool committed_ = false;
    };

} // namespace

client::client(io_context& ctx) noexcept
    : ctx_(ctx) {}

auto client::connect(connect_options opts)
    -> task<std::expected<void, std::string>>
{
    close();

    /**
     * @brief Rolls back every unsuccessful transport or protocol handshake.
     */
    struct connection_guard
    {
        client& owner;
        bool committed = false;

        ~connection_guard()
        {
            if (!committed)
                owner.close();
        }
    } guard{*this};

    opts_ = opts;
    auto connected =
        co_await async_connect_happy_eyeballs(ctx_, opts.host, opts.port);
    if (!connected)
        co_return std::unexpected(connected.error().message());
    sock_ = std::move(connected->sock);
#ifdef CNETMOD_HAS_SSL
    if (opts.tls)
    {
        auto configured = configure_tls(opts, false);
        if (!configured)
            co_return std::unexpected(std::string{configured.error().stage} + configured.error().code.message());
        auto handshake = co_await ssl_->async_handshake();
        if (!handshake)
        {
            sock_.close();
            co_return std::unexpected("ssl handshake: " +
                handshake.error().message());
        }
    }
#else
    if (opts.tls)
    {
        sock_.close();
        co_return std::unexpected(std::string("SSL not available"));
    }
#endif
    if (opts.resp3)
    {
        request hello;
        if (!opts.password.empty())
            hello.push("HELLO", "3", "AUTH",
                opts.username.empty() ? "default" : opts.username,
                opts.password);
        else
            hello.push("HELLO", "3");
        auto response = co_await exec(hello);
        if (!response)
            co_return std::unexpected("HELLO 3 failed: " + response.error());
        resp3_mode_ = response->empty() || !response->front().is_error();
        if (!resp3_mode_ && !opts.password.empty())
        {
            auto auth = co_await do_auth(opts);
            if (!auth)
                co_return auth;
        }
    }
    else
    {
        resp3_mode_ = false;
        if (!opts.password.empty())
        {
            auto auth = co_await do_auth(opts);
            if (!auth)
                co_return auth;
        }
    }
    if (opts.db > 0)
    {
        request select;
        select.push("SELECT", std::to_string(opts.db));
        auto response = co_await exec(select);
        if (!response)
            co_return std::unexpected("SELECT failed: " + response.error());
        if (!response->empty() && response->front().is_error())
            co_return std::unexpected("SELECT error: " + response->front().value);
    }
    guard.committed = true;
    co_return std::expected<void, std::string>{};
}

auto client::is_open() const noexcept -> bool
{
    return sock_.is_open();
}

auto client::is_reusable() const noexcept -> bool
{
    return sock_.is_open() && rpos_ == rbuf_.size();
}

void client::close() noexcept
{
#ifdef CNETMOD_HAS_SSL
    ssl_.reset();
    ssl_ctx_.reset();
#endif
    sock_.close();
    rbuf_.clear();
    rpos_ = 0;
    resp3_mode_ = false;
}

auto client::exec(const request& request)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!is_reusable())
    {
        close();
        co_return std::unexpected("connection has pending response data");
    }
    command_exchange_guard guard{*this};
    auto payload = request.payload();
    auto written = co_await do_write({payload.data(), payload.size()});
    if (!written)
        co_return std::unexpected(written.error().message());
    std::vector<resp3_node> result;
    for (std::size_t index = 0; index < request.size(); ++index)
    {
        auto nodes = co_await parse_one_response();
        if (!nodes)
            co_return std::unexpected(nodes.error());
        for (auto& node : *nodes)
            result.push_back(std::move(node));
    }
    if (!is_reusable())
        co_return std::unexpected("response contains unexpected trailing data");
    guard.commit();
    co_return result;
}

auto client::cmd(std::initializer_list<std::string_view> args)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (args.size() == 0)
        co_return std::unexpected(std::string("empty command"));
    if (!is_reusable())
    {
        close();
        co_return std::unexpected("connection has pending response data");
    }
    command_exchange_guard guard{*this};
    std::string payload;
    detail::add_header(payload, resp3_type::array, args.size());
    for (auto arg : args)
        detail::add_bulk(payload, arg);
    auto written = co_await do_write({payload.data(), payload.size()});
    if (!written)
        co_return std::unexpected(written.error().message());
    auto response = co_await parse_one_response();
    if (!response)
        co_return response;
    if (!is_reusable())
        co_return std::unexpected("response contains unexpected trailing data");
    guard.commit();
    co_return response;
}

auto client::cmd(std::span<const std::string> args)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (args.empty())
        co_return std::unexpected(std::string("empty command"));
    if (!is_reusable())
    {
        close();
        co_return std::unexpected("connection has pending response data");
    }
    command_exchange_guard guard{*this};
    std::string payload;
    detail::add_header(payload, resp3_type::array, args.size());
    for (auto const& arg : args)
        detail::add_bulk(payload, arg);
    auto written = co_await do_write({payload.data(), payload.size()});
    if (!written)
        co_return std::unexpected(written.error().message());
    auto response = co_await parse_one_response();
    if (!response)
        co_return response;
    if (!is_reusable())
        co_return std::unexpected("response contains unexpected trailing data");
    guard.commit();
    co_return response;
}

namespace {

    auto observe_batch(task<std::expected<std::vector<resp3_node>, std::string>> pending,
        std::size_t count, instrumentation::trace_context parent,
        instrumentation::span_exporter sink)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        auto operation = instrumentation::operation_scope::start(sink, [&]
            {
                return instrumentation::start_client_span(parent, "REDIS PIPELINE");
            });
        operation.annotate([&]
            {
                return std::vector<std::pair<std::string, std::string>>{
                    {"db.system.name", "redis"}, {"db.operation.name", "PIPELINE"},
                    {"db.operation.batch.size", std::to_string(count)}};
            });
        try
        {
            auto response = co_await std::move(pending);
            const bool failed = !response || has_error(*response);
            operation.complete({failed ? instrumentation::operation_status::error
                                       : instrumentation::operation_status::success,
                {}});
            co_return response;
        }
        catch (const std::system_error& error)
        {
            operation.complete(instrumentation::classify_error(error.code()));
            throw;
        }
        catch (...)
        {
            operation.complete({instrumentation::operation_status::error, {}});
            throw;
        }
    }

    template <typename Arguments>
    auto observe_command(client& connection, Arguments args,
        instrumentation::trace_context parent, instrumentation::span_exporter sink)
        -> task<std::expected<std::vector<resp3_node>, std::string>>
    {
        auto operation = instrumentation::operation_scope::start(sink, [&]
            {
                const auto command = args.size() == 0U ? std::string{"UNKNOWN"}
                                                       : std::string{*args.begin()};
                return instrumentation::start_client_span(parent, "REDIS " + command);
            });
        operation.annotate([&]
            {
                const auto command = args.size() == 0U ? std::string{"UNKNOWN"}
                                                       : std::string{*args.begin()};
                return std::vector<std::pair<std::string, std::string>>{
                    {"db.system.name", "redis"}, {"db.operation.name", command}};
            });
        try
        {
            auto response = co_await connection.cmd(args);
            const bool failed = !response || has_error(*response);
            operation.complete({failed ? instrumentation::operation_status::error
                                       : instrumentation::operation_status::success,
                {}});
            co_return response;
        }
        catch (const std::system_error& error)
        {
            operation.complete(instrumentation::classify_error(error.code()));
            throw;
        }
        catch (...)
        {
            operation.complete({instrumentation::operation_status::error, {}});
            throw;
        }
    }

} // namespace

auto client::exec(const request& req, const instrumentation::trace_context& parent,
    const instrumentation::span_exporter& on_end)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!on_end)
        return exec(req);
    try
    {
        return observe_batch(exec(req), req.size(), parent, on_end);
    }
    catch (...)
    {
        return exec(req);
    }
}

auto client::pipe(std::span<const std::vector<std::string>> commands,
    const instrumentation::trace_context& parent, const instrumentation::span_exporter& on_end)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!on_end)
        return pipe(commands);
    try
    {
        return observe_batch(pipe(commands), commands.size(), parent, on_end);
    }
    catch (...)
    {
        return pipe(commands);
    }
}

auto client::pipe(std::initializer_list<std::initializer_list<std::string_view>> commands,
    const instrumentation::trace_context& parent, const instrumentation::span_exporter& on_end)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!on_end)
        return pipe(commands);
    try
    {
        return observe_batch(pipe(commands), commands.size(), parent, on_end);
    }
    catch (...)
    {
        return pipe(commands);
    }
}

auto client::cmd(std::initializer_list<std::string_view> args,
    const instrumentation::trace_context& parent, const instrumentation::span_exporter& on_end)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!on_end)
        return cmd(args);
    try
    {
        return observe_command(*this, args, parent, on_end);
    }
    catch (...)
    {
        return cmd(args);
    }
}

auto client::cmd(std::span<const std::string> args,
    const instrumentation::trace_context& parent, const instrumentation::span_exporter& on_end)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!on_end)
        return cmd(args);
    try
    {
        return observe_command(*this, args, parent, on_end);
    }
    catch (...)
    {
        return cmd(args);
    }
}

auto client::cmd_follow_redirect(std::vector<std::string> args,
    std::size_t limit)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    for (std::size_t attempt = 0; attempt <= limit; ++attempt)
    {
        auto response =
            co_await cmd(std::span<const std::string>{args.data(), args.size()});
        if (!response)
            co_return response;
        auto redirect = parse_redirect(*response);
        if (!redirect)
            co_return response;
        if (attempt == limit)
            co_return std::unexpected(
                std::string("redis cluster redirect limit exceeded"));
        auto next = opts_;
        next.host = redirect->endpoint.host;
        next.port = redirect->endpoint.port;
        close();
        auto connection = co_await connect(next);
        if (!connection)
            co_return std::unexpected(connection.error());
        if (redirect->kind == redirect_kind::ask)
        {
            auto asking = co_await cmd({"ASKING"});
            if (!asking)
                co_return asking;
            if (has_error(*asking))
                co_return std::unexpected(std::string(error_message(*asking)));
        }
    }
    co_return std::unexpected(std::string("redis cluster redirect failed"));
}

auto client::pipe(
    std::initializer_list<std::initializer_list<std::string_view>> commands)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!is_reusable())
    {
        close();
        co_return std::unexpected("connection has pending response data");
    }
    command_exchange_guard guard{*this};
    std::string batch;
    for (auto const& command : commands)
    {
        detail::add_header(batch, resp3_type::array, command.size());
        for (auto arg : command)
            detail::add_bulk(batch, arg);
    }
    auto written = co_await do_write({batch.data(), batch.size()});
    if (!written)
        co_return std::unexpected(written.error().message());
    std::vector<resp3_node> result;
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        auto nodes = co_await parse_one_response();
        if (!nodes)
            co_return std::unexpected(nodes.error());
        for (auto& node : *nodes)
            result.push_back(std::move(node));
    }
    if (!is_reusable())
        co_return std::unexpected("response contains unexpected trailing data");
    guard.commit();
    co_return result;
}

auto client::pipe(std::span<const std::vector<std::string>> commands)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    if (!is_reusable())
    {
        close();
        co_return std::unexpected("connection has pending response data");
    }
    command_exchange_guard guard{*this};
    std::string batch;
    for (auto const& command : commands)
    {
        if (command.empty())
            co_return std::unexpected(std::string("empty command in pipeline"));
        detail::add_header(batch, resp3_type::array, command.size());
        for (auto const& arg : command)
            detail::add_bulk(batch, arg);
    }
    auto written = co_await do_write({batch.data(), batch.size()});
    if (!written)
        co_return std::unexpected(written.error().message());
    std::vector<resp3_node> result;
    for (std::size_t i = 0; i < commands.size(); ++i)
    {
        auto nodes = co_await parse_one_response();
        if (!nodes)
            co_return std::unexpected(nodes.error());
        for (auto& node : *nodes)
            result.push_back(std::move(node));
    }
    if (!is_reusable())
        co_return std::unexpected("response contains unexpected trailing data");
    guard.commit();
    co_return result;
}

auto client::subscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    co_return co_await subscription_command("SUBSCRIBE", names);
}

auto client::unsubscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    co_return co_await subscription_command("UNSUBSCRIBE", names);
}

auto client::psubscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    co_return co_await subscription_command("PSUBSCRIBE", names);
}

auto client::punsubscribe(std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    co_return co_await subscription_command("PUNSUBSCRIBE", names);
}

auto client::sentinel_get_master_addr_by_name(std::string_view master)
    -> task<std::expected<endpoint_info, std::string>>
{
    auto response = co_await cmd({"SENTINEL", "GET-MASTER-ADDR-BY-NAME", master});
    if (!response)
        co_return std::unexpected(response.error());
    if (has_error(*response))
        co_return std::unexpected(std::string(error_message(*response)));
    auto values = all_values(*response);
    if (values.size() < 2)
        co_return std::unexpected(
            std::string("sentinel returned no master address"));
    std::uint16_t port{};
    auto [_, error] = std::from_chars(values[1].data(),
        values[1].data() + values[1].size(), port);
    if (error != std::errc{})
        co_return std::unexpected(
            std::string("sentinel returned invalid master port"));
    co_return endpoint_info{.host = std::string(values[0]), .port = port};
}

void client::on_push(push_callback callback)
{
    push_cb_ = std::move(callback);
}

auto client::receive_push()
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    co_return co_await parse_one_response();
}

auto client::is_resp3() const noexcept -> bool
{
    return resp3_mode_;
}

auto client::ping(cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    if (cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(
            cancellation.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));
    if (!is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));

    // Pending input belongs to another exchange; never consume it as this PONG.
    if (rpos_ != rbuf_.size())
        co_return std::unexpected(std::make_error_code(std::errc::operation_in_progress));

    /**
     * @brief Invalidates the stream unless the entire exchange was verified.
     */
    struct exchange_guard
    {
        client& connection;
        bool complete = false;

        ~exchange_guard()
        {
            if (!complete)
                connection.close();
        }
    } guard{*this};

    constexpr std::string_view command = "*1\r\n$4\r\nPING\r\n";
    const_buffer outgoing{command.data(), command.size()};
    std::expected<void, std::error_code> written;
#ifdef CNETMOD_HAS_SSL
    if (ssl_)
        written = co_await ssl_->async_write_all(outgoing, cancellation);
    else
#endif
        written = co_await async_write_all(ctx_, sock_, outgoing, cancellation);
    if (!written)
        co_return std::unexpected(written.error());

    constexpr std::string_view expected = "+PONG\r\n";
    std::array<char, expected.size()> response{};
    std::size_t offset = 0;
    while (offset < response.size())
    {
        mutable_buffer incoming{response.data() + offset, response.size() - offset};
        std::expected<std::size_t, std::error_code> received;
#ifdef CNETMOD_HAS_SSL
        if (ssl_)
            received = co_await ssl_->async_read(incoming, cancellation);
        else
#endif
            received = co_await async_read(ctx_, sock_, incoming, cancellation);
        if (!received)
            co_return std::unexpected(received.error());
        if (*received == 0U)
            co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
        offset += *received;
        if (std::string_view{response.data(), offset} != expected.substr(0, offset))
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    guard.complete = true;
    co_return {};
}

auto client::do_write(const_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (!sock_.is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
#ifdef CNETMOD_HAS_SSL
    if (ssl_)
    {
        auto result = co_await ssl_->async_write_all(buffer);
        if (!result)
            co_return std::unexpected(result.error());
        co_return buffer.size;
    }
#endif
    auto result = co_await async_write_all(ctx_, sock_, buffer);
    if (!result)
        co_return std::unexpected(result.error());
    co_return buffer.size;
}

auto client::do_read(mutable_buffer buffer)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (!sock_.is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
#ifdef CNETMOD_HAS_SSL
    if (ssl_)
        co_return co_await ssl_->async_read(buffer);
#endif
    co_return co_await async_read(ctx_, sock_, buffer);
}

auto client::do_auth(const connect_options& options)
    -> task<std::expected<void, std::string>>
{
    request auth;
    if (options.username.empty())
        auth.push("AUTH", options.password);
    else
        auth.push("AUTH", options.username, options.password);
    auto response = co_await exec(auth);
    if (!response)
        co_return std::unexpected("AUTH failed: " + response.error());
    if (!response->empty() && response->front().is_error())
        co_return std::unexpected("AUTH error: " + response->front().value);
    co_return std::expected<void, std::string>{};
}

auto client::subscription_command(std::string_view command,
    std::initializer_list<std::string_view> names)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    std::string payload;
    detail::add_header(payload, resp3_type::array, names.size() + 1);
    detail::add_bulk(payload, command);
    for (auto name : names)
        detail::add_bulk(payload, name);
    auto written = co_await do_write({payload.data(), payload.size()});
    if (!written)
        co_return std::unexpected(written.error().message());
    std::vector<resp3_node> result;
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto nodes = co_await parse_one_response();
        if (!nodes)
            co_return std::unexpected(nodes.error());
        for (auto& node : *nodes)
            result.push_back(std::move(node));
    }
    co_return result;
}

auto client::parse_one_response()
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    std::vector<resp3_node> nodes;
    resp3_parser parser;
    std::error_code error;
    while (true)
    {
        if (rpos_ >= rbuf_.size() || rbuf_.empty())
        {
            if (!(co_await fill()))
                co_return std::unexpected(std::string("connection closed"));
        }
        auto view = std::string_view(rbuf_).substr(rpos_);
        auto node = parser.consume(view, error);
        if (error)
            co_return std::unexpected(error.message());
        if (node)
        {
            nodes.push_back(std::move(*node));
            rpos_ += parser.consumed();
            parser.reset();
            auto& first = nodes.front();
            if (first.is_aggregate() && first.aggregate_size > 0)
            {
                auto children = co_await parse_children(
                    first.aggregate_size * element_multiplicity(first.data_type));
                if (!children)
                    co_return std::unexpected(children.error());
                for (auto& child : *children)
                    nodes.push_back(std::move(child));
            }
            compact_buffer();
            co_return nodes;
        }
        rpos_ += parser.consumed();
        parser.reset();
        if (!(co_await fill()))
            co_return std::unexpected(std::string("connection closed"));
    }
}

auto client::parse_children(std::size_t count)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    std::vector<resp3_node> children;
    children.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        resp3_parser parser;
        std::error_code error;
        while (true)
        {
            auto view = std::string_view(rbuf_).substr(rpos_);
            if (view.empty())
            {
                if (!(co_await fill()))
                    co_return std::unexpected(std::string("connection closed"));
                continue;
            }
            auto node = parser.consume(view, error);
            if (error)
                co_return std::unexpected(error.message());
            if (!node)
            {
                rpos_ += parser.consumed();
                parser.reset();
                if (!(co_await fill()))
                    co_return std::unexpected(std::string("connection closed"));
                continue;
            }
            rpos_ += parser.consumed();
            children.push_back(std::move(*node));
            auto& child = children.back();
            if (child.is_aggregate() && child.aggregate_size > 0)
            {
                auto sub = co_await parse_children(
                    child.aggregate_size * element_multiplicity(child.data_type));
                if (!sub)
                    co_return std::unexpected(sub.error());
                for (auto& item : *sub)
                    children.push_back(std::move(item));
                i += sub->size();
            }
            break;
        }
    }
    co_return children;
}

auto client::fill() -> task<bool>
{
    std::array<std::byte, 8192> storage{};
    auto read = co_await do_read({storage.data(), storage.size()});
    if (!read || *read == 0)
        co_return false;
    rbuf_.append(reinterpret_cast<const char*>(storage.data()), *read);
    co_return true;
}

void client::compact_buffer()
{
    if (rpos_ > 4096)
    {
        rbuf_.erase(0, rpos_);
        rpos_ = 0;
    }
}

cluster_client::cluster_client(io_context& context) noexcept
    : ctx_(context), seed_(context) {}

namespace {
    auto response_extent(const std::vector<resp3_node>& nodes,
        std::size_t root) noexcept -> std::size_t
    {
        if (root >= nodes.size())
            return 0;
        std::size_t extent = 1;
        if (!nodes[root].is_aggregate())
            return extent;
        const auto children = nodes[root].aggregate_size *
            element_multiplicity(nodes[root].data_type);
        auto child = root + 1;
        for (std::size_t index = 0; index < children; ++index)
        {
            const auto child_extent = response_extent(nodes, child);
            if (child_extent == 0)
                return 0;
            extent += child_extent;
            child += child_extent;
        }
        return extent;
    }

    auto split_pipeline_responses(std::vector<resp3_node> nodes,
        std::size_t expected)
        -> std::expected<std::vector<std::vector<resp3_node>>, std::error_code>
    {
        std::vector<std::vector<resp3_node>> responses;
        responses.reserve(expected);
        std::size_t cursor = 0;
        while (cursor < nodes.size())
        {
            const auto count = response_extent(nodes, cursor);
            if (count == 0 || count > nodes.size() - cursor)
                return std::unexpected(
                    std::make_error_code(std::errc::protocol_error));
            std::vector<resp3_node> response;
            response.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
                response.push_back(std::move(nodes[cursor + index]));
            responses.push_back(std::move(response));
            cursor += count;
        }
        if (responses.size() != expected)
            return std::unexpected(
                std::make_error_code(std::errc::protocol_error));
        return responses;
    }
} // namespace

auto cluster_client::connect(connect_options seed)
    -> task<std::expected<void, std::string>>
{
    seed_options_ = std::move(seed);
    auto connected = co_await seed_.connect(seed_options_);
    if (!connected)
        co_return connected;
    co_return co_await refresh_slots();
}

auto cluster_client::connect(connect_options seed, cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    if (seed.db != 0U)
        co_return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    close();
    seed_options_ = std::move(seed);
    auto connected = co_await seed_.connect(seed_options_, cancellation);
    if (!connected)
        co_return std::unexpected(connected.error());
    auto refreshed = co_await refresh_slots(cancellation);
    if (!refreshed)
        close();
    co_return refreshed;
}

auto cluster_client::refresh_slots() -> task<std::expected<void, std::string>>
{
    auto response = co_await seed_.cmd({"CLUSTER", "SLOTS"});
    if (!response)
        co_return std::unexpected(response.error());
    auto ranges = client::parse_cluster_slots(*response);
    if (!ranges)
        co_return std::unexpected(ranges.error());
    slot_cache_.update(*ranges);
    co_return std::expected<void, std::string>{};
}

auto cluster_client::refresh_slots(cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    request command;
    command.push("CLUSTER", "SLOTS");
    auto response = co_await seed_.exchange(command, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    if (has_error(*response))
        co_return std::unexpected(make_error_code(redis_errc::resp3_simple_error));
    auto ranges = client::parse_cluster_slots(*response);
    if (!ranges)
        co_return std::unexpected(
            std::make_error_code(std::errc::protocol_error));
    slot_cache_.update(*ranges);
    co_return {};
}

auto cluster_client::cmd_for_key(std::vector<std::string> args,
    std::string_view key, std::size_t limit)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    cancel_token cancellation;
    auto response = co_await cmd_for_key(
        std::move(args), key, cancellation, limit);
    if (!response)
        co_return std::unexpected(response.error().message());
    co_return std::move(*response);
}

auto cluster_client::cmd_for_keys(std::vector<std::string> args,
    std::span<const std::string_view> keys, cancel_token& cancellation,
    std::size_t max_redirects)
    -> task<std::expected<std::vector<resp3_node>, std::error_code>>
{
    if (!keys_share_slot(keys))
        co_return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    co_return co_await cmd_for_key(std::move(args), keys.front(),
        cancellation, max_redirects);
}

auto cluster_client::cmd_for_key(std::vector<std::string> args,
    std::string_view key, cancel_token& cancellation, std::size_t limit)
    -> task<std::expected<std::vector<resp3_node>, std::error_code>>
{
    co_await operation_mutex_.lock();
    async_lock_guard operation_guard(operation_mutex_, std::adopt_lock);
    if (cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(
            cancellation.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));
    if (args.empty())
        co_return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));

    request command;
    if (!command.push(args))
        co_return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    auto endpoint = slot_cache_.endpoint_for_slot(client::key_slot(key));
    for (std::size_t attempt = 0; attempt <= limit; ++attempt)
    {
        if (!endpoint)
        {
            auto refreshed = co_await refresh_slots(cancellation);
            if (!refreshed)
                co_return std::unexpected(refreshed.error());
            endpoint = slot_cache_.endpoint_for_slot(client::key_slot(key));
        }
        if (!endpoint)
            co_return std::unexpected(
                std::make_error_code(std::errc::host_unreachable));

        auto connection = co_await connection_for(*endpoint, cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());
        auto response = co_await (*connection)->exchange(command, cancellation);
        if (!response)
            co_return std::unexpected(response.error());
        auto redirect = client::parse_redirect(*response);
        if (!redirect)
            co_return response;
        if (attempt == limit)
            co_return std::unexpected(
                std::make_error_code(std::errc::too_many_symbolic_link_levels));

        if (redirect->kind == redirect_kind::moved)
        {
            slot_cache_.update_slot(redirect->slot, redirect->endpoint);
            endpoint = redirect->endpoint;
            continue;
        }

        auto asking_connection = co_await connection_for(
            redirect->endpoint, cancellation);
        if (!asking_connection)
            co_return std::unexpected(asking_connection.error());
        request asking;
        asking.push("ASKING");
        auto acknowledged = co_await (*asking_connection)->exchange(asking, cancellation);
        if (!acknowledged)
            co_return std::unexpected(acknowledged.error());
        if (has_error(*acknowledged) || !is_ok(*acknowledged))
            co_return std::unexpected(
                std::make_error_code(std::errc::protocol_error));
        auto response_after_asking = co_await (*asking_connection)->exchange(command, cancellation);
        if (!response_after_asking)
            co_return std::unexpected(response_after_asking.error());
        if (!client::parse_redirect(*response_after_asking))
            co_return response_after_asking;
        endpoint = redirect->endpoint;
    }
    co_return std::unexpected(std::make_error_code(std::errc::io_error));
}

auto cluster_client::pipeline(std::span<const cluster_pipeline_item> items)
    -> task<std::expected<std::vector<resp3_node>, std::string>>
{
    cancel_token cancellation;
    auto ordered = co_await pipeline_ordered(items, cancellation);
    if (!ordered)
        co_return std::unexpected(ordered.error().message());
    std::vector<resp3_node> result;
    for (auto& response : *ordered)
        for (auto& node : response)
            result.push_back(std::move(node));
    co_return result;
}

auto cluster_client::pipeline_ordered(
    std::span<const cluster_pipeline_item> items,
    cancel_token& cancellation)
    -> task<std::expected<std::vector<std::vector<resp3_node>>,
        std::error_code>>
{
    co_await operation_mutex_.lock();
    async_lock_guard operation_guard(operation_mutex_, std::adopt_lock);
    if (cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(
            cancellation.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));

    struct indexed_command
    {
        std::size_t index{};
        std::vector<std::string> args;
    };

    std::map<std::string, std::vector<indexed_command>> grouped;
    std::map<std::string, endpoint_info> endpoints;
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        const auto& item = items[index];
        if (item.args.empty())
            co_return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        auto endpoint = slot_cache_.endpoint_for_slot(client::key_slot(item.key));
        if (!endpoint)
        {
            auto refreshed = co_await refresh_slots(cancellation);
            if (!refreshed)
                co_return std::unexpected(refreshed.error());
            endpoint = slot_cache_.endpoint_for_slot(client::key_slot(item.key));
        }
        if (!endpoint)
            co_return std::unexpected(
                std::make_error_code(std::errc::host_unreachable));
        auto endpoint_name = endpoint_key(*endpoint);
        endpoints[endpoint_name] = *endpoint;
        grouped[endpoint_name].push_back(indexed_command{index, item.args});
    }

    std::vector<std::vector<resp3_node>> result(items.size());
    for (auto& [endpoint_name, commands] : grouped)
    {
        auto connection = co_await connection_for(
            endpoints[endpoint_name], cancellation);
        if (!connection)
            co_return std::unexpected(connection.error());

        request batch;
        for (const auto& command : commands)
            if (!batch.push(command.args))
                co_return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
        auto response = co_await (*connection)->exchange(batch, cancellation);
        if (!response)
            co_return std::unexpected(response.error());
        auto split = split_pipeline_responses(
            std::move(*response), commands.size());
        if (!split)
            co_return std::unexpected(split.error());
        for (std::size_t index = 0; index < commands.size(); ++index)
            result[commands[index].index] = std::move((*split)[index]);
    }
    co_return result;
}

void cluster_client::close() noexcept
{
    for (auto& [_, connection] : nodes_)
        connection->close();
    nodes_.clear();
    seed_.close();
    slot_cache_.clear();
}

auto cluster_client::slots() const noexcept -> const cluster_slot_cache&
{
    return slot_cache_;
}

auto cluster_client::endpoint_key(const endpoint_info& endpoint)
    -> std::string
{
    auto ipv6 = endpoint.host.find(':') != std::string::npos &&
        !(endpoint.host.starts_with("[") && endpoint.host.ends_with("]"));
    return (ipv6 ? "[" + endpoint.host + "]" : endpoint.host) + ":" +
        std::to_string(endpoint.port);
}

auto cluster_client::connection_for(const endpoint_info& endpoint)
    -> task<client*>
{
    auto key = endpoint_key(endpoint);
    auto existing = nodes_.find(key);
    if (existing != nodes_.end() && existing->second->is_open())
        co_return existing->second.get();
    auto options = seed_options_;
    options.host = endpoint.host;
    options.port = endpoint.port;
    auto connection = std::make_unique<client>(ctx_);
    auto connected = co_await connection->connect(options);
    if (!connected)
        co_return nullptr;
    auto* result = connection.get();
    nodes_[key] = std::move(connection);
    co_return result;
}

auto cluster_client::connection_for(const endpoint_info& endpoint,
    cancel_token& cancellation)
    -> task<std::expected<client*, std::error_code>>
{
    auto key = endpoint_key(endpoint);
    auto existing = nodes_.find(key);
    if (existing != nodes_.end() && existing->second->is_open())
        co_return existing->second.get();
    auto options = seed_options_;
    options.host = endpoint.host;
    options.port = endpoint.port;
    options.db = 0;
    auto connection = std::make_unique<client>(ctx_);
    auto connected = co_await connection->connect(options, cancellation);
    if (!connected)
        co_return std::unexpected(connected.error());
    auto* result = connection.get();
    nodes_[key] = std::move(connection);
    co_return result;
}

namespace detail {
    constexpr std::array<std::uint16_t, 256> crc16_table = []
    {
        std::array<std::uint16_t, 256> table{};
        for (std::uint16_t index{}; index < 256; ++index)
        {
            auto crc = static_cast<std::uint16_t>(index << 8);
            for (int bit{}; bit < 8; ++bit)
                crc = static_cast<std::uint16_t>((crc & 0x8000) ? ((crc << 1) ^ 0x1021)
                                                                : (crc << 1));
            table[index] = crc;
        }
        return table;
    }();

    auto crc16(std::string_view value) noexcept -> std::uint16_t
    {
        std::uint16_t crc{};
        for (unsigned char character : value)
            crc = static_cast<std::uint16_t>(
                (crc << 8) ^ crc16_table[((crc >> 8) ^ character) & 0xFF]);
        return crc;
    }

    auto cluster_hash_key(std::string_view key) noexcept -> std::string_view
    {
        auto open = key.find('{');
        if (open == std::string_view::npos)
            return key;
        auto close = key.find('}', open + 1);
        return close == std::string_view::npos || close == open + 1
            ? key
            : key.substr(open + 1, close - open - 1);
    }

    auto parse_u16(std::string_view text) -> std::optional<std::uint16_t>
    {
        std::uint16_t value{};
        auto [_, error] =
            std::from_chars(text.data(), text.data() + text.size(), value);
        return error == std::errc{} ? std::optional{value} : std::nullopt;
    }

    auto parse_cluster_endpoint(const std::vector<resp3_node>& nodes,
        std::size_t& index)
        -> std::expected<endpoint_info, std::string>
    {
        if (index >= nodes.size() || !nodes[index].is_aggregate())
            return std::unexpected(
                std::string("cluster slots endpoint is not an array"));
        auto fields = nodes[index++].aggregate_size;
        if (fields < 2 || index + 1 >= nodes.size())
            return std::unexpected(std::string("cluster slots endpoint is incomplete"));
        auto host = nodes[index++].value;
        auto port = parse_u16(nodes[index++].value);
        if (host.empty() || !port)
            return std::unexpected(
                std::string("cluster slots endpoint host/port invalid"));
        for (std::size_t skipped = 2; skipped < fields && index < nodes.size();
            ++skipped)
            ++index;
        return endpoint_info{.host = std::string(host), .port = *port};
    }
} // namespace detail

auto client::key_slot(std::string_view key) noexcept -> std::uint16_t
{
    return static_cast<std::uint16_t>(
        detail::crc16(detail::cluster_hash_key(key)) % 16384);
}

auto client::parse_redirect(const std::vector<resp3_node>& nodes)
    -> std::optional<cluster_redirect>
{
    auto message = error_message(nodes);
    if (message.empty())
        return std::nullopt;
    redirect_kind kind;
    std::string_view rest;
    if (message.starts_with("MOVED "))
    {
        kind = redirect_kind::moved;
        rest = message.substr(6);
    }
    else if (message.starts_with("ASK "))
    {
        kind = redirect_kind::ask;
        rest = message.substr(4);
    }
    else
        return std::nullopt;
    auto space = rest.find(' ');
    if (space == std::string_view::npos)
        return std::nullopt;
    std::uint16_t slot{};
    auto slot_text = rest.substr(0, space);
    auto [_, slot_error] = std::from_chars(
        slot_text.data(), slot_text.data() + slot_text.size(), slot);
    if (slot_error != std::errc{})
        return std::nullopt;
    auto address = rest.substr(space + 1);
    std::string_view host, port_text;
    if (address.starts_with('['))
    {
        auto bracket = address.find(']');
        if (bracket == std::string_view::npos || bracket + 1 >= address.size() ||
            address[bracket + 1] != ':')
            return std::nullopt;
        host = address.substr(1, bracket - 1);
        port_text = address.substr(bracket + 2);
    }
    else
    {
        auto colon = address.rfind(':');
        if (colon == std::string_view::npos)
            return std::nullopt;
        host = address.substr(0, colon);
        port_text = address.substr(colon + 1);
    }
    auto port = detail::parse_u16(port_text);
    if (host.empty() || !port)
        return std::nullopt;
    return cluster_redirect{
        .kind = kind,
        .slot = slot,
        .endpoint = {.host = std::string(host), .port = *port}};
}

auto client::parse_cluster_slots(const std::vector<resp3_node>& nodes)
    -> std::expected<std::vector<cluster_slot_range>, std::string>
{
    if (nodes.empty() || !nodes.front().is_aggregate())
        return std::unexpected(
            std::string("CLUSTER SLOTS response is not an array"));
    std::vector<cluster_slot_range> ranges;
    ranges.reserve(nodes.front().aggregate_size);
    std::size_t index = 1;
    for (std::size_t range_index{}; range_index < nodes.front().aggregate_size;
        ++range_index)
    {
        if (index >= nodes.size() || !nodes[index].is_aggregate())
            return std::unexpected(
                std::string("CLUSTER SLOTS range is not an array"));
        auto fields = nodes[index++].aggregate_size;
        if (fields < 3 || index + 2 >= nodes.size())
            return std::unexpected(std::string("CLUSTER SLOTS range is incomplete"));
        auto start = detail::parse_u16(nodes[index++].value);
        auto end = detail::parse_u16(nodes[index++].value);
        if (!start || !end || *start > *end || *end > 16383)
            return std::unexpected(std::string("CLUSTER SLOTS range bounds invalid"));
        auto master = detail::parse_cluster_endpoint(nodes, index);
        if (!master)
            return std::unexpected(master.error());
        cluster_slot_range range{
            .start = *start,
            .end = *end,
            .master = *master,
            .replicas = {}};
        for (std::size_t field = 3; field < fields && index < nodes.size();
            ++field)
        {
            auto replica = detail::parse_cluster_endpoint(nodes, index);
            if (!replica)
                return std::unexpected(replica.error());
            range.replicas.push_back(*replica);
        }
        ranges.push_back(std::move(range));
    }
    return ranges;
}
} // namespace cnetmod::redis
