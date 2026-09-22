/// cnetmod.protocol.openai:long_term_store — Namespaced cross-session memory

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:long_term_store;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :foundation;
import :filters;
import :model;
import cnetmod.json;

namespace cnetmod::openai {

export using store_namespace = std::vector<std::string>;

/**
 * Represents one versioned JSON value in a hierarchical namespace.
 */
export struct long_term_item
{
    store_namespace name_space;
    std::string key;
    json value;
    std::uint64_t version = 0;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point updated_at;
    std::optional<std::chrono::system_clock::time_point> expires_at;
    std::optional<float> score;
};

/**
 * Controls TTL, semantic indexing, and optimistic concurrency for a write.
 */
export struct long_term_put_options
{
    std::optional<std::chrono::milliseconds> ttl;
    bool index_for_semantic_search = true;
    std::optional<std::uint64_t> expected_version;
};

/**
 * Describes filtered, paginated, and optionally semantic memory search.
 */
export struct long_term_search_request
{
    store_namespace namespace_prefix;
    metadata_filter filter;
    std::string query;
    float minimum_score = -1.0F;
    std::size_t limit = 10;
    std::size_t offset = 0;
    bool refresh_ttl = false;
};

/**
 * Defines provider-neutral, cross-session JSON memory persistence.
 */
export class long_term_store
{
public:
    virtual ~long_term_store() = default;
    virtual auto get(store_namespace name_space, std::string key,
        bool refresh_ttl = false)
        -> task<std::expected<std::optional<long_term_item>, std::string>> = 0;
    virtual auto put(store_namespace name_space, std::string key, json value,
        long_term_put_options options = {})
        -> task<std::expected<long_term_item, std::string>> = 0;
    virtual auto erase(store_namespace name_space, std::string key,
        std::optional<std::uint64_t> expected_version = std::nullopt)
        -> task<std::expected<bool, std::string>> = 0;
    virtual auto search(long_term_search_request request)
        -> task<std::expected<std::vector<long_term_item>, std::string>> = 0;
    virtual auto list_namespaces(store_namespace prefix = {},
        std::size_t limit = 100, std::size_t offset = 0)
        -> task<std::expected<std::vector<store_namespace>, std::string>> = 0;
};

/**
 * Provides a coroutine-safe reference store with optional embedding search.
 *
 * Expired entries are removed opportunistically. Production database adapters
 * should enforce the same version and TTL semantics in their own transactions.
 */
export class in_memory_long_term_store final : public long_term_store
{
public:
    using clock = std::function<std::chrono::system_clock::time_point()>;

    explicit in_memory_long_term_store(
        embedding_model* semantic_model = nullptr, clock now = {});

    auto get(store_namespace name_space, std::string key,
        bool refresh_ttl = false)
        -> task<std::expected<std::optional<long_term_item>, std::string>> override;
    auto put(store_namespace name_space, std::string key, json value,
        long_term_put_options options = {})
        -> task<std::expected<long_term_item, std::string>> override;
    auto erase(store_namespace name_space, std::string key,
        std::optional<std::uint64_t> expected_version = std::nullopt)
        -> task<std::expected<bool, std::string>> override;
    auto search(long_term_search_request request)
        -> task<std::expected<std::vector<long_term_item>, std::string>> override;
    auto list_namespaces(store_namespace prefix = {},
        std::size_t limit = 100, std::size_t offset = 0)
        -> task<std::expected<std::vector<store_namespace>, std::string>> override;

private:
    struct entry
    {
        long_term_item item;
        std::optional<std::chrono::milliseconds> ttl;
        std::vector<float> embedding;
    };

    using storage_key = std::pair<store_namespace, std::string>;

    void purge_expired(std::chrono::system_clock::time_point now);

    embedding_model* semantic_model_ = nullptr;
    clock now_;
    async_mutex mutex_;
    std::map<storage_key, entry> entries_;
};

} // namespace cnetmod::openai
