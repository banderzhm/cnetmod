/**
 * @file jwt.cpp
 * @brief Backend-neutral JWT HS256 implementation.
 */
module cnetmod.security.jwt;

import std;
import cnetmod.coro.bridge;
import cnetmod.json;
import cnetmod.utils.hmac_sha256;

namespace cnetmod::security {
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
        json::document header = json::document::object();
        header["alg"] = "HS256";
        header["typ"] = "JWT";

        const auto now = std::chrono::system_clock::now();
        json::document payload = json::document::object();
        payload["iss"] = options.issuer;
        payload["sub"] = options.subject;
        payload["iat"] = seconds_since_epoch(now);
        payload["exp"] = seconds_since_epoch(now + options.lifetime);
        if (!options.scopes.empty())
        {
            std::string scopes;
            for (const auto& scope : options.scopes)
            {
                if (!scopes.empty())
                    scopes.push_back(' ');
                scopes += scope;
            }
            payload["scope"] = std::move(scopes);
        }
        for (const auto& [key, value] : options.custom_claims)
            payload[key] = value;

        const auto encoded_header = json::write_document(header);
        const auto encoded_payload = json::write_document(payload);
        if (!encoded_header || !encoded_payload)
            return std::unexpected("jwt JSON serialization failed");
        auto signing_input = encode_base64url(*encoded_header) + "." +
            encode_base64url(*encoded_payload);
        return signing_input + "." + make_signature(signing_input, secret);
    }
    catch (const std::exception& error)
    {
        return std::unexpected(std::string{"jwt sign failed: "} + error.what());
    }
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
        const auto header = json::parse_document(*header_text);
        const auto payload = json::parse_document(*payload_text);
        if (!header || !header->is_object() || !payload || !payload->is_object())
            return std::unexpected("invalid JWT JSON document");
        if (header->value("alg", std::string{}) != "HS256" ||
            header->value("typ", std::string{"JWT"}) != "JWT")
            return std::unexpected("unsupported JWT header");

        const auto issued_at = payload->find("iat");
        const auto expires_at = payload->find("exp");
        if (issued_at == payload->end() || expires_at == payload->end() ||
            !issued_at->is_number_integer() || !expires_at->is_number_integer())
            return std::unexpected("JWT is missing numeric time claims");

        jwt_claims claims{};
        claims.issuer = payload->value("iss", std::string{});
        claims.subject = payload->value("sub", std::string{});
        claims.issued_at = std::chrono::system_clock::time_point{
            std::chrono::seconds{issued_at->get<std::int64_t>()}};
        claims.expires_at = std::chrono::system_clock::time_point{
            std::chrono::seconds{expires_at->get<std::int64_t>()}};
        if (std::chrono::system_clock::now() > claims.expires_at)
            return std::unexpected("JWT has expired");

        const auto scope = payload->find("scope");
        if (scope != payload->end())
        {
            if (!scope->is_string())
                return std::unexpected("JWT scope claim must be a string");
            std::string_view remaining = scope->get_ref<const std::string&>();
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

        constexpr std::array standard_claims{
            "iss", "sub", "aud", "exp", "nbf", "iat", "jti", "scope"};
        for (const auto& [key, value] :
            payload->get_ref<const json::document::object_type&>())
        {
            const auto standard = std::ranges::find(standard_claims, key) !=
                standard_claims.end();
            if (!standard && value.is_string())
                claims.custom.emplace(key, value.get<std::string>());
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
