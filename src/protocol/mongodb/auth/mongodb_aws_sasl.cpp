module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_SSL
    #include <openssl/crypto.h>
    #include <openssl/evp.h>
    #include <openssl/hmac.h>
    #include <openssl/rand.h>
#endif
module cnetmod.protocol.mongodb;
import std;
import :error;
import :bson_document;
import :aws_credentials;
import :aws_sasl;

namespace cnetmod::mongodb {
namespace {
#ifdef CNETMOD_HAS_SSL
    auto hex(std::span<const unsigned char> bytes) -> std::string
    {
        constexpr std::string_view digits{"0123456789abcdef"};
        std::string result;
        result.reserve(bytes.size() * 2);
        for (const auto byte : bytes)
        {
            result += digits[byte >> 4];
            result += digits[byte & 15];
        }
        return result;
    }

    auto digest(std::string_view text) -> std::array<unsigned char, 32>
    {
        std::array<unsigned char, 32> output{};
        unsigned size{};
        if (EVP_Digest(text.data(), text.size(), output.data(), &size, EVP_sha256(), nullptr) != 1 || size != output.size())
            throw std::runtime_error("AWS SHA-256 operation failed");
        return output;
    }

    auto mac(std::span<const unsigned char> key, std::string_view text) -> std::array<unsigned char, 32>
    {
        std::array<unsigned char, 32> output{};
        unsigned size{};
        if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                reinterpret_cast<const unsigned char*>(text.data()), text.size(), output.data(), &size) ||
            size != output.size())
            throw std::runtime_error("AWS HMAC operation failed");
        return output;
    }
#endif
} // namespace

auto make_aws_client_nonce() -> result<std::array<std::byte, 32>>
{
#ifdef CNETMOD_HAS_SSL
    std::array<std::byte, 32> nonce{};
    if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), nonce.size()) == 1)
        return nonce;
#endif
    return std::unexpected(make_error(error_code::authentication_failed, "AWS nonce generation requires working cryptography"));
}

auto aws_first_message(std::span<const std::byte, 32> nonce) -> result<std::vector<std::byte>>
{
    return encode_bson_document(bson_document{{"r", bson_binary{0, {nonce.begin(), nonce.end()}}}, {"p", std::int32_t{110}}});
}

auto aws_sts_region(std::string_view host) -> result<std::string>
{
    if (host.empty() || host.size() > 255 || host.front() == '.' || host.back() == '.' ||
        host.find("..") != std::string_view::npos || !std::ranges::all_of(host, [](unsigned char c)
                                                         {
                                                             return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                                                 (c >= '0' && c <= '9') || c == '-' || c == '.';
                                                         }))
        return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS STS hostname"));
    constexpr std::string_view global_host{"sts.amazonaws.com"};
    const auto dot = host.find('.');
    if (host == global_host || dot == std::string_view::npos)
        return std::string{"us-east-1"};
    const auto end = host.find('.', dot + 1);
    return std::string{host.substr(dot + 1, end == std::string_view::npos ? end : end - dot - 1)};
}

auto sign_aws_sts_request(std::string_view host, std::span<const std::byte, 64> nonce,
    const aws_credentials& credentials, std::string_view timestamp) -> result<aws_signed_request>
{
    auto valid = validate_aws_credentials(credentials);
    if (!valid)
        return std::unexpected(valid.error());
    auto region = aws_sts_region(host);
    if (!region)
        return std::unexpected(region.error());
    if (timestamp.size() != 16 || timestamp[8] != 'T' || timestamp[15] != 'Z')
        return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS signing timestamp"));
    for (std::size_t i{}; i < timestamp.size(); ++i)
        if (i != 8 && i != 15 && (timestamp[i] < '0' || timestamp[i] > '9'))
            return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS signing timestamp"));
#ifdef CNETMOD_HAS_SSL
    try
    {
        std::string base64(4 * ((nonce.size() + 2) / 3), '\0');
        const auto encoded_size = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(base64.data()),
            reinterpret_cast<const unsigned char*>(nonce.data()), nonce.size());
        base64.resize(encoded_size);
        std::map<std::string, std::string> headers{
            {"content-length", "43"}, {"content-type", "application/x-www-form-urlencoded"},
            {"host", std::string(host)}, {"x-amz-date", std::string(timestamp)},
            {"x-mongodb-gs2-cb-flag", "n"}, {"x-mongodb-server-nonce", base64}};
        if (!credentials.session_token.empty())
            headers.emplace("x-amz-security-token", credentials.session_token);
        std::string canonical_headers, signed_headers;
        for (const auto& [name, value] : headers)
        {
            canonical_headers += name + ":" + value + "\n";
            if (!signed_headers.empty())
                signed_headers += ';';
            signed_headers += name;
        }
        constexpr std::string_view body{"Action=GetCallerIdentity&Version=2011-06-15"};
        const auto request = "POST\n/\n\n" + canonical_headers + "\n" + signed_headers + "\n" + hex(digest(body));
        const auto date = std::string(timestamp.substr(0, 8));
        const auto scope = date + "/" + *region + "/sts/aws4_request";
        const auto to_sign = "AWS4-HMAC-SHA256\n" + std::string(timestamp) + "\n" + scope + "\n" + hex(digest(request));
        auto secret = "AWS4" + credentials.secret_access_key;
        auto key = mac({reinterpret_cast<const unsigned char*>(secret.data()), secret.size()}, date);
        OPENSSL_cleanse(secret.data(), secret.size());
        key = mac(key, *region);
        key = mac(key, "sts");
        key = mac(key, "aws4_request");
        const auto signature = hex(mac(key, to_sign));
        OPENSSL_cleanse(key.data(), key.size());
        return aws_signed_request{"AWS4-HMAC-SHA256 Credential=" + credentials.access_key_id + "/" + scope +
                ", SignedHeaders=" + signed_headers + ", Signature=" + signature,
            std::string(timestamp)};
    }
    catch (const std::exception&)
    {
        return std::unexpected(make_error(error_code::authentication_failed, "AWS signing operation failed"));
    }
#else
    return std::unexpected(make_error(error_code::authentication_failed, "AWS signing requires cryptography"));
#endif
}

auto aws_challenge_response(std::span<const std::byte> challenge,
    std::span<const std::byte, 32> client_nonce, const aws_credentials& credentials,
    std::string_view timestamp) -> result<std::vector<std::byte>>
{
    auto document = decode_bson_document(challenge);
    if (!document)
        return std::unexpected(document.error());
    const auto s = document->find("s");
    const auto h = document->find("h");
    const auto nonce = s ? s->get_if<bson_binary>() : nullptr;
    const auto host = h ? h->get_if<std::string>() : nullptr;
    if (!nonce || nonce->subtype != 0 || nonce->bytes.size() != 64 || !host ||
        !std::equal(client_nonce.begin(), client_nonce.end(), nonce->bytes.begin()))
        return std::unexpected(make_error(error_code::authentication_failed, "invalid AWS server nonce or challenge"));
    auto signed_request = sign_aws_sts_request(*host, std::span<const std::byte, 64>{nonce->bytes.data(), 64}, credentials, timestamp);
    if (!signed_request)
        return std::unexpected(signed_request.error());
    bson_document reply{{"a", signed_request->authorization}, {"d", signed_request->amz_date}};
    if (!credentials.session_token.empty())
        reply.append("t", credentials.session_token);
    return encode_bson_document(reply);
}
} // namespace cnetmod::mongodb
