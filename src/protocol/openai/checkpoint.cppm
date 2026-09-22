/// cnetmod.protocol.openai:checkpoint — Versioned workflow persistence

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:checkpoint;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :foundation;
import cnetmod.json;

namespace cnetmod::openai {

/**
 * Represents an idempotent write produced while a checkpoint step is pending.
 */
export struct checkpoint_pending_write
{
    std::string id;
    std::string channel;
    json value;
};

/**
 * Identifies a checkpoint from another branch or earlier history.
 */
export struct checkpoint_reference
{
    std::string branch;
    std::uint64_t version = 0;
};

/**
 * Contains one immutable state version and its mutable pending-write journal.
 */
export struct checkpoint_record
{
    std::string thread_id;
    std::string branch = "main";
    std::uint64_t version = 0;
    std::optional<std::uint64_t> parent_version;
    std::optional<checkpoint_reference> origin;
    json state = cnetmod::json::object();
    json metadata = cnetmod::json::object();
    std::vector<checkpoint_pending_write> pending_writes;
    std::uint64_t write_revision = 0;
    std::chrono::system_clock::time_point created_at;
};

/**
 * Defines one compare-and-swap checkpoint commit.
 */
export struct checkpoint_commit
{
    std::string thread_id;
    std::string branch = "main";
    json state = cnetmod::json::object();
    json metadata = cnetmod::json::object();
    std::optional<std::uint64_t> expected_head_version;
};

/**
 * Defines durable execution state with branching and optimistic concurrency.
 */
export class checkpoint_store
{
public:
    virtual ~checkpoint_store() = default;
    virtual auto load_latest(std::string thread_id,
        std::string branch = "main")
        -> task<std::expected<std::optional<checkpoint_record>, std::string>> = 0;
    virtual auto load(std::string thread_id, std::string branch,
        std::uint64_t version)
        -> task<std::expected<std::optional<checkpoint_record>, std::string>> = 0;
    virtual auto list(std::string thread_id, std::string branch = "main",
        std::size_t limit = 20,
        std::optional<std::uint64_t> before_version = std::nullopt)
        -> task<std::expected<std::vector<checkpoint_record>, std::string>> = 0;
    virtual auto commit(checkpoint_commit request)
        -> task<std::expected<checkpoint_record, std::string>> = 0;
    virtual auto put_pending_writes(std::string thread_id,
        std::string branch, std::uint64_t version,
        std::vector<checkpoint_pending_write> writes,
        std::optional<std::uint64_t> expected_write_revision = std::nullopt)
        -> task<std::expected<checkpoint_record, std::string>> = 0;
    virtual auto fork(std::string thread_id, std::string source_branch,
        std::uint64_t source_version, std::string target_branch)
        -> task<std::expected<checkpoint_record, std::string>> = 0;
    virtual auto rollback(std::string thread_id, std::string branch,
        std::uint64_t target_version,
        std::optional<std::uint64_t> expected_head_version = std::nullopt)
        -> task<std::expected<checkpoint_record, std::string>> = 0;
    virtual auto erase_branch(std::string thread_id, std::string branch)
        -> task<std::expected<void, std::string>> = 0;
    virtual auto erase_thread(std::string thread_id)
        -> task<std::expected<void, std::string>> = 0;
};

/**
 * Provides a coroutine-safe reference implementation with immutable history.
 */
export class in_memory_checkpoint_store final : public checkpoint_store
{
public:
    using clock = std::function<std::chrono::system_clock::time_point()>;

    explicit in_memory_checkpoint_store(clock now = {});

    auto load_latest(std::string thread_id, std::string branch = "main")
        -> task<std::expected<std::optional<checkpoint_record>, std::string>> override;
    auto load(std::string thread_id, std::string branch,
        std::uint64_t version)
        -> task<std::expected<std::optional<checkpoint_record>, std::string>> override;
    auto list(std::string thread_id, std::string branch = "main",
        std::size_t limit = 20,
        std::optional<std::uint64_t> before_version = std::nullopt)
        -> task<std::expected<std::vector<checkpoint_record>, std::string>> override;
    auto commit(checkpoint_commit request)
        -> task<std::expected<checkpoint_record, std::string>> override;
    auto put_pending_writes(std::string thread_id, std::string branch,
        std::uint64_t version, std::vector<checkpoint_pending_write> writes,
        std::optional<std::uint64_t> expected_write_revision = std::nullopt)
        -> task<std::expected<checkpoint_record, std::string>> override;
    auto fork(std::string thread_id, std::string source_branch,
        std::uint64_t source_version, std::string target_branch)
        -> task<std::expected<checkpoint_record, std::string>> override;
    auto rollback(std::string thread_id, std::string branch,
        std::uint64_t target_version,
        std::optional<std::uint64_t> expected_head_version = std::nullopt)
        -> task<std::expected<checkpoint_record, std::string>> override;
    auto erase_branch(std::string thread_id, std::string branch)
        -> task<std::expected<void, std::string>> override;
    auto erase_thread(std::string thread_id)
        -> task<std::expected<void, std::string>> override;

private:
    using branch_key = std::pair<std::string, std::string>;

    clock now_;
    async_mutex mutex_;
    std::map<branch_key, std::vector<checkpoint_record>> histories_;
};

} // namespace cnetmod::openai
