#include "test_framework.hpp"
import std;
import cnetmod.protocol.clickhouse;
namespace ch = cnetmod::clickhouse;
using bytes = std::vector<std::byte>;

static auto binary(std::initializer_list<unsigned> values) -> bytes
{
    bytes result;
    for (auto value : values)
        result.push_back(std::byte(value));
    return result;
}

TEST(native_block_matches_independent_wire_fixture)
{
    const auto expected = binary({1, 0, 2, 255, 255, 255, 255, 0, 1, 2,
        1, 'n', 6, 'U', 'I', 'n', 't', '1', '6', 1, 0, 255, 255});
    ch::block data{{{"n", "UInt16", {std::uint64_t{1}, std::uint64_t{65535}}}}};
    auto encoded = ch::wire::encode_block(data);
    ASSERT_TRUE(encoded.has_value());
    ASSERT_TRUE(*encoded == expected);
    auto decoded = ch::wire::decode_block(expected);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->data == data);
    ASSERT_EQ(decoded->consumed, expected.size());
    // Every split must report incomplete, never fabricate a partial block.
    for (std::size_t length{}; length < expected.size(); ++length)
    {
        auto partial = ch::wire::decode_block(std::span{expected}.first(length));
        ASSERT_FALSE(partial.has_value());
        ASSERT_EQ(partial.error().cause, std::make_error_code(std::errc::result_out_of_range));
    }
}

TEST(native_recursive_columns_and_exact_values)
{
    using v = ch::value;
    using seq = v::sequence;
    ch::block data{{{"signed", "Int64", {std::numeric_limits<std::int64_t>::min(), std::int64_t{-1}}},
        {"unsigned", "UInt64", {std::numeric_limits<std::uint64_t>::max(), std::uint64_t{0}}},
        {"float", "Float64", {1.25, -0.5}},
        {"text", "String", {std::string("a\0b", 3), std::string("中文")}},
        {"null", "Nullable(Int32)", {v{}, std::int64_t{-7}}},
        {"array", "Array(Nullable(String))", {seq{std::string("x"), v{}}, seq{}}},
        {"tuple", "Tuple(id UInt8, title String)", {seq{std::uint64_t{2}, std::string("a")}, seq{std::uint64_t{3}, std::string("b")}}},
        {"map", "Map(String, Int8)", {seq{seq{std::string("k"), std::int64_t{-2}}}, seq{}}},
        {"fixed", "FixedString(3)", {std::string("a\0b", 3), std::string("xyz")}},
        {"decimal", "Decimal(18, 4)", {std::int64_t{12345}, std::int64_t{-12345}}},
        {"wide", "UInt256", {std::string(32, '\xff'), std::string(32, '\0')}},
        {"uuid", "UUID", {std::string(16, '\x01'), std::string(16, '\x02')}},
        {"time", "DateTime64(6, 'UTC')", {std::int64_t{123456}, std::int64_t{-1}}},
        {"enum", "Enum8('a' = 1, 'b' = -1)", {std::int64_t{1}, std::int64_t{-1}}}}};
    auto encoded = ch::wire::encode_block(data);
    ASSERT_TRUE(encoded.has_value());
    auto decoded = ch::wire::decode_block(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->data == data);
}

TEST(native_invalid_input_and_resource_limits)
{
    ch::block mismatched{{{"a", "UInt8", {std::uint64_t{1}}}, {"b", "String", {}}}};
    ASSERT_FALSE(ch::wire::encode_block(mismatched).has_value());
    for (const auto type : {"UInt7", "Nullable()", "Array(String", "FixedString(0)", "Decimal(99, 2)", "DateTime64(10)", "Dynamic"})
        ASSERT_FALSE(ch::wire::encode_block(ch::block{{{"x", type, {}}}}).has_value());
    ASSERT_FALSE(ch::wire::encode_block(ch::block{{{"x", "UInt8", {std::uint64_t{256}}}}}).has_value());
    ASSERT_FALSE(ch::wire::encode_block(ch::block{{{"x", "Int8", {std::int64_t{-129}}}}}).has_value());
    ASSERT_FALSE(ch::wire::encode_block(ch::block{{{"x", "FixedString(2)", {std::string("abc")}}}}).has_value());
    ASSERT_FALSE(ch::wire::decode_block(binary({128, 128, 128, 128, 128, 128, 128, 128, 128, 2})).has_value());
    auto wrong_info = binary({9});
    ASSERT_FALSE(ch::wire::decode_block(wrong_info).has_value());
    ch::limits bound;
    bound.max_rows = 1;
    auto fixture = binary({0, 0, 2});
    ASSERT_FALSE(ch::wire::decode_block(fixture, bound).has_value());
    bound = {};
    bound.max_depth = 1;
    ASSERT_FALSE(ch::wire::encode_block(ch::block{{{"x", "Array(Array(Array(UInt8)))", {}}}}, bound).has_value());
    bound = {};
    bound.max_packet_bytes = 8;
    ASSERT_FALSE(ch::wire::encode_block(ch::block{}, bound).has_value());
    // Decreasing offsets must fail before allocating any flattened values.
    auto offsets = binary({0, 1, 2, 1, 'a', 12, 'A', 'r', 'r', 'a', 'y', '(', 'U', 'I', 'n', 't', '8', ')',
        2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0});
    auto malformed = ch::wire::decode_block(offsets);
    ASSERT_FALSE(malformed.has_value());
    ASSERT_FALSE(malformed.error().cause == std::errc::result_out_of_range);
}

TEST(native_low_cardinality_large_dictionary_index_width)
{
    ch::block input{{{"key", "LowCardinality(String)", {}}}};
    for (std::size_t i{}; i < 70000; ++i)
        input.columns[0].values.emplace_back(std::to_string(i));
    auto encoded = ch::wire::encode_block(input);
    ASSERT_TRUE(encoded.has_value());
    auto decoded = ch::wire::decode_block(*encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_TRUE(decoded->data == input);
    for (auto index : {std::size_t{255}, std::size_t{256}, std::size_t{65535}, std::size_t{65536}})
        ASSERT_EQ(std::get<std::string>(decoded->data.columns[0].values[index].data), std::to_string(index));
}
RUN_TESTS()
