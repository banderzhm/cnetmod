module;

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

module cnetmod.security.password;

import std;
import cnetmod.coro.bridge;

namespace cnetmod::security {
namespace {

    constexpr std::string_view prefix = "pbkdf2-sha256$";
    constexpr std::size_t maximum_password_bytes = 1024;
    constexpr std::uint32_t minimum_iterations = 100000;
    constexpr std::uint32_t maximum_iterations = 10000000;
    constexpr std::size_t minimum_salt_bytes = 8;
    constexpr std::size_t maximum_salt_bytes = 64;
    constexpr std::size_t minimum_digest_bytes = 16;
    constexpr std::size_t maximum_digest_bytes = 64;

    struct parsed_hash
    {
        std::uint32_t iterations{};
        std::vector<unsigned char> salt;
        std::vector<unsigned char> digest;
    };

    class sensitive_bytes
    {
    public:
        explicit sensitive_bytes(std::size_t size) : value_(size) {}

        ~sensitive_bytes()
        {
            if (!value_.empty())
                OPENSSL_cleanse(value_.data(), value_.size());
        }

        [[nodiscard]] auto value() noexcept -> std::vector<unsigned char>&
        {
            return value_;
        }

    private:
        std::vector<unsigned char> value_;
    };

    [[nodiscard]] auto valid_options(const password_hash_options& options) noexcept
        -> bool
    {
        return options.algorithm == password_hash_algorithm::pbkdf2_sha256 &&
            options.iterations >= minimum_iterations &&
            options.iterations <= maximum_iterations &&
            options.salt_bytes >= minimum_salt_bytes &&
            options.salt_bytes <= maximum_salt_bytes &&
            options.digest_bytes >= minimum_digest_bytes &&
            options.digest_bytes <= maximum_digest_bytes;
    }

    [[nodiscard]] auto hex_encode(std::span<const unsigned char> bytes)
        -> std::string
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);
        for (const auto byte : bytes)
        {
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0f]);
        }
        return result;
    }

    [[nodiscard]] auto hex_digit(char value) noexcept -> int
    {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    }

    [[nodiscard]] auto hex_decode(std::string_view encoded,
        std::size_t minimum_bytes, std::size_t maximum_bytes)
        -> std::optional<std::vector<unsigned char>>
    {
        if ((encoded.size() & 1U) != 0U ||
            encoded.size() < minimum_bytes * 2 ||
            encoded.size() > maximum_bytes * 2)
            return std::nullopt;
        std::vector<unsigned char> result(encoded.size() / 2);
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            const auto high = hex_digit(encoded[index * 2]);
            const auto low = hex_digit(encoded[index * 2 + 1]);
            if (high < 0 || low < 0)
                return std::nullopt;
            result[index] = static_cast<unsigned char>((high << 4) | low);
        }
        return result;
    }

    [[nodiscard]] auto parse_hash(std::string_view encoded)
        -> std::optional<parsed_hash>
    {
        if (!encoded.starts_with(prefix))
            return std::nullopt;
        encoded.remove_prefix(prefix.size());
        const auto first = encoded.find('$');
        if (first == std::string_view::npos)
            return std::nullopt;
        std::uint32_t iterations{};
        const auto parsed = std::from_chars(encoded.data(),
            encoded.data() + first, iterations);
        if (parsed.ec != std::errc{} || parsed.ptr != encoded.data() + first ||
            iterations < minimum_iterations || iterations > maximum_iterations)
            return std::nullopt;
        encoded.remove_prefix(first + 1);
        const auto second = encoded.find('$');
        if (second == std::string_view::npos ||
            encoded.find('$', second + 1) != std::string_view::npos)
            return std::nullopt;
        auto salt = hex_decode(encoded.substr(0, second), minimum_salt_bytes,
            maximum_salt_bytes);
        auto digest = hex_decode(encoded.substr(second + 1), minimum_digest_bytes,
            maximum_digest_bytes);
        if (!salt || !digest)
            return std::nullopt;
        return parsed_hash{iterations, std::move(*salt), std::move(*digest)};
    }

    [[nodiscard]] auto derive(std::string_view password,
        std::span<const unsigned char> salt, std::uint32_t iterations,
        std::span<unsigned char> output) noexcept -> bool
    {
        return password.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
            salt.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
            output.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
            iterations <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
            PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                salt.data(), static_cast<int>(salt.size()),
                static_cast<int>(iterations), EVP_sha256(),
                static_cast<int>(output.size()), output.data()) == 1;
    }

} // namespace

auto hash_password(std::string_view password, password_hash_options options)
    -> std::expected<std::string, std::error_code>
{
    if (password.empty() || password.size() > maximum_password_bytes ||
        !valid_options(options))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    std::vector<unsigned char> salt(options.salt_bytes);
    sensitive_bytes digest(options.digest_bytes);
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
        !derive(password, salt, options.iterations, digest.value()))
        return std::unexpected(std::make_error_code(std::errc::io_error));

    return std::format("{}{}${}${}", prefix, options.iterations,
        hex_encode(salt), hex_encode(digest.value()));
}

auto verify_password(std::string_view password,
    std::string_view encoded) noexcept -> bool
{
    if (password.empty() || password.size() > maximum_password_bytes)
        return false;
    try
    {
        auto parsed = parse_hash(encoded);
        if (!parsed)
            return false;
        sensitive_bytes actual(parsed->digest.size());
        if (!derive(password, parsed->salt, parsed->iterations,
                actual.value()))
            return false;
        return CRYPTO_memcmp(actual.value().data(), parsed->digest.data(),
                   parsed->digest.size()) == 0;
    }
    catch (...)
    {
        return false;
    }
}

auto password_hash_needs_rehash(std::string_view encoded,
    password_hash_options options) noexcept -> bool
{
    if (!valid_options(options))
        return true;
    try
    {
        const auto parsed = parse_hash(encoded);
        return !parsed || parsed->iterations != options.iterations ||
            parsed->salt.size() != options.salt_bytes ||
            parsed->digest.size() != options.digest_bytes;
    }
    catch (...)
    {
        return true;
    }
}

auto hash_password(thread_pool& pool, io_context& io,
    std::string password, password_hash_options options)
    -> task<std::expected<std::string, std::error_code>>
{
    co_return co_await blocking_invoke(pool, io,
        [password = std::move(password), options]
        {
            return hash_password(password, options);
        });
}

auto verify_password(thread_pool& pool, io_context& io,
    std::string password, std::string encoded) -> task<bool>
{
    co_return co_await blocking_invoke(pool, io,
        [password = std::move(password), encoded = std::move(encoded)]
        {
            return verify_password(password, encoded);
        });
}

} // namespace cnetmod::security
