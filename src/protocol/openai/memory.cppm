/// cnetmod.protocol.openai:memory — Session-aware, persistent conversation memory

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:memory;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :messages;

namespace cnetmod::openai {

export using token_counter = std::function<std::size_t(const message&)>;

export struct memory_options
{
    std::size_t max_messages = 64;
    std::size_t max_tokens = 0;
    bool preserve_system_messages = true;
    bool preserve_tool_exchanges = true;
    token_counter count_tokens;
};

/// Strategy for durable, session-keyed message persistence.
export class chat_memory_store
{
public:
    virtual ~chat_memory_store() = default;
    virtual auto load(std::string session_id)
        -> task<std::expected<std::vector<message>, std::string>> = 0;
    virtual auto save(std::string session_id, std::vector<message> messages)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> = 0;
};

/// Coroutine-safe reference store useful for tests and process-local sessions.
export class in_memory_chat_memory_store final : public chat_memory_store
{
public:
    auto load(std::string session_id)
        -> task<std::expected<std::vector<message>, std::string>> override;
    auto save(std::string session_id, std::vector<message> messages)
        -> task<std::expected<void, std::string>> override;
    auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> override;

private:
    async_mutex mutex_;
    std::map<std::string, std::vector<message>, std::less<>> sessions_;
};

/// Abstract memory contract consumed by services and agents.
export class chat_memory
{
public:
    virtual ~chat_memory() = default;
    virtual auto snapshot()
        -> task<std::expected<std::vector<message>, std::string>> = 0;
    virtual auto append(message value)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto append(std::vector<message> values)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto replace(std::vector<message> values)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto clear() -> task<std::expected<void, std::string>> = 0;
    virtual auto size() -> task<std::expected<std::size_t, std::string>> = 0;
};

/// Message/token window with optional durable store and stable session identity.
export class conversation_memory final : public chat_memory
{
public:
    explicit conversation_memory(memory_options options = {});
    conversation_memory(std::string session_id, chat_memory_store& store,
        memory_options options = {});

    auto snapshot()
        -> task<std::expected<std::vector<message>, std::string>> override;
    auto append(message value)
        -> task<std::expected<void, std::string>> override;
    auto append(std::vector<message> values)
        -> task<std::expected<void, std::string>> override;
    auto replace(std::vector<message> values)
        -> task<std::expected<void, std::string>> override;
    auto clear() -> task<std::expected<void, std::string>> override;
    auto size() -> task<std::expected<std::size_t, std::string>> override;

    [[nodiscard]] auto session_id() const noexcept -> std::string_view;

private:
    auto load_locked()
        -> task<std::expected<std::vector<message>, std::string>>;
    auto save_locked(std::vector<message> values)
        -> task<std::expected<void, std::string>>;
    void trim(std::vector<message>& values) const;
    [[nodiscard]] auto tokens(const message& value) const -> std::size_t;

    memory_options options_;
    std::string session_id_;
    chat_memory_store* store_ = nullptr;
    async_mutex mutex_;
    std::vector<message> local_messages_;
};

export using chat_memory_provider =
    std::function<chat_memory&(std::string_view session_id)>;

} // namespace cnetmod::openai
