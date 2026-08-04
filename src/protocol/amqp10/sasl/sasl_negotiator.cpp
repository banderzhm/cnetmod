module;
#include <cnetmod/config.hpp>
#ifdef CNETMOD_HAS_SSL
    #include <openssl/evp.h>
    #include <openssl/hmac.h>
#endif
module cnetmod.protocol.amqp10;
import :sasl_negotiator;
import std;
import :amqp_value_codec;
import :protocol_error;

namespace cnetmod::amqp10 {
namespace {
    auto mechanism_name(authentication_mechanism m) -> std::string_view
    {
        switch (m)
        {
        case authentication_mechanism::anonymous:
            return "ANONYMOUS";
        case authentication_mechanism::plain:
            return "PLAIN";
        case authentication_mechanism::external:
            return "EXTERNAL";
        case authentication_mechanism::scram_sha_256:
            return "SCRAM-SHA-256";
        case authentication_mechanism::scram_sha_512:
            return "SCRAM-SHA-512";
        case authentication_mechanism::oauth_bearer:
            return "OAUTHBEARER";
        }
        return {};
    }

    auto fields(const value& v) -> const list*
    {
        auto d = std::get_if<std::shared_ptr<described_value>>(&v.data);
        if (!d || !*d || !(*d)->body)
            return nullptr;
        auto l = std::get_if<std::shared_ptr<list>>(&(*d)->body->data);
        return l && *l ? l->get() : nullptr;
    }

    auto code(const value& v) -> std::optional<std::uint64_t>
    {
        auto d = std::get_if<std::shared_ptr<described_value>>(&v.data);
        if (!d || !*d)
            return {};
        if (auto p = std::get_if<std::uint64_t>(&(*d)->type.value))
            return *p;
        return {};
    }

    auto described(std::uint64_t c, list f) -> binary
    {
        encoder e;
        e.write_value(
            value::described(descriptor{c}, value::make_list(std::move(f))));
        return e.release();
    }

    auto b64decode(std::string_view text) -> binary
    {
#ifdef CNETMOD_HAS_SSL
        binary out((text.size() * 3) / 4 + 3);
        auto n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
            reinterpret_cast<const unsigned char*>(text.data()),
            static_cast<int>(text.size()));
        if (n < 0)
            return {};
        while (!text.empty() && text.back() == '=')
        {
            --n;
            text.remove_suffix(1);
        }
        out.resize(static_cast<std::size_t>(n));
        return out;
#else
        (void)text;
        return {};
#endif
    }

    auto b64encode(std::span<const std::byte> bytes) -> std::string
    {
#ifdef CNETMOD_HAS_SSL
        std::string out(4 * ((bytes.size() + 2) / 3), '\0');
        auto n =
            EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                reinterpret_cast<const unsigned char*>(bytes.data()),
                static_cast<int>(bytes.size()));
        out.resize(static_cast<std::size_t>(n));
        return out;
#else
        (void)bytes;
        return {};
#endif
    }
} // namespace

struct sasl_negotiator::impl
{
    credentials credentials;
    std::string mechanism;
    std::string nonce;
    std::string client_first_bare;
    binary server_signature;
    bool awaiting_challenge = false;
};

sasl_negotiator::sasl_negotiator(credentials c)
    : impl_(std::make_unique<impl>(impl{.credentials = std::move(c), .mechanism = "", .nonce = "", .client_first_bare = {}, .server_signature = {}, .awaiting_challenge = false})) {}

sasl_negotiator::~sasl_negotiator() = default;
sasl_negotiator::sasl_negotiator(sasl_negotiator&&) noexcept = default;
auto sasl_negotiator::operator=(sasl_negotiator&&) noexcept
    -> sasl_negotiator& = default;

auto sasl_negotiator::select(std::span<const symbol> offered,
    std::string_view host)
    -> std::expected<sasl_init, error>
{
    auto wanted = mechanism_name(impl_->credentials.mechanism);
    auto found = std::ranges::find_if(
        offered, [&](const symbol& s)
        {
            return s.text == wanted;
        });
    if (found == offered.end())
        return std::unexpected(make_error(
            error_stage::authentication, errc::authentication_failed,
            "server did not offer SASL " + std::string(wanted)));
    impl_->mechanism = std::string(wanted);
    binary initial;
    if (wanted == "PLAIN")
    {
        initial.push_back(std::byte{});
        auto append = [&](std::string_view s)
        {
            auto b = std::as_bytes(std::span(s));
            initial.insert(initial.end(), b.begin(), b.end());
        };
        append(impl_->credentials.username);
        initial.push_back(std::byte{});
        append(impl_->credentials.password);
    }
    else if (wanted == "OAUTHBEARER")
    {
        std::string text =
            "n,,\x01auth=Bearer " + impl_->credentials.token + "\x01\x01";
        auto b = std::as_bytes(std::span(text));
        initial.assign(b.begin(), b.end());
    }
    else if (wanted.starts_with("SCRAM-"))
    {
        impl_->nonce = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        impl_->client_first_bare =
            "n=" + impl_->credentials.username + ",r=" + impl_->nonce;
        std::string first = "n,," + impl_->client_first_bare;
        auto b = std::as_bytes(std::span(first));
        initial.assign(b.begin(), b.end());
        impl_->awaiting_challenge = true;
    }
    return sasl_init{symbol{std::string(wanted)}, std::move(initial),
        std::string(host)};
}

auto sasl_negotiator::respond(std::span<const std::byte> challenge)
    -> std::expected<sasl_response, error>
{
    if (!impl_->awaiting_challenge)
        return std::unexpected(make_error(error_stage::authentication,
            errc::protocol_state,
            "unexpected SASL challenge"));
    std::string server(reinterpret_cast<const char*>(challenge.data()),
        challenge.size());
    auto param = [&](char key) -> std::string
    {
        auto pos = server.find(std::string(1, key) + "=");
        if (pos == std::string::npos)
            return {};
        pos += 2;
        auto end = server.find(',', pos);
        return server.substr(pos, end - pos);
    };
    auto nonce = param('r');
    auto salt64 = param('s');
    auto iterations_text = param('i');
    if (!nonce.starts_with(impl_->nonce) || salt64.empty() ||
        iterations_text.empty())
        return std::unexpected(make_error(error_stage::authentication,
            errc::authentication_failed,
            "invalid SCRAM server-first message"));
#ifdef CNETMOD_HAS_SSL
    auto salt = b64decode(salt64);
    int iterations{};
    auto parsed = std::from_chars(iterations_text.data(),
        iterations_text.data() + iterations_text.size(),
        iterations);
    if (parsed.ec != std::errc{} || iterations < 4096)
        return std::unexpected(make_error(error_stage::authentication,
            errc::authentication_failed,
            "invalid SCRAM iteration count"));
    const EVP_MD* md =
        impl_->mechanism == "SCRAM-SHA-512" ? EVP_sha512() : EVP_sha256();
    const auto length = static_cast<std::size_t>(EVP_MD_size(md));
    binary salted(length);
    if (PKCS5_PBKDF2_HMAC(impl_->credentials.password.data(),
            static_cast<int>(impl_->credentials.password.size()),
            reinterpret_cast<const unsigned char*>(salt.data()),
            static_cast<int>(salt.size()), iterations, md,
            static_cast<int>(length),
            reinterpret_cast<unsigned char*>(salted.data())) != 1)
        return std::unexpected(make_error(error_stage::authentication,
            errc::authentication_failed,
            "SCRAM PBKDF2 failed"));
    auto hmac = [&](std::span<const std::byte> key, std::string_view data)
    {
        binary out(length);
        unsigned int n{};
        HMAC(md, key.data(), static_cast<int>(key.size()),
            reinterpret_cast<const unsigned char*>(data.data()), data.size(),
            reinterpret_cast<unsigned char*>(out.data()), &n);
        out.resize(n);
        return out;
    };
    auto hash = [&](std::span<const std::byte> data)
    {
        binary out(length);
        unsigned int n{};
        EVP_Digest(data.data(), data.size(),
            reinterpret_cast<unsigned char*>(out.data()), &n, md, nullptr);
        out.resize(n);
        return out;
    };
    auto client_key = hmac(salted, "Client Key");
    auto stored_key = hash(client_key);
    std::string final_bare = "c=biws,r=" + nonce;
    std::string auth = impl_->client_first_bare + "," + server + "," + final_bare;
    auto signature = hmac(stored_key, auth);
    auto server_key = hmac(salted, "Server Key");
    impl_->server_signature = hmac(server_key, auth);
    for (std::size_t i = 0; i < client_key.size(); ++i)
        client_key[i] ^= signature[i];
    std::string final = final_bare + ",p=" + b64encode(client_key);
    binary response(std::as_bytes(std::span(final)).begin(),
        std::as_bytes(std::span(final)).end());
    impl_->awaiting_challenge = false;
    return sasl_response{std::move(response)};
#else
    return std::unexpected(make_error(error_stage::authentication,
        errc::authentication_failed,
        "SCRAM requires SSL crypto support"));
#endif
}

auto sasl_negotiator::finish(const sasl_outcome& o)
    -> std::expected<void, error>
{
    if (o.code != sasl_code::ok)
        return std::unexpected(make_error(error_stage::authentication,
            errc::authentication_failed,
            "SASL server rejected credentials",
            o.code == sasl_code::sys_temporary));
    if (!impl_->server_signature.empty())
    {
        std::string final(reinterpret_cast<const char*>(o.additional_data.data()),
            o.additional_data.size());
        if (!final.starts_with("v=") ||
            b64decode(std::string_view(final).substr(2)) != impl_->server_signature)
            return std::unexpected(make_error(
                error_stage::authentication, errc::authentication_failed,
                "SCRAM server signature verification failed"));
    }
    return {};
}

auto encode_sasl_performative(const sasl_performative& p) -> binary
{
    return std::visit(
        [](const auto& x) -> binary
        {
            using T = std::remove_cvref_t<decltype(x)>;
            if constexpr (std::same_as<T, sasl_mechanisms>)
            {
                array a;
                for (const auto& m : x.mechanisms)
                    a.push_back(value{m});
                return described(0x40, {value::make_array(std::move(a))});
            }
            else if constexpr (std::same_as<T, sasl_init>)
                return described(
                    0x41,
                    {value{x.mechanism},
                        x.initial_response.empty() ? value{} : value{x.initial_response},
                        x.hostname.empty() ? value{} : value{x.hostname}});
            else if constexpr (std::same_as<T, sasl_challenge>)
                return described(0x42, {value{x.challenge}});
            else if constexpr (std::same_as<T, sasl_response>)
                return described(0x43, {value{x.response}});
            else
                return described(
                    0x44,
                    {value{std::uint8_t(x.code)},
                        x.additional_data.empty() ? value{} : value{x.additional_data}});
        },
        p);
}

auto decode_sasl_performative(std::span<const std::byte> b)
    -> std::expected<sasl_performative, std::error_code>
{
    decoder d(b);
    auto root = d.read_value();
    if (!root)
        return std::unexpected(root.error());
    auto c = code(*root);
    auto f = fields(*root);
    if (!c || !f)
        return std::unexpected(make_error_code(errc::unexpected_performative));
    auto at = [&](std::size_t i) -> const value&
    {
        static value n;
        return i < f->size() ? (*f)[i] : n;
    };
    switch (*c)
    {
    case 0x40:
    {
        sasl_mechanisms x;
        if (auto a = std::get_if<std::shared_ptr<array>>(&at(0).data); a && *a)
            for (const auto& v : **a)
                if (auto s = std::get_if<symbol>(&v.data))
                    x.mechanisms.push_back(*s);
        return sasl_performative{std::move(x)};
    }
    case 0x41:
    {
        sasl_init x;
        if (auto s = std::get_if<symbol>(&at(0).data))
            x.mechanism = *s;
        if (auto v = std::get_if<binary>(&at(1).data))
            x.initial_response = *v;
        if (auto v = std::get_if<std::string>(&at(2).data))
            x.hostname = *v;
        return sasl_performative{std::move(x)};
    }
    case 0x42:
    {
        sasl_challenge x;
        if (auto v = std::get_if<binary>(&at(0).data))
            x.challenge = *v;
        return sasl_performative{std::move(x)};
    }
    case 0x43:
    {
        sasl_response x;
        if (auto v = std::get_if<binary>(&at(0).data))
            x.response = *v;
        return sasl_performative{std::move(x)};
    }
    case 0x44:
    {
        sasl_outcome x;
        if (auto v = std::get_if<std::uint8_t>(&at(0).data))
            x.code = static_cast<sasl_code>(*v);
        if (auto v = std::get_if<binary>(&at(1).data))
            x.additional_data = *v;
        return sasl_performative{std::move(x)};
    }
    default:
        return std::unexpected(make_error_code(errc::unexpected_performative));
    }
}
} // namespace cnetmod::amqp10
