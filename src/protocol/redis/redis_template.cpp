module cnetmod.protocol.redis;

import std;
import cnetmod.instrumentation.error;
import cnetmod.instrumentation.operation_scope;
import :redis_template;

namespace cnetmod::redis {

namespace {

    [[nodiscard]] auto invalid_argument() -> std::error_code
    {
        return std::make_error_code(std::errc::invalid_argument);
    }

    [[nodiscard]] auto protocol_error() -> std::error_code
    {
        return std::make_error_code(std::errc::protocol_error);
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
    instrumentation::span_exporter spans)
    : pool_(pool), options_(std::move(options)), parent_(std::move(parent)), spans_(std::move(spans))
{
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
