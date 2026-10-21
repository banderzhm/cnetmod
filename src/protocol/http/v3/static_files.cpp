module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.v3.static_files;

import std;
import cnetmod.core.file;
import cnetmod.coro.cancel;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.http.v3.session;
import cnetmod.utils.concurrent_containers.striped_hash_map;

namespace cnetmod::http::v3 {
namespace {

    struct cached_static_file
    {
        std::filesystem::path selected_path;
        std::filesystem::file_time_type modified_at;
        std::uintmax_t size{};
        std::string mime_type;
        std::string content_encoding;
        std::shared_ptr<const std::string> content;
        std::chrono::steady_clock::time_point validated_at;
    };

    struct static_file_state
    {
        explicit static_file_state(static_file_options value)
            : options(std::move(value)), entries(64U, 16U) {}

        static_file_options options;
        concurrent_containers::striped_hash_map<std::string,
            std::shared_ptr<const cached_static_file>>
            entries;
    };

    auto hex_value(char value) noexcept -> int
    {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    }

    auto decode_path(std::string_view encoded)
        -> std::expected<std::string, std::error_code>
    {
        std::string decoded;
        decoded.reserve(encoded.size());
        for (std::size_t index = 0; index < encoded.size(); ++index)
        {
            if (encoded[index] != '%')
            {
                decoded.push_back(encoded[index]);
                continue;
            }
            if (index + 2 >= encoded.size())
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            const auto high = hex_value(encoded[index + 1]);
            const auto low = hex_value(encoded[index + 2]);
            if (high < 0 || low < 0)
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            const auto value = static_cast<char>((high << 4) | low);
            if (value == '\0' || value == '\\')
                return std::unexpected(
                    std::make_error_code(std::errc::invalid_argument));
            decoded.push_back(value);
            index += 2;
        }
        return decoded;
    }

    auto is_within_root(const std::filesystem::path& root,
        const std::filesystem::path& candidate) -> bool
    {
        const auto relative = candidate.lexically_relative(root);
        return !relative.empty() && !relative.is_absolute() &&
            *relative.begin() != "..";
    }

    auto populate_response(const cached_static_file& resource,
        http_method method, http3_response& response)
        -> std::expected<void, std::error_code>
    {
        response.status = status::ok;
        response.headers.emplace("content-type", resource.mime_type);
        response.headers.emplace("content-length", std::to_string(resource.size));
        response.headers.emplace("vary", "Accept-Encoding");
        if (!resource.content_encoding.empty())
            response.headers.emplace("content-encoding", resource.content_encoding);

        if (method == http_method::HEAD || resource.size == 0)
            return {};
        if (resource.content)
        {
            response.shared_body = resource.content;
            return {};
        }

        auto opened = file::open(resource.selected_path, open_mode::read);
        if (!opened)
            return std::unexpected(opened.error());
        auto source = std::make_shared<file>(std::move(*opened));
        auto offset = std::make_shared<std::uint64_t>(0);
        auto remaining = std::make_shared<std::uint64_t>(resource.size);
        response.body_source = std::make_shared<response_body_source>(
            [source, offset, remaining](cancel_token& cancellation)
                -> task<std::optional<request_body_chunk>>
            {
                if (cancellation.is_cancelled() || *remaining == 0)
                    co_return std::nullopt;
                auto* io = io_context::current();
                if (!io)
                    co_return std::nullopt;
                constexpr std::size_t chunk_size = 64U * 1024U;
                const auto requested = static_cast<std::size_t>(
                    std::min<std::uint64_t>(*remaining, chunk_size));
                request_body_chunk chunk(requested);
                auto read = co_await async_file_read(*io, *source,
                    chunk.writable_view(), *offset, cancellation);
                if (!read || *read == 0)
                    co_return std::nullopt;
                chunk.resize(*read);
                *offset += *read;
                *remaining -= *read;
                co_return chunk;
            },
            resource.size);
        return {};
    }

} // namespace

auto serve_dir(static_file_options options) -> static_file_handler
{
    std::error_code canonical_error;
    options.root = std::filesystem::weakly_canonical(
        options.root, canonical_error);
    auto shared = std::make_shared<static_file_state>(std::move(options));

    return [shared, canonical_error](http3_request& request,
               http3_response& response) -> std::expected<bool, std::error_code>
    {
        if (canonical_error)
            return std::unexpected(canonical_error);

        const auto query = request.path.find('?');
        const auto path = std::string_view{request.path}.substr(0, query);
        if (!path.starts_with(shared->options.url_prefix))
            return false;

        if (request.method != http_method::GET &&
            request.method != http_method::HEAD)
        {
            response.status = status::method_not_allowed;
            response.headers.emplace("allow", "GET, HEAD");
            return true;
        }

        auto decoded = decode_path(path.substr(shared->options.url_prefix.size()));
        if (!decoded)
        {
            response.status = status::bad_request;
            return true;
        }
        if (decoded->empty())
            *decoded = shared->options.index_file;

        const std::filesystem::path relative{*decoded};
        if (relative.is_absolute() || relative.has_root_name())
        {
            response.status = status::forbidden;
            return true;
        }
        for (const auto& component : relative)
        {
            if (component == "..")
            {
                response.status = status::forbidden;
                return true;
            }
        }

        const auto encoding = request.headers.find("accept-encoding");
        const auto accepted = encoding == request.headers.end()
            ? std::string_view{}
            : std::string_view{encoding->second};
        auto requested_encoding = std::string_view{"identity"};
        if (shared->options.serve_precompressed)
        {
            if (accepts_content_encoding(accepted, "br"))
                requested_encoding = "br";
            else if (accepts_content_encoding(accepted, "gzip"))
                requested_encoding = "gzip";
        }
        auto key = decoded.value();
        key.push_back('\n');
        key.append(requested_encoding);
        const auto now = std::chrono::steady_clock::now();
        auto cached = shared->entries.find(key).value_or(nullptr);
        if (cached &&
            now - cached->validated_at < shared->options.revalidate_interval)
        {
            auto populated = populate_response(*cached, request.method, response);
            if (!populated)
                return std::unexpected(populated.error());
            return true;
        }

        std::error_code path_error;
        auto original = std::filesystem::weakly_canonical(
            shared->options.root / relative, path_error);
        if (path_error || !is_within_root(shared->options.root, original))
        {
            response.status = status::not_found;
            return true;
        }
        if (std::filesystem::is_directory(original, path_error))
            original = std::filesystem::weakly_canonical(
                original / shared->options.index_file, path_error);
        if (path_error || !std::filesystem::is_regular_file(original, path_error))
        {
            response.status = status::not_found;
            return true;
        }

        auto selected = original;
        std::string content_encoding;
        if (requested_encoding == "br")
        {
            auto brotli = original;
            brotli += ".br";
            if (std::filesystem::is_regular_file(brotli, path_error))
            {
                selected = std::move(brotli);
                content_encoding = "br";
            }
            path_error.clear();
        }
        if ((requested_encoding == "gzip" ||
                (requested_encoding == "br" && content_encoding.empty() &&
                    accepts_content_encoding(accepted, "gzip"))))
        {
            auto gzip = original;
            gzip += ".gz";
            if (std::filesystem::is_regular_file(gzip, path_error))
            {
                selected = std::move(gzip);
                content_encoding = "gzip";
            }
            path_error.clear();
        }

        const auto size = std::filesystem::file_size(selected, path_error);
        if (path_error)
            return std::unexpected(path_error);
        const auto modified = std::filesystem::last_write_time(
            selected, path_error);
        if (path_error)
            return std::unexpected(path_error);

        std::shared_ptr<const std::string> content;
        if (cached && cached->selected_path == selected &&
            cached->modified_at == modified && cached->size == size)
            content = cached->content;
        else if (shared->options.cache_max_file_size != 0U &&
            size <= shared->options.cache_max_file_size)
        {
            auto loaded = std::make_shared<std::string>(
                static_cast<std::size_t>(size), '\0');
            std::ifstream input{selected, std::ios::binary};
            if (!input ||
                !input.read(loaded->data(),
                    static_cast<std::streamsize>(loaded->size())))
                return std::unexpected(std::make_error_code(std::errc::io_error));
            content = std::move(loaded);
        }

        auto refreshed = std::make_shared<cached_static_file>(cached_static_file{
            .selected_path = std::move(selected),
            .modified_at = modified,
            .size = size,
            .mime_type = std::string{
                guess_mime_type(original.extension().string())},
            .content_encoding = std::move(content_encoding),
            .content = std::move(content),
            .validated_at = now,
        });
        shared->entries.insert_or_assign(std::move(key), refreshed);
        auto populated = populate_response(*refreshed, request.method, response);
        if (!populated)
            return std::unexpected(populated.error());
        return true;
    };
}

} // namespace cnetmod::http::v3
