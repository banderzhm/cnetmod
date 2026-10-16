#include "test_framework.hpp"

import std;
import cnetmod.security;

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

TEST(password_hash_round_trip_and_random_salt)
{
    const auto first = cnetmod::security::hash_password("correct horse battery");
    const auto second = cnetmod::security::hash_password("correct horse battery");
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(first->starts_with("pbkdf2-sha256$210000$"));
    ASSERT_NE(*first, *second);
    ASSERT_TRUE(cnetmod::security::verify_password("correct horse battery", *first));
    ASSERT_FALSE(cnetmod::security::verify_password("wrong password", *first));
    ASSERT_FALSE(cnetmod::security::password_hash_needs_rehash(*first));
}

TEST(password_hash_rejects_invalid_inputs_and_encodings)
{
    ASSERT_FALSE(cnetmod::security::hash_password("").has_value());
    ASSERT_FALSE(cnetmod::security::verify_password("x", ""));
    ASSERT_FALSE(cnetmod::security::verify_password("x",
        "pbkdf2-sha256$0$0011223344556677$00112233445566778899aabbccddeeff"));
    ASSERT_FALSE(cnetmod::security::verify_password("x",
        "pbkdf2-sha256$210000$0011223344556677$00112233445566778899aabbccddeeff$extra"));
}

TEST(password_hash_policy_detects_rehash_requirement)
{
    const auto encoded = cnetmod::security::hash_password("policy-test");
    ASSERT_TRUE(encoded.has_value());
    ASSERT_TRUE(cnetmod::security::password_hash_needs_rehash(*encoded,
        {.iterations = 310000}));
}

RUN_TESTS()
