export module cnetmod.protocol.mongodb:authentication;
import std;

export namespace cnetmod::mongodb {
enum class authentication_mechanism
{
    automatic,
    scram_sha256,
    aws,
    oidc,
    x509
};
inline constexpr std::string_view aws_mechanism_name{"MONGODB-AWS"};
inline constexpr std::string_view oidc_mechanism_name{"MONGODB-OIDC"};
inline constexpr std::string_view x509_mechanism_name{"MONGODB-X509"};
inline constexpr std::string_view scram_sha256_mechanism_name{"SCRAM-SHA-256"};
inline constexpr std::string_view default_authentication_database{"admin"};
inline constexpr std::string_view external_authentication_database{"$external"};
} // namespace cnetmod::mongodb
