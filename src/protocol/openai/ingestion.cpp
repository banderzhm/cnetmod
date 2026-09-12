/// cnetmod.protocol.openai:ingestion — implementations

module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.openai;

import std;
import cnetmod.coro.task;
import :model;
import :retrieval;
import :ingestion;

namespace cnetmod::openai {

static_document_source::static_document_source(std::vector<document> documents)
    : documents_(std::move(documents))
{
}

auto static_document_source::load(const run_config& config)
    -> task<std::expected<std::vector<document>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document loading cancelled");
    co_return documents_;
}

functional_document_transformer::functional_document_transformer(
    document_transform transform)
    : transform_(std::move(transform))
{
    if (!transform_)
        throw std::invalid_argument("document transform cannot be empty");
}

auto functional_document_transformer::transform(
    std::vector<document> documents, const run_config& config)
    -> task<std::expected<std::vector<document>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document transformation cancelled");
    co_return co_await transform_(std::move(documents), config);
}

metadata_enricher::metadata_enricher(
    std::map<std::string, json, std::less<>> metadata, bool overwrite)
    : metadata_(std::move(metadata)), overwrite_(overwrite)
{
}

auto metadata_enricher::transform(std::vector<document> documents,
    const run_config& config)
    -> task<std::expected<std::vector<document>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document transformation cancelled");
    for (auto& item : documents)
    {
        for (const auto& [key, value] : metadata_)
        {
            if (overwrite_ || !item.metadata.contains(key))
                item.metadata[key] = value;
        }
    }
    co_return documents;
}

document_filter::document_filter(document_predicate predicate)
    : predicate_(std::move(predicate))
{
    if (!predicate_)
        throw std::invalid_argument("document predicate cannot be empty");
}

auto document_filter::transform(std::vector<document> documents,
    const run_config& config)
    -> task<std::expected<std::vector<document>, std::string>>
{
    if (config.is_cancelled())
        co_return std::unexpected("document transformation cancelled");
    std::erase_if(documents, [&](const auto& item)
        {
            return !predicate_(item);
        });
    co_return documents;
}

recursive_text_splitter::recursive_text_splitter(text_splitter_options options)
    : options_(std::move(options))
{
    if (options_.chunk_size == 0)
        throw std::invalid_argument("chunk_size must be greater than zero");
    if (options_.chunk_overlap >= options_.chunk_size)
        throw std::invalid_argument("chunk_overlap must be smaller than chunk_size");
}

auto recursive_text_splitter::split(std::vector<document> documents)
    -> std::expected<std::vector<document>, std::string>
{
    std::vector<document> chunks;
    for (auto& source : documents)
    {
        if (source.page_content.empty())
            continue;
        std::size_t begin = 0;
        std::size_t chunk_index = 0;
        while (begin < source.page_content.size())
        {
            auto end = std::min(begin + options_.chunk_size,
                source.page_content.size());
            if (end < source.page_content.size())
            {
                const auto minimum_boundary = begin + options_.chunk_size / 2;
                for (const auto& separator : options_.separators)
                {
                    if (separator.empty())
                        continue;
                    const auto boundary = source.page_content.rfind(separator, end);
                    if (boundary != std::string::npos &&
                        boundary >= minimum_boundary)
                    {
                        end = boundary + separator.size();
                        break;
                    }
                }
            }
            if (end <= begin)
                return std::unexpected("document splitter made no progress");

            auto metadata = source.metadata;
            metadata["source_id"] = source.id;
            metadata["chunk_index"] = chunk_index;
            metadata["start_offset"] = begin;
            metadata["end_offset"] = end;
            chunks.push_back({.id = std::format("{}#chunk-{}", source.id,
                                  chunk_index),
                .page_content = source.page_content.substr(begin, end - begin),
                .metadata = std::move(metadata)});
            ++chunk_index;
            if (end == source.page_content.size())
                break;
            const auto next = end > options_.chunk_overlap
                ? end - options_.chunk_overlap
                : end;
            begin = std::max(begin + 1, next);
        }
    }
    return chunks;
}

markdown_header_splitter::markdown_header_splitter(
    markdown_splitter_options options)
    : options_(options)
{
    if (options_.maximum_heading_level == 0 ||
        options_.maximum_heading_level > 6)
        throw std::invalid_argument(
            "maximum_heading_level must be in [1, 6]");
}

auto markdown_header_splitter::split(std::vector<document> documents)
    -> std::expected<std::vector<document>, std::string>
{
    std::vector<document> sections;
    for (const auto& source : documents)
    {
        std::string heading;
        std::size_t heading_level = 0;
        std::string content;
        std::size_t section_index = 0;
        const auto flush = [&]
        {
            if (content.find_first_not_of(" \t\r\n") == std::string::npos)
                return;
            auto metadata = source.metadata;
            metadata["source_id"] = source.id;
            metadata["section_index"] = section_index;
            if (!heading.empty())
            {
                metadata["heading"] = heading;
                metadata["heading_level"] = heading_level;
            }
            sections.push_back({.id = std::format("{}#section-{}",
                                    source.id, section_index++),
                .page_content = std::move(content),
                .metadata = std::move(metadata)});
            content.clear();
        };

        std::size_t cursor = 0;
        while (cursor <= source.page_content.size())
        {
            const auto line_end = source.page_content.find('\n', cursor);
            const auto end = line_end == std::string::npos
                ? source.page_content.size()
                : line_end;
            const auto line = std::string_view{source.page_content}.substr(
                cursor, end - cursor);
            std::size_t level = 0;
            while (level < line.size() && line[level] == '#')
                ++level;
            const auto is_heading = level > 0 &&
                level <= options_.maximum_heading_level &&
                level < line.size() && line[level] == ' ';
            if (is_heading)
            {
                flush();
                heading = std::string{line.substr(level + 1)};
                if (!heading.empty() && heading.back() == '\r')
                    heading.pop_back();
                heading_level = level;
                if (options_.include_heading)
                {
                    content.append(line.data(), line.size());
                    content.push_back('\n');
                }
            }
            else
            {
                content.append(line);
                if (line_end != std::string::npos)
                    content.push_back('\n');
            }
            if (line_end == std::string::npos)
                break;
            cursor = line_end + 1;
        }
        flush();
    }
    return sections;
}

vector_store_index::vector_store_index(embedding_store& store) noexcept
    : store_(store)
{
}

auto vector_store_index::add(std::vector<document> documents)
    -> task<std::expected<void, std::string>>
{
    co_return co_await store_.add_documents(std::move(documents));
}

auto ingestion_pipeline::add_transformer(document_transformer& transformer)
    -> ingestion_pipeline&
{
    transformers_.push_back(&transformer);
    return *this;
}

auto ingestion_pipeline::with_splitter(document_splitter& splitter) noexcept
    -> ingestion_pipeline&
{
    splitter_ = &splitter;
    return *this;
}

auto ingestion_pipeline::run(document_source& source, document_index& index,
    const run_config& config)
    -> task<std::expected<ingestion_result, std::string>>
{
    auto documents = co_await source.load(config);
    if (!documents)
        co_return std::unexpected("document source failed: " + documents.error());
    const auto loaded = documents->size();
    for (auto* transformer : transformers_)
    {
        if (config.is_cancelled())
            co_return std::unexpected("document ingestion cancelled");
        auto transformed = co_await transformer->transform(
            std::move(*documents), config);
        if (!transformed)
            co_return std::unexpected("document transformer failed: " +
                transformed.error());
        documents = std::move(transformed);
    }
    if (splitter_)
    {
        auto split = splitter_->split(std::move(*documents));
        if (!split)
            co_return std::unexpected("document splitting failed: " + split.error());
        documents = std::move(split);
    }
    const auto segments = documents->size();
    auto indexed = co_await index.add(std::move(*documents));
    if (!indexed)
        co_return std::unexpected("document indexing failed: " + indexed.error());
    co_return ingestion_result{
        .loaded_documents = loaded,
        .indexed_segments = segments};
}

} // namespace cnetmod::openai
