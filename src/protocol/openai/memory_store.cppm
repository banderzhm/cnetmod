/// cnetmod.protocol.openai:memory_store — Durable conversation persistence

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:memory_store;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :memory;

namespace cnetmod::openai {

export struct file_memory_options
{
    std::size_t max_session_bytes = 16 * 1024 * 1024;
};

/// Durable per-session JSON store using same-directory atomic replacement.
export class file_chat_memory_store final : public chat_memory_store
{
public:
    file_chat_memory_store(io_context& context, thread_pool& pool,
        std::filesystem::path directory, file_memory_options options = {});

    auto load(std::string session_id)
        -> task<std::expected<std::vector<message>, std::string>> override;
    auto save(std::string session_id, std::vector<message> messages)
        -> task<std::expected<void, std::string>> override;
    auto erase(std::string session_id)
        -> task<std::expected<void, std::string>> override;

private:
    io_context& context_;
    thread_pool& pool_;
    std::filesystem::path directory_;
    file_memory_options options_;
    async_mutex mutex_;
};

} // namespace cnetmod::openai
