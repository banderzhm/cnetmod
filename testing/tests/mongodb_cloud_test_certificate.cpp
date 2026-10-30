#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
import std;
#include "mongodb_cloud_test_certificate.hpp"

auto write_mongodb_cloud_test_certificate(std::string_view certificate_path, std::string_view key_path) -> bool
{
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context{EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free};
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1 || EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 2048) != 1)
        return false;
    EVP_PKEY* raw{};
    if (EVP_PKEY_keygen(context.get(), &raw) != 1)
        return false;
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{raw, EVP_PKEY_free};
    std::unique_ptr<X509, decltype(&X509_free)> certificate{X509_new(), X509_free};
    if (!certificate || X509_set_version(certificate.get(), 2) != 1 || ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) != 1 ||
        !X509_gmtime_adj(X509_get_notBefore(certificate.get()), -60) || !X509_gmtime_adj(X509_get_notAfter(certificate.get()), 86400) ||
        X509_set_pubkey(certificate.get(), key.get()) != 1)
        return false;
    auto name = X509_get_subject_name(certificate.get());
    if (!name || X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0) != 1 ||
        X509_set_issuer_name(certificate.get(), name) != 1)
        return false;
    X509V3_CTX extension_context{};
    X509V3_set_ctx(&extension_context, certificate.get(), certificate.get(), nullptr, nullptr, 0);
    for (const auto& [nid, value] : std::array<std::pair<int, const char*>, 3>{{{NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1"}, {NID_basic_constraints, "critical,CA:TRUE"},
             {NID_key_usage, "critical,digitalSignature,keyEncipherment,keyCertSign"}}})
    {
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension{
            X509V3_EXT_nconf_nid(nullptr, &extension_context, nid, value), X509_EXTENSION_free};
        if (!extension || X509_add_ext(certificate.get(), extension.get(), -1) != 1)
            return false;
    }
    if (X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0)
        return false;
    std::unique_ptr<BIO, decltype(&BIO_free)> certificate_bio{BIO_new(BIO_s_mem()), BIO_free}, key_bio{BIO_new(BIO_s_mem()), BIO_free};
    if (!certificate_bio || !key_bio || PEM_write_bio_X509(certificate_bio.get(), certificate.get()) != 1 ||
        PEM_write_bio_PrivateKey(key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1)
        return false;
    const auto write = [](BIO* bio, std::string_view path)
    {
        char* bytes{};
        const auto size = BIO_get_mem_data(bio, &bytes);
        if (size <= 0)
            return false;
        std::ofstream output(std::filesystem::path{path}, std::ios::binary);
        output.write(bytes, size);
        return static_cast<bool>(output);
    };
    return write(certificate_bio.get(), certificate_path) && write(key_bio.get(), key_path);
}
