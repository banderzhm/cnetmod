module;

#include <cnetmod/config.hpp>
#include <ctime>

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
    #include <cstdlib>
    #include <liburing.h>
#endif

module cnetmod.protocol.http;

import std;
import cnetmod.protocol.http.semantics;
import :parser;
import :request;
import :response;
import :router;
import :cookie;
import :server;
import cnetmod.core.error;
import cnetmod.core.log;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.file;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.coro.spawn;
import cnetmod.coro.wait_group;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.utils.concurrent_containers.striped_hash_map;
import cnetmod.protocol.http.v2.session;
import cnetmod.protocol.http.v2.header_compression;

#if defined(CNETMOD_HAS_IO_URING) && defined(CNETMOD_HAS_IO_URING_BUFFER_RING)
import cnetmod.io.platform.io_uring;
import cnetmod.io.platform.io_uring_recv_service;
import cnetmod.io.platform.io_uring_multishot_recv;
#endif

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::http {

// =============================================================================
// Static File Serving Implementation
// =============================================================================

namespace detail {

    std::atomic<std::uint64_t> next_static_file_state_id{1};

    struct cached_static_file
    {
        std::filesystem::path selected_path;
        std::filesystem::file_time_type modified_at;
        std::uint64_t size{};
        std::string mime_type;
        std::string content_encoding;
        std::shared_ptr<const std::string> content;
        std::chrono::steady_clock::time_point validated_at;
        // The fixed 200 response shape is HPACK-encoded once per resource
        // without dynamic-table references, so HTTP/2 can replay it verbatim.
        std::shared_ptr<const preencoded_header_block> h2_headers;
    };

    struct static_file_state
    {
        explicit static_file_state(static_file_options value)
            : identity(next_static_file_state_id.fetch_add(
                  1, std::memory_order_relaxed)),
              options(std::move(value)), entries(64U, 16U) {}

        std::uint64_t identity;
        static_file_options options;
        concurrent_containers::striped_hash_map<std::string,
            std::shared_ptr<const cached_static_file>>
            entries;
    };

    struct local_static_file_entry
    {
        const static_file_state* state{};
        std::uint64_t identity{};
        std::string relative;
        std::string encoding;
        std::shared_ptr<const cached_static_file> resource;
    };

    auto local_static_file_slot(const static_file_state& state,
        std::string_view relative, std::string_view encoding) noexcept
        -> local_static_file_entry&
    {
        // Request coroutines stay on their owning event-loop thread. A tiny
        // direct-mapped cache therefore removes the shared striped-map lock and
        // shared_ptr refcount from the static-file hit path without weakening
        // cross-loop cache ownership or revalidation.
        thread_local std::array<local_static_file_entry, 8> entries;
        auto hash = std::hash<std::string_view>{}(relative);
        hash ^= std::hash<std::string_view>{}(encoding) +
            0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
        hash ^= static_cast<std::size_t>(state.identity);
        return entries[hash & (entries.size() - 1U)];
    }

    struct static_file_stream
    {
        file source;
        io_context* io{};
        std::uint64_t offset{};
        std::uint64_t remaining{};
    };

    auto refresh_static_file(static_file_state& state, request_context& ctx,
        const std::string& relative, std::string_view accepted,
        std::string_view requested_encoding, std::string key,
        std::chrono::steady_clock::time_point now,
        std::shared_ptr<const cached_static_file> cached)
        -> task<std::expected<std::shared_ptr<const cached_static_file>,
            std::error_code>>
    {
        auto original = state.options.root / relative;
        std::error_code error;
        if (std::filesystem::is_directory(original, error))
            original /= state.options.index_file;
        if (error || !std::filesystem::is_regular_file(original, error))
            co_return std::unexpected(error ? error : std::make_error_code(std::errc::no_such_file_or_directory));

        auto selected = original;
        auto content_encoding = std::string{};
        if (requested_encoding != "identity")
        {
            auto variant = original;
            variant += requested_encoding == "br" ? ".br" : ".gz";
            if (std::filesystem::is_regular_file(variant, error) && !error)
            {
                selected = std::move(variant);
                content_encoding = requested_encoding == "br" ? "br" : "gzip";
            }
            error.clear();
            if (content_encoding.empty() && requested_encoding == "br" &&
                accepts_content_encoding(accepted, "gzip"))
            {
                variant = original;
                variant += ".gz";
                if (std::filesystem::is_regular_file(variant, error) && !error)
                {
                    selected = std::move(variant);
                    content_encoding = "gzip";
                }
                error.clear();
            }
        }

        const auto size = std::filesystem::file_size(selected, error);
        if (error)
            co_return std::unexpected(error);
        const auto modified = std::filesystem::last_write_time(selected, error);
        if (error)
            co_return std::unexpected(error);

        if (cached && cached->selected_path == selected &&
            cached->size == size && cached->modified_at == modified)
        {
            auto refreshed = std::make_shared<cached_static_file>(*cached);
            refreshed->validated_at = now;
            state.entries.insert_or_assign(key, refreshed);
            co_return refreshed;
        }

        std::shared_ptr<const std::string> content;
        if (state.options.cache_max_file_size != 0U &&
            size <= state.options.cache_max_file_size)
        {
            auto loaded = co_await async_file_read_all(ctx.io_ctx(), selected);
            if (!loaded)
                co_return std::unexpected(loaded.error());
            content = std::make_shared<const std::string>(std::move(*loaded));
        }

        auto result = std::make_shared<cached_static_file>(cached_static_file{
            .selected_path = std::move(selected),
            .modified_at = modified,
            .size = size,
            .mime_type = std::string{guess_mime_type(
                original.extension().string())},
            .content_encoding = std::move(content_encoding),
            .content = std::move(content),
            .validated_at = now,
        });
        if (result->content)
        {
            std::vector<v2::header_field> fields{
                {":status", "200"},
                {"content-length", std::to_string(result->size)},
                {"content-type", result->mime_type},
                {"accept-ranges", "bytes"}};
            if (state.options.serve_precompressed)
                fields.push_back({"vary", "Accept-Encoding"});
            if (!result->content_encoding.empty())
                fields.push_back({"content-encoding", result->content_encoding});
            // Capacity 0 forbids dynamic-table insertions, so the resulting
            // block is self-contained and safe to share across connections.
            v2::header_compression encoder(0);
            if (auto block = encoder.encode(fields))
            {
                result->h2_headers =
                    std::make_shared<const preencoded_header_block>(
                        preencoded_header_block{status::ok, std::move(*block)});
            }
        }
        state.entries.insert_or_assign(std::move(key), result);
        co_return result;
    }

    auto serve_dir_body(static_file_state& state, request_context& ctx)
        -> task<void>
    {
        auto rel_path = ctx.wildcard();
        if (rel_path.empty())
            rel_path = state.options.index_file;

        auto rel_str = std::string(rel_path);
        if (rel_str.find("..") != std::string::npos)
        {
            ctx.text(status::forbidden, "403 Forbidden");
            co_return;
        }

        const auto range_hdr = ctx.get_header("Range");
        const auto accepted = ctx.get_header("Accept-Encoding");
        auto requested_encoding = std::string_view{"identity"};
        if (range_hdr.empty() && state.options.serve_precompressed)
        {
            if (accepts_content_encoding(accepted, "br"))
                requested_encoding = "br";
            else if (accepts_content_encoding(accepted, "gzip"))
                requested_encoding = "gzip";
        }
        const auto now = std::chrono::steady_clock::now();
        auto& local = local_static_file_slot(
            state, rel_path, requested_encoding);
        const auto local_hit = local.state == &state &&
            local.identity == state.identity && local.relative == rel_path &&
            local.encoding == requested_encoding && local.resource &&
            now - local.resource->validated_at <
                state.options.revalidate_interval;

        std::shared_ptr<const cached_static_file> resolved_owner;
        const cached_static_file* resolved_resource{};
        if (local_hit)
            resolved_resource = local.resource.get();
        else
        {
            auto key = rel_str;
            key.push_back('\n');
            key.append(requested_encoding);
            auto cached = state.entries.find(key).value_or(nullptr);
            std::expected<std::shared_ptr<const cached_static_file>,
                std::error_code>
                resolved{cached};
            if (!cached || now - cached->validated_at >=
                    state.options.revalidate_interval)
                resolved = co_await refresh_static_file(state, ctx, rel_str,
                    accepted, requested_encoding, std::move(key), now,
                    std::move(cached));
            if (!resolved)
            {
                if (resolved.error() ==
                    std::errc::no_such_file_or_directory)
                    ctx.not_found();
                else
                    ctx.text(status::internal_server_error,
                        "500 Cannot read file");
                co_return;
            }
            resolved_owner = std::move(*resolved);
            local.state = &state;
            local.identity = state.identity;
            local.relative = rel_str;
            local.encoding = requested_encoding;
            local.resource = resolved_owner;
            resolved_resource = resolved_owner.get();
        }
        const auto& resource = *resolved_resource;
        const auto file_size = resource.size;

        std::uint64_t range_start = 0;
        std::uint64_t range_end = file_size == 0 ? 0 : file_size - 1;
        bool is_range = false;

        if (!range_hdr.empty() && range_hdr.starts_with("bytes="))
        {
            auto spec = range_hdr.substr(6);
            auto dash = spec.find('-');
            if (dash != std::string_view::npos)
            {
                auto start_str = spec.substr(0, dash);
                auto end_str = spec.substr(dash + 1);
                if (!start_str.empty())
                {
                    std::from_chars(start_str.data(), start_str.data() + start_str.size(),
                        range_start);
                }
                if (!end_str.empty())
                {
                    std::from_chars(end_str.data(), end_str.data() + end_str.size(),
                        range_end);
                }
                if (file_size != 0 && range_start <= range_end &&
                    range_end < file_size)
                {
                    is_range = true;
                }
            }
        }

        auto& resp = ctx.resp();
        if (!is_range && resource.content && resp.version() == http_version::http_2 &&
            resource.h2_headers)
        {
            // The immutable HPACK block already contains status, length,
            // content type, range support, encoding and Vary. Building the
            // dynamic header map first only to discard it in the H2 encoder
            // adds several string/map allocations to every cache hit.
            resp.set_status(status::ok);
            resp.set_shared_body_preserve_headers(resource.content);
            resp.set_preencoded_h2_headers(resource.h2_headers);
            co_return;
        }
        if (is_range)
        {
            resp.set_status(status::partial_content);
            auto content_len = range_end - range_start + 1;
            resp.set_header("Content-Range", std::format("bytes {}-{}/{}", range_start, range_end, file_size));
            resp.set_header("Content-Length", std::to_string(content_len));
        }
        else
        {
            resp.set_status(status::ok);
            resp.set_header("Content-Length", std::to_string(file_size));
            range_start = 0;
            range_end = file_size == 0 ? 0 : file_size - 1;
        }
        resp.set_header("Content-Type", resource.mime_type);
        resp.set_header("Accept-Ranges", "bytes");
        if (state.options.serve_precompressed)
            resp.set_header("Vary", "Accept-Encoding");
        if (!resource.content_encoding.empty())
            resp.set_header("Content-Encoding", resource.content_encoding);

        const auto content_length = file_size == 0
            ? std::uint64_t{0}
            : range_end - range_start + 1;
        if (resource.content)
        {
            if (!is_range)
            {
                resp.set_shared_body(resource.content);
                resp.set_preencoded_h2_headers(resource.h2_headers);
            }
            else
                resp.set_body(std::string_view{
                    resource.content->data() + range_start,
                    static_cast<std::size_t>(content_length)});
            co_return;
        }

        auto opened = file::open(resource.selected_path, open_mode::read);
        if (!opened)
        {
            ctx.text(status::internal_server_error, "500 Cannot open file");
            co_return;
        }
        auto stream = std::make_shared<static_file_stream>(static_file_stream{
            .source = std::move(*opened),
            .io = &ctx.io_ctx(),
            .offset = range_start,
            .remaining = content_length,
        });
        constexpr std::size_t chunk_size = 64U * 1024U;
        resp.set_body_source(std::make_shared<response_body_source>(
            [stream, chunk_size](cancel_token& token)
                -> task<std::optional<request_body_chunk>>
            {
                if (token.is_cancelled() || stream->remaining == 0)
                    co_return std::nullopt;
                const auto read_size = static_cast<std::size_t>(
                    std::min<std::uint64_t>(stream->remaining, chunk_size));
                request_body_chunk chunk(read_size);
                auto read = co_await async_file_read(*stream->io,
                    stream->source, chunk.writable_view(), stream->offset, token);
                if (!read || *read == 0)
                    co_return std::nullopt;
                chunk.resize(*read);
                stream->offset += *read;
                stream->remaining -= *read;
                co_return chunk;
            },
            content_length));
        co_return;
    }

} // namespace detail

auto serve_dir(static_file_options opts) -> handler_fn
{
    auto state = std::make_shared<detail::static_file_state>(std::move(opts));
    return [state](request_context& ctx) -> task<void>
    {
        return detail::serve_dir_body(*state, ctx);
    };
}

// =============================================================================
// File Upload Implementation
// =============================================================================

namespace detail {

    auto save_upload_body(const upload_options& opts, request_context& ctx)
        -> task<void>
    {
        auto body = ctx.body();
        if (body.empty())
        {
            ctx.text(status::bad_request, "Empty body");
            co_return;
        }

        if (body.size() > opts.max_size)
        {
            ctx.text(status::payload_too_large, "File too large");
            co_return;
        }

        std::error_code ec;
        std::filesystem::create_directories(opts.save_dir, ec);

        auto ct = ctx.get_header("Content-Type");
        auto ct_parsed = parse_content_type(ct);

        if (ct_parsed.mime == "multipart/form-data")
        {
            auto form_r = ctx.parse_form();
            if (!form_r)
            {
                ctx.text(status::bad_request, std::format("Multipart parse error: {}", form_r.error().message()));
                co_return;
            }
            auto& form = **form_r;

            std::string json_files = "[";
            bool first = true;

            for (auto& ff : form.all_files())
            {
                auto fname = ff.filename;
                if (fname.empty())
                    fname = opts.default_filename;
                if (fname.find("..") != std::string::npos ||
                    fname.find('/') != std::string::npos ||
                    fname.find('\\') != std::string::npos)
                {
                    fname = opts.default_filename;
                }

                auto full_path = opts.save_dir / fname;
                auto f = file::open(full_path, open_mode::write | open_mode::create | open_mode::truncate);
                if (!f)
                    continue;

                auto wr = co_await async_file_write(
                    ctx.io_ctx(), *f, const_buffer{ff.data.data(), ff.data.size()});
                if (!wr)
                    continue;

                if (!first)
                    json_files += ",";
                json_files += std::format(
                    R"({{"field":"{}","filename":"{}","size":{},"type":"{}"}})",
                    ff.field_name, fname, ff.data.size(), ff.content_type);
                first = false;
            }
            json_files += "]";

            std::string json_fields = "[";
            first = true;
            for (auto& field : form.all_fields())
            {
                if (!first)
                    json_fields += ",";
                json_fields += std::format(R"({{"name":"{}","value":"{}"}})", field.name,
                    field.value);
                first = false;
            }
            json_fields += "]";

            ctx.json(
                status::ok,
                std::format(
                    R"({{"files":{},"fields":{},"file_count":{},"field_count":{}}})",
                    json_files, json_fields, form.file_count(), form.field_count()));
            co_return;
        }

        // Raw body mode
        std::string filename = opts.default_filename;
        auto qs = ctx.query_string();
        auto name_pos = qs.find("name=");
        if (name_pos != std::string_view::npos)
        {
            auto start = name_pos + 5;
            auto end = qs.find('&', start);
            auto name = (end != std::string_view::npos) ? qs.substr(start, end - start)
                                                        : qs.substr(start);
            if (!name.empty())
            {
                auto name_str = std::string(name);
                if (name_str.find("..") == std::string::npos &&
                    name_str.find('/') == std::string::npos &&
                    name_str.find('\\') == std::string::npos)
                {
                    filename = std::move(name_str);
                }
            }
        }

        auto full_path = opts.save_dir / filename;
        auto f = file::open(full_path, open_mode::write | open_mode::create | open_mode::truncate);
        if (!f)
        {
            ctx.text(status::internal_server_error, "Cannot create file");
            co_return;
        }

        auto wr = co_await async_file_write(ctx.io_ctx(), *f,
            const_buffer{body.data(), body.size()});
        if (!wr)
        {
            ctx.text(status::internal_server_error, "Write failed");
            co_return;
        }

        ctx.json(status::ok, std::format(R"({{"filename":"{}","size":{}}})", filename, body.size()));
        co_return;
    }

    auto not_found_handler(request_context& ctx) -> task<void>
    {
        ctx.not_found();
        co_return;
    }

    auto unmatched_route_handler(const router& routes, std::string_view path)
        -> handler_fn
    {
        auto methods = routes.allowed_methods(path);
        auto custom = routes.unmatched_handler();
        if (methods.empty())
        {
            if (!custom)
                return not_found_handler;
            return [custom = std::move(custom)](
                       request_context& context) -> task<void>
            {
                co_await custom(context, unmatched_request{.status = status::not_found});
            };
        }

        std::string allow;
        for (const auto method : methods)
        {
            if (!allow.empty())
                allow += ", ";
            allow += method_to_string(method);
        }
        return [allow = std::move(allow), methods = std::move(methods),
                   custom = std::move(custom)](
                   request_context& context) -> task<void>
        {
            context.resp().set_header("Allow", allow);
            if (custom)
            {
                co_await custom(context, unmatched_request{.status = status::method_not_allowed, .allowed = methods});
                co_return;
            }
            context.text(status::method_not_allowed,
                "405 Method Not Allowed");
            co_return;
        };
    }

    auto take_h2_response(response& output, bool borrow_body = false)
        -> v2::server_response
    {
        v2::server_response result;
        result.status = static_cast<std::uint32_t>(output.status_code());
        result.shared_body = output.take_shared_body();
        result.body_source = output.take_body_source();
        if (!result.shared_body && !result.body_source)
        {
            if (borrow_body)
                result.borrowed_body_text = output.body();
            else
                result.body_text = output.take_body();
        }
        result.preencoded_headers = output.take_preencoded_h2_headers();
        if (result.preencoded_headers)
        {
            // The cached block already carries the fixed status and fields.
            // Consume the builder's values without materializing strings.
            output.take_headers([](std::string, std::string) {});
            (void)output.take_trailers();
            return result;
        }
        result.headers.reserve(output.header_count());
        output.take_headers(
            [&result](std::string name, std::string value)
            {
                for (auto& character : name)
                    if (character >= 'A' && character <= 'Z')
                        character = static_cast<char>(
                            character + ('a' - 'A'));
                result.headers.push_back(
                    {std::move(name), std::move(value)});
            });
        auto trailers = output.take_trailers();
        result.trailers.reserve(trailers.size());
        for (auto& [name, value] : trailers)
        {
            for (auto& character : name)
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character + ('a' - 'A'));
            result.trailers.push_back(
                {std::move(name), std::move(value)});
        }
        return result;
    }

} // namespace detail

auto save_upload(upload_options opts) -> handler_fn
{
    auto opts_ptr = std::make_shared<upload_options>(std::move(opts));
    return [opts_ptr](request_context& ctx) -> task<void>
    {
        return detail::save_upload_body(*opts_ptr, ctx);
    };
}

auto take_h2_response(response& output) -> v2::server_response
{
    return detail::take_h2_response(output);
}

// =============================================================================
// Date Cache Implementation
// =============================================================================

auto date_cache::get() -> std::array<char, 29U>
{
    const auto now = std::time(nullptr);
    if (now != cached_time_.load(std::memory_order_acquire) &&
        !refreshing_.test_and_set(std::memory_order_acquire))
    {
        std::tm gmt{};
#ifdef CNETMOD_PLATFORM_WINDOWS
        gmtime_s(&gmt, &now);
#else
        gmtime_r(&now, &gmt);
#endif
        char buf[30];
        std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &gmt);

        // Odd means that a refresh owns the words.  The words themselves are
        // atomic so this is a standard C++ data-race-free seqlock, rather
        // than relying on an unsafe concurrent memcpy of ordinary chars.
        sequence_.fetch_add(1U, std::memory_order_acq_rel);
        std::array<std::uint64_t, word_count> snapshot{};
        std::memcpy(snapshot.data(), buf, date_size);
        for (std::size_t index{}; index < word_count; ++index)
            words_[index].store(snapshot[index], std::memory_order_relaxed);
        cached_time_.store(now, std::memory_order_release);
        sequence_.fetch_add(1U, std::memory_order_release);
        sequence_.notify_all();
        refreshing_.clear(std::memory_order_release);
    }

    for (;;)
    {
        const auto before = sequence_.load(std::memory_order_acquire);
        if ((before & 1U) != 0U)
        {
            sequence_.wait(before, std::memory_order_relaxed);
            continue;
        }

        std::array<std::uint64_t, word_count> snapshot{};
        for (std::size_t index{}; index < word_count; ++index)
            snapshot[index] = words_[index].load(std::memory_order_relaxed);

        if (sequence_.load(std::memory_order_acquire) == before)
        {
            std::array<char, date_size> result{};
            std::memcpy(result.data(), snapshot.data(), result.size());
            return result;
        }
    }
}

// =============================================================================
// Server Implementation
// =============================================================================

struct server::conn_count_guard
{
    std::atomic<std::size_t>* counter;

    conn_count_guard(std::atomic<std::size_t>& c) noexcept
        : counter(&c)
    {
        counter->fetch_add(1, std::memory_order_relaxed);
    }

    conn_count_guard(std::atomic<std::size_t>& c, std::adopt_lock_t) noexcept
        : counter(&c) {}

    [[nodiscard]] static auto try_acquire(
        std::atomic<std::size_t>& counter, std::size_t limit) noexcept
        -> std::optional<conn_count_guard>
    {
        if (limit == 0)
            return conn_count_guard{counter};
        auto current = counter.load(std::memory_order_relaxed);
        while (current < limit)
        {
            if (counter.compare_exchange_weak(current, current + 1,
                    std::memory_order_acquire, std::memory_order_relaxed))
                return conn_count_guard{counter, std::adopt_lock};
        }
        return std::nullopt;
    }

    ~conn_count_guard()
    {
        if (counter)
            counter->fetch_sub(1, std::memory_order_release);
    }

    conn_count_guard(conn_count_guard&& other) noexcept
        : counter(std::exchange(other.counter, nullptr)) {}

    conn_count_guard(const conn_count_guard&) = delete;
    auto operator=(const conn_count_guard&) -> conn_count_guard& = delete;
};

/**
 * @brief Tracks a socket only while its owning coroutine can use it.
 *
 * Intrusive registration does not allocate. Removal precedes destruction of
 * the socket parameter, preventing shutdown from accessing a retired socket.
 */
struct server::connection_registration
{
    server& owner;
    socket& client;
    cancel_token cancellation;
    cancel_token write_cancellation;
    connection_registration* previous{};
    connection_registration* next{};

    connection_registration(server& server, socket& connection) noexcept
        : owner(server), client(connection)
    {
        concurrent_containers::exclusive_latch_guard lock{owner.connections_latch_};
        next = owner.connections_;
        if (next)
            next->previous = this;
        owner.connections_ = this;
        if (owner.connections_aborted_)
        {
            cancellation.cancel();
            write_cancellation.cancel();
            client.shutdown_both();
        }
    }

    ~connection_registration()
    {
        concurrent_containers::exclusive_latch_guard lock{owner.connections_latch_};
        if (previous)
            previous->next = next;
        else
            owner.connections_ = next;
        if (next)
            next->previous = previous;
    }

    connection_registration(const connection_registration&) = delete;
    auto operator=(const connection_registration&) -> connection_registration& = delete;
};

struct server::recv_service_state
{
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
    struct shard
    {
        io_context* owner{};
        std::weak_ptr<io_uring_recv_service> service;
    };

    /// One provided-buffer ring per event loop, shared by every server on
    /// that loop. A process commonly runs several listeners (plain, h2c, TLS)
    /// on the same workers; a ring per server multiplied the 2 MiB of
    /// receive blocks by the listener count without adding capacity.
    struct loop_registry
    {
        concurrent_containers::atomic_rw_latch latch;
        std::vector<std::pair<std::uint64_t, std::weak_ptr<io_uring_recv_service>>> loops;
    };

    static auto registry() -> loop_registry&
    {
        static loop_registry instance;
        return instance;
    }

    static auto shared_service(io_uring_context& uring)
        -> std::shared_ptr<io_uring_recv_service>
    {
        auto& shared = registry();
        concurrent_containers::exclusive_latch_guard lock{shared.latch};
        std::erase_if(shared.loops, [](const auto& entry)
            {
                return entry.second.expired();
            });
        for (const auto& [id, service] : shared.loops)
            if (id == uring.instance_id())
                if (auto existing = service.lock())
                    return existing;
        static std::atomic<std::uint32_t> next_group{1};
        const auto group = static_cast<std::uint16_t>(
            next_group.fetch_add(1, std::memory_order_relaxed));
        auto created = std::make_shared<io_uring_recv_service>(uring, group, 8192, 256);
        uring.retain_owner_resource(created);
        shared.loops.emplace_back(uring.instance_id(), created);
        return created;
    }

    void add(io_context& owner)
    {
        if (dynamic_cast<io_uring_context*>(&owner))
            shards.push_back({&owner, {}});
    }

    [[nodiscard]] auto find(io_context& owner) noexcept
        -> io_uring_recv_service*
    {
        for (auto& shard : shards)
            if (shard.owner == &owner)
            {
                if (auto existing = shard.service.lock())
                    return existing.get();
                if (!owner.running_in_this_thread())
                    return nullptr;
                try
                {
                    auto created = shared_service(
                        static_cast<io_uring_context&>(owner));
                    shard.service = created;
                    return created.get();
                }
                catch (const std::exception& error)
                {
                    logger::warn("HTTP multishot recv disabled for event loop: {}",
                        error.what());
                    return nullptr;
                }
            }
        return nullptr;
    }

    std::vector<shard> shards;
#else
    void add(io_context&) noexcept {}
#endif
};

server::server(io_context& ctx)
    : ctx_(ctx), recv_services_(std::make_unique<recv_service_state>())
{
    recv_services_->add(ctx_);
}

server::server(server_context& sctx)
    : ctx_(sctx.accept_io()), sctx_(&sctx), recv_services_(std::make_unique<recv_service_state>())
{
    for (auto* worker : sctx.worker_ios())
        recv_services_->add(*worker);
}

server::~server() = default;

#ifdef CNETMOD_HAS_SSL
void server::set_ssl_context(ssl_context& ssl_ctx)
{
    ssl_ctx_ = &ssl_ctx;
}
#endif

auto server::listen(std::string_view host, std::uint16_t port,
    socket_options opts, int backlog)
    -> std::expected<void, std::error_code>
{
    auto addr_r = ip_address::from_string(host);
    if (!addr_r)
        return std::unexpected(addr_r.error());

    auto ep = cnetmod::endpoint{*addr_r, port};
    opts.reuse_address = true;

#ifdef CNETMOD_PLATFORM_LINUX
    if (sctx_ && sctx_->worker_count() > 1)
    {
        // Shared-nothing admission: the kernel hashes each flow directly to a
        // listener owned by its final event loop. No accepted socket or
        // connection coroutine crosses threads.
        opts.reuse_port = true;
        auto workers = sctx_->worker_ios();
        worker_acceptors_.reserve(workers.size());
        worker_accept_cancellations_.reserve(workers.size());
        auto selected_endpoint = ep;
        for (std::size_t index{}; index < workers.size(); ++index)
        {
            auto listener = std::make_unique<tcp::acceptor>(*workers[index]);
            auto opened = listener->open(selected_endpoint, opts, backlog);
            if (!opened)
            {
                worker_acceptors_.clear();
                worker_accept_cancellations_.clear();
                return std::unexpected(opened.error());
            }
            if (index == 0 && port == 0)
            {
                auto local = listener->native_socket().local_endpoint();
                if (!local)
                    return std::unexpected(local.error());
                selected_endpoint = *local;
            }
            worker_acceptors_.push_back(std::move(listener));
            worker_accept_cancellations_.push_back(
                std::make_unique<cancel_token>());
        }
        host_ = std::string(host);
        port_ = selected_endpoint.port();
        return {};
    }
#endif

    acc_ = std::make_unique<tcp::acceptor>(ctx_);
    auto r = acc_->open(ep, opts, backlog);
    if (!r)
        return std::unexpected(r.error());

    host_ = std::string(host);
    port_ = port;
    return {};
}

auto server::local_endpoint()
    -> std::expected<cnetmod::endpoint, std::error_code>
{
    if (!worker_acceptors_.empty())
        return worker_acceptors_.front()->native_socket().local_endpoint();
    if (!acc_)
        return std::unexpected(make_error_code(errc::bad_descriptor));
    return acc_->native_socket().local_endpoint();
}

void server::set_router(router r)
{
    router_ = std::move(r);
}

void server::set_h2_sync_adapter(v2::sync_server_handler handler,
    v2::buffered_request_filter filter)
{
    if (static_cast<bool>(handler) != static_cast<bool>(filter))
        throw std::invalid_argument("HTTP/2 synchronous adapters require a matching request filter");
    h2_sync_adapter_ = std::move(handler);
    h2_sync_filter_ = std::move(filter);
}

void server::use(middleware_fn mw)
{
    if (mw)
        middlewares_.push_back(std::move(mw));
}

void server::set_max_connections(std::size_t n)
{
    max_connections_ = n;
}

void server::set_response_header_options(
    response_header_options options) noexcept
{
    response_headers_ = options;
}

auto server::active_connections() const noexcept -> std::size_t
{
    return active_connections_.load(std::memory_order_acquire);
}

void server::stop()
{
    running_ = false;
    accept_cancellation_.cancel();
    for (auto& cancellation : worker_accept_cancellations_)
        cancellation->cancel();
    if (acc_ && !accept_cancellation_.pending_.load(std::memory_order_acquire))
        acc_->close();
}

void server::abort_connections() noexcept
{
    concurrent_containers::exclusive_latch_guard lock{connections_latch_};
    connections_aborted_ = true;
    for (auto* connection = connections_; connection; connection = connection->next)
    {
        // Cancellation adapters marshal kernel cancellation to the operation's
        // owning io_context. shutdown() is deliberately issued as well: a
        // persistent multishot receive is not the token's currently registered
        // one-shot operation, so token cancellation alone cannot wake it. The
        // descriptor remains owned by the connection coroutine and is closed
        // there after the terminal receive completion; this avoids destroying a
        // worker-loop registration from the control loop.
        connection->cancellation.cancel();
        connection->write_cancellation.cancel();
        connection->client.shutdown_both();
    }
}

// =============================================================================
// Server Run Method
// =============================================================================

namespace {

    /**
     * @brief Reports an isolated connection failure without exposing exception text.
     *
     * Called while the guarded dispatch still owns the connection coroutine.
     * Diagnostic failures are contained by spawn_guarded.
     */
    void report_connection_failure(std::exception_ptr failure)
    {
        std::error_code code = std::make_error_code(std::errc::io_error);
        try
        {
            if (failure)
                std::rethrow_exception(failure);
        }
        catch (const std::system_error& error)
        {
            code = error.code();
        }
        catch (const std::bad_alloc&)
        {
            code = std::make_error_code(std::errc::not_enough_memory);
        }
        catch (...)
        {
        }
        logger::error("HTTP connection task failed: category={}, code={}", code.category().name(), code.value());
    }

    /**
     * @brief Delivers an admission response before draining the rejected peer.
     *
     * The caller bounds both writing and draining with one cancellation deadline.
     * Half-closing avoids discarding the response when unread request bytes remain.
     */
    auto reject_connection(io_context& io, socket& client, cancel_token& token)
        -> task<std::expected<void, std::error_code>>
    {
        response resp(status::too_many_requests);
        resp.set_header("Connection", "close");
        resp.set_header("Retry-After", "1");
        resp.set_body(std::string_view{"429 Too Many Requests: too many connections"});
        const auto data = resp.serialize();
        const auto written = co_await async_write_all(io, client,
            const_buffer{data.data(), data.size()}, token);
        if (!written)
            co_return std::unexpected(written.error());
        client.shutdown_send();
        std::array<std::byte, 4096> discarded;
        while (!token.is_cancelled())
        {
            const auto received = co_await async_read(io, client,
                mutable_buffer{discarded.data(), discarded.size()}, token);
            if (!received || *received == 0)
                break;
        }
        co_return {};
    }

} // namespace

auto server::run() -> task<void>
{
    constexpr std::size_t accept_batch_limit = 64;

    if (!worker_acceptors_.empty())
    {
        async_wait_group completed;
        completed.add(static_cast<int>(worker_acceptors_.size()));
        running_ = true;
        for (std::size_t index{}; index < worker_acceptors_.size(); ++index)
        {
            auto* listener = worker_acceptors_[index].get();
            auto* cancellation = worker_accept_cancellations_[index].get();
            auto* owner = &listener->context();
            spawn_guarded<report_connection_failure>(*owner,
                run_accept_shard(
                    *listener, *owner, *cancellation, completed));
        }
        co_await completed.wait();
        co_return;
    }

    accept_cancellation_.reset();
    running_ = true;
    while (running_)
    {
        auto r = co_await async_accept(ctx_, acc_->native_socket(), accept_cancellation_);
        if (!r)
        {
            if (!running_)
                break;
            continue;
        }

        if (!running_)
            break;

        // Connection limit check
        if (max_connections_ > 0 &&
            active_connections_.load(std::memory_order_relaxed) >=
                max_connections_)
        {
            // A rejected peer cannot hold the listener indefinitely. The
            // deadline wrapper joins its timer and I/O before token reuse.
            (void)co_await with_timeout(ctx_, std::chrono::seconds{1},
                reject_connection(ctx_, *r, accept_cancellation_), accept_cancellation_);
            r->close();
            if (running_)
                accept_cancellation_.reset();
            continue;
        }

        dispatch_connection(std::move(*r));

        // One readiness/completion notification can represent many queued
        // connections. Drain a bounded batch before submitting the next
        // asynchronous accept, while retaining a fairness point for timers and
        // shutdown work after at most accept_batch_limit admissions.
        for (std::size_t accepted = 1;
            running_ && accepted < accept_batch_limit; ++accepted)
        {
            if (max_connections_ > 0 &&
                active_connections_.load(std::memory_order_relaxed) >=
                    max_connections_)
                break;
            auto next = acc_->try_accept();
            if (!next || !*next)
                break;
            dispatch_connection(std::move(**next));
        }
    }
    if (acc_)
        acc_->close();
}

auto server::run_accept_shard(tcp::acceptor& listener, io_context& owner,
    cancel_token& cancellation, async_wait_group& completed) -> task<void>
{
    struct completion_guard
    {
        async_wait_group& group;

        ~completion_guard()
        {
            group.done();
        }
    } completion{completed};

    constexpr std::size_t accept_batch_limit = 64;
    cancellation.reset();
    while (running_)
    {
        auto accepted = co_await async_accept(
            owner, listener.native_socket(), cancellation);
        if (!accepted)
        {
            if (!running_)
                break;
            continue;
        }
        if (!running_)
            break;

        auto admit = [&](socket client, conn_count_guard slot)
        {
            spawn_guarded<report_connection_failure>(owner,
                handle_connection(
                    std::move(client), owner, std::move(slot)));
        };

        auto slot = conn_count_guard::try_acquire(
            active_connections_, max_connections_);
        if (slot)
            admit(std::move(*accepted), std::move(*slot));
        else
        {
            (void)co_await with_timeout(owner, std::chrono::seconds{1},
                reject_connection(owner, *accepted, cancellation),
                cancellation);
            accepted->close();
            if (running_)
                cancellation.reset();
        }

        for (std::size_t count = 1;
            running_ && count < accept_batch_limit; ++count)
        {
            auto next = listener.try_accept();
            if (!next || !*next)
                break;
            auto next_slot = conn_count_guard::try_acquire(
                active_connections_, max_connections_);
            if (!next_slot)
            {
                (**next).close();
                break;
            }
            admit(std::move(**next), std::move(*next_slot));
        }
    }
    listener.close();
}

void server::dispatch_connection(socket client)
{
    if (sctx_)
    {
        // Multi-core mode: round-robin dispatch to worker io_context.
        auto& worker = sctx_->next_worker_io();
        spawn_guarded<report_connection_failure>(worker,
            handle_connection(std::move(client), worker,
                conn_count_guard{active_connections_}));
        return;
    }

    // Single-threaded mode: handle on the listener io_context.
    spawn_guarded<report_connection_failure>(ctx_,
        handle_connection(std::move(client), ctx_,
            conn_count_guard{active_connections_}));
}

// =============================================================================
// Connection Handling
// =============================================================================

auto server::handle_connection(socket client, io_context& io,
    conn_count_guard /* ownership */) -> task<void>
{
    connection_registration registration{*this, client};

#ifdef CNETMOD_PLATFORM_WINDOWS
    // A local, small HTTP response commonly completes WSASend inline.  The
    // IOCP awaiters already handle that path without suspension, but Windows
    // only suppresses the otherwise redundant completion-port notification
    // when this mode is enabled on the connected socket.  This is a best
    // effort capability: an unusual Winsock provider may decline it without
    // changing HTTP semantics.
    (void)client.apply_options(
        {.non_blocking = false, .skip_completion_on_success = true});
#endif

#ifdef CNETMOD_HAS_SSL
    // --- TLS path ---
    if (ssl_ctx_)
    {
        ssl_stream ssl(*ssl_ctx_, io, client);
        ssl.set_accept_state();
        auto hr = co_await ssl.async_handshake(registration.cancellation);
        if (!hr)
        {
            co_return;
        }

        if (ssl.get_alpn_selected() == "h2")
        {
            (void)client.apply_options({.non_blocking = false, .no_delay = true});
            co_await handle_h2_tls(client, io, ssl, registration.cancellation,
                registration.write_cancellation);
        }
        else
        {
            co_await handle_h1_tls(client, io, ssl, registration.cancellation);
        }
        co_return;
    }
#endif

    // --- Cleartext path ---
    // Read initial data and check for HTTP/2 client connection preface
    constexpr std::string_view h2_client_magic =
        "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    auto peek_buf = std::make_unique<std::array<std::byte, 8192>>();
    auto peek_rd = co_await async_read(
        io, client, mutable_buffer{peek_buf->data(), peek_buf->size()},
        registration.cancellation);
    if (!peek_rd || *peek_rd == 0)
    {
        co_return;
    }
    auto peek_len = *peek_rd;

    auto matches_h2_preface_prefix = [&]() -> bool
    {
        if (peek_len > h2_client_magic.size())
            return false;
        auto view = std::string_view{
            reinterpret_cast<const char*>(peek_buf->data()),
            peek_len,
        };
        return h2_client_magic.starts_with(view);
    };

    while (peek_len < h2_client_magic.size() && matches_h2_preface_prefix())
    {
        auto rd = co_await async_read(
            io, client,
            mutable_buffer{peek_buf->data() + peek_len,
                peek_buf->size() - peek_len},
            registration.cancellation);
        if (!rd || *rd == 0)
        {
            co_return;
        }
        peek_len += *rd;
    }

    if (peek_len >= h2_client_magic.size() &&
        std::string_view{reinterpret_cast<const char*>(peek_buf->data()),
            h2_client_magic.size()} == h2_client_magic)
    {
        // TCP_NODELAY reduces latency for HTTP/2 multiplexed frames
        (void)client.apply_options({.non_blocking = false, .no_delay = true});

        co_await handle_h2(client, io, {peek_buf->data(), peek_len},
            registration.cancellation, registration.write_cancellation);
        co_return;
    }

    // HTTP/1.1 cleartext — feed already-read bytes to parser
    co_await handle_h1_clear(
        client, io, std::move(peek_buf), peek_len,
        registration.cancellation);
}

auto server::make_h2_streaming_handler(io_context& io, socket& client)
    -> v2::borrowed_streaming_server_handler
{
    return [this, &io, &client](v2::server_request& request,
               cancel_token& cancellation) -> task<v2::server_response>
    {
        std::string_view method = "GET";
        std::string_view uri = "/";
        std::array<request_header_view, 32> inline_headers{};
        std::vector<request_header_view> overflow_headers;
        std::size_t header_count{};
        auto add_header = [&](const v2::header_field& field)
        {
            const request_header_view view{field.name, field.value};
            if (overflow_headers.empty() &&
                header_count < inline_headers.size())
            {
                inline_headers[header_count++] = view;
                return;
            }
            if (overflow_headers.empty())
            {
                overflow_headers.reserve(request.headers.size());
                overflow_headers.insert(overflow_headers.end(),
                    inline_headers.begin(),
                    inline_headers.begin() +
                        static_cast<std::ptrdiff_t>(header_count));
            }
            overflow_headers.push_back(view);
        };
        for (const auto& field : request.headers)
        {
            if (field.name == ":method")
                method = field.value;
            else if (field.name == ":path")
                uri = field.value;
            else if (!field.name.starts_with(':'))
                add_header(field);
        }
        const auto headers = overflow_headers.empty()
            ? std::span<const request_header_view>{
                  inline_headers.data(), header_count}
            : std::span<const request_header_view>{overflow_headers};
        const auto query = uri.find('?');
        const auto path = query == std::string::npos
            ? std::string_view(uri)
            : std::string_view(uri).substr(0, query);
        auto match = router_.match(method, path);
        response output(status::ok, http_version::http_2);
        if (response_headers_.emit_server)
            output.set_header("Server", "cnetmod");
        if (response_headers_.emit_date)
            output.set_cached_date_header(date_cache_.get());
        route_params params;
        handler_fn route;
        sync_handler_fn sync_route;
        if (match)
        {
            route = std::move(match->handler);
            sync_route = std::move(match->sync_handler);
            params = std::move(match->params);
        }
        else
        {
            route = detail::unmatched_route_handler(router_, path);
        }
        if (!request.body_stream && match && match->request_stream)
        {
            request.body_stream = std::make_shared<request_body_stream>(
                match->request_stream->chunk_capacity,
                match->request_stream->max_bytes);
            request.body_stream->close();
        }
        std::string buffered_body;
        if (request.body_stream && (!match || !match->request_stream))
        {
            while (auto chunk = co_await request.body_stream->receive())
                buffered_body.append(
                    reinterpret_cast<const char*>(chunk->data()), chunk->size());
        }
        if (request.body_stream && match && match->request_stream)
            request.body_stream->constrain_limit(match->request_stream->max_bytes);
        const auto body = request.body.empty()
            ? std::string_view{buffered_body}
            : std::string_view{reinterpret_cast<const char*>(request.body.data()),
                  request.body.size()};
        request_context context(io, client, method, uri, headers, body, output,
            std::move(params), match && match->request_stream ? request.body_stream : std::shared_ptr<request_body_stream>{});
        if (cancellation.is_cancelled())
            context.cancel_pending_operations();
        if (middlewares_.empty() && sync_route)
            sync_route(context);
        else if (middlewares_.empty())
            co_await route(context);
        else if (sync_route)
        {
            handler_fn adapted = [&sync_route](request_context& request_context)
                -> task<void>
            {
                sync_route(request_context);
                co_return;
            };
            co_await execute_chain(context, adapted);
        }
        else
            co_await execute_chain(context, route);
        if (request.body_stream && request.body_stream->error() == std::make_error_code(std::errc::message_size))
            output.set_status(status::payload_too_large);
        co_return detail::take_h2_response(output);
    };
}

auto server::make_h2_sync_handler(io_context& io, socket& client)
    -> v2::sync_server_handler
{
    auto output = std::make_shared<response>(status::ok,
        http_version::http_2);
    return [this, &io, &client, output = std::move(output)](
               v2::server_request& request,
               cancel_token& cancellation)
               -> std::optional<v2::server_response>
    {
        if (!middlewares_.empty() || request.body_stream)
            return std::nullopt;

        if (h2_sync_adapter_)
            if (auto response = h2_sync_adapter_(request, cancellation))
                return response;

        std::string_view method = "GET";
        std::string_view uri = "/";
        std::array<request_header_view, 32> headers{};
        std::size_t header_count{};
        for (const auto& field : request.headers)
        {
            if (field.name == ":method")
                method = field.value;
            else if (field.name == ":path")
                uri = field.value;
            else if (!field.name.starts_with(':'))
            {
                if (header_count == headers.size())
                    return std::nullopt;
                headers[header_count++] = {field.name, field.value};
            }
        }
        const auto query = uri.find('?');
        const auto path = query == std::string_view::npos
            ? uri
            : uri.substr(0, query);
        auto match = router_.match_borrowed(method, path);
        if (!match || !match->route->sync_handler ||
            match->route->request_stream)
            return std::nullopt;

        output->reset(status::ok, http_version::http_2);
        if (response_headers_.emit_server)
            output->set_header("Server", "cnetmod");
        if (response_headers_.emit_date)
            output->set_cached_date_header(date_cache_.get());
        const auto body = std::string_view{
            reinterpret_cast<const char*>(request.body.data()),
            request.body.size()};
        request_context context(io, client, method, uri,
            std::span<const request_header_view>{headers.data(), header_count},
            body, *output, std::move(match->params), {});
        if (cancellation.is_cancelled())
            context.cancel_pending_operations();
        match->route->sync_handler(context);

        return detail::take_h2_response(*output, true);
    };
}

auto server::make_h2_buffered_request_filter() -> v2::buffered_request_filter
{
    const bool sync_routes = std::ranges::any_of(router_.entries_,
        [](const auto& route)
        {
            return static_cast<bool>(route.sync_handler);
        });
    if (!middlewares_.empty() || (!sync_routes && !h2_sync_filter_))
        return {};
    return [this, sync_routes](std::span<const v2::header_field> headers)
    {
        if (h2_sync_filter_ && h2_sync_filter_(headers))
            return true;
        if (!sync_routes)
            return false;
        std::string_view method = "GET", uri = "/";
        for (const auto& field : headers)
        {
            if (field.name == ":method")
                method = field.value;
            else if (field.name == ":path")
                uri = field.value;
        }
        const auto path = uri.substr(0, uri.find('?'));
        auto match = router_.match_borrowed(method, path);
        return match && match->route->sync_handler && !match->route->request_stream;
    };
}

auto server::handle_h2(socket& client, io_context& io,
    std::span<const std::byte> initial, cancel_token& cancellation,
    cancel_token& write_cancellation) -> task<void>
{
    // Reads and writes overlap on multiplexed connections. A cancel_token
    // owns one pending operation; sharing it loses the reader's completion.
    // abort_connections() cancels both tokens and shuts down the socket.
    auto reader = [&io, &client, &cancellation](mutable_buffer buffer)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        co_return co_await async_read(io, client, buffer, cancellation);
    };
    auto writer = [&io, &client, &write_cancellation](const_buffer buffer)
        -> task<std::expected<void, std::error_code>>
    {
        co_return co_await async_write_all(io, client, buffer, write_cancellation);
    };
    auto filter = make_h2_buffered_request_filter();
    const bool buffer_complete = static_cast<bool>(filter);
    v2::session session(io, client, make_h2_streaming_handler(io, client), {},
        std::move(reader), std::move(writer),
        make_h2_sync_handler(io, client), buffer_complete, std::move(filter));
    co_await session.run(initial);
}

// =============================================================================
// HTTP/1.1 Cleartext Handler
// =============================================================================

auto server::handle_h1_clear(socket& client, io_context& io,
    std::unique_ptr<std::array<std::byte, 8192>> initial_buffer,
    std::size_t initial_len,
    cancel_token& cancellation)
    -> task<void>
{
    auto* initial_data = reinterpret_cast<const char*>(initial_buffer->data());
    bool keep_alive = true;
    response resp(status::ok);
    std::string pending_responses;
    request_parser parser;
    std::unique_ptr<std::array<std::byte, 8192>> fallback_buffer;
    bool parse_buffered_request = false;

#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
    std::unique_ptr<io_uring_multishot_recv> recv_stream;
    if (auto* service = recv_services_->find(io))
    {
        try
        {
            recv_stream = service->make_receiver(client);
        }
        catch (...)
        {
            recv_stream.reset();
        }
    }

    auto stop_recv = [&]() -> task<void>
    {
        if (recv_stream)
            (void)co_await recv_stream->async_stop();
    };
#endif

#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
    // Keep every early return inside a child task so the owning coroutine can
    // always join the persistent io_uring receive before its operation state
    // is destroyed.  Handler and middleware exceptions are preserved after
    // the kernel operation has settled. Other backends retain the direct loop
    // below and therefore pay no additional coroutine-frame cost.
    auto serve_connection = [&]() -> task<void>
    {
#endif
        while (keep_alive)
        {
            if (parse_buffered_request)
            {
                parse_buffered_request = false;
                if (!parser.reset_for_next_message())
                    co_return;
                if (auto parsed = parser.consume_buffered(); !parsed)
                {
                    if (!pending_responses.empty())
                        (void)co_await async_write_all(io, client,
                            const_buffer{pending_responses.data(),
                                pending_responses.size()},
                            cancellation);
                    co_return;
                }
                // Only coalesce responses while complete requests are already in
                // memory. If the next pipelined request is fragmented, release the
                // valid preceding responses before waiting for more input.
                if (!parser.ready() && !pending_responses.empty())
                {
                    auto pending = co_await async_write_all(io, client,
                        const_buffer{pending_responses.data(),
                            pending_responses.size()},
                        cancellation);
                    if (!pending)
                        co_return;
                    pending_responses.clear();
                }
            }
            else
            {
                parser.reset();
            }

            // Feed pre-read data on first iteration
            if (initial_data && initial_len > 0)
            {
                auto consumed = parser.consume(initial_data, initial_len);
                if (!consumed)
                    co_return;
                initial_data = nullptr;
                initial_len = 0;
                initial_buffer.reset();
            }

            auto receive_more = [&]() -> task<bool>
            {
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
                if (recv_stream)
                {
                    auto rd = co_await recv_stream->async_next();
                    if (!rd)
                    {
                        co_await stop_recv();
                        recv_stream.reset();
                    }
                    else if (rd->size() == 0)
                    {
                        co_await stop_recv();
                        co_return false;
                    }
                    else
                    {
                        auto data = rd->data();
                        auto consumed = parser.consume(
                            static_cast<const char*>(data.data), data.size);
                        rd->release();
                        if (!consumed)
                        {
                            co_await stop_recv();
                            co_return false;
                        }
                        co_return true;
                    }
                }
#endif
                if (!fallback_buffer)
                    fallback_buffer =
                        std::make_unique<std::array<std::byte, 8192>>();
                auto rd = co_await async_read(io, client,
                    mutable_buffer{fallback_buffer->data(),
                        fallback_buffer->size()},
                    cancellation);
                if (!rd || *rd == 0)
                {
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
                    co_await stop_recv();
#endif
                    co_return false;
                }

                auto consumed =
                    parser.consume(reinterpret_cast<const char*>(
                                       fallback_buffer->data()),
                        *rd);
                if (!consumed)
                {
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
                    co_await stop_recv();
#endif
                    co_return false;
                }
                co_return true;
            };

            while (!parser.headers_ready())
                if (!(co_await receive_more()))
                    co_return;

            // Route matching
            auto uri = parser.uri();
            auto qpos = uri.find('?');
            auto path = (qpos != std::string_view::npos) ? uri.substr(0, qpos) : uri;

            // Borrow the route: the synchronous fast path calls the router's
            // handler in place (as HTTP/2 does) instead of copying two
            // std::function objects per request.
            auto mr = router_.match_borrowed(parser.method(), path);

            resp.reset(status::ok, parser.version());
            if (response_headers_.emit_server)
                resp.set_header("Server", "cnetmod");
            if (response_headers_.emit_date)
                resp.set_cached_date_header(date_cache_.get());
            route_params rp;
            const sync_handler_fn* sync_route = nullptr;
            std::optional<handler_fn> owned_handler;
            connection_upgrade_handler connection_upgrade;

            if (mr)
            {
                if (mr->route->sync_handler)
                    sync_route = &mr->route->sync_handler;
                rp = std::move(mr->params);
            }
            // Asynchronous handlers and middleware chains take a mutable copy;
            // it is materialised only when one of those paths needs it.
            auto handler_for_chain = [&]() -> handler_fn&
            {
                if (!owned_handler)
                    owned_handler.emplace(mr ? mr->route->handler
                                             : detail::unmatched_route_handler(router_, path));
                return *owned_handler;
            };

            if (mr && mr->route->request_stream)
            {
                const auto options = *mr->route->request_stream;
                // The body pump may append receive data while the handler is
                // using request-line/header views.
                parser.preserve_headers();
                auto body_stream = std::make_shared<request_body_stream>(
                    options.chunk_capacity, options.max_bytes);
                request_context rctx(io, client, parser, resp, std::move(rp),
                    body_stream);
                rctx.set_stream_writer(
                    [&io, &client, &pending_responses](std::string_view bytes,
                        cancel_token& token)
                        -> task<std::expected<void, std::error_code>>
                    {
                        if (!pending_responses.empty())
                        {
                            auto pending = co_await async_write_all(io, client,
                                const_buffer{pending_responses.data(),
                                    pending_responses.size()},
                                token);
                            if (!pending)
                                co_return std::unexpected(pending.error());
                            pending_responses.clear();
                        }
                        co_return co_await async_write_all(io, client,
                            const_buffer{bytes.data(), bytes.size()}, token);
                    });
                auto pump_body = [&]() -> task<void>
                {
                    for (;;)
                    {
                        auto data = parser.take_body_chunk();
                        if (!data.empty() && !body_stream->is_closed())
                        {
                            request_body_chunk chunk(
                                reinterpret_cast<const std::byte*>(data.data()),
                                reinterpret_cast<const std::byte*>(data.data()) +
                                    data.size());
                            (void)co_await body_stream->send(std::move(chunk));
                        }
                        if (parser.ready())
                        {
                            body_stream->close();
                            co_return;
                        }
                        if (!(co_await receive_more()))
                        {
                            body_stream->fail(
                                std::make_error_code(std::errc::connection_aborted));
                            co_return;
                        }
                    }
                };
                auto invoke_handler = [&]() -> task<void>
                {
                    if (middlewares_.empty())
                        co_await handler_for_chain()(rctx);
                    else
                        co_await execute_chain(rctx, handler_for_chain());
                    body_stream->close();
                };
                co_await when_all(invoke_handler(), pump_body());
                if (body_stream->error() ==
                        std::make_error_code(std::errc::message_size) &&
                    resp.get_header("X-Streamed") != "1")
                    rctx.text(status::payload_too_large,
                        "Request body exceeds the route limit");
                connection_upgrade = rctx.take_connection_upgrade();
            }
            else
            {
                while (!parser.ready())
                    if (!(co_await receive_more()))
                        co_return;
                request_context rctx(io, client, parser, resp, std::move(rp));
                rctx.enable_connection_upgrade();
                if (middlewares_.empty() && sync_route)
                    (*sync_route)(rctx);
                else
                {
                    rctx.set_stream_writer(
                        [&io, &client, &pending_responses](
                            std::string_view bytes, cancel_token& token)
                            -> task<std::expected<void, std::error_code>>
                        {
                            if (!pending_responses.empty())
                            {
                                auto pending = co_await async_write_all(io, client,
                                    const_buffer{pending_responses.data(),
                                        pending_responses.size()},
                                    token);
                                if (!pending)
                                    co_return std::unexpected(pending.error());
                                pending_responses.clear();
                            }
                            co_return co_await async_write_all(io, client,
                                const_buffer{bytes.data(), bytes.size()}, token);
                        });
                    if (middlewares_.empty())
                        co_await handler_for_chain()(rctx);
                    else
                    {
                        handler_fn adapted;
                        auto* selected = &handler_for_chain();
                        if (sync_route)
                        {
                            adapted = [sync_route](request_context& context)
                                -> task<void>
                            {
                                (*sync_route)(context);
                                co_return;
                            };
                            selected = &adapted;
                        }
                        co_await execute_chain(rctx, *selected);
                    }
                }
                connection_upgrade = rctx.take_connection_upgrade();
            }

            if (connection_upgrade)
            {
                if (parser.version() != http_version::http_1_1 ||
                    resp.status_code() != status::switching_protocols ||
                    !resp.body().empty() || resp.body_source() ||
                    resp.get_header("X-Streamed") == "1")
                    co_return;
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
                auto received = parser.take_buffered_data();
                co_await stop_recv();
                if (recv_stream)
                    recv_stream->append_buffered_data(received);
                recv_stream.reset();
#else
            auto received = parser.take_buffered_data();
#endif
                resp.append_serialized_to(pending_responses);
                auto written = co_await async_write_all(io, client,
                    const_buffer{pending_responses.data(),
                        pending_responses.size()},
                    cancellation);
                if (!written)
                    co_return;
                pending_responses.clear();
                co_await connection_upgrade(
                    io, std::move(client), std::move(received));
                co_return;
            }
            const bool has_body_source = static_cast<bool>(resp.body_source());
            if (has_body_source)
            {
                if (!pending_responses.empty())
                {
                    auto pending = co_await async_write_all(io, client,
                        const_buffer{pending_responses.data(),
                            pending_responses.size()},
                        cancellation);
                    if (!pending)
                        co_return;
                    pending_responses.clear();
                }
                if (!(co_await send_body_source(
                        io, client, resp, cancellation)))
                    co_return;
            }
            // Check if chunked encoding is needed
            bool use_chunked = false;
            if (!has_body_source && resp.get_header("X-Streamed") != "1")
            {
                // If no Content-Length and body not empty, use chunked
                if (resp.get_header("Content-Length").empty() && !resp.body().empty() &&
                    parser.version() == http_version::http_1_1)
                {
                    use_chunked = true;
                    resp.set_header("Transfer-Encoding", "chunked");
                }

                if (use_chunked)
                {
                    if (!pending_responses.empty())
                    {
                        auto pending = co_await async_write_all(io, client,
                            const_buffer{pending_responses.data(),
                                pending_responses.size()},
                            cancellation);
                        if (!pending)
                            co_return;
                        pending_responses.clear();
                    }
                    // Send chunked response
                    co_await send_chunked_response(io, client, resp, cancellation);
                }
                else
                {
                    // Keep complete pipelined responses in connection-owned
                    // storage. They are submitted together when the receive batch
                    // is drained, reducing write SQEs without changing ordering.
                    resp.append_serialized_to(pending_responses);
                }
            }

            auto conn_hdr = parser.get_header("Connection");
            if (parser.version() == http_version::http_1_1)
            {
                keep_alive = (conn_hdr != "close");
            }
            else
            {
                keep_alive = false;
            }

            if (keep_alive && parser.has_buffered_data())
            {
                parse_buffered_request = true;
                continue;
            }

            if (!pending_responses.empty())
            {
                // Small HTTP responses normally complete in one send. Avoid the
                // async_write_all coroutine frame on that hot path, retaining its
                // correctness loop only for an actual partial write.
                auto written = co_await async_write(io, client,
                    const_buffer{pending_responses.data(),
                        pending_responses.size()},
                    cancellation);
                if (!written)
                    co_return;
                if (*written < pending_responses.size())
                {
                    auto remainder = co_await async_write_all(io, client,
                        const_buffer{pending_responses.data() + *written,
                            pending_responses.size() - *written},
                        cancellation);
                    if (!remainder)
                        co_return;
                }
                pending_responses.clear();
            }
        }
#if defined(CNETMOD_HAS_IO_URING) &&             \
    defined(CNETMOD_HAS_IO_URING_BUFFER_RING) && \
    defined(IORING_RECV_MULTISHOT) &&            \
    defined(IORING_CQE_F_BUFFER) && defined(IORING_CQE_F_MORE)
    };

    std::exception_ptr failure;
    try
    {
        co_await serve_connection();
    }
    catch (...)
    {
        failure = std::current_exception();
    }

    co_await stop_recv();

    if (failure)
        std::rethrow_exception(failure);
#endif
}

#ifdef CNETMOD_HAS_SSL
// =============================================================================
// TLS Handlers
// =============================================================================

auto server::handle_h2_tls(socket& client, io_context& io, ssl_stream& ssl,
    cancel_token& cancellation, cancel_token& write_cancellation) -> task<void>
{
    auto reader = [&ssl, &cancellation](mutable_buffer buffer)
        -> task<std::expected<std::size_t, std::error_code>>
    {
        co_return co_await ssl.async_read(buffer, cancellation);
    };
    auto writer =
        [&ssl, &write_cancellation](
            const_buffer buffer) -> task<std::expected<void, std::error_code>>
    {
        co_return co_await ssl.async_write_all(buffer, write_cancellation);
    };
    auto filter = make_h2_buffered_request_filter();
    const bool buffer_complete = static_cast<bool>(filter);
    v2::session session(io, client, make_h2_streaming_handler(io, client), {},
        std::move(reader), std::move(writer),
        make_h2_sync_handler(io, client), buffer_complete, std::move(filter));
    // Static responses are framed by reference and sealed straight from the
    // shared file cache instead of being copied into the frame buffer first.
    session.set_gather_writer(
        [&ssl, &write_cancellation](std::span<const const_buffer> parts)
            -> task<std::expected<void, std::error_code>>
        {
            co_return co_await ssl.async_write_gather(parts, write_cancellation);
        });
    co_await session.run();
}

auto server::handle_h1_tls(socket& client, io_context& io, ssl_stream& ssl,
    cancel_token& cancellation) -> task<void>
{
    // Bodies at least this large are sealed from their own storage; smaller
    // ones are cheaper to batch with pipelined neighbours by copying.
    constexpr std::size_t tls_gather_body_threshold = 4U * 1024U;
    bool keep_alive = true;
    response resp(status::ok);
    std::string pending_responses;
    request_parser parser;
    std::array<std::byte, 8192> buf;
    bool parse_buffered_request = false;
    auto configure_stream_writer = [&ssl, &pending_responses](
                                       request_context& context)
    {
        context.set_stream_writer(
            [&ssl, &pending_responses](std::string_view bytes,
                cancel_token& token)
                -> task<std::expected<void, std::error_code>>
            {
                if (!pending_responses.empty())
                {
                    auto pending = co_await ssl.async_write_all(const_buffer{
                                                                    pending_responses.data(), pending_responses.size()},
                        token);
                    if (!pending)
                        co_return std::unexpected(pending.error());
                    pending_responses.clear();
                }
                co_return co_await ssl.async_write_all(
                    const_buffer{bytes.data(), bytes.size()}, token);
            });
    };

    while (keep_alive)
    {
        if (parse_buffered_request)
        {
            parse_buffered_request = false;
            if (!parser.reset_for_next_message())
                co_return;
            if (auto parsed = parser.consume_buffered(); !parsed)
            {
                if (!pending_responses.empty())
                    (void)co_await ssl.async_write_all(const_buffer{
                                                           pending_responses.data(), pending_responses.size()},
                        cancellation);
                co_return;
            }
            if (!parser.ready() && !pending_responses.empty())
            {
                auto pending = co_await ssl.async_write_all(const_buffer{
                                                                pending_responses.data(), pending_responses.size()},
                    cancellation);
                if (!pending)
                    co_return;
                pending_responses.clear();
            }
        }
        else
        {
            parser.reset();
        }

        auto receive_more = [&]() -> task<bool>
        {
            auto rd = co_await ssl.async_read(
                mutable_buffer{buf.data(), buf.size()}, cancellation);
            if (!rd || *rd == 0)
                co_return false;

            auto consumed =
                parser.consume(reinterpret_cast<const char*>(buf.data()), *rd);
            if (!consumed)
                co_return false;
            co_return true;
        };

        while (!parser.headers_ready())
            if (!(co_await receive_more()))
                co_return;

        auto uri = parser.uri();
        auto qpos = uri.find('?');
        auto path = (qpos != std::string_view::npos) ? uri.substr(0, qpos) : uri;

        // Borrow the route: the synchronous fast path calls the router's
        // handler in place (as HTTP/2 does) instead of copying two
        // std::function objects per request.
        auto mr = router_.match_borrowed(parser.method(), path);

        resp.reset(status::ok, parser.version());
        if (response_headers_.emit_server)
            resp.set_header("Server", "cnetmod");
        if (response_headers_.emit_date)
            resp.set_cached_date_header(date_cache_.get());
        route_params rp;
        const sync_handler_fn* sync_route = nullptr;
        std::optional<handler_fn> owned_handler;

        if (mr)
        {
            if (mr->route->sync_handler)
                sync_route = &mr->route->sync_handler;
            rp = std::move(mr->params);
        }
        // Asynchronous handlers and middleware chains take a mutable copy;
        // it is materialised only when one of those paths needs it.
        auto handler_for_chain = [&]() -> handler_fn&
        {
            if (!owned_handler)
                owned_handler.emplace(mr ? mr->route->handler
                                         : detail::unmatched_route_handler(router_, path));
            return *owned_handler;
        };

        if (mr && mr->route->request_stream)
        {
            const auto options = *mr->route->request_stream;
            auto body_stream = std::make_shared<request_body_stream>(
                options.chunk_capacity, options.max_bytes);
            request_context rctx(io, client, parser, resp, std::move(rp),
                body_stream);
            configure_stream_writer(rctx);
            auto pump_body = [&]() -> task<void>
            {
                for (;;)
                {
                    auto data = parser.take_body_chunk();
                    if (!data.empty() && !body_stream->is_closed())
                    {
                        request_body_chunk chunk(
                            reinterpret_cast<const std::byte*>(data.data()),
                            reinterpret_cast<const std::byte*>(data.data()) +
                                data.size());
                        (void)co_await body_stream->send(std::move(chunk));
                    }
                    if (parser.ready())
                    {
                        body_stream->close();
                        co_return;
                    }
                    if (!(co_await receive_more()))
                    {
                        body_stream->fail(
                            std::make_error_code(std::errc::connection_aborted));
                        co_return;
                    }
                }
            };
            auto invoke_handler = [&]() -> task<void>
            {
                if (middlewares_.empty())
                    co_await handler_for_chain()(rctx);
                else
                    co_await execute_chain(rctx, handler_for_chain());
                body_stream->close();
            };
            co_await when_all(invoke_handler(), pump_body());
            if (body_stream->error() ==
                    std::make_error_code(std::errc::message_size) &&
                resp.get_header("X-Streamed") != "1")
                rctx.text(status::payload_too_large,
                    "Request body exceeds the route limit");
        }
        else
        {
            while (!parser.ready())
                if (!(co_await receive_more()))
                    co_return;
            request_context rctx(io, client, parser, resp, std::move(rp));
            if (middlewares_.empty() && sync_route)
                (*sync_route)(rctx);
            else
            {
                configure_stream_writer(rctx);
                if (middlewares_.empty())
                    co_await handler_for_chain()(rctx);
                else
                {
                    handler_fn adapted;
                    auto* selected = &handler_for_chain();
                    if (sync_route)
                    {
                        adapted = [sync_route](request_context& context)
                            -> task<void>
                        {
                            (*sync_route)(context);
                            co_return;
                        };
                        selected = &adapted;
                    }
                    co_await execute_chain(rctx, *selected);
                }
            }
        }
        const bool has_body_source = static_cast<bool>(resp.body_source());
        if (has_body_source)
        {
            if (!pending_responses.empty())
            {
                auto pending = co_await ssl.async_write_all(const_buffer{
                                                                pending_responses.data(), pending_responses.size()},
                    cancellation);
                if (!pending)
                    co_return;
                pending_responses.clear();
            }
            if (!(co_await send_body_source_tls(
                    ssl, resp, cancellation)))
                co_return;
        }
        // Check if chunked encoding is needed
        bool use_chunked = false;
        if (!has_body_source && resp.get_header("X-Streamed") != "1")
        {
            // If no Content-Length and body not empty, use chunked
            if (resp.get_header("Content-Length").empty() && !resp.body().empty() &&
                parser.version() == http_version::http_1_1)
            {
                use_chunked = true;
                resp.set_header("Transfer-Encoding", "chunked");
            }

            if (use_chunked)
            {
                if (!pending_responses.empty())
                {
                    auto pending = co_await ssl.async_write_all(const_buffer{
                                                                    pending_responses.data(), pending_responses.size()},
                        cancellation);
                    if (!pending)
                        co_return;
                    pending_responses.clear();
                }
                // Send chunked response (TLS)
                co_await send_chunked_response_tls(io, ssl, resp, cancellation);
            }
            else if (const auto body = resp.body(); body.size() >= tls_gather_body_threshold)
            {
                // Seal a large body straight from response storage (often the
                // shared static-file cache) instead of copying it behind the
                // header block; queued pipelined responses go out first in
                // the same flush.
                resp.append_serialized_headers_to(pending_responses);
                const std::array<const_buffer, 2> parts{
                    const_buffer{pending_responses.data(), pending_responses.size()},
                    const_buffer{body.data(), body.size()}};
                auto written = co_await ssl.async_write_gather(parts, cancellation);
                if (!written)
                    co_return;
                pending_responses.clear();
            }
            else
            {
                resp.append_serialized_to(pending_responses);
            }
        }

        auto conn_hdr = parser.get_header("Connection");
        if (parser.version() == http_version::http_1_1)
        {
            keep_alive = (conn_hdr != "close");
        }
        else
        {
            keep_alive = false;
        }

        if (keep_alive && parser.has_buffered_data())
        {
            parse_buffered_request = true;
            continue;
        }
        if (!pending_responses.empty())
        {
            auto written = co_await ssl.async_write(const_buffer{
                                                        pending_responses.data(), pending_responses.size()},
                cancellation);
            if (!written)
                co_return;
            if (*written < pending_responses.size())
            {
                auto remainder = co_await ssl.async_write_all(const_buffer{
                                                                  pending_responses.data() + *written,
                                                                  pending_responses.size() - *written},
                    cancellation);
                if (!remainder)
                    co_return;
            }
            pending_responses.clear();
        }
    }
}
#endif // CNETMOD_HAS_SSL

// =============================================================================
// Middleware Chain Execution
// =============================================================================

auto server::execute_chain(request_context& ctx, handler_fn& handler,
    std::size_t idx) -> task<void>
{
    if (idx >= middlewares_.size())
    {
        co_await handler(ctx);
        co_return;
    }
    // next is a regular (non-coroutine) closure that directly returns the next
    // layer's task<void>
    next_fn next = [this, &ctx, &handler, idx]() -> task<void>
    {
        return execute_chain(ctx, handler, idx + 1);
    };
    co_await middlewares_[idx](ctx, next);
}

// =============================================================================
// Chunked Response Sending
// =============================================================================

auto server::send_chunked_response(io_context& io, socket& client,
    response& resp, cancel_token& cancellation) -> task<void>
{
    // Send headers (without body)
    auto body = std::move(resp.body());
    resp.set_body(std::string{}); // Clear body

    auto header_data = resp.serialize();
    auto wr = co_await async_write_all(
        io, client, const_buffer{header_data.data(), header_data.size()}, cancellation);
    if (!wr)
        co_return;

    // Send chunked body
    constexpr std::size_t CHUNK_SIZE = 8192;
    std::size_t offset = 0;

    while (offset < body.size())
    {
        auto chunk_size = std::min(CHUNK_SIZE, body.size() - offset);

        // Send chunk size (hex)
        auto size_str = std::format("{:x}\r\n", chunk_size);
        auto wr1 = co_await async_write_all(
            io, client, const_buffer{size_str.data(), size_str.size()}, cancellation);
        if (!wr1)
            co_return;

        // Send chunk data
        auto wr2 = co_await async_write_all(
            io, client, const_buffer{body.data() + offset, chunk_size}, cancellation);
        if (!wr2)
            co_return;

        // Send chunk ending \r\n
        auto wr3 = co_await async_write_all(
            io, client, const_buffer{"\r\n", 2}, cancellation);
        if (!wr3)
            co_return;

        offset += chunk_size;
    }

    // Send end chunk (0\r\n\r\n)
    auto wr_end =
        co_await async_write_all(
            io, client, const_buffer{"0\r\n\r\n", 5}, cancellation);
    (void)wr_end;
}

auto server::send_body_source(io_context& io, socket& client, response& resp,
    cancel_token& cancellation) -> task<bool>
{
    auto source = resp.take_body_source();
    if (!source)
        co_return false;
    const bool chunked = !source->content_length() &&
        resp.version() == http_version::http_1_1;
    if (chunked)
        resp.set_header("Transfer-Encoding", "chunked");
    auto header = resp.serialize();
    auto written = co_await async_write_all(io, client,
        const_buffer{header.data(), header.size()}, cancellation);
    if (!written)
        co_return false;
    std::uint64_t total = 0;
    while (auto chunk = co_await source->next(cancellation))
    {
        if (chunk->empty())
            continue;
        std::string framed;
        const_buffer bytes{chunk->data(), chunk->size()};
        if (chunked)
        {
            framed = std::format("{:x}\r\n", chunk->size());
            framed.append(reinterpret_cast<const char*>(chunk->data()),
                chunk->size());
            framed += "\r\n";
            bytes = {framed.data(), framed.size()};
        }
        auto result = co_await async_write_all(
            io, client, bytes, cancellation);
        if (!result)
            co_return false;
        total += chunk->size();
    }
    if (chunked)
    {
        auto ended = co_await async_write_all(io, client,
            const_buffer{"0\r\n\r\n", 5}, cancellation);
        if (!ended)
            co_return false;
    }
    co_return !source->content_length() || total == *source->content_length();
}

#ifdef CNETMOD_HAS_SSL
auto server::send_body_source_tls(ssl_stream& ssl, response& resp,
    cancel_token& cancellation) -> task<bool>
{
    auto source = resp.take_body_source();
    if (!source)
        co_return false;
    const bool chunked = !source->content_length() &&
        resp.version() == http_version::http_1_1;
    if (chunked)
        resp.set_header("Transfer-Encoding", "chunked");
    auto header = resp.serialize();
    auto written = co_await ssl.async_write_all(
        const_buffer{header.data(), header.size()}, cancellation);
    if (!written)
        co_return false;
    std::uint64_t total = 0;
    while (auto chunk = co_await source->next(cancellation))
    {
        if (chunk->empty())
            continue;
        std::string framed;
        const_buffer bytes{chunk->data(), chunk->size()};
        if (chunked)
        {
            framed = std::format("{:x}\r\n", chunk->size());
            framed.append(reinterpret_cast<const char*>(chunk->data()),
                chunk->size());
            framed += "\r\n";
            bytes = {framed.data(), framed.size()};
        }
        auto result = co_await ssl.async_write_all(bytes, cancellation);
        if (!result)
            co_return false;
        total += chunk->size();
    }
    if (chunked)
    {
        auto ended = co_await ssl.async_write_all(
            const_buffer{"0\r\n\r\n", 5}, cancellation);
        if (!ended)
            co_return false;
    }
    co_return !source->content_length() || total == *source->content_length();
}

auto server::send_chunked_response_tls(io_context&, ssl_stream& ssl,
    response& resp, cancel_token& cancellation) -> task<void>
{
    // Send headers (without body)
    auto body = std::move(resp.body());
    resp.set_body(std::string{}); // Clear body

    auto header_data = resp.serialize();
    auto wr = co_await ssl.async_write_all(
        const_buffer{header_data.data(), header_data.size()}, cancellation);
    if (!wr)
        co_return;

    // Send chunked body
    constexpr std::size_t CHUNK_SIZE = 8192;
    std::size_t offset = 0;

    while (offset < body.size())
    {
        auto chunk_size = std::min(CHUNK_SIZE, body.size() - offset);

        // One TLS record per HTTP chunk. This preserves streaming latency while
        // avoiding three SSL_write calls for the size, payload and CRLF.
        auto record = std::format("{:x}\r\n", chunk_size);
        record.append(body.data() + offset, chunk_size);
        record += "\r\n";
        auto written = co_await ssl.async_write_all(
            const_buffer{record.data(), record.size()}, cancellation);
        if (!written)
            co_return;

        offset += chunk_size;
    }

    // Send end chunk (0\r\n\r\n)
    auto wr_end = co_await ssl.async_write_all(
        const_buffer{"0\r\n\r\n", 5}, cancellation);
    (void)wr_end;
}
#endif

} // namespace cnetmod::http
