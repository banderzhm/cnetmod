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

conversation_memory::conversation_memory(memory_options options)
    : options_(std::move(options))
{
}

conversation_memory::conversation_memory(std::string session_id,
    chat_memory_store& store, memory_options options)
    : options_(std::move(options)), session_id_(std::move(session_id)), store_(&store)
{
}

auto conversation_memory::load_locked()
    -> task<std::expected<std::vector<message>, std::string>>
{
    if (!store_)
        co_return local_messages_;
    co_return co_await store_->load(session_id_);
}

auto conversation_memory::save_locked(std::vector<message> values)
    -> task<std::expected<void, std::string>>
{
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
    auto current = co_await load_locked();
    if (!current)
        co_return std::unexpected(current.error());
    current->insert(current->end(), std::make_move_iterator(values.begin()),
        std::make_move_iterator(values.end()));
    trim(*current);
    co_return co_await save_locked(std::move(*current));
}

auto conversation_memory::replace(std::vector<message> values)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    trim(values);
    co_return co_await save_locked(std::move(values));
}

auto conversation_memory::clear() -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
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

auto conversation_memory::tokens(const message& value) const -> std::size_t
{
    if (options_.count_tokens)
        return options_.count_tokens(value);
    std::size_t characters = value.role.size() + value.content.size() + value.name.size();
    for (const auto& part : value.content_parts)
        characters += part.text.size() + part.image_url.url.size();
    for (const auto& call : value.tool_calls)
        characters += call.function.name.size() + call.function.arguments.size();
    return std::max<std::size_t>(1, (characters + 3) / 4);
}

void conversation_memory::trim(std::vector<message>& values) const
{
    const auto total_tokens = [this, &values]
    {
        std::size_t total = 0;
        for (const auto& value : values)
            total += tokens(value);
        return total;
    };
    const auto over_limit = [&]
    {
        return (options_.max_messages > 0 && values.size() > options_.max_messages) ||
            (options_.max_tokens > 0 && total_tokens() > options_.max_tokens);
    };

    while (!values.empty() && over_limit())
    {
        auto removable = std::ranges::find_if(values, [this](const message& item)
            {
                return !options_.preserve_system_messages ||
                    (item.role != "system" && item.role != "developer");
            });
        if (removable == values.end())
            removable = values.begin();

        if (options_.preserve_tool_exchanges && !removable->tool_calls.empty())
        {
            auto next = std::next(removable);
            while (next != values.end() && next->role == "tool")
                next = values.erase(next);
        }
        values.erase(removable);
    }
}

} // namespace cnetmod::openai
