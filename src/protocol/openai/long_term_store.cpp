/// cnetmod.protocol.openai:long_term_store — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import :foundation;
import :filters;
import :model;
import :long_term_store;

namespace cnetmod::openai {

namespace {
    auto valid_namespace(const store_namespace& value) -> bool
    {
        return !value.empty() && std::ranges::all_of(value, [](const std::string& component)
                                     {
                                         return !component.empty();
                                     });
    }

    auto has_prefix(const store_namespace& value,
        const store_namespace& prefix) -> bool
    {
        return prefix.size() <= value.size() &&
            std::equal(prefix.begin(), prefix.end(), value.begin());
    }

    auto cosine_similarity(const std::vector<float>& left,
        const std::vector<float>& right) -> std::optional<float>
    {
        if (left.empty() || left.size() != right.size())
            return std::nullopt;
        double dot = 0.0;
        double left_norm = 0.0;
        double right_norm = 0.0;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            dot += static_cast<double>(left[index]) * right[index];
            left_norm += static_cast<double>(left[index]) * left[index];
            right_norm += static_cast<double>(right[index]) * right[index];
        }
        if (left_norm == 0.0 || right_norm == 0.0)
            return std::nullopt;
        return static_cast<float>(dot /
            (std::sqrt(left_norm) * std::sqrt(right_norm)));
    }
} // namespace

in_memory_long_term_store::in_memory_long_term_store(
    embedding_model* semantic_model, clock now)
    : semantic_model_(semantic_model), now_(std::move(now))
{
    if (!now_)
    {
        now_ = []
        {
            return std::chrono::system_clock::now();
        };
    }
}

void in_memory_long_term_store::purge_expired(
    std::chrono::system_clock::time_point now)
{
    std::erase_if(entries_, [now](const auto& value)
        {
            return value.second.item.expires_at &&
                *value.second.item.expires_at <= now;
        });
}

auto in_memory_long_term_store::get(store_namespace name_space,
    std::string key, bool refresh_ttl)
    -> task<std::expected<std::optional<long_term_item>, std::string>>
{
    if (!valid_namespace(name_space) || key.empty())
        co_return std::unexpected("long-term memory namespace and key cannot be empty");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto now = now_();
    purge_expired(now);
    const auto found = entries_.find({name_space, key});
    if (found == entries_.end())
        co_return std::optional<long_term_item>{};
    if (refresh_ttl && found->second.ttl)
        found->second.item.expires_at = now + *found->second.ttl;
    co_return std::optional<long_term_item>{found->second.item};
}

auto in_memory_long_term_store::put(store_namespace name_space,
    std::string key, json value, long_term_put_options options)
    -> task<std::expected<long_term_item, std::string>>
{
    if (!valid_namespace(name_space) || key.empty())
        co_return std::unexpected("long-term memory namespace and key cannot be empty");
    if (options.ttl && *options.ttl <= std::chrono::milliseconds::zero())
        co_return std::unexpected("long-term memory TTL must be positive");

    std::vector<float> embedding;
    if (options.index_for_semantic_search && semantic_model_)
    {
        auto encoded = cnetmod::json::write_document(value);
        if (!encoded)
            co_return std::unexpected("long-term value serialization failed");
        auto embedded = co_await semantic_model_->embed_documents({*encoded});
        if (!embedded)
            co_return std::unexpected("long-term memory indexing failed: " +
                embedded.error());
        if (embedded->size() != 1 || embedded->front().empty())
            co_return std::unexpected(
                "long-term memory indexing returned an invalid vector");
        embedding = std::move(embedded->front());
    }

    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto now = now_();
    purge_expired(now);
    const storage_key storage{name_space, key};
    const auto found = entries_.find(storage);
    const auto current_version = found == entries_.end()
        ? std::uint64_t{0}
        : found->second.item.version;
    if (options.expected_version && *options.expected_version != current_version)
        co_return std::unexpected(std::format(
            "long-term memory version conflict: expected {}, current {}",
            *options.expected_version, current_version));

    long_term_item item{.name_space = std::move(name_space),
        .key = std::move(key),
        .value = std::move(value),
        .version = current_version + 1,
        .created_at = found == entries_.end()
            ? now
            : found->second.item.created_at,
        .updated_at = now};
    if (options.ttl)
        item.expires_at = now + *options.ttl;
    entries_.insert_or_assign(storage,
        entry{.item = item, .ttl = options.ttl, .embedding = std::move(embedding)});
    co_return item;
}

auto in_memory_long_term_store::erase(store_namespace name_space,
    std::string key, std::optional<std::uint64_t> expected_version)
    -> task<std::expected<bool, std::string>>
{
    if (!valid_namespace(name_space) || key.empty())
        co_return std::unexpected("long-term memory namespace and key cannot be empty");
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    purge_expired(now_());
    const auto found = entries_.find({name_space, key});
    if (found == entries_.end())
        co_return false;
    if (expected_version && found->second.item.version != *expected_version)
        co_return std::unexpected(std::format(
            "long-term memory version conflict: expected {}, current {}",
            *expected_version, found->second.item.version));
    entries_.erase(found);
    co_return true;
}

auto in_memory_long_term_store::search(long_term_search_request request)
    -> task<std::expected<std::vector<long_term_item>, std::string>>
{
    if (request.limit == 0)
        co_return std::vector<long_term_item>{};
    std::vector<float> query_embedding;
    if (!request.query.empty())
    {
        if (!semantic_model_)
            co_return std::unexpected(
                "semantic search requires an embedding model");
        auto embedded = co_await semantic_model_->embed_query(request.query);
        if (!embedded)
            co_return std::unexpected("long-term memory query embedding failed: " +
                embedded.error());
        if (embedded->empty())
            co_return std::unexpected(
                "long-term memory query embedding is empty");
        query_embedding = std::move(*embedded);
    }

    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    const auto now = now_();
    purge_expired(now);
    std::vector<long_term_item> matches;
    for (auto& [storage, value] : entries_)
    {
        if (!has_prefix(storage.first, request.namespace_prefix) ||
            (!request.filter.empty() &&
                !request.filter.matches(value.item.value)))
            continue;
        auto item = value.item;
        if (!query_embedding.empty())
        {
            const auto score = cosine_similarity(
                query_embedding, value.embedding);
            if (!score || *score < request.minimum_score)
                continue;
            item.score = *score;
        }
        if (request.refresh_ttl && value.ttl)
        {
            value.item.expires_at = now + *value.ttl;
            item.expires_at = value.item.expires_at;
        }
        matches.push_back(std::move(item));
    }
    if (!query_embedding.empty())
    {
        std::ranges::stable_sort(matches, [](const auto& left, const auto& right)
            {
                return left.score.value_or(-1.0F) >
                    right.score.value_or(-1.0F);
            });
    }
    else
    {
        std::ranges::sort(matches, [](const auto& left, const auto& right)
            {
                return std::tie(left.name_space, left.key) <
                    std::tie(right.name_space, right.key);
            });
    }
    if (request.offset >= matches.size())
        co_return std::vector<long_term_item>{};
    const auto last = std::min(matches.size(),
        request.offset + request.limit);
    co_return std::vector<long_term_item>{
        std::make_move_iterator(matches.begin() +
            static_cast<std::ptrdiff_t>(request.offset)),
        std::make_move_iterator(matches.begin() +
            static_cast<std::ptrdiff_t>(last))};
}

auto in_memory_long_term_store::list_namespaces(store_namespace prefix,
    std::size_t limit, std::size_t offset)
    -> task<std::expected<std::vector<store_namespace>, std::string>>
{
    if (limit == 0)
        co_return std::vector<store_namespace>{};
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    purge_expired(now_());
    std::set<store_namespace> unique;
    for (const auto& [storage, value] : entries_)
    {
        (void)value;
        if (has_prefix(storage.first, prefix))
            unique.insert(storage.first);
    }
    if (offset >= unique.size())
        co_return std::vector<store_namespace>{};
    auto first = unique.begin();
    std::ranges::advance(first, static_cast<std::ptrdiff_t>(offset));
    std::vector<store_namespace> result;
    result.reserve(std::min(limit, unique.size() - offset));
    for (; first != unique.end() && result.size() < limit; ++first)
        result.push_back(*first);
    co_return result;
}

} // namespace cnetmod::openai
