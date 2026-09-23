#include "test_framework.hpp"

import std;
import cnetmod.security.jwt;

TEST(secure_token_is_hex_encoded)
{
    const auto token = cnetmod::security::generate_secure_token(32);
    ASSERT_EQ(token.size(), 64U);
    ASSERT_TRUE(std::ranges::all_of(token, [](char value)
        {
            return (value >= '0' && value <= '9') ||
                (value >= 'a' && value <= 'f');
        }));
}

TEST(secure_token_supports_zero_bytes)
{
    ASSERT_TRUE(cnetmod::security::generate_secure_token(0).empty());
}

TEST(secure_token_uses_fresh_randomness)
{
    ASSERT_TRUE(cnetmod::security::generate_secure_token() !=
        cnetmod::security::generate_secure_token());
}

RUN_TESTS()
