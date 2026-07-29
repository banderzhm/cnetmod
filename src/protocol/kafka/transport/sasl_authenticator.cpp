module cnetmod.protocol.kafka.sasl_authenticator;
import std;

namespace cnetmod::kafka {
namespace {
    class plain final : public sasl_authenticator
    {
    public:
        plain(std::string u, std::string p)
            : u_(std::move(u)), p_(std::move(p)) {}

        auto mechanism_name() const noexcept -> std::string_view override
        {
            return "PLAIN";
        }

        auto initial_response() -> result<bytes> override
        {
            bytes b(1 + u_.size() + 1 + p_.size());
            auto* o = reinterpret_cast<char*>(b.data());
            o[0] = 0;
            std::ranges::copy(u_, o + 1);
            o[1 + u_.size()] = 0;
            std::ranges::copy(p_, o + 2 + u_.size());
            done_ = true;
            return b;
        }

        auto challenge(std::span<const std::byte>) -> result<bytes> override
        {
            return bytes{};
        }

        auto complete() const noexcept -> bool override
        {
            return done_;
        }

    private:
        std::string u_, p_;
        bool done_ = false;
    };

    class scram final : public sasl_authenticator
    {
    public:
        scram(sasl_mechanism m, std::string u, std::string p,
            std::shared_ptr<scram_crypto_provider> c)
            : m_(m), u_(std::move(u)), p_(std::move(p)), crypto_(std::move(c)) {}

        auto mechanism_name() const noexcept -> std::string_view override
        {
            return m_ == sasl_mechanism::scram_sha_512 ? "SCRAM-SHA-512"
                                                       : "SCRAM-SHA-256";
        }

        auto initial_response() -> result<bytes> override
        {
            auto n = crypto_->nonce(24);
            if (!n)
                return std::unexpected(n.error());
            nonce_ = *n;
            bare_ = "n=" + u_ + ",r=" + nonce_;
            auto s = "n,," + bare_;
            stage_ = 1;
            return bytes(reinterpret_cast<const std::byte*>(s.data()),
                reinterpret_cast<const std::byte*>(s.data() + s.size()));
        }

        auto challenge(std::span<const std::byte> raw) -> result<bytes> override
        {
            std::string s(reinterpret_cast<const char*>(raw.data()), raw.size());
            if (stage_ == 1)
            {
                server_first_ = s;
                auto r = field(s, 'r'), salt = field(s, 's'), iter = field(s, 'i');
                if (!r || !salt || !iter || !r->starts_with(nonce_))
                    return std::unexpected(
                        make_error(error_code::illegal_sasl_state,
                            "invalid SCRAM server-first message"));
                std::uint32_t count = 0;
                auto [p, ec] =
                    std::from_chars(iter->data(), iter->data() + iter->size(), count);
                if (ec != std::errc{} || count < 4096)
                    return std::unexpected(make_error(error_code::illegal_sasl_state,
                        "invalid SCRAM iteration count"));
                auto salt_bytes = crypto_->base64_decode(*salt);
                if (!salt_bytes)
                    return std::unexpected(salt_bytes.error());
                auto salted = crypto_->pbkdf2(m_, p_, *salt_bytes, count);
                if (!salted)
                    return std::unexpected(salted.error());
                static constexpr std::string_view ck = "Client Key", sk = "Server Key";
                auto client_key = crypto_->hmac(
                    m_, *salted,
                    {reinterpret_cast<const std::byte*>(ck.data()), ck.size()});
                if (!client_key)
                    return std::unexpected(client_key.error());
                auto stored = crypto_->hash(m_, *client_key);
                if (!stored)
                    return std::unexpected(stored.error());
                auto final = "c=biws,r=" + *r;
                auto auth = bare_ + "," + server_first_ + "," + final;
                auto signature = crypto_->hmac(
                    m_, *stored,
                    {reinterpret_cast<const std::byte*>(auth.data()), auth.size()});
                if (!signature)
                    return std::unexpected(signature.error());
                bytes proof(client_key->size());
                for (std::size_t i = 0; i < proof.size(); ++i)
                    proof[i] = (*client_key)[i] ^ (*signature)[i];
                auto server_key = crypto_->hmac(
                    m_, *salted,
                    {reinterpret_cast<const std::byte*>(sk.data()), sk.size()});
                if (!server_key)
                    return std::unexpected(server_key.error());
                server_signature_ =
                    crypto_
                        ->hmac(m_, *server_key,
                            {reinterpret_cast<const std::byte*>(auth.data()),
                                auth.size()})
                        .value_or(bytes{});
                auto encoded_proof = crypto_->base64_encode(proof);
                if (!encoded_proof)
                    return std::unexpected(encoded_proof.error());
                final += ",p=" + *encoded_proof;
                stage_ = 2;
                return bytes(
                    reinterpret_cast<const std::byte*>(final.data()),
                    reinterpret_cast<const std::byte*>(final.data() + final.size()));
            }
            if (stage_ == 2)
            {
                auto v = field(s, 'v');
                auto expected_signature = crypto_->base64_encode(server_signature_);
                if (!v || !expected_signature || *v != *expected_signature)
                    return std::unexpected(make_error(error_code::illegal_sasl_state,
                        "SCRAM server signature mismatch"));
                stage_ = 3;
                return bytes{};
            }
            return std::unexpected(make_error(error_code::illegal_sasl_state,
                "unexpected SCRAM challenge"));
        }

        auto complete() const noexcept -> bool override
        {
            return stage_ == 3;
        }

    private:
        static auto field(std::string_view s, char key)
            -> std::optional<std::string>
        {
            for (auto part : s | std::views::split(','))
            {
                std::string x(part.begin(), part.end());
                if (x.size() > 2 && x[0] == key && x[1] == '=')
                    return x.substr(2);
            }
            return std::nullopt;
        }

        static auto hex(std::span<const std::byte> b) -> std::string
        {
            static constexpr char h[] = "0123456789abcdef";
            std::string o;
            for (auto x : b)
            {
                auto n = std::to_integer<unsigned>(x);
                o += h[n >> 4];
                o += h[n & 15];
            }
            return o;
        }

        sasl_mechanism m_;
        std::string u_, p_, nonce_, bare_, server_first_;
        std::shared_ptr<scram_crypto_provider> crypto_;
        bytes server_signature_;
        int stage_ = 0;
    };
} // namespace

auto make_plain_authenticator(std::string u, std::string p)
    -> std::unique_ptr<sasl_authenticator>
{
    return std::make_unique<plain>(std::move(u), std::move(p));
}

auto make_scram_authenticator(sasl_mechanism m, std::string u, std::string p,
    std::shared_ptr<scram_crypto_provider> c)
    -> result<std::unique_ptr<sasl_authenticator>>
{
    if ((m != sasl_mechanism::scram_sha_256 &&
            m != sasl_mechanism::scram_sha_512) ||
        !c)
        return std::unexpected(
            make_error(error_code::configuration,
                "SCRAM requires SHA-256/SHA-512 and a crypto provider"));
    return std::unique_ptr<sasl_authenticator>(
        std::make_unique<scram>(m, std::move(u), std::move(p), std::move(c)));
}
} // namespace cnetmod::kafka
