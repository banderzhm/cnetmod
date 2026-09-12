/// cnetmod.protocol.openai:memory_store — implementations

module;

#include <cnetmod/config.hpp>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.mutex;
import cnetmod.coro.bridge;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import :messages;
import :memory;
import :memory_store;

namespace cnetmod::openai {

namespace {
    auto session_hash(std::string_view value) noexcept -> std::uint64_t
    {
        std::uint64_t result = 14695981039346656037ULL;
        for (const auto character : value)
        {
            result ^= static_cast<unsigned char>(character);
            result *= 1099511628211ULL;
        }
        return result;
    }

    auto session_path(const std::filesystem::path& directory,
        std::string_view session_id) -> std::filesystem::path
    {
        return directory /
            std::format("session-{:016x}.json", session_hash(session_id));
    }

    auto replace_file(const std::filesystem::path& temporary,
        const std::filesystem::path& target) -> std::expected<void, std::string>
    {
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return std::unexpected(std::format(
                "cannot replace memory file '{}': Windows error {}",
                target.string(), GetLastError()));
#else
        std::error_code error;
        std::filesystem::rename(temporary, target, error);
        if (error)
            return std::unexpected(std::format(
                "cannot replace memory file '{}': {}",
                target.string(), error.message()));
#endif
        return {};
    }

    auto read_session(const std::filesystem::path& directory,
        std::string_view session_id, std::size_t max_bytes)
        -> std::expected<std::vector<message>, std::string>
    {
        const auto path = session_path(directory, session_id);
        std::error_code error;
        if (!std::filesystem::exists(path, error))
            return std::vector<message>{};
        if (error)
            return std::unexpected("cannot inspect memory file: " + error.message());
        const auto size = std::filesystem::file_size(path, error);
        if (error)
            return std::unexpected("cannot inspect memory size: " + error.message());
        if (size > max_bytes)
            return std::unexpected(std::format(
                "memory session exceeds {} bytes", max_bytes));
        std::ifstream input{path, std::ios::binary};
        if (!input)
            return std::unexpected("cannot open memory file: " + path.string());
        auto payload = json::parse(input, nullptr, false);
        if (!payload.is_object() || payload.value("version", 0) != 1 ||
            payload.value("session_id", "") != session_id ||
            !payload.contains("messages") || !payload["messages"].is_array())
            return std::unexpected("memory file has an invalid envelope");
        std::vector<message> messages;
        messages.reserve(payload["messages"].size());
        for (const auto& value : payload["messages"])
        {
            auto decoded = message::from_json_object(value);
            if (!decoded)
                return std::unexpected("invalid persisted message: " +
                    decoded.error());
            messages.push_back(std::move(*decoded));
        }
        return messages;
    }

    auto write_session(const std::filesystem::path& directory,
        std::string_view session_id, const std::vector<message>& messages,
        std::size_t max_bytes) -> std::expected<void, std::string>
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error)
            return std::unexpected("cannot create memory directory: " +
                error.message());
        json encoded = {{"version", 1}, {"session_id", session_id},
            {"messages", json::array()}};
        for (const auto& value : messages)
            encoded["messages"].push_back(value.to_json_object());
        auto content = encoded.dump();
        if (content.size() > max_bytes)
            return std::unexpected(std::format(
                "memory session exceeds {} bytes", max_bytes));

        const auto target = session_path(directory, session_id);
        static std::atomic<std::uint64_t> sequence{0};
        auto temporary = target;
        temporary += std::format(".tmp-{}",
            sequence.fetch_add(1, std::memory_order_relaxed));
        {
            std::ofstream output{temporary,
                std::ios::binary | std::ios::trunc};
            if (!output)
                return std::unexpected(
                    "cannot create temporary memory file: " + temporary.string());
            output.write(content.data(),
                static_cast<std::streamsize>(content.size()));
            output.flush();
            if (!output)
            {
                std::filesystem::remove(temporary, error);
                return std::unexpected(
                    "cannot write temporary memory file: " + temporary.string());
            }
        }
        auto replaced = replace_file(temporary, target);
        if (!replaced)
            std::filesystem::remove(temporary, error);
        return replaced;
    }
} // namespace

file_chat_memory_store::file_chat_memory_store(io_context& context,
    thread_pool& pool, std::filesystem::path directory,
    file_memory_options options)
    : context_(context), pool_(pool), directory_(std::move(directory)), options_(options)
{
    if (directory_.empty())
        throw std::invalid_argument("memory directory cannot be empty");
    if (options_.max_session_bytes == 0)
        throw std::invalid_argument("memory session size limit cannot be zero");
}

auto file_chat_memory_store::load(std::string session_id)
    -> task<std::expected<std::vector<message>, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await blocking_invoke(pool_, context_,
        [directory = directory_, session_id = std::move(session_id),
            max_bytes = options_.max_session_bytes]
        {
            return read_session(directory, session_id, max_bytes);
        });
}

auto file_chat_memory_store::save(std::string session_id,
    std::vector<message> messages)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await blocking_invoke(pool_, context_,
        [directory = directory_, session_id = std::move(session_id),
            messages = std::move(messages),
            max_bytes = options_.max_session_bytes]
        {
            return write_session(directory, session_id, messages, max_bytes);
        });
}

auto file_chat_memory_store::erase(std::string session_id)
    -> task<std::expected<void, std::string>>
{
    co_await mutex_.lock();
    async_lock_guard guard(mutex_, std::adopt_lock);
    co_return co_await blocking_invoke(pool_, context_,
        [directory = directory_, session_id = std::move(session_id)]
        -> std::expected<void, std::string>
        {
            std::error_code error;
            std::filesystem::remove(
                session_path(directory, session_id), error);
            if (error)
                return std::unexpected(
                    "cannot erase memory session: " + error.message());
            return {};
        });
}

} // namespace cnetmod::openai
