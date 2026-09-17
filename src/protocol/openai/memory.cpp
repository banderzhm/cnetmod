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

auto trim_messages(std::vector<message>& messages,
    const memory_options& options) -> trim_result
{
    trim_result result;
    const auto pinned_count = [&messages, &options]
    {
        return std::min(options.pinned_prefix_messages, messages.size());
    };
    const auto budgeted_message_count = [&messages, &pinned_count]
    {
        return messages.size() - pinned_count();
    };
    const auto total_tokens = [&messages, &options, &pinned_count]
    {
        std::size_t total = 0;
        for (std::size_t index = pinned_count(); index < messages.size(); ++index)
            total += message_tokens(messages[index], options);
        return total;
    };
    const auto over_limit = [&options, &budgeted_message_count, &total_tokens]
    {
        return (options.max_messages > 0 &&
                   budgeted_message_count() > options.max_messages) ||
            (options.max_tokens > 0 && total_tokens() > options.max_tokens);
    };
    const auto protected_index = [&messages, &options, &pinned_count](
                                     std::size_t index)
    {
        if (index < pinned_count())
            return true;
        const auto tail = std::min(options.preserved_tail_messages,
            messages.size());
        if (index >= messages.size() - tail)
            return true;
        return options.preserve_system_messages &&
            (messages[index].role == "system" ||
                messages[index].role == "developer");
    };

    while (!messages.empty() && over_limit())
    {
        std::optional<std::pair<std::size_t, std::size_t>> removable;
        for (std::size_t index = 0; index < messages.size(); ++index)
        {
            if (protected_index(index))
                continue;
            auto end = index + 1;
            if (options.preserve_tool_exchanges &&
                !messages[index].tool_calls.empty())
            {
                while (end < messages.size() && messages[end].role == "tool")
                    ++end;
                bool group_protected = false;
                for (auto member = index; member < end; ++member)
                    group_protected = group_protected || protected_index(member);
                if (group_protected)
                    continue;
            }
            removable = std::pair{index, end};
            break;
        }
        if (!removable)
            break;
        for (auto index = removable->first; index < removable->second; ++index)
            result.removed_tokens += message_tokens(messages[index], options);
        result.removed_messages += removable->second - removable->first;
        messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(removable->first),
            messages.begin() + static_cast<std::ptrdiff_t>(removable->second));
    }
    result.remaining_tokens = total_tokens();
    result.limit_satisfied = !over_limit();
    return result;
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

auto append_only_chat_record_store::append(std::string session_id,
    persisted_chat_message value)
    -> task<std::expected<persisted_chat_message, std::string>>
{
    auto appended = co_await append_batch(std::move(session_id),
        std::vector<persisted_chat_message>{std::move(value)});
    if (!appended)
        co_return std::unexpected(appended.error());
    if (appended->size() != 1)
        co_return std::unexpected(
            "chat record store returned an invalid append result");
    co_return std::move(appended->front());
}

auto in_memory_append_only_chat_record_store::append(std::string session_id,
    persisted_chat_message value)
    -> task<std::expected<persisted_chat_message, std::string>>
{
    auto appended = co_await append_batch(std::move(session_id),
        std::vector<persisted_chat_message>{std::move(value)});
    if (!appended)
        co_return std::unexpected(appended.error());
    co_return std::move(appended->front());
}

auto in_memory_append_only_chat_record_store::append_batch(
    std::string session_id, std::vector<persisted_chat_message> values)
    -> task<std::expected<std::vector<persisted_chat_message>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    auto& destination = sessions_[std::move(session_id)];
    destination.insert(destination.end(), values.begin(), values.end());
    co_return values;
}

auto in_memory_append_only_chat_record_store::load_recent(
    std::string session_id, std::size_t limit)
    -> task<std::expected<std::vector<persisted_chat_message>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto found = sessions_.find(session_id);
    if (found == sessions_.end())
        co_return std::vector<persisted_chat_message>{};
    const auto begin = limit == 0 || found->second.size() <= limit
        ? found->second.begin()
        : found->second.end() - static_cast<std::ptrdiff_t>(limit);
    co_return std::vector<persisted_chat_message>{begin, found->second.end()};
}

auto in_memory_append_only_chat_record_store::erase(std::string session_id)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    sessions_.erase(session_id);
    co_return std::expected<void, std::string>{};
}

chat_record_memory_adapter::chat_record_memory_adapter(
    append_only_chat_record_store& store)
    : store_(store)
{
}

auto chat_record_memory_adapter::append(std::string session_id, message value)
    -> task<std::expected<void, std::string>>
{
    auto appended = co_await store_.append(std::move(session_id),
        {.value = std::move(value)});
    if (!appended)
        co_return std::unexpected(appended.error());
    co_return std::expected<void, std::string>{};
}

auto chat_record_memory_adapter::append_batch(std::string session_id,
    std::vector<message> values)
    -> task<std::expected<void, std::string>>
{
    std::vector<persisted_chat_message> records;
    records.reserve(values.size());
    for (auto& value : values)
        records.push_back({.value = std::move(value)});
    auto appended = co_await store_.append_batch(std::move(session_id),
        std::move(records));
    if (!appended)
        co_return std::unexpected(appended.error());
    co_return std::expected<void, std::string>{};
}

auto chat_record_memory_adapter::load_recent(std::string session_id,
    std::size_t limit)
    -> task<std::expected<std::vector<message>, std::string>>
{
    auto loaded = co_await store_.load_recent(std::move(session_id), limit);
    if (!loaded)
        co_return std::unexpected(loaded.error());
    std::vector<message> messages;
    messages.reserve(loaded->size());
    for (auto& record : *loaded)
        messages.push_back(std::move(record.value));
    co_return messages;
}

auto chat_record_memory_adapter::erase(std::string session_id)
    -> task<std::expected<void, std::string>>
{
    co_return co_await store_.erase(std::move(session_id));
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
