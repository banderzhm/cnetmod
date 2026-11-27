module;

#include <openssl/rand.h>

#include "../detail/simdjson_depth.hpp"

/**
 * @file jwt.cpp
 * @brief Backend-neutral JWT HS256 implementation.
 */
module cnetmod.security.jwt;

import std;
import cnetmod.coro.bridge;
import cnetmod.utils.hmac_sha256;

namespace cnetmod::security {

auto generate_secure_token(std::size_t bytes) -> std::string
{
    if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("secure token length exceeds RAND_bytes limit");

    std::vector<unsigned char> random(bytes);
    if (bytes != 0 && RAND_bytes(random.data(), static_cast<int>(bytes)) != 1)
        throw std::runtime_error("secure token generation failed");

    constexpr std::string_view hex = "0123456789abcdef";
    std::string token;
    token.reserve(bytes * 2);
    for (const auto byte : random)
    {
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 0x0f]);
    }
    return token;
}

namespace {

constexpr std::string_view base64url_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/**
 * @brief Encodes bytes with the unpadded base64url alphabet required by JWT.
 */
auto encode_base64url(std::span<const std::byte> input) -> std::string
{
    std::string result;
    result.reserve((input.size() * 4 + 2) / 3);
    for (std::size_t offset = 0; offset < input.size(); offset += 3)
    {
        const auto first = std::to_integer<std::uint32_t>(input[offset]);
        const auto second = offset + 1 < input.size()
            ? std::to_integer<std::uint32_t>(input[offset + 1])
            : 0U;
        const auto third = offset + 2 < input.size()
            ? std::to_integer<std::uint32_t>(input[offset + 2])
            : 0U;
        const auto value = (first << 16U) | (second << 8U) | third;
        result.push_back(base64url_alphabet[(value >> 18U) & 0x3fU]);
        result.push_back(base64url_alphabet[(value >> 12U) & 0x3fU]);
        if (offset + 1 < input.size())
            result.push_back(base64url_alphabet[(value >> 6U) & 0x3fU]);
        if (offset + 2 < input.size())
            result.push_back(base64url_alphabet[value & 0x3fU]);
    }
    return result;
}

auto encode_base64url(std::string_view input) -> std::string
{
    return encode_base64url(std::as_bytes(std::span{input.data(), input.size()}));
}

auto decode_digit(char value) noexcept -> std::optional<std::uint8_t>
{
    if (value >= 'A' && value <= 'Z')
        return static_cast<std::uint8_t>(value - 'A');
    if (value >= 'a' && value <= 'z')
        return static_cast<std::uint8_t>(value - 'a' + 26);
    if (value >= '0' && value <= '9')
        return static_cast<std::uint8_t>(value - '0' + 52);
    if (value == '-')
        return 62;
    if (value == '_')
        return 63;
    return std::nullopt;
}

/**
 * @brief Decodes an unpadded base64url string and rejects non-canonical input.
 */
auto decode_base64url(std::string_view input)
    -> std::expected<std::string, std::string>
{
    if (input.size() % 4 == 1)
        return std::unexpected("invalid JWT base64url length");
    std::string result;
    result.reserve(input.size() * 3 / 4);
    std::uint32_t accumulator{};
    unsigned bits{};
    for (const char value : input)
    {
        const auto digit = decode_digit(value);
        if (!digit)
            return std::unexpected("invalid JWT base64url character");
        accumulator = (accumulator << 6U) | *digit;
        bits += 6U;
        if (bits >= 8U)
        {
            bits -= 8U;
            result.push_back(static_cast<char>((accumulator >> bits) & 0xffU));
        }
    }
    if (bits != 0U && (accumulator & ((1U << bits) - 1U)) != 0U)
        return std::unexpected("non-canonical JWT base64url value");
    return result;
}

auto secure_equal(std::string_view left, std::string_view right) noexcept -> bool
{
    if (left.size() != right.size())
        return false;
    unsigned difference{};
    for (std::size_t index = 0; index < left.size(); ++index)
        difference |= static_cast<unsigned>(
            static_cast<unsigned char>(left[index]) ^
            static_cast<unsigned char>(right[index]));
    return difference == 0U;
}

auto seconds_since_epoch(std::chrono::system_clock::time_point value)
    -> std::int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        value.time_since_epoch()).count();
}

auto make_signature(std::string_view signing_input, std::string_view secret)
    -> std::string
{
    const auto digest = cnetmod::utils::hmac_sha256(secret, signing_input);
    return encode_base64url(
        std::span<const std::byte>{digest.data(), digest.size()});
}

auto sign_jwt_sync(const jwt_sign_options& options, std::string_view secret)
    -> std::expected<std::string, std::string>
{
    try
    {
        simdjson::builder::string_builder header;
        header.start_object();
        header.append_key_value("alg", "HS256");
        header.append_comma();
        header.append_key_value("typ", "JWT");
        header.end_object();

        simdjson::builder::string_builder payload;
        payload.start_object();
        bool first = true;
        const auto append = [&](std::string_view key, const auto& value)
        {
            if (!std::exchange(first, false))
                payload.append_comma();
            payload.append_key_value(key, value);
        };
        const auto append_default = [&](const char* key, const auto& value)
        {
            if (!options.custom_claims.contains(key))
                append(key, value);
        };

        const auto now = std::chrono::system_clock::now();
        append_default("iss", options.issuer);
        append_default("sub", options.subject);
        append_default("iat", seconds_since_epoch(now));
        append_default("exp", seconds_since_epoch(now + options.lifetime));
        if (!options.scopes.empty())
        {
            std::string scopes;
            for (const auto& scope : options.scopes)
            {
                if (!scopes.empty())
                    scopes.push_back(' ');
                scopes += scope;
            }
            append_default("scope", scopes);
        }
        for (const auto& [key, value] : options.custom_claims)
            append(key, value);
        payload.end_object();

        std::string_view encoded_header;
        std::string_view encoded_payload;
        if (header.view().get(encoded_header) || !payload.validate_unicode() ||
            payload.view().get(encoded_payload))
            return std::unexpected("jwt JSON serialization failed");
        auto signing_input = encode_base64url(encoded_header) + "." +
            encode_base64url(encoded_payload);
        return signing_input + "." + make_signature(signing_input, secret);
    }
    catch (const std::exception& error)
    {
        return std::unexpected(std::string{"jwt sign failed: "} + error.what());
    }
}

auto parse_claim_object(simdjson::dom::parser& parser, std::string_view text,
    simdjson::dom::object& object) noexcept -> bool
{
    simdjson::dom::element document;
    return cnetmod::detail::parse_bounded_json(parser, text, document) &&
        !document.get_object().get(object);
}

auto numeric_date(simdjson::dom::element value)
    -> std::optional<std::chrono::system_clock::time_point>
{
    using clock = std::chrono::system_clock;
    using seconds = std::chrono::seconds;
    constexpr auto earliest = std::chrono::duration_cast<seconds>(
        clock::duration::min()).count();
    constexpr auto latest = std::chrono::duration_cast<seconds>(
        clock::duration::max()).count();
    std::int64_t whole = 0;
    if (value.get_int64().get(whole))
    {
        double fraction = 0.0;
        if (!value.is_double() || value.get_double().get(fraction) ||
            fraction < static_cast<double>(earliest) ||
            fraction > static_cast<double>(latest))
            return std::nullopt;
        whole = static_cast<std::int64_t>(fraction);
    }
    if (whole < earliest || whole > latest)
        return std::nullopt;
    return clock::time_point{seconds{whole}};
}

auto verify_jwt_sync(std::string_view token, std::string_view secret)
    -> std::expected<jwt_claims, std::string>
{
    try
    {
        const auto first = token.find('.');
        const auto second = first == std::string_view::npos
            ? std::string_view::npos
            : token.find('.', first + 1);
        if (first == std::string_view::npos || second == std::string_view::npos ||
            token.find('.', second + 1) != std::string_view::npos)
            return std::unexpected("invalid JWT compact serialization");

        const auto signing_input = token.substr(0, second);
        if (!secure_equal(token.substr(second + 1),
                make_signature(signing_input, secret)))
            return std::unexpected("JWT signature verification failed");

        const auto header_text = decode_base64url(token.substr(0, first));
        const auto payload_text = decode_base64url(
            token.substr(first + 1, second - first - 1));
        if (!header_text || !payload_text)
            return std::unexpected(!header_text ? header_text.error()
                                                : payload_text.error());
        simdjson::dom::parser header_parser;
        simdjson::dom::parser payload_parser;
        simdjson::dom::object header;
        simdjson::dom::object payload;
        if (!parse_claim_object(header_parser, *header_text, header) ||
            !parse_claim_object(payload_parser, *payload_text, payload))
            return std::unexpected("invalid JWT JSON document");

        std::string_view algorithm;
        std::string_view type = "JWT";
        for (const auto field : header)
        {
            if (field.key != "alg" && field.key != "typ")
                continue;
            std::string_view value;
            const auto error = field.value.get_string().get(value);
            if (field.key == "alg")
                algorithm = error ? std::string_view{} : value;
            else
                type = error ? std::string_view{"JWT"} : value;
        }
        if (algorithm != "HS256" || type != "JWT")
            return std::unexpected("unsupported JWT header");

        jwt_claims claims{};
        std::optional<std::chrono::system_clock::time_point> issued_at;
        std::optional<std::chrono::system_clock::time_point> expires_at;
        std::optional<simdjson::dom::element> scope;
        constexpr std::array standard_claims{"aud", "nbf", "jti"};
        for (const auto field : payload)
        {
            if (field.key == "iat")
                issued_at = numeric_date(field.value);
            else if (field.key == "exp")
                expires_at = numeric_date(field.value);
            else if (field.key == "scope")
                scope = field.value;
            else if (field.key == "iss" || field.key == "sub")
            {
                auto& target = field.key == "iss" ? claims.issuer : claims.subject;
                std::string_view value;
                if (field.value.get_string().get(value))
                    target.clear();
                else
                    target = value;
            }
            else if (std::ranges::find(standard_claims, field.key) == standard_claims.end())
            {
                std::string_view value;
                if (!field.value.get_string().get(value))
                    claims.custom.insert_or_assign(std::string{field.key}, std::string{value});
                else
                    claims.custom.erase(std::string{field.key});
            }
        }

        if (!issued_at || !expires_at)
            return std::unexpected("JWT is missing numeric time claims");
        claims.issued_at = *issued_at;
        claims.expires_at = *expires_at;
        if (std::chrono::system_clock::now() > claims.expires_at)
            return std::unexpected("JWT has expired");

        if (scope)
        {
            std::string_view remaining;
            if (scope->get_string().get(remaining))
                return std::unexpected("JWT scope claim must be a string");
            while (!remaining.empty())
            {
                const auto separator = remaining.find(' ');
                const auto item = remaining.substr(0, separator);
                if (!item.empty())
                    claims.scopes.emplace_back(item);
                if (separator == std::string_view::npos)
                    break;
                remaining.remove_prefix(separator + 1);
            }
        }
        return claims;
    }
    catch (const std::exception& error)
    {
        return std::unexpected(std::string{"jwt verify failed: "} + error.what());
    }
}

} // namespace

auto sign_jwt(thread_pool& pool, io_context& io,
    const jwt_sign_options& options, std::string_view secret)
    -> task<std::expected<std::string, std::string>>
{
    auto owned_secret = std::string{secret};
    co_return co_await blocking_invoke(pool, io,
        [options, secret = std::move(owned_secret)] {
            return sign_jwt_sync(options, secret);
        });
}

auto verify_jwt(thread_pool& pool, io_context& io,
    std::string_view token, std::string_view secret)
    -> task<std::expected<jwt_claims, std::string>>
{
    auto owned_token = std::string{token};
    auto owned_secret = std::string{secret};
    co_return co_await blocking_invoke(pool, io,
        [token = std::move(owned_token), secret = std::move(owned_secret)] {
            return verify_jwt_sync(token, secret);
        });
}

} // namespace cnetmod::security
