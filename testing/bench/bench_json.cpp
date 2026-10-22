/// cnetmod benchmark - typed JSON serialization of an HttpArena-shaped list.
///
/// Usage: bench_json [dataset.json] [iterations]
/// Without a dataset path a deterministic 50-item list is generated.

#include <cnetmod/json.hpp>

import std;
import cnetmod.json;

namespace {

struct rating
{
    std::int64_t score{};
    std::int64_t count{};
};

struct item
{
    std::int64_t id{};
    std::string name;
    std::string category;
    std::int64_t price{};
    std::int64_t quantity{};
    bool active{};
    std::vector<std::string> tags;
    rating rating_value;
    std::int64_t total{};
};

struct item_list
{
    std::vector<item> items;
    std::size_t count{};
};

} // namespace

CNETMOD_JSON(rating, CNETMOD_JSON_FIELD(score), CNETMOD_JSON_FIELD(count))

template <> struct cnetmod::json::document_traits<item>
{
    static constexpr auto fields()
    {
        return std::tuple{cnetmod::json::field("id", &item::id),
            cnetmod::json::field("name", &item::name),
            cnetmod::json::field("category", &item::category),
            cnetmod::json::field("price", &item::price),
            cnetmod::json::field("quantity", &item::quantity),
            cnetmod::json::field("active", &item::active),
            cnetmod::json::field("tags", &item::tags),
            cnetmod::json::field("rating", &item::rating_value),
            cnetmod::json::field("total", &item::total)};
    }
};

CNETMOD_JSON(item_list, CNETMOD_JSON_FIELD(items), CNETMOD_JSON_FIELD(count))

namespace {

auto generated() -> item_list
{
    constexpr std::array names{"Alpha Widget", "Pro Valve", "Core Relay", "Mini Gear", "Flex Panel"};
    constexpr std::array categories{"electronics", "tools", "books", "home", "office"};
    constexpr std::array tags{"sale", "heavy-duty", "popular", "fast", "new", "wireless", "eco"};
    item_list list;
    for (std::int64_t index = 0; index < 50; ++index)
    {
        item value{.id = index + 1,
            .name = names[static_cast<std::size_t>(index) % names.size()],
            .category = categories[static_cast<std::size_t>(index) % categories.size()],
            .price = 100 + index * 7,
            .quantity = 1 + index * 3,
            .active = index % 3 != 0,
            .rating_value = {.score = index % 50, .count = 10 + index * 11}};
        for (std::size_t tag = 0; tag < 2 + static_cast<std::size_t>(index) % 3; ++tag)
            value.tags.emplace_back(tags[(static_cast<std::size_t>(index) + tag) % tags.size()]);
        value.total = value.price * value.quantity * 4;
        list.items.push_back(std::move(value));
    }
    list.count = list.items.size();
    return list;
}

auto load(const char* path) -> std::optional<item_list>
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    auto document = cnetmod::json::parse_document(text);
    if (!document)
        return std::nullopt;
    item_list list;
    for (const auto& entry : document->get<std::vector<cnetmod::json::document>>())
    {
        item value;
        value.id = entry.at("id").get<std::int64_t>();
        value.name = entry.at("name").get<std::string>();
        value.category = entry.at("category").get<std::string>();
        value.price = entry.at("price").get<std::int64_t>();
        value.quantity = entry.at("quantity").get<std::int64_t>();
        value.active = entry.at("active").get<bool>();
        value.tags = entry.at("tags").get<std::vector<std::string>>();
        value.rating_value = {entry.at("rating").at("score").get<std::int64_t>(),
            entry.at("rating").at("count").get<std::int64_t>()};
        value.total = value.price * value.quantity * 4;
        list.items.push_back(std::move(value));
    }
    list.count = list.items.size();
    return list;
}

} // namespace

auto main(int argc, char** argv) -> int
{
    auto list = argc > 1 ? load(argv[1]) : std::optional<item_list>{generated()};
    if (!list)
    {
        std::puts("cannot load dataset");
        return 2;
    }
    const std::size_t iterations = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 200000U;
    std::size_t bytes = 0;
    for (std::size_t warm = 0; warm < 2000; ++warm)
        bytes += cnetmod::json::write(*list)->size();
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t round = 0; round < iterations; ++round)
    {
        auto encoded = cnetmod::json::write(*list);
        bytes += encoded->size();
    }
    const auto elapsed = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - started).count();
    const auto size = cnetmod::json::write(*list)->size();
    std::printf("items %zu, %zu bytes: %.3f us/document, %.2f GB/s (checksum %zu)\n",
        list->items.size(), size, elapsed / static_cast<double>(iterations),
        static_cast<double>(size) * static_cast<double>(iterations) / elapsed / 1000.0, bytes);
    return 0;
}
