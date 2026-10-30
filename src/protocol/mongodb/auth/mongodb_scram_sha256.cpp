module;
#include <cnetmod/config.hpp>
#include <unicode/usprep.h>
#include <unicode/ustring.h>
#ifdef CNETMOD_HAS_SSL
    #include <openssl/crypto.h>
    #include <openssl/evp.h>
    #include <openssl/hmac.h>
    #include <openssl/rand.h>
#endif
module cnetmod.protocol.mongodb;

import std;
import :error;
import :scram_sha256;

namespace cnetmod::mongodb {
namespace {
    auto sasl_escape(std::string_view input) -> std::string
    {
        std::string output;
        for (char character : input)
        {
            if (character == ',')
                output += "=2C";
            else if (character == '=')
                output += "=3D";
            else
                output += character;
        }
        return output;
    }

    auto parameter(std::string_view message, char key) -> std::optional<std::string>
    {
        std::size_t position{};
        std::array<bool, 256> seen{};
        std::optional<std::string> result;
        while (position <= message.size())
        {
            const auto separator = message.find(',', position);
            const auto field = message.substr(position,
                separator == std::string_view::npos ? message.size() - position : separator - position);
            if (field.size() < 3 || field[1] != '=' || field[0] == 'm')
                return {};
            const auto attribute = static_cast<unsigned char>(field[0]);
            if (seen[attribute])
                return {};
            seen[attribute] = true;
            if (field[0] == key)
                result = std::string(field.substr(2));
            if (separator == std::string_view::npos)
                break;
            position = separator + 1;
        }
        return result;
    }

#ifdef CNETMOD_HAS_SSL
    auto base64_encode(std::span<const std::byte> bytes) -> std::string
    {
        std::string output(4 * ((bytes.size() + 2) / 3), '\0');
        auto n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()),
            reinterpret_cast<const unsigned char*>(bytes.data()),
            static_cast<int>(bytes.size()));
        output.resize(static_cast<std::size_t>(n));
        return output;
    }

    auto base64_decode(std::string_view text) -> std::optional<std::vector<std::byte>>
    {
        if (text.empty() || text.size() % 4 != 0)
            return {};
        const auto padding = text.ends_with("==") ? 2U : (text.ends_with("=") ? 1U : 0U);
        const auto data = text.substr(0, text.size() - padding);
        if (!std::ranges::all_of(data, [](unsigned char value)
                {
                    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                        (value >= '0' && value <= '9') || value == '+' || value == '/';
                }))
            return {};
        std::vector<std::byte> output(text.size() / 4 * 3 + 3);
        auto n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(output.data()),
            reinterpret_cast<const unsigned char*>(text.data()),
            static_cast<int>(text.size()));
        if (n < 0)
            return {};
        while (!text.empty() && text.back() == '=')
        {
            --n;
            text.remove_suffix(1);
        }
        output.resize(static_cast<std::size_t>(n));
        return output;
    }
#endif
} // namespace

auto prepare_scram_sha256_password(std::string_view input) -> std::expected<std::string, std::string>
{
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::unexpected("SASLprep input is too large");
    UErrorCode status = U_ZERO_ERROR;
    std::int32_t utf16_length{};
    u_strFromUTF8(nullptr, 0, &utf16_length, input.data(),
        static_cast<std::int32_t>(input.size()), &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
        return std::unexpected("SASLprep input is not valid UTF-8");
    status = U_ZERO_ERROR;
    std::vector<UChar> utf16(static_cast<std::size_t>(utf16_length) + 1);
    u_strFromUTF8(utf16.data(), static_cast<std::int32_t>(utf16.size()),
        &utf16_length, input.data(), static_cast<std::int32_t>(input.size()), &status);
    if (U_FAILURE(status))
        return std::unexpected("SASLprep UTF-8 conversion failed");

    UStringPrepProfile* profile = usprep_openByType(USPREP_RFC4013_SASLPREP, &status);
    if (U_FAILURE(status) || !profile)
        return std::unexpected("ICU RFC4013 SASLprep profile is unavailable");

    struct profile_guard
    {
        UStringPrepProfile* value;

        ~profile_guard()
        {
            usprep_close(value);
        }
    } guard{profile};

    UParseError parse_error{};
    status = U_ZERO_ERROR;
    auto prepared_length = usprep_prepare(profile, utf16.data(), utf16_length,
        nullptr, 0, USPREP_DEFAULT, &parse_error, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
        return std::unexpected(std::format("SASLprep rejected the credential at UTF-16 offset {}", parse_error.offset));
    status = U_ZERO_ERROR;
    std::vector<UChar> normalized(static_cast<std::size_t>(prepared_length) + 1);
    prepared_length = usprep_prepare(profile, utf16.data(), utf16_length,
        normalized.data(), static_cast<std::int32_t>(normalized.size()),
        USPREP_DEFAULT, &parse_error, &status);
    if (U_FAILURE(status))
        return std::unexpected(std::format("SASLprep rejected the credential at UTF-16 offset {}", parse_error.offset));

    std::int32_t utf8_length{};
    status = U_ZERO_ERROR;
    u_strToUTF8(nullptr, 0, &utf8_length, normalized.data(), prepared_length, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
        return std::unexpected("SASLprep UTF-8 sizing failed");
    status = U_ZERO_ERROR;
    std::string prepared(static_cast<std::size_t>(utf8_length), '\0');
    u_strToUTF8(prepared.data(), utf8_length, &utf8_length,
        normalized.data(), prepared_length, &status);
    if (U_FAILURE(status))
        return std::unexpected("SASLprep UTF-8 encoding failed");
    return prepared;
}

struct scram_sha256_client::implementation
{
    std::string username;
    std::string password;
    std::string nonce;
    std::string first_bare;
    std::vector<std::byte> expected_server_signature;
    bool started = false;
    bool challenge_processed = false;
};

scram_sha256_client::scram_sha256_client(std::string username, std::string password)
    : implementation_(std::make_unique<implementation>(implementation{
          .username = std::move(username),
          .password = std::move(password)})) {}

scram_sha256_client::~scram_sha256_client()
{
    if (!implementation_)
        return;
#ifdef CNETMOD_HAS_SSL
    OPENSSL_cleanse(implementation_->password.data(),
        implementation_->password.size());
    if (!implementation_->expected_server_signature.empty())
        OPENSSL_cleanse(implementation_->expected_server_signature.data(),
            implementation_->expected_server_signature.size());
#else
    std::fill(implementation_->password.begin(),
        implementation_->password.end(), '\0');
#endif
}

scram_sha256_client::scram_sha256_client(scram_sha256_client&&) noexcept = default;
auto scram_sha256_client::operator=(scram_sha256_client&&) noexcept
    -> scram_sha256_client& = default;

auto scram_sha256_client::initial_message() -> result<std::vector<std::byte>>
{
    if (implementation_->started)
        return std::unexpected(make_error(error_code::authentication_failed,
            "SCRAM exchange was already started"));
#ifdef CNETMOD_HAS_SSL
    auto prepared = prepare_scram_sha256_password(implementation_->password);
    if (!prepared)
        return std::unexpected(make_error(error_code::authentication_failed, prepared.error()));
    OPENSSL_cleanse(implementation_->password.data(), implementation_->password.size());
    implementation_->password = std::move(*prepared);
    std::array<std::byte, 24> random{};
    if (RAND_bytes(reinterpret_cast<unsigned char*>(random.data()),
            static_cast<int>(random.size())) != 1)
        return std::unexpected(make_error(error_code::authentication_failed,
            "secure SCRAM nonce generation failed"));
    implementation_->nonce = base64_encode(random);
    implementation_->first_bare = "n=" + sasl_escape(implementation_->username) +
        ",r=" + implementation_->nonce;
    auto message = "n,," + implementation_->first_bare;
    implementation_->started = true;
    auto bytes = std::as_bytes(std::span(message));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
#else
    return std::unexpected(make_error(error_code::authentication_failed,
        "SCRAM-SHA-256 requires the OpenSSL crypto dependency"));
#endif
}

auto scram_sha256_client::respond(std::span<const std::byte> server_bytes)
    -> result<std::vector<std::byte>>
{
    if (!implementation_->started || implementation_->challenge_processed)
        return std::unexpected(make_error(error_code::authentication_failed,
            "unexpected SCRAM server-first message"));
#ifdef CNETMOD_HAS_SSL
    std::string server(reinterpret_cast<const char*>(server_bytes.data()), server_bytes.size());
    auto nonce = parameter(server, 'r');
    auto salt_text = parameter(server, 's');
    auto iteration_text = parameter(server, 'i');
    if (!nonce || !nonce->starts_with(implementation_->nonce) ||
        nonce->size() <= implementation_->nonce.size() || !salt_text || !iteration_text)
        return std::unexpected(make_error(error_code::authentication_failed,
            "invalid SCRAM server-first attributes"));
    std::uint32_t iterations{};
    auto parsed = std::from_chars(iteration_text->data(),
        iteration_text->data() + iteration_text->size(), iterations);
    if (parsed.ec != std::errc{} || parsed.ptr != iteration_text->data() + iteration_text->size() ||
        iterations < 4096 || iterations > 10'000'000)
        return std::unexpected(make_error(error_code::authentication_failed,
            "unsafe SCRAM iteration count"));
    auto salt = base64_decode(*salt_text);
    if (!salt || salt->empty())
        return std::unexpected(make_error(error_code::authentication_failed,
            "invalid SCRAM salt"));
    std::array<std::byte, 32> salted_password{};
    if (PKCS5_PBKDF2_HMAC(implementation_->password.data(),
            static_cast<int>(implementation_->password.size()),
            reinterpret_cast<const unsigned char*>(salt->data()),
            static_cast<int>(salt->size()), static_cast<int>(iterations), EVP_sha256(),
            static_cast<int>(salted_password.size()),
            reinterpret_cast<unsigned char*>(salted_password.data())) != 1)
        return std::unexpected(make_error(error_code::authentication_failed,
            "SCRAM key derivation failed"));
    auto hmac = [](std::span<const std::byte> key, std::string_view data)
    {
        std::vector<std::byte> output(32);
        unsigned int length{};
        HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
            reinterpret_cast<const unsigned char*>(data.data()), data.size(),
            reinterpret_cast<unsigned char*>(output.data()), &length);
        output.resize(length);
        return output;
    };
    auto digest = [](std::span<const std::byte> bytes)
    {
        std::vector<std::byte> output(32);
        unsigned int length{};
        EVP_Digest(bytes.data(), bytes.size(), reinterpret_cast<unsigned char*>(output.data()),
            &length, EVP_sha256(), nullptr);
        output.resize(length);
        return output;
    };
    auto client_key = hmac(salted_password, "Client Key");
    auto stored_key = digest(client_key);
    auto final_without_proof = "c=biws,r=" + *nonce;
    auto auth_message = implementation_->first_bare + "," + server + "," + final_without_proof;
    auto client_signature = hmac(stored_key, auth_message);
    if (client_signature.size() != client_key.size())
        return std::unexpected(make_error(error_code::authentication_failed,
            "SCRAM signature length mismatch"));
    for (std::size_t i{}; i < client_key.size(); ++i)
        client_key[i] ^= client_signature[i];
    auto server_key = hmac(salted_password, "Server Key");
    implementation_->expected_server_signature = hmac(server_key, auth_message);
    implementation_->challenge_processed = true;
    auto final = final_without_proof + ",p=" + base64_encode(client_key);
    auto bytes = std::as_bytes(std::span(final));
    return std::vector<std::byte>(bytes.begin(), bytes.end());
#else
    (void)server_bytes;
    return std::unexpected(make_error(error_code::authentication_failed,
        "SCRAM-SHA-256 requires the OpenSSL crypto dependency"));
#endif
}

auto scram_sha256_client::verify(std::span<const std::byte> server_bytes)
    -> result<void>
{
    if (!implementation_->challenge_processed)
        return std::unexpected(make_error(error_code::authentication_failed,
            "unexpected SCRAM server-final message"));
#ifdef CNETMOD_HAS_SSL
    std::string server(reinterpret_cast<const char*>(server_bytes.data()), server_bytes.size());
    if (auto server_error = parameter(server, 'e'))
        return std::unexpected(make_error(error_code::authentication_failed,
            "MongoDB SCRAM authentication rejected: " + *server_error));
    auto signature_text = parameter(server, 'v');
    auto signature = signature_text ? base64_decode(*signature_text) : std::nullopt;
    if (!signature || signature->size() != implementation_->expected_server_signature.size() ||
        CRYPTO_memcmp(signature->data(), implementation_->expected_server_signature.data(),
            signature->size()) != 0)
        return std::unexpected(make_error(error_code::authentication_failed,
            "MongoDB SCRAM server signature verification failed"));
    return {};
#else
    (void)server_bytes;
    return std::unexpected(make_error(error_code::authentication_failed,
        "SCRAM-SHA-256 requires the OpenSSL crypto dependency"));
#endif
}

} // namespace cnetmod::mongodb
