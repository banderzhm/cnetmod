#include "test_framework.hpp"

import std;
import cnetmod.utils.flat_map;

TEST(flat_map_keeps_keys_sorted_and_unique)
{
    cnetmod::flat_map<int, std::string> values;
    values.reserve(4);

    ASSERT_TRUE(values.emplace(3, "three").second);
    ASSERT_TRUE(values.emplace(1, "one").second);
    ASSERT_TRUE(values.emplace(2, "two").second);
    ASSERT_FALSE(values.emplace(2, "duplicate").second);
    ASSERT_EQ(values.size(), 3U);

    auto iterator = values.begin();
    ASSERT_EQ(iterator++->first, 1);
    ASSERT_EQ(iterator++->first, 2);
    ASSERT_EQ(iterator->first, 3);
}

TEST(flat_map_supports_lookup_update_and_erase)
{
    cnetmod::flat_map<std::string, int, std::less<>> values;
    values["alpha"] = 1;
    values.insert_or_assign("beta", 2);
    values.insert_or_assign("alpha", 3);

    ASSERT_TRUE(values.contains(std::string_view{"alpha"}));
    ASSERT_EQ(values.find(std::string_view{"beta"})->second, 2);
    ASSERT_EQ(values.at("alpha"), 3);
    ASSERT_EQ(values.erase("alpha"), 1U);
    ASSERT_FALSE(values.contains("alpha"));
    ASSERT_EQ(values.erase("missing"), 0U);
}

TEST(flat_map_range_constructor_removes_duplicate_keys)
{
    const std::array<std::pair<int, int>, 4> source{{
        {4, 40},
        {2, 20},
        {4, 400},
        {1, 10},
    }};
    cnetmod::flat_map<int, int> values(source.begin(), source.end());

    ASSERT_EQ(values.size(), 3U);
    ASSERT_EQ(values.begin()->first, 1);
    ASSERT_EQ(values.find(4)->second, 40);
}

RUN_TESTS()
