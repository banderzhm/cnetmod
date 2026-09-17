/// cnetmod.protocol.openai:memory — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :messages;
import :memory;

namespace cnetmod::openai {

namespace {
    auto message_tokens(const message& value, const memory_options& options)
        -> std::size_t
    {
        if (options.count_tokens)
            return options.count_tokens(value);
        std::size_t characters = value.role.size() + value.content.size() + value.name.size();
        for (const auto& part : value.content_parts)
            characters += part.text.size() + part.image_url.url.size();
        for (const auto& call : value.tool_calls)
            characters += call.function.name.size() + call.function.arguments.size();
        return std::max<std::size_t>(1, (characters + 3) / 4);
    }
} // namespace

void trim_messages(std::vector<message>& messages,
    const memory_options& options)
{
    const auto total_tokens = [&messages, &options]
    {
        std::size_t total = 0;
        for (const auto& value : messages)
            total += message_tokens(value, options);
        return total;
    };
    const auto over_limit = [&]
    {
        return (options.max_messages > 0 &&
                   messages.size() > options.max_messages) ||
            (options.max_tokens > 0 && total_tokens() > options.max_tokens);
    };

    while (!messages.empty() && over_limit())
    {
        auto removable = std::ranges::find_if(messages,
            [&options](const message& item)
            {
                return !options.preserve_system_messages ||
                    (item.role != "system" && item.role != "developer");
            });
        if (removable == messages.end())
            removable = messages.begin();

        if (options.preserve_tool_exchanges && !removable->tool_calls.empty())
        {
            auto next = std::next(removable);
            while (next != messages.end() && next->role == "tool")
                next = messages.erase(next);
        }
        messages.erase(removable);
    }
}

auto in_memory_chat_memory_store::load(std::string session_id)
    -> task<std::expected<std::vector<message>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = sessions_.find(session_id);
    if (found == sessions_.end())
        co_return std::vector<message>{};
    co_return found->second;
}

auto in_memory_chat_memory_store::save(std::string session_id,
    std::vector<message> messages)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    sessions_.insert_or_assign(std::move(session_id), std::move(messages));
    co_return std::expected<void, std::string>{};
}

auto in_memory_chat_memory_store::erase(std::string session_id)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    sessions_.erase(session_id);
    co_return std::expected<void, std::string>{};
}

auto append_only_chat_memory_store::append(std::string session_id,
    message value)
    -> task<std::expected<void, std::string>>
{
    std::vector<message> values;
    values.push_back(std::move(value));
    co_return co_await append_batch(std::move(session_id), std::move(values));
}

auto in_memory_append_only_chat_memory_store::append(std::string session_id,
    message value) -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    sessions_[std::move(session_id)].push_back(std::move(value));
    co_return std::expected<void, std::string>{};
}

auto in_memory_append_only_chat_memory_store::append_batch(
    std::string session_id, std::vector<message> values)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    auto& session = sessions_[std::move(session_id)];
    session.insert(session.end(), std::make_move_iterator(values.begin()),
        std::make_move_iterator(values.end()));
    co_return std::expected<void, std::string>{};
}

auto in_memory_append_only_chat_memory_store::load_recent(
    std::string session_id, std::size_t limit)
    -> task<std::expected<std::vector<message>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = sessions_.find(session_id);
    if (found == sessions_.end())
        co_return std::vector<message>{};
    if (limit == 0 || found->second.size() <= limit)
        co_return found->second;

    auto begin = found->second.size() - limit;
    if (found->second[begin].role == "tool")
    {
        auto request = begin;
        while (request > 0)
        {
            --request;
            if (!found->second[request].tool_calls.empty())
            {
                begin = request;
                break;
            }
            if (found->second[request].role != "tool")
                break;
        }
    }

    std::vector<message> result;
    result.reserve(found->second.size() - begin);
    for (std::size_t index = 0; index < begin; ++index)
    {
        const auto& value = found->second[index];
        if (value.role == "system" || value.role == "developer")
            result.push_back(value);
    }
    result.insert(result.end(), found->second.begin() + static_cast<std::ptrdiff_t>(begin),
        found->second.end());
    co_return result;
}

auto in_memory_append_only_chat_memory_store::erase(std::string session_id)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    sessions_.erase(session_id);
    co_return std::expected<void, std::string>{};
}

conversation_memory::conversation_memory(memory_options options)
    : options_(std::move(options))
{
}

conversation_memory::conversation_memory(std::string session_id,
    chat_memory_store& store, memory_options options)
    : options_(std::move(options)), session_id_(std::move(session_id)), store_(&store)
{
}

conversation_memory::conversation_memory(std::string session_id,
    append_only_chat_memory_store& store, memory_options options)
    : options_(std::move(options)), session_id_(std::move(session_id)), append_store_(&store)
{
}

auto conversation_memory::load_locked()
    -> task<std::expected<std::vector<message>, std::string>>
{
    if (append_store_)
    {
        auto values = co_await append_store_->load_recent(
            session_id_, options_.max_messages);
        if (!values)
            co_return std::unexpected(values.error());
        trim_messages(*values, options_);
        co_return values;
    }
    if (!store_)
        co_return local_messages_;
    co_return co_await store_->load(session_id_);
}

auto conversation_memory::save_locked(std::vector<message> values)
    -> task<std::expected<void, std::string>>
{
    if (append_store_)
        co_return std::unexpected(
            "snapshot replacement is not supported by append-only memory stores");
    if (!store_)
    {
        local_messages_ = std::move(values);
        co_return std::expected<void, std::string>{};
    }
    co_return co_await store_->save(session_id_, std::move(values));
}

auto conversation_memory::snapshot()
    -> task<std::expected<std::vector<message>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await load_locked();
}

auto conversation_memory::append(message value)
    -> task<std::expected<void, std::string>>
{
    std::vector<message> values;
    values.push_back(std::move(value));
    co_return co_await append(std::move(values));
}

auto conversation_memory::append(std::vector<message> values)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    if (append_store_)
        co_return co_await append_store_->append_batch(
            session_id_, std::move(values));
    auto current = co_await load_locked();
    if (!current)
        co_return std::unexpected(current.error());
    current->insert(current->end(), std::make_move_iterator(values.begin()),
        std::make_move_iterator(values.end()));
    trim_messages(*current, options_);
    co_return co_await save_locked(std::move(*current));
}

auto conversation_memory::replace(std::vector<message> values)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    trim_messages(values, options_);
    co_return co_await save_locked(std::move(values));
}

auto conversation_memory::clear() -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    if (append_store_)
        co_return co_await append_store_->erase(session_id_);
    if (store_)
        co_return co_await store_->erase(session_id_);
    local_messages_.clear();
    co_return std::expected<void, std::string>{};
}

auto conversation_memory::size()
    -> task<std::expected<std::size_t, std::string>>
{
    auto values = co_await snapshot();
    if (!values)
        co_return std::unexpected(values.error());
    co_return values->size();
}

auto conversation_memory::session_id() const noexcept -> std::string_view
{
    return session_id_;
}

} // namespace cnetmod::openai
