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

namespace cnetmod::http::v3 {
namespace {

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

    auto accepts_encoding(std::string_view header, std::string_view wanted) noexcept
        -> bool
    {
        while (!header.empty())
        {
            const auto comma = header.find(',');
            auto item = header.substr(0, comma);
            const auto semicolon = item.find(';');
            auto name = item.substr(0, semicolon);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
                name.remove_prefix(1);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                name.remove_suffix(1);

            bool disabled = false;
            if (semicolon != std::string_view::npos)
            {
                auto parameters = item.substr(semicolon + 1);
                const auto quality = parameters.find("q=");
                if (quality != std::string_view::npos)
                {
                    auto value = parameters.substr(quality + 2);
                    const auto end = value.find(';');
                    value = value.substr(0, end);
                    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                        value.remove_suffix(1);
                    disabled = value == "0" || value == "0.0" ||
                        value == "0.00" || value == "0.000";
                }
            }
            if ((name == wanted || name == "*") && !disabled)
                return true;
            if (comma == std::string_view::npos)
                break;
            header.remove_prefix(comma + 1);
        }
        return false;
    }

    auto is_within_root(const std::filesystem::path& root,
        const std::filesystem::path& candidate) -> bool
    {
        const auto relative = candidate.lexically_relative(root);
        return !relative.empty() && !relative.is_absolute() &&
            *relative.begin() != "..";
    }

} // namespace

auto serve_dir(static_file_options options) -> static_file_handler
{
    std::error_code canonical_error;
    options.root = std::filesystem::weakly_canonical(
        options.root, canonical_error);
    auto shared = std::make_shared<const static_file_options>(
        std::move(options));

    return [shared, canonical_error](http3_request& request,
               http3_response& response) -> std::expected<bool, std::error_code>
    {
        if (canonical_error)
            return std::unexpected(canonical_error);

        const auto query = request.path.find('?');
        const auto path = std::string_view{request.path}.substr(0, query);
        if (!path.starts_with(shared->url_prefix))
            return false;

        if (request.method != http_method::GET &&
            request.method != http_method::HEAD)
        {
            response.status = status::method_not_allowed;
            response.headers.emplace("allow", "GET, HEAD");
            return true;
        }

        auto decoded = decode_path(path.substr(shared->url_prefix.size()));
        if (!decoded)
        {
            response.status = status::bad_request;
            return true;
        }
        if (decoded->empty())
            *decoded = shared->index_file;

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

        std::error_code path_error;
        auto original = std::filesystem::weakly_canonical(
            shared->root / relative, path_error);
        if (path_error || !is_within_root(shared->root, original))
        {
            response.status = status::not_found;
            return true;
        }
        if (std::filesystem::is_directory(original, path_error))
            original = std::filesystem::weakly_canonical(
                original / shared->index_file, path_error);
        if (path_error || !std::filesystem::is_regular_file(original, path_error))
        {
            response.status = status::not_found;
            return true;
        }

        auto selected = original;
        std::string content_encoding;
        const auto encoding = request.headers.find("accept-encoding");
        const auto accepted = encoding == request.headers.end()
            ? std::string_view{}
            : std::string_view{encoding->second};
        if (shared->serve_precompressed && accepts_encoding(accepted, "br"))
        {
            auto brotli = original;
            brotli += ".br";
            if (std::filesystem::is_regular_file(brotli, path_error))
            {
                selected = std::move(brotli);
                content_encoding = "br";
            }
        }
        if (shared->serve_precompressed && content_encoding.empty() &&
            accepts_encoding(accepted, "gzip"))
        {
            auto gzip = original;
            gzip += ".gz";
            if (std::filesystem::is_regular_file(gzip, path_error))
            {
                selected = std::move(gzip);
                content_encoding = "gzip";
            }
        }

        auto opened = file::open(selected, open_mode::read);
        if (!opened)
        {
            response.status = status::not_found;
            return true;
        }
        auto size = opened->size();
        if (!size)
            return std::unexpected(size.error());

        response.status = status::ok;
        response.headers.emplace("content-type",
            std::string{guess_mime_type(original.extension().string())});
        response.headers.emplace("content-length", std::to_string(*size));
        response.headers.emplace("vary", "Accept-Encoding");
        if (!content_encoding.empty())
            response.headers.emplace("content-encoding",
                std::move(content_encoding));

        if (request.method == http_method::HEAD || *size == 0)
            return true;

        auto source = std::make_shared<file>(std::move(*opened));
        auto offset = std::make_shared<std::uint64_t>(0);
        auto remaining = std::make_shared<std::uint64_t>(*size);
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
            *size);
        return true;
    };
}

} // namespace cnetmod::http::v3
