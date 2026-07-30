module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_ICU
    #include <unicode/usprep.h>
    #include <unicode/ustring.h>
#endif
#ifdef CNETMOD_HAS_SSL
    #include <openssl/crypto.h>
    #include <openssl/evp.h>
    #include <openssl/hmac.h>
    #include <openssl/rand.h>
#endif

module cnetmod.protocol.postgresql;

import std;
import :wire_protocol;

namespace cnetmod::postgresql::detail {
namespace {

    void put_i16(std::vector<std::uint8_t>& out, std::uint16_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    void put_i32(std::vector<std::uint8_t>& out, std::uint32_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value >> 24));
        out.push_back(static_cast<std::uint8_t>(value >> 16));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    auto read_i32(std::span<const std::uint8_t> in) -> std::uint32_t
    {
        return (std::uint32_t(in[0]) << 24) | (std::uint32_t(in[1]) << 16) |
            (std::uint32_t(in[2]) << 8) | std::uint32_t(in[3]);
    }

    void put_cstring(std::vector<std::uint8_t>& out, std::string_view value)
    {
        out.insert(out.end(), value.begin(), value.end());
        out.push_back(0);
    }

    void finish_message(std::vector<std::uint8_t>& out)
    {
        const auto length = static_cast<std::uint32_t>(out.size() - 1);
        out[1] = static_cast<std::uint8_t>(length >> 24);
        out[2] = static_cast<std::uint8_t>(length >> 16);
        out[3] = static_cast<std::uint8_t>(length >> 8);
        out[4] = static_cast<std::uint8_t>(length);
    }

    auto b64_encode(std::span<const std::uint8_t> input) -> std::string
    {
#ifdef CNETMOD_HAS_SSL
        std::string out(4 * ((input.size() + 2) / 3), '\0');
        const auto n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
            input.data(), static_cast<int>(input.size()));
        out.resize(static_cast<std::size_t>(n));
        return out;
#else
        (void)input;
        return {};
#endif
    }

    auto b64_decode(std::string_view input)
        -> std::expected<std::vector<std::uint8_t>, std::string>
    {
#ifdef CNETMOD_HAS_SSL
        if (input.empty() || input.size() % 4 != 0)
            return std::unexpected("invalid SCRAM base64 value");
        std::vector<std::uint8_t> out(3 * input.size() / 4);
        const auto n = EVP_DecodeBlock(out.data(),
            reinterpret_cast<const unsigned char*>(input.data()),
            static_cast<int>(input.size()));
        if (n < 0)
            return std::unexpected("invalid SCRAM base64 value");
        auto size = static_cast<std::size_t>(n);
        if (input.ends_with("=="))
            size -= 2;
        else if (input.ends_with('='))
            size -= 1;
        out.resize(size);
        return out;
#else
        (void)input;
        return std::unexpected("SCRAM requires OpenSSL support");
#endif
    }

#ifdef CNETMOD_HAS_SSL
    auto digest(const EVP_MD* md, std::span<const std::uint8_t> data)
        -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> out(static_cast<std::size_t>(EVP_MD_get_size(md)));
        unsigned int size{};
        if (EVP_Digest(data.data(), data.size(), out.data(), &size, md, nullptr) != 1)
            return {};
        out.resize(size);
        return out;
    }

    auto hmac_sha256(std::span<const std::uint8_t> key, std::string_view data)
        -> std::vector<std::uint8_t>
    {
        std::vector<std::uint8_t> out(EVP_MAX_MD_SIZE);
        unsigned int size{};
        if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
                reinterpret_cast<const unsigned char*>(data.data()), data.size(),
                out.data(), &size))
            return {};
        out.resize(size);
        return out;
    }

    auto md5_hex(std::string_view input) -> std::expected<std::string, std::string>
    {
        auto bytes = digest(EVP_md5(), std::span{reinterpret_cast<const std::uint8_t*>(input.data()), input.size()});
        if (bytes.empty())
            return std::unexpected("MD5 digest failed");
        static constexpr char hex[] = "0123456789abcdef";
        std::string out(bytes.size() * 2, '\0');
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            out[i * 2] = hex[bytes[i] >> 4];
            out[i * 2 + 1] = hex[bytes[i] & 15];
        }
        return out;
    }
#endif

    auto parameter_text(const param_value& value) -> std::optional<std::string>
    {
        using kind = param_value::kind_t;
        switch (value.kind)
        {
        case kind::null_kind:
            return std::nullopt;
        case kind::int64_kind:
            return std::to_string(value.int_val);
        case kind::uint64_kind:
            return std::to_string(value.uint_val);
        case kind::double_kind:
            return std::format("{}", value.double_val);
        case kind::string_kind:
        case kind::blob_kind:
            return value.str_val;
        case kind::date_kind:
            return value.date_val.to_string();
        case kind::datetime_kind:
            return value.datetime_val.to_string();
        case kind::time_kind:
            return value.time_val.to_string();
        }
        return std::nullopt;
    }

    template <class Integer>
    auto parse_integer(std::string_view text, Integer& value) -> bool
    {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
    }

} // namespace

auto startup_message(const connection_options& options) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out(4, 0);
    put_i32(out, 196608);
    put_cstring(out, "user");
    put_cstring(out, options.username);
    put_cstring(out, "database");
    put_cstring(out, options.database);
    put_cstring(out, "application_name");
    put_cstring(out, options.application_name);
    put_cstring(out, "client_encoding");
    put_cstring(out, "UTF8");
    for (const auto& [key, value] : options.startup_parameters)
    {
        if (key.find('\0') != std::string::npos || value.find('\0') != std::string::npos)
            continue;
        put_cstring(out, key);
        put_cstring(out, value);
    }
    out.push_back(0);
    const auto length = static_cast<std::uint32_t>(out.size());
    out[0] = static_cast<std::uint8_t>(length >> 24);
    out[1] = static_cast<std::uint8_t>(length >> 16);
    out[2] = static_cast<std::uint8_t>(length >> 8);
    out[3] = static_cast<std::uint8_t>(length);
    return out;
}

auto ssl_request() -> std::array<std::uint8_t, 8>
{
    return {0, 0, 0, 8, 4, 210, 22, 47};
}

auto password_message(std::string_view password) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{'p', 0, 0, 0, 0};
    put_cstring(out, password);
    finish_message(out);
    return out;
}

auto simple_query_message(std::string_view sql) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{'Q', 0, 0, 0, 0};
    put_cstring(out, sql);
    finish_message(out);
    return out;
}

auto terminate_message() -> std::array<std::uint8_t, 5>
{
    return {'X', 0, 0, 0, 4};
}

auto parse_message(std::span<const std::uint8_t> input)
    -> std::expected<backend_message, std::string>
{
    if (input.size() < 5)
        return std::unexpected("truncated PostgreSQL message");
    const auto length = read_i32(input.subspan(1, 4));
    if (length < 4 || input.size() != static_cast<std::size_t>(length) + 1)
        return std::unexpected("invalid PostgreSQL message length");
    return backend_message{static_cast<char>(input[0]),
        std::vector<std::uint8_t>(input.begin() + 5, input.end())};
}

auto parse_error(std::span<const std::uint8_t> payload) -> server_error
{
    server_error out;
    std::size_t pos{};
    while (pos < payload.size() && payload[pos] != 0)
    {
        const char code = static_cast<char>(payload[pos++]);
        const auto end = std::find(payload.begin() + static_cast<std::ptrdiff_t>(pos), payload.end(), 0);
        if (end == payload.end())
            break;
        std::string value(reinterpret_cast<const char*>(payload.data() + pos),
            static_cast<std::size_t>(end - (payload.begin() + static_cast<std::ptrdiff_t>(pos))));
        pos += value.size() + 1;
        if (code == 'S' || code == 'V')
            out.severity = std::move(value);
        else if (code == 'C')
            out.sql_state = std::move(value);
        else if (code == 'M')
            out.message = std::move(value);
        else if (code == 'D')
            out.detail = std::move(value);
        else if (code == 'H')
            out.hint = std::move(value);
    }
    return out;
}

auto md5_password(std::string_view user, std::string_view password,
    std::span<const std::uint8_t, 4> salt)
    -> std::expected<std::string, std::string>
{
#ifdef CNETMOD_HAS_SSL
    auto first = md5_hex(std::string(password) + std::string(user));
    if (!first)
        return std::unexpected(first.error());
    std::string second_input = *first;
    second_input.append(reinterpret_cast<const char*>(salt.data()), salt.size());
    auto second = md5_hex(second_input);
    if (!second)
        return std::unexpected(second.error());
    return "md5" + *second;
#else
    (void)user;
    (void)password;
    (void)salt;
    return std::unexpected("MD5 authentication requires OpenSSL support");
#endif
}

auto scram_client::begin(std::string_view username) -> std::string
{
    auto prepared_username = saslprep(username);
    if (!prepared_username)
        throw std::runtime_error(prepared_username.error());
    std::array<std::uint8_t, 18> random{};
#ifdef CNETMOD_HAS_SSL
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1)
        throw std::runtime_error("SCRAM nonce generation failed");
#else
    std::random_device source;
    for (auto& byte : random)
        byte = static_cast<std::uint8_t>(source());
#endif
    nonce = b64_encode(random);
    std::string escaped;
    for (char c : *prepared_username)
    {
        if (c == ',')
            escaped += "=2C";
        else if (c == '=')
            escaped += "=3D";
        else
            escaped.push_back(c);
    }
    client_first_bare = "n=" + escaped + ",r=" + nonce;
    return "n,," + client_first_bare;
}

auto saslprep(std::string_view input) -> std::expected<std::string, std::string>
{
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return std::unexpected("SASLprep input is too large");
#ifndef CNETMOD_HAS_ICU
    for (const auto byte : input)
    {
        const auto code_point = static_cast<unsigned char>(byte);
        if (code_point > 0x7f)
            return std::unexpected("non-ASCII SCRAM credentials require ICU RFC 4013 SASLprep support");
        if (code_point < 0x20 || code_point == 0x7f)
            return std::unexpected("SASLprep rejected an ASCII control character");
    }
    return std::string(input);
#else
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
#endif
}

auto scram_client::respond(std::string_view password, std::string_view challenge)
    -> std::expected<std::string, std::string>
{
#ifdef CNETMOD_HAS_SSL
    auto prepared_password = saslprep(password);
    if (!prepared_password)
        return std::unexpected(prepared_password.error());
    server_first = std::string(challenge);
    std::string server_nonce, salt_text, iterations_text;
    std::size_t pos{};
    while (pos <= challenge.size())
    {
        auto end = challenge.find(',', pos);
        if (end == std::string_view::npos)
            end = challenge.size();
        auto item = challenge.substr(pos, end - pos);
        if (item.starts_with("r="))
            server_nonce = std::string(item.substr(2));
        else if (item.starts_with("s="))
            salt_text = std::string(item.substr(2));
        else if (item.starts_with("i="))
            iterations_text = std::string(item.substr(2));
        if (end == challenge.size())
            break;
        pos = end + 1;
    }
    if (!server_nonce.starts_with(nonce) || server_nonce.size() <= nonce.size())
        return std::unexpected("SCRAM server nonce is invalid");
    auto salt = b64_decode(salt_text);
    if (!salt)
        return std::unexpected(salt.error());
    std::uint32_t iterations{};
    auto parsed = std::from_chars(iterations_text.data(),
        iterations_text.data() + iterations_text.size(), iterations);
    if (parsed.ec != std::errc{} || parsed.ptr != iterations_text.data() + iterations_text.size() ||
        iterations < 4096 || iterations > 10000000)
        return std::unexpected("SCRAM iteration count is invalid");
    std::vector<std::uint8_t> salted(32);
    if (PKCS5_PBKDF2_HMAC(prepared_password->data(), static_cast<int>(prepared_password->size()),
            salt->data(), static_cast<int>(salt->size()), static_cast<int>(iterations),
            EVP_sha256(), static_cast<int>(salted.size()), salted.data()) != 1)
        return std::unexpected("SCRAM PBKDF2 failed");
    const auto client_key = hmac_sha256(salted, "Client Key");
    const auto stored_key = digest(EVP_sha256(), client_key);
    const std::string final_without_proof = "c=biws,r=" + server_nonce;
    auth_message = client_first_bare + "," + server_first + "," + final_without_proof;
    const auto signature = hmac_sha256(stored_key, auth_message);
    std::vector<std::uint8_t> proof(client_key.size());
    std::transform(client_key.begin(), client_key.end(), signature.begin(), proof.begin(),
        std::bit_xor<std::uint8_t>{});
    const auto server_key = hmac_sha256(salted, "Server Key");
    server_signature = hmac_sha256(server_key, auth_message);
    return final_without_proof + ",p=" + b64_encode(proof);
#else
    (void)password;
    (void)challenge;
    return std::unexpected("SCRAM-SHA-256 requires OpenSSL support");
#endif
}

auto scram_client::verify(std::string_view final_message) const
    -> std::expected<void, std::string>
{
    if (final_message.starts_with("e="))
        return std::unexpected("SCRAM server error: " + std::string(final_message.substr(2)));
    if (!final_message.starts_with("v="))
        return std::unexpected("SCRAM server signature is missing");
    auto signature_text = final_message.substr(2);
    if (auto comma = signature_text.find(','); comma != std::string_view::npos)
        signature_text = signature_text.substr(0, comma);
    auto actual = b64_decode(signature_text);
#ifdef CNETMOD_HAS_SSL
    if (!actual || actual->size() != server_signature.size() ||
        CRYPTO_memcmp(actual->data(), server_signature.data(), actual->size()) != 0)
#else
    if (!actual || *actual != server_signature)
#endif
        return std::unexpected("SCRAM server signature mismatch");
    return {};
}

auto scram_initial_response(std::string_view mechanism, std::string_view data)
    -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{'p', 0, 0, 0, 0};
    put_cstring(out, mechanism);
    put_i32(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    finish_message(out);
    return out;
}

auto scram_response(std::string_view data) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> out{'p', 0, 0, 0, 0};
    out.insert(out.end(), data.begin(), data.end());
    finish_message(out);
    return out;
}

auto extended_query_messages(std::string_view statement_name,
    std::string_view sql, std::span<const param_value> params, bool parse)
    -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> all;
    if (parse)
    {
        std::vector<std::uint8_t> msg{'P', 0, 0, 0, 0};
        put_cstring(msg, statement_name);
        put_cstring(msg, sql);
        put_i16(msg, 0);
        finish_message(msg);
        all.insert(all.end(), msg.begin(), msg.end());
    }
    std::vector<std::uint8_t> bind{'B', 0, 0, 0, 0};
    put_cstring(bind, "");
    put_cstring(bind, statement_name);
    put_i16(bind, 0);
    put_i16(bind, static_cast<std::uint16_t>(params.size()));
    for (const auto& param : params)
    {
        auto text = parameter_text(param);
        if (!text)
            put_i32(bind, 0xffffffffU);
        else
        {
            put_i32(bind, static_cast<std::uint32_t>(text->size()));
            bind.insert(bind.end(), text->begin(), text->end());
        }
    }
    put_i16(bind, 0);
    finish_message(bind);
    all.insert(all.end(), bind.begin(), bind.end());
    std::vector<std::uint8_t> describe{'D', 0, 0, 0, 6, 'P', 0};
    all.insert(all.end(), describe.begin(), describe.end());
    std::vector<std::uint8_t> execute{'E', 0, 0, 0, 9, 0, 0, 0, 0, 0};
    all.insert(all.end(), execute.begin(), execute.end());
    std::array<std::uint8_t, 5> sync{'S', 0, 0, 0, 4};
    all.insert(all.end(), sync.begin(), sync.end());
    return all;
}

auto prepare_statement_messages(std::string_view statement_name,
    std::string_view sql) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> all;
    std::vector<std::uint8_t> parse{'P', 0, 0, 0, 0};
    put_cstring(parse, statement_name);
    put_cstring(parse, sql);
    put_i16(parse, 0);
    finish_message(parse);
    all.insert(all.end(), parse.begin(), parse.end());
    std::vector<std::uint8_t> describe{'D', 0, 0, 0, 0, 'S'};
    put_cstring(describe, statement_name);
    finish_message(describe);
    all.insert(all.end(), describe.begin(), describe.end());
    std::array<std::uint8_t, 5> sync{'S', 0, 0, 0, 4};
    all.insert(all.end(), sync.begin(), sync.end());
    return all;
}

auto streaming_portal_start_messages(std::string_view portal_name,
    std::string_view sql, std::uint32_t maximum_rows)
    -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> all;
    std::vector<std::uint8_t> parse{'P', 0, 0, 0, 0};
    put_cstring(parse, "");
    put_cstring(parse, sql);
    put_i16(parse, 0);
    finish_message(parse);
    all.insert(all.end(), parse.begin(), parse.end());
    std::vector<std::uint8_t> bind{'B', 0, 0, 0, 0};
    put_cstring(bind, portal_name);
    put_cstring(bind, "");
    put_i16(bind, 0);
    put_i16(bind, 0);
    put_i16(bind, 0);
    finish_message(bind);
    all.insert(all.end(), bind.begin(), bind.end());
    std::vector<std::uint8_t> describe{'D', 0, 0, 0, 0, 'P'};
    put_cstring(describe, portal_name);
    finish_message(describe);
    all.insert(all.end(), describe.begin(), describe.end());
    auto execute = streaming_portal_continue_messages(portal_name, maximum_rows);
    all.insert(all.end(), execute.begin(), execute.end());
    return all;
}

auto streaming_portal_continue_messages(std::string_view portal_name,
    std::uint32_t maximum_rows) -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> execute{'E', 0, 0, 0, 0};
    put_cstring(execute, portal_name);
    put_i32(execute, maximum_rows);
    finish_message(execute);
    execute.insert(execute.end(), {'H', 0, 0, 0, 4}); // Flush, not Sync: portal survives.
    return execute;
}

auto streaming_portal_close_messages(std::string_view portal_name)
    -> std::vector<std::uint8_t>
{
    std::vector<std::uint8_t> close{'C', 0, 0, 0, 0, 'P'};
    put_cstring(close, portal_name);
    finish_message(close);
    auto sync = synchronization_message();
    close.insert(close.end(), sync.begin(), sync.end());
    return close;
}

auto synchronization_message() -> std::array<std::uint8_t, 5>
{
    return {'S', 0, 0, 0, 4};
}

auto count_postgresql_parameters(std::string_view sql) -> std::size_t
{
    std::size_t max{};
    bool single{}, quoted_identifier{};
    for (std::size_t i = 0; i < sql.size(); ++i)
    {
        if (sql[i] == '\'' && !quoted_identifier)
            single = !single;
        else if (sql[i] == '"' && !single)
            quoted_identifier = !quoted_identifier;
        else if (!single && !quoted_identifier && sql[i] == '$')
        {
            std::size_t value{}, end = i + 1;
            while (end < sql.size() && std::isdigit(static_cast<unsigned char>(sql[end])))
                value = value * 10 + static_cast<unsigned>(sql[end++] - '0');
            max = std::max(max, value);
        }
    }
    return max;
}

auto decode_text_field(std::uint32_t oid, std::string_view value) -> field_value
{
    if (oid == 16)
        return field_value::from_int64(value == "t" ? 1 : 0);
    if (oid == 20 || oid == 21 || oid == 23)
    {
        std::int64_t number{};
        if (auto r = std::from_chars(value.data(), value.data() + value.size(), number); r.ec == std::errc{})
            return field_value::from_int64(number);
    }
    if (oid == 700 || oid == 701)
    {
        double number{};
        if (auto r = std::from_chars(value.data(), value.data() + value.size(), number); r.ec == std::errc{})
            return field_value::from_double(number);
    }
    if (oid == 1082 && value.size() == 10)
    {
        unsigned year{}, month{}, day{};
        if (parse_integer(value.substr(0, 4), year) &&
            parse_integer(value.substr(5, 2), month) &&
            parse_integer(value.substr(8, 2), day))
            return field_value::from_date(database::calendar_date{
                static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month),
                static_cast<std::uint8_t>(day)});
    }
    if ((oid == 1114 || oid == 1184) && value.size() >= 19)
    {
        unsigned year{}, month{}, day{}, hour{}, minute{}, second{}, microsecond{};
        const bool valid = parse_integer(value.substr(0, 4), year) &&
            parse_integer(value.substr(5, 2), month) &&
            parse_integer(value.substr(8, 2), day) &&
            parse_integer(value.substr(11, 2), hour) &&
            parse_integer(value.substr(14, 2), minute) &&
            parse_integer(value.substr(17, 2), second);
        if (valid)
        {
            if (value.size() > 20 && value[19] == '.')
            {
                const auto end = value.find_first_not_of("0123456789", 20);
                auto fraction = value.substr(20, end == std::string_view::npos ? value.size() - 20 : end - 20);
                if (fraction.size() > 6)
                    fraction = fraction.substr(0, 6);
                (void)parse_integer(fraction, microsecond);
                for (std::size_t i = fraction.size(); i < 6; ++i)
                    microsecond *= 10;
            }
            return field_value::from_datetime(database::calendar_datetime{
                static_cast<std::uint16_t>(year), static_cast<std::uint8_t>(month),
                static_cast<std::uint8_t>(day), static_cast<std::uint8_t>(hour),
                static_cast<std::uint8_t>(minute), static_cast<std::uint8_t>(second), microsecond});
        }
    }
    if (oid == 1083 && value.size() >= 8)
    {
        bool negative = value.front() == '-';
        auto time = negative ? value.substr(1) : value;
        const auto first_colon = time.find(':');
        const auto second_colon = first_colon == std::string_view::npos
            ? std::string_view::npos
            : time.find(':', first_colon + 1);
        unsigned hours{}, minutes{}, seconds{}, microsecond{};
        if (first_colon != std::string_view::npos && second_colon != std::string_view::npos &&
            parse_integer(time.substr(0, first_colon), hours) &&
            parse_integer(time.substr(first_colon + 1, second_colon - first_colon - 1), minutes))
        {
            auto seconds_text = time.substr(second_colon + 1);
            const auto dot = seconds_text.find('.');
            auto whole_seconds = seconds_text.substr(0, dot);
            if (parse_integer(whole_seconds, seconds))
            {
                if (dot != std::string_view::npos)
                {
                    auto fraction = seconds_text.substr(dot + 1);
                    if (fraction.size() > 6)
                        fraction = fraction.substr(0, 6);
                    (void)parse_integer(fraction, microsecond);
                    for (std::size_t i = fraction.size(); i < 6; ++i)
                        microsecond *= 10;
                }
                return field_value::from_time(database::clock_time{negative, hours,
                    static_cast<std::uint8_t>(minutes), static_cast<std::uint8_t>(seconds), microsecond});
            }
        }
    }
    if (oid == 17 && value.starts_with("\\x"))
    {
        std::string bytes;
        bytes.reserve((value.size() - 2) / 2);
        for (std::size_t i = 2; i + 1 < value.size(); i += 2)
        {
            unsigned int byte{};
            auto r = std::from_chars(value.data() + i, value.data() + i + 2, byte, 16);
            if (r.ec != std::errc{})
                return field_value::from_string(std::string(value));
            bytes.push_back(static_cast<char>(byte));
        }
        return field_value::from_blob(std::move(bytes));
    }
    return field_value::from_string(std::string(value));
}

} // namespace cnetmod::postgresql::detail
