#include "test_framework.hpp"

import std;
import cnetmod.utils.charconv;

TEST(character_conversion_requires_complete_input)
{
    double double_value = 0.0;
    ASSERT_EQ(static_cast<int>(
                  cnetmod::from_chars_double("12.5", double_value)),
        static_cast<int>(std::errc{}));
    ASSERT_EQ(double_value, 12.5);
    ASSERT_EQ(static_cast<int>(
                  cnetmod::from_chars_double("12.5suffix", double_value)),
        static_cast<int>(std::errc::invalid_argument));

    float float_value = 0.0F;
    ASSERT_EQ(static_cast<int>(
                  cnetmod::from_chars_float("3.25", float_value)),
        static_cast<int>(std::errc{}));
    ASSERT_EQ(float_value, 3.25F);
    ASSERT_EQ(static_cast<int>(
                  cnetmod::from_chars_float("3.25suffix", float_value)),
        static_cast<int>(std::errc::invalid_argument));
}

TEST(character_conversion_reports_invalid_and_out_of_range_values)
{
    double value = 0.0;
    ASSERT_EQ(static_cast<int>(
                  cnetmod::from_chars_double("not-a-number", value)),
        static_cast<int>(std::errc::invalid_argument));
    ASSERT_EQ(static_cast<int>(
                  cnetmod::from_chars_double("1e9999", value)),
        static_cast<int>(std::errc::result_out_of_range));
}

TEST(character_conversion_formats_floating_point_portably)
{
    std::array<char, 64> buffer{};
    auto double_result = cnetmod::to_chars_double(
        buffer.data(), buffer.data() + buffer.size(), 12.5);
    ASSERT_EQ(static_cast<int>(double_result.ec), static_cast<int>(std::errc{}));
    ASSERT_EQ(std::string_view(buffer.data(), double_result.ptr),
        std::string_view{"12.5"});

    auto float_result = cnetmod::to_chars_float(
        buffer.data(), buffer.data() + buffer.size(), 3.25F);
    ASSERT_EQ(static_cast<int>(float_result.ec), static_cast<int>(std::errc{}));
    ASSERT_EQ(std::string_view(buffer.data(), float_result.ptr),
        std::string_view{"3.25"});

    auto too_small = cnetmod::to_chars_double(
        buffer.data(), buffer.data() + 1, 12345.0);
    ASSERT_EQ(static_cast<int>(too_small.ec),
        static_cast<int>(std::errc::value_too_large));
}

RUN_TESTS()
