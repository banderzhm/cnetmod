/// cnetmod unit tests — direct JSON writer output and its document fallback.

#include "test_framework.hpp"
#include <cnetmod/json.hpp>

import std;
import cnetmod.json;

namespace writer_test {
struct rating
{
    std::int64_t score{};
    std::int64_t count{};
};

struct item
{
    std::int64_t id{};
    std::string name;
    bool active{};
    std::vector<std::string> tags;
    rating rating_value;
    std::optional<std::int64_t> total;
    double weight{};
    std::uint64_t serial{};
};

struct listing
{
    std::vector<item> items;
    std::size_t count{};
    std::map<std::string, std::int32_t> counters;
    std::unordered_map<std::string, std::int32_t> unordered;
};

enum class level : std::uint8_t { low = 1, high = 7 };

struct with_enum
{
    level value{level::low};
    std::optional<std::string> note;
};
} // namespace writer_test

CNETMOD_JSON(writer_test::rating, CNETMOD_JSON_FIELD(score), CNETMOD_JSON_FIELD(count))

template <> struct cnetmod::json::document_traits<writer_test::item>
{
    using _cnetmod_json_type = writer_test::item;
    static constexpr auto fields()
    {
        return std::tuple{cnetmod::json::field("id", &_cnetmod_json_type::id),
            cnetmod::json::field("name", &_cnetmod_json_type::name),
            cnetmod::json::field("active", &_cnetmod_json_type::active),
            cnetmod::json::field("tags", &_cnetmod_json_type::tags),
            cnetmod::json::field("rating", &_cnetmod_json_type::rating_value),
            cnetmod::json::field("total", &_cnetmod_json_type::total),
            cnetmod::json::field("weight", &_cnetmod_json_type::weight),
            cnetmod::json::field("serial", &_cnetmod_json_type::serial)};
    }
};

namespace writer_test {
struct repeated
{
    std::int64_t first{};
    std::int64_t second{};
    std::optional<std::string> label;
};
} // namespace writer_test

// "x" is mapped twice; like assignment into the backend's map, the field
// declared last provides the value.
template <> struct cnetmod::json::document_traits<writer_test::repeated>
{
    static constexpr auto fields()
    {
        return std::tuple{cnetmod::json::field("x", &writer_test::repeated::first),
            cnetmod::json::field("a", &writer_test::repeated::label),
            cnetmod::json::field("x", &writer_test::repeated::second)};
    }
};

CNETMOD_JSON(writer_test::listing, CNETMOD_JSON_FIELD(items), CNETMOD_JSON_FIELD(count),
    CNETMOD_JSON_FIELD(counters), CNETMOD_JSON_FIELD(unordered))
CNETMOD_JSON(writer_test::with_enum, CNETMOD_JSON_FIELD(value), CNETMOD_JSON_FIELD(note))

namespace {
auto sample() -> writer_test::listing
{
    writer_test::listing value;
    value.items.push_back({.id = -3, .name = "Al\"pha\\\n\t\x01 \xE4\xBD\xA0",
        .active = true, .tags = {"a", ""}, .rating_value = {4, 12}, .total = 30,
        .weight = 1.5, .serial = 18446744073709551615ULL});
    value.items.push_back({.id = 2, .name = "b", .weight = 0.1});
    value.count = value.items.size();
    value.counters = {{"z", 1}, {"a", -2}};
    value.unordered = {{"y", 5}, {"b", 6}, {"m", 7}};
    return value;
}
} // namespace

TEST(json_writer_matches_document_backend_byte_for_byte)
{
    const auto value = sample();
    const auto direct = cnetmod::json::write(value);
    ASSERT_TRUE(direct.has_value());
    const auto document = cnetmod::json::to_document(value);
    ASSERT_TRUE(document.has_value());
    // The document's own pretty printer is the untouched backend path; its
    // compact form must agree with the direct writer once parsed back.
    const auto reparsed = cnetmod::json::parse_document(*direct);
    ASSERT_TRUE(reparsed.has_value());
    ASSERT_TRUE(*reparsed == *document);
    const auto pretty = cnetmod::json::write_document(*document, true);
    ASSERT_TRUE(pretty.has_value());
    const auto from_pretty = cnetmod::json::parse_document(*pretty);
    ASSERT_TRUE(from_pretty.has_value());
    const auto compact = cnetmod::json::write_document(*from_pretty);
    ASSERT_TRUE(compact.has_value());
    ASSERT_EQ(*direct, *compact);
}

TEST(json_writer_emits_sorted_keys_escapes_and_number_formats)
{
    const auto direct = cnetmod::json::write(sample());
    ASSERT_TRUE(direct.has_value());
    const std::string expected =
        R"({"count":2,"counters":{"a":-2,"z":1},"items":[)"
        R"({"active":true,"id":-3,"name":"Al\"pha\\\n\t\u0001 )" "\xE4\xBD\xA0"
        R"(","rating":{"count":12,"score":4},"serial":18446744073709551615,)"
        R"("tags":["a",""],"total":30,"weight":1.5},)"
        R"({"active":false,"id":2,"name":"b","rating":{"count":0,"score":0},)"
        R"("serial":0,"tags":[],"weight":0.1}],"unordered":{"b":6,"m":7,"y":5}})";
    ASSERT_EQ(*direct, expected);
}

TEST(json_writer_repeated_names_and_plain_strings_match_the_backend)
{
    const writer_test::repeated value{.first = 1, .second = 2};
    const auto direct = cnetmod::json::write(value);
    ASSERT_TRUE(direct.has_value());
    ASSERT_EQ(*direct, std::string{R"({"x":2})"});
    ASSERT_EQ(*direct, cnetmod::json::write_document(*cnetmod::json::to_document(value)).value());
    ASSERT_EQ(*cnetmod::json::write(writer_test::repeated{.first = 1, .second = 2, .label = "q"}),
        std::string{R"({"a":"q","x":2})"});

    // Every length around the 8-byte word boundary, plain and with one byte
    // that needs escaping at the last position.
    for (std::size_t length = 0; length <= 17U; ++length)
    {
        std::string plain;
        for (std::size_t index = 0; index < length; ++index)
            plain.push_back(static_cast<char>('a' + index % 26U));
        ASSERT_EQ(*cnetmod::json::write(plain), "\"" + plain + "\"");
        if (length == 0U)
            continue;
        auto quoted = plain;
        quoted.back() = '"';
        const auto expected = "\"" + plain.substr(0, length - 1U) + "\\\"\"";
        ASSERT_EQ(*cnetmod::json::write(quoted), expected);
    }
}

TEST(json_writer_null_policies_enums_and_doubles)
{
    writer_test::with_enum value{.value = writer_test::level::high};
    ASSERT_EQ(*cnetmod::json::write(value), std::string{R"({"value":7})"});
    ASSERT_EQ(*cnetmod::json::write_explicit_nulls(value),
        std::string{R"({"note":null,"value":7})"});
    ASSERT_EQ(*cnetmod::json::write(std::vector<double>{3.0, 1e100, -0.0,
                  std::numeric_limits<double>::infinity()}),
        std::string{"[3.0,1e+100,-0.0,null]"});
    ASSERT_EQ(*cnetmod::json::write(std::optional<std::int32_t>{}), std::string{"null"});
}

TEST(json_writer_rejects_malformed_utf8)
{
    for (const auto* bad : {"\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE4\xBD", "\x80"})
    {
        const auto written = cnetmod::json::write(std::vector<std::string>{bad});
        ASSERT_FALSE(written.has_value());
        ASSERT_TRUE(written.error() ==
            cnetmod::json::make_error_code(cnetmod::json::errc::serialization_failed));
        const auto document = cnetmod::json::to_document(std::vector<std::string>{bad});
        ASSERT_FALSE(cnetmod::json::write_document(*document).has_value());
    }
    ASSERT_EQ(*cnetmod::json::write(std::vector<std::string>{"\xF0\x9F\x98\x80"}),
        std::string{"[\"\xF0\x9F\x98\x80\"]"});
}

TEST(json_writer_escapes_every_position_like_a_byte_reference)
{
    // Independent byte-at-a-time escaper; the writer scans eight bytes at once.
    const auto reference = [](std::string_view text)
    {
        std::string out{"[\""};
        for (const auto character : text)
        {
            const auto byte = static_cast<unsigned char>(character);
            switch (byte)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20U)
                {
                    constexpr char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[byte >> 4U]);
                    out.push_back(hex[byte & 0x0FU]);
                }
                else
                    out.push_back(character);
            }
        }
        out += "\"]";
        return out;
    };
    const std::array<std::string_view, 12> pieces{"a", "Z", "~", " ", "\"", "\\",
        "\x7f", "\x01", "\x1f", "\n", "\xC3\xA9", "\xE4\xBD\xA0"};
    std::mt19937 random{20261022U};
    for (int round = 0; round < 20000; ++round)
    {
        std::string text;
        const auto length = random() % 40U;
        for (std::uint32_t index = 0; index < length; ++index)
            text += pieces[random() % pieces.size()];
        const auto written = cnetmod::json::write(std::vector<std::string>{text});
        ASSERT_TRUE(written.has_value());
        ASSERT_EQ(*written, reference(text));
    }
}

RUN_TESTS()
