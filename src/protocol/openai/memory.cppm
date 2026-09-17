/// cnetmod.protocol.openai:memory — Session-aware, persistent conversation memory

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:memory;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :foundation;
import :messages;

namespace cnetmod::openai {

export using token_counter = std::function<std::size_t(const message&)>;

export struct memory_options
{
    /**
     * Maximum number of budgeted messages. Zero disables this limit.
     */
    std::size_t max_messages = 64;

    /**
     * Maximum number of budgeted tokens. Zero disables this limit.
     */
    std::size_t max_tokens = 0;
    bool preserve_system_messages = true;
    bool preserve_tool_exchanges = true;
    token_counter count_tokens;

    /**
     * Number of leading messages preserved and excluded from both budgets.
     */
    std::size_t pinned_prefix_messages = 0;

    /**
     * Number of trailing messages that trimming must never remove.
     */
    std::size_t preserved_tail_messages = 1;
};

/**
 * Describes the observable effect of one message-window trim operation.
 */
export struct trim_result
{
    std::size_t removed_messages = 0;
    std::size_t removed_tokens = 0;
    std::size_t remaining_tokens = 0;
    bool limit_satisfied = true;
};

/**
 * Trims a message sequence to the configured message and token windows.
 *
 * System instructions and complete tool exchanges are preserved according to
 * the supplied options. The function has no persistence side effects, so an
 * application can reuse the framework window policy with its own data store.
 */
export auto trim_messages(std::vector<message>& messages,
    const memory_options& options) -> trim_result;

/**
 * Defines snapshot persistence for session-keyed conversation messages.
 */
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

/**
 * Provides coroutine-safe snapshot storage for process-local sessions.
 */
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

/**
 * Defines append-only persistence for databases that retain message history as
 * the single source of truth.
 *
 * A zero recent-message limit requests all available messages. A finite limit
 * is a target context size: implementations may prepend retained instructions
 * or a tool-call request needed to keep the returned suffix protocol-valid.
 * Batch append must be atomic so an Agent tool exchange cannot be half stored.
 */
export class append_only_chat_memory_store
{
public:
    virtual ~append_only_chat_memory_store() = default;
    virtual auto append(std::string session_id, message value)
        -> task<std::expected<void, std::string>>;
    virtual auto append_batch(std::string session_id,
        std::vector<message> values)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto load_recent(std::string session_id, std::size_t limit)
        -> task<std::expected<std::vector<message>, std::string>> = 0;
    virtual auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> = 0;
};

/**
 * Provides coroutine-safe append-only storage for process-local sessions.
 */
export class in_memory_append_only_chat_memory_store final
    : public append_only_chat_memory_store
{
public:
    auto append(std::string session_id, message value)
        -> task<std::expected<void, std::string>> override;
    auto append_batch(std::string session_id, std::vector<message> values)
        -> task<std::expected<void, std::string>> override;
    auto load_recent(std::string session_id, std::size_t limit)
        -> task<std::expected<std::vector<message>, std::string>> override;
    auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> override;

private:
    async_mutex mutex_;
    std::map<std::string, std::vector<message>, std::less<>> sessions_;
};

/**
 * Keeps protocol content separate from application persistence metadata.
 *
 * Metadata is never serialized into an OpenAI request. A database adapter may
 * use it for identifiers, model names, token accounting, timestamps, or other
 * application-owned fields.
 */
export struct persisted_chat_message
{
    message value;
    json metadata = json::object();
};

/**
 * Defines append-only persistence that returns database-enriched records.
 */
export class append_only_chat_record_store
{
public:
    virtual ~append_only_chat_record_store() = default;
    virtual auto append(std::string session_id, persisted_chat_message value)
        -> task<std::expected<persisted_chat_message, std::string>>;
    virtual auto append_batch(std::string session_id,
        std::vector<persisted_chat_message> values)
        -> task<std::expected<std::vector<persisted_chat_message>,
            std::string>> = 0;
    virtual auto load_recent(std::string session_id, std::size_t limit)
        -> task<std::expected<std::vector<persisted_chat_message>,
            std::string>> = 0;
    virtual auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> = 0;
};

/**
 * Provides coroutine-safe process-local storage for enriched chat records.
 */
export class in_memory_append_only_chat_record_store final
    : public append_only_chat_record_store
{
public:
    auto append(std::string session_id, persisted_chat_message value)
        -> task<std::expected<persisted_chat_message, std::string>> override;
    auto append_batch(std::string session_id,
        std::vector<persisted_chat_message> values)
        -> task<std::expected<std::vector<persisted_chat_message>,
            std::string>> override;
    auto load_recent(std::string session_id, std::size_t limit)
        -> task<std::expected<std::vector<persisted_chat_message>,
            std::string>> override;
    auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> override;

private:
    async_mutex mutex_;
    std::map<std::string, std::vector<persisted_chat_message>, std::less<>>
        sessions_;
};

/**
 * Adapts enriched record persistence to the protocol-only memory contract.
 */
export class chat_record_memory_adapter final
    : public append_only_chat_memory_store
{
public:
    explicit chat_record_memory_adapter(append_only_chat_record_store& store);

    auto append(std::string session_id, message value)
        -> task<std::expected<void, std::string>> override;
    auto append_batch(std::string session_id, std::vector<message> values)
        -> task<std::expected<void, std::string>> override;
    auto load_recent(std::string session_id, std::size_t limit)
        -> task<std::expected<std::vector<message>, std::string>> override;
    auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> override;

private:
    append_only_chat_record_store& store_;
};

/**
 * Defines the conversation-memory contract consumed by services and agents.
 */
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

/**
 * Applies bounded message views over local, snapshot, or append-only storage.
 *
 * Append-only storage is never rewritten while appending. Window trimming is
 * applied only to values loaded for model context, preserving database history.
 */
export class conversation_memory final : public chat_memory
{
public:
    explicit conversation_memory(memory_options options = {});
    conversation_memory(std::string session_id, chat_memory_store& store,
        memory_options options = {});
    conversation_memory(std::string session_id,
        append_only_chat_memory_store& store, memory_options options = {});

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

    memory_options options_;
    std::string session_id_;
    chat_memory_store* store_ = nullptr;
    append_only_chat_memory_store* append_store_ = nullptr;
    async_mutex mutex_;
    std::vector<message> local_messages_;
};

export using chat_memory_provider =
    std::function<chat_memory&(std::string_view session_id)>;

} // namespace cnetmod::openai
