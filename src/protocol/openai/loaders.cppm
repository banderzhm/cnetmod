/// cnetmod.protocol.openai:loaders — Concrete document sources

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:loaders;

import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.http;
import :model;
import :retrieval;
import :ingestion;

namespace cnetmod::openai {

/// Strategy that converts source bytes and provenance into a retrieval document.
export class document_parser
{
public:
    virtual ~document_parser() = default;
    virtual auto parse(document source, const run_config& config)
        -> task<std::expected<document, std::string>> = 0;
};

export using document_parse_handler = std::function<task<
    std::expected<document, std::string>>(
    document source, const run_config& config)>;

/// Adapter for external parsers such as Tika, Docling, PDF or Office services.
export class functional_document_parser final : public document_parser
{
public:
    explicit functional_document_parser(document_parse_handler handler);
    auto parse(document source, const run_config& config)
        -> task<std::expected<document, std::string>> override;

private:
    document_parse_handler handler_;
};

export struct plain_text_parser_options
{
    bool remove_utf8_bom = true;
    bool reject_blank_documents = true;
    bool reject_embedded_nul = true;
    bool validate_utf8 = true;
};

/// UTF-8 text parser with BOM normalization and blank-document validation.
export class plain_text_document_parser final : public document_parser
{
public:
    explicit plain_text_document_parser(
        plain_text_parser_options options = {}) noexcept;
    auto parse(document source, const run_config& config)
        -> task<std::expected<document, std::string>> override;

private:
    plain_text_parser_options options_;
};

export struct markdown_parser_options
{
    bool remove_utf8_bom = true;
    bool remove_front_matter = true;
    bool extract_front_matter = true;
    bool extract_title = true;
    bool reject_blank_documents = true;
};

/// Parses UTF-8 Markdown, extracts bounded YAML-style front matter and title
/// metadata, and optionally removes the front matter from indexed content.
export class markdown_document_parser final : public document_parser
{
public:
    explicit markdown_document_parser(
        markdown_parser_options options = {}) noexcept;
    auto parse(document source, const run_config& config)
        -> task<std::expected<document, std::string>> override;

private:
    markdown_parser_options options_;
};

export struct html_parser_options
{
    bool extract_title = true;
    bool reject_blank_documents = true;
    std::size_t max_output_bytes = 16 * 1024 * 1024;
};

/// Converts HTML into normalized visible text while excluding script, style,
/// template and noscript bodies. The document title is retained as metadata.
export class html_document_parser final : public document_parser
{
public:
    explicit html_document_parser(html_parser_options options = {});
    auto parse(document source, const run_config& config)
        -> task<std::expected<document, std::string>> override;

private:
    html_parser_options options_;
};

/// Extension-based parser selection. Parsers are non-owning and must outlive
/// every source using the registry.
export class document_parser_registry
{
public:
    [[nodiscard]] auto add(std::string extension, document_parser& parser)
        -> std::expected<void, std::string>;
    [[nodiscard]] auto add_media_type(std::string media_type,
        document_parser& parser) -> std::expected<void, std::string>;
    void set_fallback(document_parser& parser) noexcept;
    [[nodiscard]] auto resolve(const std::filesystem::path& path) const noexcept
        -> document_parser*;
    [[nodiscard]] auto resolve(const std::filesystem::path& path,
        std::string_view media_type) const noexcept -> document_parser*;

private:
    std::map<std::string, document_parser*, std::less<>> parsers_;
    std::map<std::string, document_parser*, std::less<>> media_type_parsers_;
    document_parser* fallback_ = nullptr;
};

/// Loads files through cnetmod's asynchronous file I/O and delegates format
/// handling to an optional parser registry.
export class file_document_source final : public document_source
{
public:
    file_document_source(io_context& context,
        std::vector<std::filesystem::path> paths);
    file_document_source(io_context& context,
        std::vector<std::filesystem::path> paths,
        document_parser_registry& parsers);

    auto load(const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> override;

private:
    io_context& context_;
    std::vector<std::filesystem::path> paths_;
    document_parser_registry* parsers_ = nullptr;
};

/// Compatibility name for callers that only load text-like files.
export using text_file_source = file_document_source;

export struct downloaded_document
{
    std::string url;
    std::string body;
    std::string content_type;
};

export class document_fetcher
{
public:
    virtual ~document_fetcher() = default;
    virtual auto fetch(std::string url, const run_config& config)
        -> task<std::expected<downloaded_document, std::string>> = 0;
};

export class http_document_fetcher final : public document_fetcher
{
public:
    explicit http_document_fetcher(http::client& client,
        std::size_t max_response_bytes = 16 * 1024 * 1024);
    auto fetch(std::string url, const run_config& config)
        -> task<std::expected<downloaded_document, std::string>> override;

private:
    http::client& client_;
    std::size_t max_response_bytes_;
};

/// Loads remote documents through an injected fetch strategy and optional
/// extension-based parser registry.
export class url_document_source final : public document_source
{
public:
    url_document_source(document_fetcher& fetcher,
        std::vector<std::string> urls,
        document_parser_registry* parsers = nullptr);
    auto load(const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> override;

private:
    document_fetcher& fetcher_;
    std::vector<std::string> urls_;
    document_parser_registry* parsers_;
};

export struct directory_source_options
{
    bool recursive = true;
    std::set<std::string, std::less<>> extensions;
    std::size_t max_files = 10'000;
    std::uintmax_t max_file_bytes = 8 * 1024 * 1024;
    bool ignore_unreadable_files = false;
};

/// Discovers a bounded, deterministic set of regular text files off the I/O
/// event loop, then loads their contents through asynchronous file I/O.
export class directory_document_source final : public document_source
{
public:
    directory_document_source(io_context& context, thread_pool& pool,
        std::filesystem::path root, directory_source_options options = {});
    directory_document_source(io_context& context, thread_pool& pool,
        std::filesystem::path root, document_parser_registry& parsers,
        directory_source_options options = {});

    auto load(const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> override;

private:
    io_context& context_;
    thread_pool& pool_;
    std::filesystem::path root_;
    directory_source_options options_;
    document_parser_registry* parsers_ = nullptr;
};

} // namespace cnetmod::openai
