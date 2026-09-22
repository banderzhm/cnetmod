module cnetmod.protocol.redis;

import std;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.operation_scope;
import cnetmod.coro.timer;
import :redis_template;

namespace cnetmod::redis {

namespace {

    constexpr std::string_view acquire_lock_script =
        "local acquired=redis.call('SET',KEYS[1],ARGV[1],'NX','PX',ARGV[2]);" "if acquired then return redis.call('INCR',KEYS[2]);end;return 0";
    constexpr std::string_view renew_lock_script =
        "if redis.call('GET',KEYS[1])==ARGV[1] then " "return redis.call('PEXPIRE',KEYS[1],ARGV[2]);end;return 0";
    constexpr std::string_view release_lock_script =
        "if redis.call('GET',KEYS[1])==ARGV[1] then " "return redis.call('DEL',KEYS[1]);end;return 0";

    [[nodiscard]] auto invalid_argument() -> std::error_code
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    [[nodiscard]] auto protocol_error() -> std::error_code
    {
        return std::make_error_code(std::errc::protocol_error);
    }

    [[nodiscard]] auto make_owner_token()
        -> std::expected<std::string, std::error_code>
    {
        try
        {
            std::random_device random;
            constexpr char digits[] = "0123456789abcdef";
            std::array<std::uint32_t, 4> words{};
            for (auto& word : words)
                word = random();
            std::string token(32U, '0');
            auto offset = std::size_t{};
            for (const auto word : words)
                for (auto shift = 28; shift >= 0; shift -= 4)
                    token[offset++] = digits[(word >> shift) & 0x0fU];
            return token;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(
                std::make_error_code(std::errc::not_enough_memory));
        }
        catch (...)
        {
            return std::unexpected(
                std::make_error_code(std::errc::io_error));
        }
    }

    [[nodiscard]] auto valid_lock_options(
        const distributed_lock_options& options) noexcept -> bool
    {
        return options.lease > std::chrono::milliseconds::zero() &&
            options.wait_timeout >= std::chrono::milliseconds::zero() &&
            options.retry_interval > std::chrono::milliseconds::zero() &&
            std::isfinite(options.retry_jitter) &&
            options.retry_jitter >= 0.0 && options.retry_jitter <= 1.0;
    }

    [[nodiscard]] auto safe_operation_name(std::string_view value)
        -> std::string
    {
        if (value.empty() || value.size() > 32U ||
            !std::ranges::all_of(value, [](unsigned char character)
                {
                    return (character >= 'A' && character <= 'Z') ||
                        (character >= '0' && character <= '9') ||
                        character == '_';
                }))
            return "UNKNOWN";
        return std::string{value};
    }

    [[nodiscard]] auto server_error(const reply& value) -> std::error_code
    {
        const auto found = std::ranges::find_if(value,
            [](const resp3_node& node)
            {
                return node.is_error();
            });
        if (found == value.end())
            return {};
        return make_error_code(found->data_type == resp3_type::blob_error
                ? redis_errc::resp3_blob_error
                : redis_errc::resp3_simple_error);
    }

    [[nodiscard]] auto split_replies(std::vector<resp3_node> nodes,
        std::size_t expected)
        -> std::expected<std::vector<pipeline_reply>, std::error_code>
    {
        std::vector<pipeline_reply> result;
        result.reserve(expected);
        std::size_t begin = 0;
        while (begin < nodes.size())
        {
            if (nodes[begin].depth != 0U)
                return std::unexpected(protocol_error());
            auto end = begin + 1U;
            while (end < nodes.size() && nodes[end].depth != 0U)
                ++end;
            reply current;
            current.reserve(end - begin);
            for (auto index = begin; index < end; ++index)
                current.push_back(std::move(nodes[index]));
            if (auto error = server_error(current); error)
                result.emplace_back(std::unexpected(error));
            else
                result.emplace_back(std::move(current));
            begin = end;
        }
        if (result.size() != expected)
            return std::unexpected(protocol_error());
        return result;
    }

    [[nodiscard]] auto scalar(const reply& value)
        -> std::expected<const resp3_node*, std::error_code>
    {
        if (value.size() != 1U || value.front().is_aggregate())
            return std::unexpected(protocol_error());
        return &value.front();
    }

    [[nodiscard]] auto integer(const reply& value)
        -> std::expected<std::int64_t, std::error_code>
    {
        auto node = scalar(value);
        if (!node || (*node)->data_type != resp3_type::number)
            return std::unexpected(protocol_error());
        std::int64_t parsed{};
        const auto text = std::string_view{(*node)->value};
        const auto [last, error] = std::from_chars(
            text.data(), text.data() + text.size(), parsed);
        if (error != std::errc{} || last != text.data() + text.size())
            return std::unexpected(make_error_code(redis_errc::not_a_number));
        return parsed;
    }

    [[nodiscard]] auto optional_string(const reply& value)
        -> std::expected<std::optional<std::string>, std::error_code>
    {
        auto node = scalar(value);
        if (!node)
            return std::unexpected(node.error());
        if ((*node)->is_null())
            return std::optional<std::string>{};
        if ((*node)->data_type != resp3_type::blob_string &&
            (*node)->data_type != resp3_type::simple_string &&
            (*node)->data_type != resp3_type::verbatim_string)
            return std::unexpected(protocol_error());
        return std::optional<std::string>{(*node)->value};
    }

    [[nodiscard]] auto direct_children(const reply& value)
        -> std::expected<std::vector<const resp3_node*>, std::error_code>
    {
        if (value.empty() || !value.front().is_aggregate())
            return std::unexpected(protocol_error());
        std::vector<const resp3_node*> result;
        for (std::size_t index = 1; index < value.size(); ++index)
            if (value[index].depth == value.front().depth + 1U)
                result.push_back(&value[index]);
        const auto expected = value.front().aggregate_size *
            element_multiplicity(value.front().data_type);
        if (result.size() != expected)
            return std::unexpected(protocol_error());
        return result;
    }

    void complete_scopes(
        std::vector<instrumentation::operation_scope>& scopes,
        const std::vector<pipeline_reply>* replies, std::error_code error = {}) noexcept
    {
        for (std::size_t index = 0; index < scopes.size(); ++index)
        {
            if (error)
                scopes[index].complete(instrumentation::classify_error(error));
            else
                scopes[index].complete({replies && index < replies->size() &&
                            (*replies)[index]
                        ? instrumentation::operation_status::success
                        : instrumentation::operation_status::error,
                    {}});
        }
    }

} // namespace

distributed_lock::distributed_lock(redis_template& owner,
    std::string physical_key, std::string owner_token,
    std::uint64_t fencing_token) noexcept
    : owner_(&owner), physical_key_(std::move(physical_key)), owner_token_(std::move(owner_token)), fencing_token_(fencing_token), owned_(true)
{
}

distributed_lock::distributed_lock(distributed_lock&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      physical_key_(std::move(other.physical_key_)),
      owner_token_(std::move(other.owner_token_)),
      fencing_token_(std::exchange(other.fencing_token_, 0)),
      owned_(std::exchange(other.owned_, false))
{
}

auto distributed_lock::owns_lock() const noexcept -> bool
{
    return owned_;
}

auto distributed_lock::fencing_token() const noexcept -> std::uint64_t
{
    return fencing_token_;
}

auto distributed_lock::renew(std::chrono::milliseconds lease)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await renew(lease, cancellation);
}

auto distributed_lock::renew(std::chrono::milliseconds lease,
    cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    if (!owned_ || !owner_)
        co_return false;
    auto renewed = co_await owner_->renew_lock(
        physical_key_, owner_token_, lease, cancellation);
    if (renewed && !*renewed)
        owned_ = false;
    co_return renewed;
}

auto distributed_lock::release()
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await release(cancellation);
}

auto distributed_lock::release(cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    if (!owned_ || !owner_)
        co_return false;
    auto released = co_await owner_->release_lock(
        physical_key_, owner_token_, cancellation);
    if (released)
        owned_ = false;
    co_return released;
}

auto key_namespace::key(std::string_view suffix) const -> std::string
{
    std::string result;
    result.reserve(prefix.size() + suffix.size());
    result.append(prefix);
    result.append(suffix);
    return result;
}

pipeline_builder::pipeline_builder(key_namespace keys, ttl_seconds default_ttl)
    : namespace_(std::move(keys)), default_ttl_(default_ttl)
{
}

auto pipeline_builder::physical_key(std::string_view key) const -> std::string
{
    return namespace_.key(key);
}

auto pipeline_builder::effective_ttl(ttl_seconds ttl) const noexcept
    -> ttl_seconds
{
    return ttl == ttl_seconds::zero() ? default_ttl_ : ttl;
}

auto pipeline_builder::append(std::vector<std::string> arguments)
    -> pipeline_builder&
{
    if (batch_.push(std::span<const std::string>{arguments}))
        operations_.push_back(safe_operation_name(arguments.front()));
    return *this;
}

auto pipeline_builder::raw_command(std::span<const std::string> arguments)
    -> pipeline_builder&
{
    if (batch_.push(arguments))
        operations_.push_back(safe_operation_name(arguments.front()));
    return *this;
}

auto pipeline_builder::get(std::string_view key) -> pipeline_builder&
{
    return append({"GET", physical_key(key)});
}

auto pipeline_builder::set(std::string_view key, std::string_view value,
    ttl_seconds ttl) -> pipeline_builder&
{
    auto resolved = effective_ttl(ttl);
    if (resolved < ttl_seconds::zero())
    {
        valid_ = false;
        return *this;
    }
    std::vector<std::string> arguments{"SET", physical_key(key),
        std::string{value}};
    if (resolved > ttl_seconds::zero())
    {
        arguments.emplace_back("EX");
        arguments.push_back(std::to_string(resolved.count()));
    }
    return append(std::move(arguments));
}

auto pipeline_builder::del(std::string_view key) -> pipeline_builder&
{
    return append({"DEL", physical_key(key)});
}

auto pipeline_builder::exists(std::string_view key) -> pipeline_builder&
{
    return append({"EXISTS", physical_key(key)});
}

auto pipeline_builder::incr(std::string_view key, std::int64_t by)
    -> pipeline_builder&
{
    return append({"INCRBY", physical_key(key), std::to_string(by)});
}

auto pipeline_builder::expire(std::string_view key, ttl_seconds ttl)
    -> pipeline_builder&
{
    if (ttl < ttl_seconds::zero())
    {
        valid_ = false;
        return *this;
    }
    return append({"EXPIRE", physical_key(key), std::to_string(ttl.count())});
}

auto pipeline_builder::hset(std::string_view key, std::string_view field,
    std::string_view value, ttl_seconds ttl) -> pipeline_builder&
{
    const auto physical = physical_key(key);
    if (effective_ttl(ttl) < ttl_seconds::zero())
    {
        valid_ = false;
        return *this;
    }
    append({"HSET", physical, std::string{field}, std::string{value}});
    const auto resolved = effective_ttl(ttl);
    if (resolved > ttl_seconds::zero())
        append({"EXPIRE", physical, std::to_string(resolved.count())});
    return *this;
}

auto pipeline_builder::hdel(std::string_view key, std::string_view field)
    -> pipeline_builder&
{
    return append({"HDEL", physical_key(key), std::string{field}});
}

auto pipeline_builder::sadd(std::string_view key, std::string_view member,
    ttl_seconds ttl) -> pipeline_builder&
{
    const auto physical = physical_key(key);
    if (effective_ttl(ttl) < ttl_seconds::zero())
    {
        valid_ = false;
        return *this;
    }
    append({"SADD", physical, std::string{member}});
    const auto resolved = effective_ttl(ttl);
    if (resolved > ttl_seconds::zero())
        append({"EXPIRE", physical, std::to_string(resolved.count())});
    return *this;
}

auto pipeline_builder::srem(std::string_view key, std::string_view member)
    -> pipeline_builder&
{
    return append({"SREM", physical_key(key), std::string{member}});
}

auto pipeline_builder::sismember(std::string_view key,
    std::string_view member) -> pipeline_builder&
{
    return append({"SISMEMBER", physical_key(key), std::string{member}});
}

void pipeline_builder::clear()
{
    batch_.clear();
    operations_.clear();
    valid_ = true;
}

auto pipeline_builder::empty() const noexcept -> bool
{
    return batch_.empty();
}

auto pipeline_builder::size() const noexcept -> std::size_t
{
    return batch_.size();
}

redis_template::redis_template(connection_pool& pool, template_options options,
    instrumentation::trace_context parent,
    instrumentation::span_exporter spans, io_context* timer_context)
    : pool_(pool), options_(std::move(options)), parent_(std::move(parent)), spans_(std::move(spans)), timer_context_(timer_context)
{
}

auto redis_template::try_lock(std::string_view key,
    std::chrono::milliseconds lease)
    -> task<std::expected<std::optional<distributed_lock>, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await try_lock(key, lease, cancellation);
}

auto redis_template::try_lock(std::string_view key,
    std::chrono::milliseconds lease, cancel_token& cancellation)
    -> task<std::expected<std::optional<distributed_lock>, std::error_code>>
{
    if (key.empty() || lease <= std::chrono::milliseconds::zero())
        co_return std::unexpected(invalid_argument());
    auto token = make_owner_token();
    if (!token)
        co_return std::unexpected(token.error());
    auto physical = physical_key(key);
    auto response = co_await execute_one({"EVAL",
                                             std::string{acquire_lock_script}, "2", physical, physical + ":fence",
                                             *token, std::to_string(lease.count())},
        cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto fence = integer(*response);
    if (!fence)
        co_return std::unexpected(fence.error());
    if (*fence == 0)
        co_return std::optional<distributed_lock>{};
    if (*fence < 0)
        co_return std::unexpected(protocol_error());
    distributed_lock acquired{*this, std::move(physical),
        std::move(*token), static_cast<std::uint64_t>(*fence)};
    co_return std::optional<distributed_lock>{std::move(acquired)};
}

auto redis_template::lock(std::string_view key,
    distributed_lock_options options)
    -> task<std::expected<distributed_lock, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await lock(key, options, cancellation);
}

auto redis_template::lock(std::string_view key,
    distributed_lock_options options, cancel_token& cancellation)
    -> task<std::expected<distributed_lock, std::error_code>>
{
    if (key.empty() || !valid_lock_options(options))
        co_return std::unexpected(invalid_argument());
    if (options.wait_timeout > std::chrono::milliseconds::zero() &&
        !timer_context_)
        co_return std::unexpected(
            std::make_error_code(std::errc::operation_not_supported));

    const auto expires = std::chrono::steady_clock::now() +
        options.wait_timeout;
    auto seed = static_cast<std::uint64_t>(
                    std::chrono::steady_clock::now().time_since_epoch().count()) ^
        static_cast<std::uint64_t>(std::hash<std::string_view>{}(key));
    std::mt19937_64 generator{seed};
    std::uniform_real_distribution<double> jitter{
        -options.retry_jitter, options.retry_jitter};

    while (true)
    {
        auto acquired = co_await try_lock(key, options.lease, cancellation);
        if (!acquired)
            co_return std::unexpected(acquired.error());
        if (*acquired)
            co_return std::move(**acquired);
        if (options.wait_timeout == std::chrono::milliseconds::zero() ||
            std::chrono::steady_clock::now() >= expires)
            co_return std::unexpected(
                std::make_error_code(std::errc::timed_out));

        const auto remaining = std::chrono::duration_cast<
            std::chrono::milliseconds>(expires - std::chrono::steady_clock::now());
        const auto factor = 1.0 + jitter(generator);
        auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(
            options.retry_interval * factor);
        delay = std::max(std::chrono::milliseconds{1},
            std::min(delay, remaining));
        auto waited = co_await async_timer_wait(
            *timer_context_, delay, cancellation);
        if (!waited)
            co_return std::unexpected(waited.error());
    }
}

auto redis_template::renew_lock(std::string_view physical_key,
    std::string_view owner_token, std::chrono::milliseconds lease,
    cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    if (lease <= std::chrono::milliseconds::zero())
        co_return std::unexpected(invalid_argument());
    auto response = co_await execute_one({"EVAL",
                                             std::string{renew_lock_script}, "1", std::string{physical_key},
                                             std::string{owner_token}, std::to_string(lease.count())},
        cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto result = integer(*response);
    if (!result)
        co_return std::unexpected(result.error());
    co_return *result != 0;
}

auto redis_template::release_lock(std::string_view physical_key,
    std::string_view owner_token, cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    auto response = co_await execute_one({"EVAL",
                                             std::string{release_lock_script}, "1", std::string{physical_key},
                                             std::string{owner_token}},
        cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto result = integer(*response);
    if (!result)
        co_return std::unexpected(result.error());
    co_return *result != 0;
}

auto redis_template::physical_key(std::string_view key) const -> std::string
{
    return options_.ns.key(key);
}

auto redis_template::effective_ttl(ttl_seconds ttl) const noexcept
    -> ttl_seconds
{
    if (ttl != ttl_seconds::zero())
        return ttl;
    if (options_.default_ttl <= std::chrono::steady_clock::duration::zero())
        return {};
    auto result = std::chrono::duration_cast<ttl_seconds>(options_.default_ttl);
    return result == ttl_seconds::zero() ? ttl_seconds{1} : result;
}

auto redis_template::execute_one(std::vector<std::string> arguments,
    cancel_token& cancellation)
    -> task<std::expected<reply, std::error_code>>
{
    pipeline_builder builder;
    builder.raw_command(std::span<const std::string>{arguments});
    auto result = co_await execute(builder, cancellation);
    if (!result)
        co_return std::unexpected(result.error());
    if (result->size() != 1U)
        co_return std::unexpected(protocol_error());
    if (!result->front())
        co_return std::unexpected(result->front().error());
    co_return std::move(*result->front());
}

auto redis_template::pipeline() const -> pipeline_builder
{
    return {options_.ns, effective_ttl({})};
}

auto redis_template::execute(pipeline_builder& builder)
    -> task<std::expected<std::vector<pipeline_reply>, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await execute(builder, cancellation);
}

auto redis_template::execute(pipeline_builder& builder,
    cancel_token& cancellation)
    -> task<std::expected<std::vector<pipeline_reply>, std::error_code>>
{
    if (!builder.valid_ || builder.empty() ||
        builder.operations_.size() != builder.size() ||
        options_.response_byte_limit == 0U)
        co_return std::unexpected(invalid_argument());

    std::vector<instrumentation::operation_scope> scopes;
    if (spans_)
    {
        try
        {
            scopes.reserve(builder.operations_.size());
            for (const auto& command : builder.operations_)
            {
                auto scope = instrumentation::operation_scope::start(spans_, [&]
                    {
                        return instrumentation::start_client_span(
                            parent_, "REDIS " + command);
                    });
                scope.annotate([&]
                    {
                        return std::vector<std::pair<std::string, std::string>>{
                            {"db.system.name", "redis"},
                            {"db.operation.name", command}};
                    });
                scopes.push_back(std::move(scope));
            }
        }
        catch (...)
        {
            scopes.clear();
        }
    }

    auto lease = co_await pool_.async_get_connection(cancellation);
    if (!lease)
    {
        complete_scopes(scopes, nullptr, lease.error());
        co_return std::unexpected(lease.error());
    }
    auto exchanged = co_await lease->get().exchange(builder.batch_, cancellation,
        options_.response_byte_limit);
    if (!exchanged)
    {
        complete_scopes(scopes, nullptr, exchanged.error());
        co_return std::unexpected(exchanged.error());
    }
    auto replies = split_replies(std::move(*exchanged), builder.size());
    if (!replies)
    {
        lease->get().close();
        complete_scopes(scopes, nullptr, replies.error());
        co_return std::unexpected(replies.error());
    }
    complete_scopes(scopes, &*replies);
    co_return replies;
}

auto redis_template::get(std::string_view key)
    -> task<std::expected<std::optional<std::string>, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await get(key, cancellation);
}

auto redis_template::get(std::string_view key, cancel_token& cancellation)
    -> task<std::expected<std::optional<std::string>, std::error_code>>
{
    auto response = co_await execute_one(
        {"GET", physical_key(key)}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    co_return optional_string(*response);
}

auto redis_template::set(std::string_view key, std::string_view value,
    ttl_seconds ttl) -> task<std::expected<void, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await set(key, value, ttl, cancellation);
}

auto redis_template::set(std::string_view key, std::string_view value,
    ttl_seconds ttl, cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    const auto resolved = effective_ttl(ttl);
    if (resolved < ttl_seconds::zero())
        co_return std::unexpected(invalid_argument());
    std::vector<std::string> arguments{"SET", physical_key(key),
        std::string{value}};
    if (resolved > ttl_seconds::zero())
    {
        arguments.emplace_back("EX");
        arguments.push_back(std::to_string(resolved.count()));
    }
    auto response = co_await execute_one(std::move(arguments), cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto value_node = scalar(*response);
    if (!value_node || (*value_node)->value != "OK")
        co_return std::unexpected(protocol_error());
    co_return {};
}

auto redis_template::del(std::string_view key)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await del(key, cancellation);
}

auto redis_template::del(std::string_view key, cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    auto response = co_await execute_one({"DEL", physical_key(key)}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto count = integer(*response);
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::exists(std::string_view key)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await exists(key, cancellation);
}

auto redis_template::exists(std::string_view key, cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    auto response = co_await execute_one(
        {"EXISTS", physical_key(key)}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto count = integer(*response);
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::incr(std::string_view key, std::int64_t by)
    -> task<std::expected<std::int64_t, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await incr(key, by, cancellation);
}

auto redis_template::incr(std::string_view key, std::int64_t by,
    cancel_token& cancellation)
    -> task<std::expected<std::int64_t, std::error_code>>
{
    auto response = co_await execute_one(
        {"INCRBY", physical_key(key), std::to_string(by)}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    co_return integer(*response);
}

auto redis_template::expire(std::string_view key, ttl_seconds ttl)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await expire(key, ttl, cancellation);
}

auto redis_template::expire(std::string_view key, ttl_seconds ttl,
    cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    if (ttl < ttl_seconds::zero())
        co_return std::unexpected(invalid_argument());
    auto response = co_await execute_one({"EXPIRE", physical_key(key),
                                             std::to_string(ttl.count())},
        cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto count = integer(*response);
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::pttl(std::string_view key)
    -> task<std::expected<std::optional<std::chrono::milliseconds>,
        std::error_code>>
{
    cancel_token cancellation;
    co_return co_await pttl(key, cancellation);
}

auto redis_template::pttl(std::string_view key, cancel_token& cancellation)
    -> task<std::expected<std::optional<std::chrono::milliseconds>,
        std::error_code>>
{
    auto response = co_await execute_one({"PTTL", physical_key(key)}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto value = integer(*response);
    if (!value)
        co_return std::unexpected(value.error());
    if (*value == -2)
        co_return std::optional<std::chrono::milliseconds>{};
    if (*value < -2)
        co_return std::unexpected(protocol_error());
    co_return std::optional{std::chrono::milliseconds{*value}};
}

auto redis_template::mget(std::span<const std::string> keys)
    -> task<std::expected<std::vector<std::optional<std::string>>,
        std::error_code>>
{
    cancel_token cancellation;
    co_return co_await mget(keys, cancellation);
}

auto redis_template::mget(std::span<const std::string> keys,
    cancel_token& cancellation)
    -> task<std::expected<std::vector<std::optional<std::string>>,
        std::error_code>>
{
    if (keys.empty())
        co_return std::vector<std::optional<std::string>>{};
    std::vector<std::string> arguments;
    arguments.reserve(keys.size() + 1U);
    arguments.emplace_back("MGET");
    for (const auto& key : keys)
        arguments.push_back(physical_key(key));
    auto response = co_await execute_one(std::move(arguments), cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto children = direct_children(*response);
    if (!children || children->size() != keys.size())
        co_return std::unexpected(children ? protocol_error() : children.error());
    std::vector<std::optional<std::string>> result;
    result.reserve(keys.size());
    for (const auto* node : *children)
    {
        if (node->is_null())
            result.emplace_back(std::nullopt);
        else if (node->data_type == resp3_type::blob_string ||
            node->data_type == resp3_type::simple_string)
            result.emplace_back(node->value);
        else
            co_return std::unexpected(protocol_error());
    }
    co_return result;
}

auto redis_template::hset(std::string_view key, std::string_view field,
    std::string_view value, ttl_seconds ttl)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await hset(key, field, value, ttl, cancellation);
}

auto redis_template::hset(std::string_view key, std::string_view field,
    std::string_view value, ttl_seconds ttl, cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    const auto resolved = effective_ttl(ttl);
    if (resolved < ttl_seconds::zero())
        co_return std::unexpected(invalid_argument());
    pipeline_builder builder;
    const auto physical = physical_key(key);
    std::vector<std::string> command{"HSET", physical, std::string{field},
        std::string{value}};
    builder.raw_command(command);
    if (resolved > ttl_seconds::zero())
    {
        command = {"EXPIRE", physical, std::to_string(resolved.count())};
        builder.raw_command(command);
    }
    auto responses = co_await execute(builder, cancellation);
    if (!responses)
        co_return std::unexpected(responses.error());
    for (const auto& response : *responses)
        if (!response)
            co_return std::unexpected(response.error());
    if (responses->size() > 1U)
    {
        auto ttl_applied = integer(*responses->at(1));
        if (!ttl_applied || *ttl_applied == 0)
            co_return std::unexpected(ttl_applied ? protocol_error()
                                                  : ttl_applied.error());
    }
    auto count = integer(*responses->front());
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::hget(std::string_view key, std::string_view field)
    -> task<std::expected<std::optional<std::string>, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await hget(key, field, cancellation);
}

auto redis_template::hget(std::string_view key, std::string_view field,
    cancel_token& cancellation)
    -> task<std::expected<std::optional<std::string>, std::error_code>>
{
    auto response = co_await execute_one(
        {"HGET", physical_key(key), std::string{field}}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    co_return optional_string(*response);
}

auto redis_template::hgetall(std::string_view key)
    -> task<std::expected<std::vector<std::pair<std::string, std::string>>,
        std::error_code>>
{
    cancel_token cancellation;
    co_return co_await hgetall(key, cancellation);
}

auto redis_template::hgetall(std::string_view key,
    cancel_token& cancellation)
    -> task<std::expected<std::vector<std::pair<std::string, std::string>>,
        std::error_code>>
{
    auto response = co_await execute_one(
        {"HGETALL", physical_key(key)}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto children = direct_children(*response);
    if (!children || children->size() % 2U != 0U)
        co_return std::unexpected(children ? protocol_error() : children.error());
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(children->size() / 2U);
    for (std::size_t index = 0; index < children->size(); index += 2U)
    {
        const auto* field = (*children)[index];
        const auto* value = (*children)[index + 1U];
        if (field->is_aggregate() || value->is_aggregate() || field->is_null() ||
            value->is_null())
            co_return std::unexpected(protocol_error());
        result.emplace_back(field->value, value->value);
    }
    co_return result;
}

auto redis_template::hdel(std::string_view key, std::string_view field)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await hdel(key, field, cancellation);
}

auto redis_template::hdel(std::string_view key, std::string_view field,
    cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    auto response = co_await execute_one(
        {"HDEL", physical_key(key), std::string{field}}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto count = integer(*response);
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::sadd(std::string_view key, std::string_view member,
    ttl_seconds ttl) -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await sadd(key, member, ttl, cancellation);
}

auto redis_template::sadd(std::string_view key, std::string_view member,
    ttl_seconds ttl, cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    const auto resolved = effective_ttl(ttl);
    if (resolved < ttl_seconds::zero())
        co_return std::unexpected(invalid_argument());
    pipeline_builder builder;
    const auto physical = physical_key(key);
    std::vector<std::string> command{"SADD", physical, std::string{member}};
    builder.raw_command(command);
    if (resolved > ttl_seconds::zero())
    {
        command = {"EXPIRE", physical, std::to_string(resolved.count())};
        builder.raw_command(command);
    }
    auto responses = co_await execute(builder, cancellation);
    if (!responses)
        co_return std::unexpected(responses.error());
    for (const auto& response : *responses)
        if (!response)
            co_return std::unexpected(response.error());
    if (responses->size() > 1U)
    {
        auto ttl_applied = integer(*responses->at(1));
        if (!ttl_applied || *ttl_applied == 0)
            co_return std::unexpected(ttl_applied ? protocol_error()
                                                  : ttl_applied.error());
    }
    auto count = integer(*responses->front());
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::srem(std::string_view key, std::string_view member)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await srem(key, member, cancellation);
}

auto redis_template::srem(std::string_view key, std::string_view member,
    cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    auto response = co_await execute_one(
        {"SREM", physical_key(key), std::string{member}}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto count = integer(*response);
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::sismember(std::string_view key, std::string_view member)
    -> task<std::expected<bool, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await sismember(key, member, cancellation);
}

auto redis_template::sismember(std::string_view key, std::string_view member,
    cancel_token& cancellation)
    -> task<std::expected<bool, std::error_code>>
{
    auto response = co_await execute_one(
        {"SISMEMBER", physical_key(key), std::string{member}}, cancellation);
    if (!response)
        co_return std::unexpected(response.error());
    auto count = integer(*response);
    if (!count)
        co_return std::unexpected(count.error());
    co_return *count != 0;
}

auto redis_template::sscan_all(std::string_view key)
    -> task<std::expected<std::vector<std::string>, std::error_code>>
{
    cancel_token cancellation;
    co_return co_await sscan_all(key, cancellation);
}

auto redis_template::sscan_all(std::string_view key,
    cancel_token& cancellation)
    -> task<std::expected<std::vector<std::string>, std::error_code>>
{
    if (options_.scan_page == 0U || options_.scan_limit == 0U)
        co_return std::unexpected(invalid_argument());
    const auto physical = physical_key(key);
    std::string cursor{"0"};
    std::vector<std::string> result;
    std::unordered_set<std::string> seen;
    do
    {
        if (cancellation.is_cancelled())
            co_return std::unexpected(std::make_error_code(
                cancellation.reason() == cancellation_reason::deadline_exceeded
                    ? std::errc::timed_out
                    : std::errc::operation_canceled));
        auto response = co_await execute_one({"SSCAN", physical, cursor, "COUNT",
                                                 std::to_string(options_.scan_page)},
            cancellation);
        if (!response)
            co_return std::unexpected(response.error());
        if (response->size() < 3U || !response->front().is_aggregate() ||
            response->front().aggregate_size != 2U ||
            (*response)[1].depth != 1U || !(*response)[2].is_aggregate() ||
            (*response)[2].depth != 1U)
            co_return std::unexpected(protocol_error());
        cursor = (*response)[1].value;
        const auto members = (*response)[2].aggregate_size;
        std::size_t consumed = 0;
        for (std::size_t index = 3U; index < response->size(); ++index)
        {
            const auto& node = (*response)[index];
            if (node.depth != 2U || node.is_aggregate() || node.is_null())
                co_return std::unexpected(protocol_error());
            ++consumed;
            if (seen.insert(node.value).second)
            {
                if (result.size() == options_.scan_limit)
                    co_return std::unexpected(
                        std::make_error_code(std::errc::value_too_large));
                result.push_back(node.value);
            }
        }
        if (consumed != members)
            co_return std::unexpected(protocol_error());
    } while (cursor != "0");
    co_return result;
}

} // namespace cnetmod::redis
