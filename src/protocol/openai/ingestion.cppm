/// cnetmod.protocol.openai:ingestion — Document ingestion pipeline

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.openai:ingestion;

import std;
import cnetmod.coro.task;
import :model;
import :retrieval;

namespace cnetmod::openai {

export class document_source
{
public:
    virtual ~document_source() = default;
    virtual auto load(const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> = 0;
};

export class document_transformer
{
public:
    virtual ~document_transformer() = default;
    virtual auto transform(std::vector<document> documents,
        const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> = 0;
};

export using document_transform = std::function<task<
    std::expected<std::vector<document>, std::string>>(
    std::vector<document> documents, const run_config& config)>;

/// Adapter for application-defined asynchronous document transformations.
export class functional_document_transformer final : public document_transformer
{
public:
    explicit functional_document_transformer(document_transform transform);
    auto transform(std::vector<document> documents,
        const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> override;

private:
    document_transform transform_;
};

export class metadata_enricher final : public document_transformer
{
public:
    explicit metadata_enricher(
        std::map<std::string, json, std::less<>> metadata,
        bool overwrite = false);
    auto transform(std::vector<document> documents,
        const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> override;

private:
    std::map<std::string, json, std::less<>> metadata_;
    bool overwrite_;
};

export using document_predicate =
    std::function<bool(const document& candidate)>;

export class document_filter final : public document_transformer
{
public:
    explicit document_filter(document_predicate predicate);
    auto transform(std::vector<document> documents,
        const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> override;

private:
    document_predicate predicate_;
};

export class document_splitter
{
public:
    virtual ~document_splitter() = default;
    virtual auto split(std::vector<document> documents)
        -> std::expected<std::vector<document>, std::string> = 0;
};

export class document_index
{
public:
    virtual ~document_index() = default;
    virtual auto add(std::vector<document> documents)
        -> task<std::expected<void, std::string>> = 0;
};

export class static_document_source final : public document_source
{
public:
    explicit static_document_source(std::vector<document> documents);
    auto load(const run_config& config)
        -> task<std::expected<std::vector<document>, std::string>> override;

private:
    std::vector<document> documents_;
};

export struct text_splitter_options
{
    std::size_t chunk_size = 1000;
    std::size_t chunk_overlap = 200;
    std::vector<std::string> separators = {"\n\n", "\n", " "};
};

/// Recursive boundary-aware character splitter preserving source metadata.
export class recursive_text_splitter final : public document_splitter
{
public:
    explicit recursive_text_splitter(text_splitter_options options = {});
    auto split(std::vector<document> documents)
        -> std::expected<std::vector<document>, std::string> override;

private:
    text_splitter_options options_;
};

export struct markdown_splitter_options
{
    bool include_heading = true;
    std::size_t maximum_heading_level = 6;
};

/// Splits Markdown at ATX headings while attaching heading metadata.
export class markdown_header_splitter final : public document_splitter
{
public:
    explicit markdown_header_splitter(markdown_splitter_options options = {});
    auto split(std::vector<document> documents)
        -> std::expected<std::vector<document>, std::string> override;

private:
    markdown_splitter_options options_;
};

export class vector_store_index final : public document_index
{
public:
    explicit vector_store_index(embedding_store& store) noexcept;
    auto add(std::vector<document> documents)
        -> task<std::expected<void, std::string>> override;

private:
    embedding_store& store_;
};

export struct ingestion_result
{
    std::size_t loaded_documents = 0;
    std::size_t indexed_segments = 0;
};

/// Pipeline coordinating load, transformation, splitting and indexing.
export class ingestion_pipeline
{
public:
    ingestion_pipeline& add_transformer(document_transformer& transformer);
    auto with_splitter(document_splitter& splitter) noexcept
        -> ingestion_pipeline&;
    auto run(document_source& source, document_index& index,
        const run_config& config = {})
        -> task<std::expected<ingestion_result, std::string>>;

private:
    std::vector<document_transformer*> transformers_;
    document_splitter* splitter_ = nullptr;
};

} // namespace cnetmod::openai
