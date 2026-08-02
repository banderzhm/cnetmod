module;

#include <cnetmod/config.hpp>

module cnetmod.executor.async_op;

import std;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.file;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;

namespace cnetmod {
namespace detail {

    auto find_delimiter(const_buffer buf, std::string_view delimiter) noexcept
        -> std::optional<std::size_t>
    {
        if (delimiter.empty())
        {
            return std::nullopt;
        }

        auto* first = static_cast<const std::byte*>(buf.data);
        auto* last = first + buf.size;
        auto* delimiter_first =
            reinterpret_cast<const std::byte*>(delimiter.data());
        auto* delimiter_last = delimiter_first + delimiter.size();

        auto pos = std::search(first, last, delimiter_first, delimiter_last);
        if (pos == last)
        {
            return std::nullopt;
        }

        return static_cast<std::size_t>((pos - first) + delimiter.size());
    }

    auto async_read_until_impl(io_context& ctx, socket& sock, dynamic_buffer& buf,
        std::string delimiter, std::size_t max_bytes,
        std::size_t read_chunk_size)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        if (delimiter.empty() || read_chunk_size == 0)
        {
            co_return std::unexpected(make_error_code(errc::invalid_argument));
        }

        while (true)
        {
            if (auto found = find_delimiter(buf.data(), delimiter))
            {
                co_return *found;
            }

            if (buf.readable_bytes() >= max_bytes)
            {
                co_return std::unexpected(make_error_code(errc::no_buffer_space));
            }

            auto to_read =
                std::min(read_chunk_size, max_bytes - buf.readable_bytes());
            auto dst = buf.prepare(to_read);
            auto result = co_await async_read(ctx, sock, dst);
            if (!result)
            {
                co_return std::unexpected(result.error());
            }
            if (*result == 0)
            {
                co_return std::unexpected(make_error_code(errc::end_of_file));
            }

            buf.commit(*result);
        }
    }

    auto async_read_until_impl(io_context& ctx, socket& sock, dynamic_buffer& buf,
        std::string delimiter, cancel_token& token,
        std::size_t max_bytes,
        std::size_t read_chunk_size)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        if (delimiter.empty() || read_chunk_size == 0)
        {
            co_return std::unexpected(make_error_code(errc::invalid_argument));
        }

        while (true)
        {
            if (auto found = find_delimiter(buf.data(), delimiter))
            {
                co_return *found;
            }

            if (buf.readable_bytes() >= max_bytes)
            {
                co_return std::unexpected(make_error_code(errc::no_buffer_space));
            }

            auto to_read =
                std::min(read_chunk_size, max_bytes - buf.readable_bytes());
            auto dst = buf.prepare(to_read);
            auto result = co_await async_read(ctx, sock, dst, token);
            if (!result)
            {
                co_return std::unexpected(result.error());
            }
            if (*result == 0)
            {
                co_return std::unexpected(make_error_code(errc::end_of_file));
            }

            buf.commit(*result);
        }
    }

} // namespace detail

auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    std::string_view delimiter, std::size_t max_bytes,
    std::size_t read_chunk_size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, max_bytes, read_chunk_size);
}

auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    std::string_view delimiter, cancel_token& token,
    std::size_t max_bytes, std::size_t read_chunk_size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, token, max_bytes,
        read_chunk_size);
}

auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    char delimiter, std::size_t max_bytes,
    std::size_t read_chunk_size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, max_bytes, read_chunk_size);
}

auto async_read_until(io_context& ctx, socket& sock, dynamic_buffer& buf,
    char delimiter, cancel_token& token,
    std::size_t max_bytes, std::size_t read_chunk_size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    return detail::async_read_until_impl(
        ctx, sock, buf, std::string{delimiter}, token, max_bytes,
        read_chunk_size);
}

auto async_write_all(io_context& ctx, socket& sock, const_buffer buf)
    -> task<std::expected<void, std::error_code>>
{
    auto* data = static_cast<const std::byte*>(buf.data);
    std::size_t written = 0;

    while (written < buf.size)
    {
        auto result = co_await async_write(
            ctx, sock, const_buffer{data + written, buf.size - written});
        if (!result)
        {
            co_return std::unexpected(result.error());
        }
        if (*result == 0)
        {
            co_return std::unexpected(make_error_code(errc::broken_pipe));
        }
        written += *result;
    }

    co_return {};
}

auto async_write_all(io_context& ctx, socket& sock, const_buffer buf,
    cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    auto* data = static_cast<const std::byte*>(buf.data);
    std::size_t written = 0;

    while (written < buf.size)
    {
        auto result = co_await async_write(
            ctx, sock, const_buffer{data + written, buf.size - written}, token);
        if (!result)
        {
            co_return std::unexpected(result.error());
        }
        if (*result == 0)
        {
            co_return std::unexpected(make_error_code(errc::broken_pipe));
        }
        written += *result;
    }

    co_return {};
}

auto async_file_read_pipeline(io_context& ctx, file& source,
    file_chunk_handler handler,
    file_pipeline_options options)
    -> task<std::expected<std::uint64_t, std::error_code>>
{
    if (!handler || options.chunk_size == 0)
        co_return std::unexpected(make_error_code(errc::invalid_argument));
    if (options.byte_count == 0)
        co_return std::uint64_t{0};

    std::array<std::vector<std::byte>, 2> buffers{
        std::vector<std::byte>(options.chunk_size),
        std::vector<std::byte>(options.chunk_size),
    };

    std::size_t current = 0;
    auto initial_size = static_cast<std::size_t>(std::min<std::uint64_t>(
        options.byte_count, options.chunk_size));
    auto initial = co_await async_file_read(
        ctx, source, mutable_buffer{buffers[current].data(), initial_size},
        options.offset);
    if (!initial)
        co_return std::unexpected(initial.error());
    if (*initial == 0)
        co_return std::uint64_t{0};

    std::size_t current_size = *initial;
    std::uint64_t current_offset = options.offset;
    std::uint64_t transferred = 0;

    while (true)
    {
        const auto consumed_after_current = transferred + current_size;
        const auto remaining =
            options.byte_count - consumed_after_current;
        if (remaining == 0)
        {
            auto handled = co_await handler(
                const_buffer{buffers[current].data(), current_size},
                current_offset);
            if (!handled)
                co_return std::unexpected(handled.error());
            transferred = consumed_after_current;
            break;
        }

        const auto next = current ^ 1;
        const auto next_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(remaining, options.chunk_size));
        auto read_next = async_file_read(
            ctx, source,
            mutable_buffer{buffers[next].data(), next_size},
            current_offset + current_size);
        auto handle_current = handler(
            const_buffer{buffers[current].data(), current_size},
            current_offset);

        auto [read_result, handle_result] = co_await when_all(
            std::move(read_next), std::move(handle_current));
        if (!handle_result)
            co_return std::unexpected(handle_result.error());
        transferred = consumed_after_current;
        if (!read_result)
            co_return std::unexpected(read_result.error());
        if (*read_result == 0)
            break;

        current = next;
        current_size = *read_result;
        current_offset = options.offset + transferred;
    }

    co_return transferred;
}

#ifndef CNETMOD_HAS_IO_URING

auto async_file_read_batch(
    io_context& ctx, std::span<const file_read_request> requests)
    -> task<std::vector<file_io_result>>
{
    std::vector<file_io_result> results;
    results.reserve(requests.size());
    for (const auto& request : requests)
    {
        if (!request.source)
        {
            results.emplace_back(std::unexpected(
                make_error_code(errc::invalid_argument)));
            continue;
        }
        results.emplace_back(co_await async_file_read(
            ctx, *request.source, request.destination, request.offset));
    }
    co_return results;
}

auto async_file_write_batch(
    io_context& ctx, std::span<const file_write_request> requests)
    -> task<std::vector<file_io_result>>
{
    std::vector<file_io_result> results;
    results.reserve(requests.size());
    for (const auto& request : requests)
    {
        if (!request.destination)
        {
            results.emplace_back(std::unexpected(
                make_error_code(errc::invalid_argument)));
            continue;
        }
        results.emplace_back(co_await async_file_write(
            ctx, *request.destination, request.source, request.offset));
    }
    co_return results;
}

#endif

// =============================================================================
// Convenience File I/O
// =============================================================================

auto async_file_read_all(io_context& ctx, const std::filesystem::path& path)
    -> task<std::expected<std::string, std::error_code>>
{
    auto handle = co_await async_file_open(ctx, path, open_mode::read);
    if (!handle)
        co_return std::unexpected(handle.error());

    auto st = co_await async_file_stat(ctx, path);
    if (!st)
    {
        co_await async_file_close(ctx, *handle);
        co_return std::unexpected(st.error());
    }

    std::string content(static_cast<std::size_t>(st->size), '\0');
    if (st->size > 0)
    {
        auto n = co_await async_file_read(ctx, *handle,
            mutable_buffer{content.data(), content.size()}, 0);
        if (!n)
        {
            co_await async_file_close(ctx, *handle);
            co_return std::unexpected(n.error());
        }
        content.resize(*n);
    }

    co_await async_file_close(ctx, *handle);
    co_return content;
}

auto async_file_write_all(io_context& ctx, const std::filesystem::path& path,
    std::string_view content)
    -> task<std::expected<void, std::error_code>>
{
    auto handle = co_await async_file_open(ctx, path,
        open_mode::write | open_mode::create | open_mode::truncate);
    if (!handle)
        co_return std::unexpected(handle.error());

    if (!content.empty())
    {
        std::size_t written = 0;
        while (written < content.size())
        {
            auto n = co_await async_file_write(ctx, *handle,
                const_buffer{content.data() + written, content.size() - written},
                written);
            if (!n)
            {
                co_await async_file_close(ctx, *handle);
                co_return std::unexpected(n.error());
            }
            written += *n;
        }
    }

    co_await async_file_close(ctx, *handle);
    co_return {};
}

} // namespace cnetmod
