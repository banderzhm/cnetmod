/// cnetmod.protocol.openai:loaders — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import cnetmod.coro.bridge;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import cnetmod.io.io_context;
import :model;
import :retrieval;
import :ingestion;
import :loaders;

namespace cnetmod::openai {

namespace {

    auto lower_ascii(std::string value) -> std::string
    {
        std::ranges::transform(value, value.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return value;
    }

    auto trim(std::string_view value) -> std::string_view
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0)
            value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            value.remove_suffix(1);
        return value;
    }

    auto is_blank(std::string_view value) -> bool
    {
        return std::ranges::all_of(value, [](unsigned char character)
            {
                return std::isspace(character) != 0;
            });
    }

    auto is_valid_utf8(std::string_view value) -> bool
    {
        for (std::size_t index = 0; index < value.size();)
        {
            const auto lead = static_cast<unsigned char>(value[index]);
            if (lead <= 0x7F)
            {
                ++index;
                continue;
            }
            std::size_t count = 0;
            std::uint32_t code_point = 0;
            if ((lead & 0xE0) == 0xC0)
            {
                count = 2;
                code_point = lead & 0x1F;
            }
            else if ((lead & 0xF0) == 0xE0)
            {
                count = 3;
                code_point = lead & 0x0F;
            }
            else if ((lead & 0xF8) == 0xF0)
            {
                count = 4;
                code_point = lead & 0x07;
            }
            else
                return false;
            if (index + count > value.size())
                return false;
            for (std::size_t offset = 1; offset < count; ++offset)
            {
                const auto continuation =
                    static_cast<unsigned char>(value[index + offset]);
                if ((continuation & 0xC0) != 0x80)
                    return false;
                code_point = (code_point << 6) | (continuation & 0x3F);
            }
            if ((count == 2 && code_point < 0x80) ||
                (count == 3 && code_point < 0x800) ||
                (count == 4 && code_point < 0x10000) ||
                code_point > 0x10FFFF ||
                (code_point >= 0xD800 && code_point <= 0xDFFF))
                return false;
            index += count;
        }
        return true;
    }

    auto normalized_media_type(std::string_view value) -> std::string
    {
        value = value.substr(0, value.find(';'));
        return lower_ascii(std::string{trim(value)});
    }

    void append_utf8(std::string& output, std::uint32_t code_point)
    {
        if (code_point <= 0x7F)
            output.push_back(static_cast<char>(code_point));
        else if (code_point <= 0x7FF)
        {
            output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
        else if (code_point <= 0xFFFF)
        {
            output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }

    auto decode_html_entities(std::string_view value) -> std::string
    {
        std::string output;
        output.reserve(value.size());
        for (std::size_t index = 0; index < value.size();)
        {
            if (value[index] != '&')
            {
                output.push_back(value[index++]);
                continue;
            }
            const auto end = value.find(';', index + 1);
            if (end == std::string_view::npos || end - index > 12)
            {
                output.push_back(value[index++]);
                continue;
            }
            const auto entity = value.substr(index + 1, end - index - 1);
            if (entity == "amp")
                output.push_back('&');
            else if (entity == "lt")
                output.push_back('<');
            else if (entity == "gt")
                output.push_back('>');
            else if (entity == "quot")
                output.push_back('"');
            else if (entity == "apos")
                output.push_back('\'');
            else if (entity == "nbsp")
                output.push_back(' ');
            else if (entity.starts_with('#'))
            {
                const bool hexadecimal = entity.size() > 2 &&
                    (entity[1] == 'x' || entity[1] == 'X');
                const auto digits = entity.substr(hexadecimal ? 2 : 1);
                std::uint32_t code_point = 0;
                const auto parsed = std::from_chars(digits.data(),
                    digits.data() + digits.size(), code_point,
                    hexadecimal ? 16 : 10);
                if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
                    code_point == 0 || code_point > 0x10FFFF ||
                    (code_point >= 0xD800 && code_point <= 0xDFFF))
                {
                    output.append(value.substr(index, end - index + 1));
                    index = end + 1;
                    continue;
                }
                append_utf8(output, code_point);
            }
            else
            {
                output.append(value.substr(index, end - index + 1));
                index = end + 1;
                continue;
            }
            index = end + 1;
        }
        return output;
    }

    auto normalized_visible_text(std::string value) -> std::string
    {
        std::string output;
        output.reserve(value.size());
        bool pending_space = false;
        bool pending_newline = false;
        for (const unsigned char character : value)
        {
            if (character == '\r' || character == '\n')
            {
                pending_newline = !output.empty();
                pending_space = false;
            }
            else if (std::isspace(character) != 0)
                pending_space = !output.empty() && !pending_newline;
            else
            {
                if (pending_newline && output.back() != '\n')
                    output.push_back('\n');
                else if (pending_space && output.back() != ' ' && output.back() != '\n')
                    output.push_back(' ');
                pending_space = false;
                pending_newline = false;
                output.push_back(static_cast<char>(character));
            }
        }
        while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back())) != 0)
            output.pop_back();
        return output;
    }

    auto html_to_text(std::string_view html, std::size_t max_output_bytes)
        -> std::expected<std::string, std::string>
    {
        const auto lower = lower_ascii(std::string{html});
        std::string visible;
        visible.reserve(std::min(html.size(), max_output_bytes));
        constexpr std::array<std::string_view, 5> excluded{
            "head", "script", "style", "template", "noscript"};
        constexpr std::array<std::string_view, 27> block_tags{"address", "article", "aside", "blockquote",
            "br", "div", "footer", "h1", "h2", "h3", "h4", "h5", "h6",
            "header", "hr", "li", "main", "nav", "ol", "p", "pre", "section",
            "table", "td", "th", "tr", "ul"};
        for (std::size_t index = 0; index < html.size();)
        {
            if (html[index] != '<')
            {
                visible.push_back(html[index++]);
                if (visible.size() > max_output_bytes)
                    return std::unexpected("parsed HTML exceeds max_output_bytes");
                continue;
            }
            if (lower.compare(index, 4, "<!--") == 0)
            {
                const auto comment_end = lower.find("-->", index + 4);
                index = comment_end == std::string::npos ? html.size() : comment_end + 3;
                continue;
            }
            const auto tag_end = lower.find('>', index + 1);
            if (tag_end == std::string::npos)
                return std::unexpected("unterminated HTML tag");
            auto tag = trim(std::string_view{lower}.substr(index + 1,
                tag_end - index - 1));
            if (tag.starts_with('/'))
                tag.remove_prefix(1);
            const auto name_end = tag.find_first_of(" \t\r\n/");
            const auto name = tag.substr(0, name_end);
            if (std::ranges::find(excluded, name) != excluded.end())
            {
                const auto close = std::format("</{}", name);
                const auto close_start = lower.find(close, tag_end + 1);
                if (close_start == std::string::npos)
                    return std::unexpected(std::format(
                        "HTML <{}> element has no closing tag", name));
                const auto close_end = lower.find('>', close_start + close.size());
                index = close_end == std::string::npos ? html.size() : close_end + 1;
                continue;
            }
            if (std::ranges::find(block_tags, name) != block_tags.end() &&
                !visible.empty() && visible.back() != '\n')
                visible.push_back('\n');
            index = tag_end + 1;
        }
        auto decoded = decode_html_entities(visible);
        auto normalized = normalized_visible_text(std::move(decoded));
        if (normalized.size() > max_output_bytes)
            return std::unexpected("parsed HTML exceeds max_output_bytes");
        return normalized;
    }

    auto normalized_extension(std::filesystem::path path) -> std::string
    {
        auto extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            });
        return extension;
    }

    auto discover_directory_files(const std::filesystem::path& root,
        const directory_source_options& options)
        -> std::expected<std::vector<std::filesystem::path>, std::string>
    {
        if (options.max_files == 0)
            return std::unexpected("directory source max_files must be greater than zero");
        if (options.max_file_bytes == 0)
            return std::unexpected(
                "directory source max_file_bytes must be greater than zero");

        std::error_code error;
        if (!std::filesystem::is_directory(root, error))
            return std::unexpected(std::format("document source '{}' is not a directory: {}",
                root.string(), error ? error.message() : "path not found"));

        std::set<std::string, std::less<>> extensions;
        for (auto extension : options.extensions)
        {
            std::ranges::transform(extension, extension.begin(), [](unsigned char value)
                {
                    return static_cast<char>(std::tolower(value));
                });
            if (!extension.empty() && extension.front() != '.')
                extension.insert(extension.begin(), '.');
            extensions.insert(std::move(extension));
        }

        std::vector<std::filesystem::path> paths;
        const auto inspect = [&](const std::filesystem::directory_entry& entry)
            -> std::expected<void, std::string>
        {
            std::error_code status_error;
            const auto status = entry.symlink_status(status_error);
            if (status_error)
            {
                if (options.ignore_unreadable_files)
                    return {};
                return std::unexpected(std::format("failed to inspect '{}': {}",
                    entry.path().string(), status_error.message()));
            }
            if (std::filesystem::is_symlink(status) ||
                !std::filesystem::is_regular_file(status))
                return {};
            if (!extensions.empty() &&
                !extensions.contains(normalized_extension(entry.path())))
                return {};
            const auto size = entry.file_size(status_error);
            if (status_error || size > options.max_file_bytes)
            {
                if (options.ignore_unreadable_files)
                    return {};
                return std::unexpected(status_error
                        ? std::format("failed to inspect '{}': {}",
                              entry.path().string(), status_error.message())
                        : std::format("document '{}' exceeds max_file_bytes={}",
                              entry.path().string(), options.max_file_bytes));
            }
            if (paths.size() == options.max_files)
                return std::unexpected(std::format(
                    "directory source exceeds max_files={}", options.max_files));
            paths.push_back(entry.path());
            return {};
        };

        const auto iterator_options =
            std::filesystem::directory_options::skip_permission_denied;
        if (options.recursive)
        {
            for (std::filesystem::recursive_directory_iterator iterator{
                     root, iterator_options, error};
                iterator != std::default_sentinel; iterator.increment(error))
            {
                if (error)
                {
                    if (!options.ignore_unreadable_files)
                        return std::unexpected(std::format(
                            "failed to enumerate '{}': {}", root.string(),
                            error.message()));
                    error.clear();
                    continue;
                }
                auto accepted = inspect(*iterator);
                if (!accepted)
                    return std::unexpected(accepted.error());
            }
        }
        else
        {
            for (std::filesystem::directory_iterator iterator{
                     root, iterator_options, error};
                iterator != std::default_sentinel; iterator.increment(error))
            {
                if (error)
                {
                    if (!options.ignore_unreadable_files)
                        return std::unexpected(std::format(
                            "failed to enumerate '{}': {}", root.string(),
                            error.message()));
                    error.clear();
                    continue;
                }
                auto accepted = inspect(*iterator);
                if (!accepted)
                    return std::unexpected(accepted.error());
            }
        }
        std::ranges::sort(paths, {}, [](const auto& path)
            {
                return path.lexically_normal().generic_string();
            });
        return paths;
    }

} // namespace

functional_document_parser::functional_document_parser(
    document_parse_handler handler)
    : handler_(std::move(handler))
{
    if (!handler_)
        throw std::invalid_argument("document parser handler cannot be empty");
}

auto functional_document_parser::parse(document source,
    const run_config& config) -> task<std::expected<document, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document parsing cancelled");
    co_return co_await handler_(std::move(source), config);
}

plain_text_document_parser::plain_text_document_parser(
    plain_text_parser_options options) noexcept
    : options_(options)
{
}

auto plain_text_document_parser::parse(document source,
    const run_config& config)
    -> task<std::expected<document, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document parsing cancelled");
    if (options_.remove_utf8_bom && source.page_content.starts_with("\xEF\xBB\xBF"))
        source.page_content.erase(0, 3);
    if (options_.reject_embedded_nul && source.page_content.contains('\0'))
        co_return std::unexpected(std::format(
            "document '{}' contains embedded NUL bytes", source.id));
    if (options_.validate_utf8 && !is_valid_utf8(source.page_content))
        co_return std::unexpected(std::format(
            "document '{}' is not valid UTF-8", source.id));
    if (options_.reject_blank_documents && is_blank(source.page_content))
        co_return std::unexpected(std::format(
            "document '{}' is blank", source.id));
    co_return source;
}

markdown_document_parser::markdown_document_parser(
    markdown_parser_options options) noexcept
    : options_(options)
{
}

auto markdown_document_parser::parse(document source,
    const run_config& config) -> task<std::expected<document, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document parsing cancelled");
    if (options_.remove_utf8_bom && source.page_content.starts_with("\xEF\xBB\xBF"))
        source.page_content.erase(0, 3);
    if (!is_valid_utf8(source.page_content))
        co_return std::unexpected(std::format(
            "Markdown document '{}' is not valid UTF-8", source.id));

    std::size_t content_start = 0;
    if (source.page_content.starts_with("---\n") ||
        source.page_content.starts_with("---\r\n"))
    {
        const auto first_line_end = source.page_content.find('\n');
        auto cursor = first_line_end + 1;
        std::size_t closing_start = std::string::npos;
        std::size_t closing_end = std::string::npos;
        json fields = cnetmod::json::object();
        while (cursor < source.page_content.size() && cursor <= 64 * 1024)
        {
            const auto line_end = source.page_content.find('\n', cursor);
            const auto raw_end = line_end == std::string::npos
                ? source.page_content.size()
                : line_end;
            auto line = std::string_view{source.page_content}.substr(
                cursor, raw_end - cursor);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            const auto clean = trim(line);
            if (clean == "---" || clean == "...")
            {
                closing_start = cursor;
                closing_end = line_end == std::string::npos
                    ? raw_end
                    : line_end + 1;
                break;
            }
            if (options_.extract_front_matter && !clean.empty() &&
                !clean.starts_with('#'))
            {
                const auto separator = clean.find(':');
                if (separator != std::string_view::npos)
                {
                    const auto key = trim(clean.substr(0, separator));
                    auto value = trim(clean.substr(separator + 1));
                    if (!key.empty())
                    {
                        if (value.size() >= 2 &&
                            ((value.front() == '"' && value.back() == '"') ||
                                (value.front() == '\'' && value.back() == '\'')))
                            value = value.substr(1, value.size() - 2);
                        fields[std::string{key}] = std::string{value};
                    }
                }
            }
            if (line_end == std::string::npos)
                break;
            cursor = line_end + 1;
        }
        if (closing_start == std::string::npos)
            co_return std::unexpected(std::format(
                "Markdown document '{}' has unterminated front matter", source.id));
        if (options_.extract_front_matter)
        {
            source.metadata["front_matter"] = std::move(fields);
            source.metadata["front_matter_raw"] = source.page_content.substr(
                first_line_end + 1, closing_start - first_line_end - 1);
        }
        content_start = closing_end;
    }
    if (options_.remove_front_matter && content_start != 0)
        source.page_content.erase(0, content_start);
    if (options_.extract_title)
    {
        std::string_view content = source.page_content;
        std::size_t cursor = 0;
        while (cursor < content.size())
        {
            const auto line_end = content.find('\n', cursor);
            auto line = trim(content.substr(cursor,
                (line_end == std::string_view::npos ? content.size() : line_end) - cursor));
            if (line.starts_with("# "))
            {
                source.metadata["title"] = std::string{trim(line.substr(2))};
                break;
            }
            if (line_end == std::string_view::npos)
                break;
            cursor = line_end + 1;
        }
    }
    source.metadata["format"] = "markdown";
    if (options_.reject_blank_documents && is_blank(source.page_content))
        co_return std::unexpected(std::format(
            "Markdown document '{}' is blank", source.id));
    co_return source;
}

html_document_parser::html_document_parser(html_parser_options options)
    : options_(options)
{
    if (options_.max_output_bytes == 0)
        throw std::invalid_argument("HTML parser max_output_bytes must be greater than zero");
}

auto html_document_parser::parse(document source,
    const run_config& config) -> task<std::expected<document, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document parsing cancelled");
    if (!is_valid_utf8(source.page_content))
        co_return std::unexpected(std::format(
            "HTML document '{}' is not valid UTF-8", source.id));
    const auto lower = lower_ascii(source.page_content);
    if (options_.extract_title)
    {
        const auto title_start = lower.find("<title");
        if (title_start != std::string::npos)
        {
            const auto body_start = lower.find('>', title_start + 6);
            const auto body_end = body_start == std::string::npos
                ? std::string::npos
                : lower.find("</title", body_start + 1);
            if (body_start != std::string::npos && body_end != std::string::npos)
            {
                auto title = decode_html_entities(std::string_view{source.page_content}
                        .substr(body_start + 1, body_end - body_start - 1));
                source.metadata["title"] = normalized_visible_text(std::move(title));
            }
        }
    }
    auto content = html_to_text(source.page_content, options_.max_output_bytes);
    if (!content)
        co_return std::unexpected(std::format(
            "failed to parse HTML document '{}': {}", source.id, content.error()));
    source.page_content = std::move(*content);
    source.metadata["format"] = "html";
    if (options_.reject_blank_documents && is_blank(source.page_content))
        co_return std::unexpected(std::format(
            "HTML document '{}' has no visible text", source.id));
    co_return source;
}

auto document_parser_registry::add(std::string extension,
    document_parser& parser) -> std::expected<void, std::string>
{
    std::ranges::transform(extension, extension.begin(), [](unsigned char value)
        {
            return static_cast<char>(std::tolower(value));
        });
    if (extension.empty())
        return std::unexpected("document parser extension cannot be empty");
    if (extension.front() != '.')
        extension.insert(extension.begin(), '.');
    if (parsers_.contains(extension))
        return std::unexpected("duplicate document parser extension: " + extension);
    parsers_.emplace(std::move(extension), &parser);
    return {};
}

auto document_parser_registry::add_media_type(std::string media_type,
    document_parser& parser) -> std::expected<void, std::string>
{
    media_type = normalized_media_type(media_type);
    if (media_type.empty() || !media_type.contains('/'))
        return std::unexpected("document parser media type is invalid");
    if (media_type_parsers_.contains(media_type))
        return std::unexpected("duplicate document parser media type: " + media_type);
    media_type_parsers_.emplace(std::move(media_type), &parser);
    return {};
}

void document_parser_registry::set_fallback(document_parser& parser) noexcept
{
    fallback_ = &parser;
}

auto document_parser_registry::resolve(
    const std::filesystem::path& path) const noexcept -> document_parser*
{
    const auto found = parsers_.find(normalized_extension(path));
    return found == parsers_.end() ? fallback_ : found->second;
}

auto document_parser_registry::resolve(const std::filesystem::path& path,
    std::string_view media_type) const noexcept -> document_parser*
{
    const auto normalized = normalized_media_type(media_type);
    if (!normalized.empty())
    {
        const auto found = media_type_parsers_.find(normalized);
        if (found != media_type_parsers_.end())
            return found->second;
    }
    return resolve(path);
}

file_document_source::file_document_source(io_context& context,
    std::vector<std::filesystem::path> paths)
    : context_(context), paths_(std::move(paths))
{
}

file_document_source::file_document_source(io_context& context,
    std::vector<std::filesystem::path> paths,
    document_parser_registry& parsers)
    : context_(context), paths_(std::move(paths)), parsers_(&parsers)
{
}

auto file_document_source::load(const run_config& config)
    -> task<std::expected<std::vector<document>, std::string>>
{
    std::vector<document> documents;
    documents.reserve(paths_.size());
    for (const auto& path : paths_)
    {
        if (config.is_cancelled())
            co_return std::unexpected("document loading cancelled");
        auto content = co_await async_file_read_all(context_, path);
        if (!content)
            co_return std::unexpected(std::format(
                "failed to read '{}': {}", path.string(),
                content.error().message()));
        auto normalized = path.lexically_normal().generic_string();
        document loaded{.id = normalized,
            .page_content = std::move(*content),
            .metadata = {
                {"source", normalized},
                {"file_name", path.filename().string()},
                {"extension", path.extension().string()}}};
        if (parsers_)
        {
            if (auto* parser = parsers_->resolve(path))
            {
                auto parsed = co_await parser->parse(std::move(loaded), config);
                if (!parsed)
                    co_return std::unexpected(std::format(
                        "failed to parse '{}': {}", path.string(), parsed.error()));
                loaded = std::move(*parsed);
            }
        }
        documents.push_back(std::move(loaded));
    }
    co_return documents;
}

http_document_fetcher::http_document_fetcher(http::client& client,
    std::size_t max_response_bytes)
    : client_(client), max_response_bytes_(max_response_bytes)
{
    if (max_response_bytes_ == 0)
        throw std::invalid_argument(
            "HTTP document max_response_bytes must be greater than zero");
}

auto http_document_fetcher::fetch(std::string url,
    const run_config& config)
    -> task<std::expected<downloaded_document, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document download cancelled");
    std::expected<http::response, std::error_code> response;
    if (config.cancellation)
    {
        http::request request{http::http_method::GET, url};
        response = co_await client_.send(request, *config.cancellation);
    }
    else
    {
        response = co_await client_.get(url);
    }
    if (!response)
        co_return std::unexpected(std::format(
            "failed to download '{}': {}", url, response.error().message()));
    if (response->status_code() < 200 || response->status_code() >= 300)
        co_return std::unexpected(std::format(
            "document download '{}' returned HTTP {}", url,
            response->status_code()));
    if (response->body().size() > max_response_bytes_)
        co_return std::unexpected(std::format(
            "document download '{}' exceeds max_response_bytes={}",
            url, max_response_bytes_));
    co_return downloaded_document{.url = std::move(url),
        .body = std::string{response->body()},
        .content_type = std::string{response->get_header("Content-Type")}};
}

url_document_source::url_document_source(document_fetcher& fetcher,
    std::vector<std::string> urls, document_parser_registry* parsers)
    : fetcher_(fetcher), urls_(std::move(urls)), parsers_(parsers)
{
}

auto url_document_source::load(const run_config& config)
    -> task<std::expected<std::vector<document>, std::string>>
{
    std::vector<document> documents;
    documents.reserve(urls_.size());
    for (const auto& url : urls_)
    {
        if (config.is_cancelled())
            co_return std::unexpected("document loading cancelled");
        auto downloaded = co_await fetcher_.fetch(url, config);
        if (!downloaded)
            co_return std::unexpected(downloaded.error());
        document loaded{.id = downloaded->url,
            .page_content = std::move(downloaded->body),
            .metadata = {{"source", downloaded->url}, {"url", downloaded->url},
                {"content_type", downloaded->content_type}}};
        if (parsers_)
        {
            auto path = downloaded->url.substr(0,
                downloaded->url.find_first_of("?#"));
            if (auto* parser = parsers_->resolve(path, downloaded->content_type))
            {
                auto parsed = co_await parser->parse(std::move(loaded), config);
                if (!parsed)
                    co_return std::unexpected(std::format(
                        "failed to parse '{}': {}", url, parsed.error()));
                loaded = std::move(*parsed);
            }
        }
        documents.push_back(std::move(loaded));
    }
    co_return documents;
}

directory_document_source::directory_document_source(io_context& context,
    thread_pool& pool, std::filesystem::path root,
    directory_source_options options)
    : context_(context), pool_(pool), root_(std::move(root)), options_(std::move(options))
{
}

directory_document_source::directory_document_source(io_context& context,
    thread_pool& pool, std::filesystem::path root,
    document_parser_registry& parsers, directory_source_options options)
    : context_(context), pool_(pool), root_(std::move(root)), options_(std::move(options)), parsers_(&parsers)
{
}

auto directory_document_source::load(const run_config& config)
    -> task<std::expected<std::vector<document>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document loading cancelled");
    auto paths = co_await blocking_invoke(pool_, context_,
        [root = root_, options = options_]
        {
            return discover_directory_files(root, options);
        });
    if (!paths)
        co_return std::unexpected(paths.error());
    if (config.is_cancelled())
        co_return std::unexpected("document loading cancelled");

    auto files = parsers_
        ? file_document_source{context_, std::move(*paths), *parsers_}
        : file_document_source{context_, std::move(*paths)};
    auto documents = co_await files.load(config);
    if (!documents)
        co_return std::unexpected(documents.error());
    for (auto& document : *documents)
        document.metadata["source_root"] = root_.lexically_normal().generic_string();
    co_return documents;
}

} // namespace cnetmod::openai
