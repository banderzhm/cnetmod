module;
#include <cnetmod/config.hpp>
#include <openssl/aead.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

// Test-only implementation unit of the same primary module. This grants
// access to the non-exported primitive without adding a public test API.
module cnetmod.protocol.quic;

import std;
import :crypto;
import cnetmod.core.log;
import cnetmod.core.ssl;

namespace cnetmod::quic {

struct quic_crypto_regression_access
{
    static auto install(quic_tls_session& session, std::uint32_t cipher,
        std::span<const std::uint8_t> secret, bool reading) -> bool
    {
        return session.install_secret(encryption_level::application,
            cipher, secret, reading);
    }
};

} // namespace cnetmod::quic

namespace {
namespace q = cnetmod::quic;

struct results
{
    std::size_t cases{};
    std::size_t checks{};
    std::size_t failures{};
    auto check(bool condition, std::string_view label) -> bool
    {
        ++checks;
        if (!condition)
        {
            ++failures;
            logger::error{"in-place crypto case {}: {}", cases, label};
        }
        return condition;
    }
};

struct suite
{
    std::uint32_t id;
    const EVP_AEAD* aead;
    std::size_t key_size;
};

auto make_keys(const suite& cipher) -> q::quic_level_keys
{
    q::quic_level_keys keys;
    keys.cipher_id = cipher.id;
    keys.aead = cipher.aead;
    keys.secret.assign(32U, 0x57U); // Deterministic test-only material.
    keys.aead_key.resize(cipher.key_size);
    keys.hp_key.resize(cipher.key_size);
    keys.aead_iv.resize(12U);
    for (std::size_t index{}; index < cipher.key_size; ++index)
    {
        keys.aead_key[index] = static_cast<std::uint8_t>(index * 7U + 3U);
        keys.hp_key[index] = static_cast<std::uint8_t>(index * 11U + 5U);
    }
    for (std::size_t index{}; index < keys.aead_iv.size(); ++index)
        keys.aead_iv[index] = static_cast<std::uint8_t>(index * 13U + 1U);
    keys.tag_len = 16U;
    return keys;
}

auto make_header(std::size_t cid_size, std::uint64_t pn, bool key_phase)
    -> std::vector<std::byte>
{
    std::vector<std::byte> header(1U + cid_size + 4U);
    header[0] = key_phase ? std::byte{0x47} : std::byte{0x43};
    for (std::size_t index{}; index < cid_size; ++index)
        header[1U + index] = static_cast<std::byte>(0xa0U + index);
    for (std::size_t index{}; index < 4U; ++index)
        header[1U + cid_size + index] = static_cast<std::byte>(
            (pn >> ((3U - index) * 8U)) & 0xffU);
    return header;
}

auto joined(const std::vector<std::byte>& header,
    const std::vector<std::byte>& plaintext) -> std::vector<std::byte>
{
    std::vector<std::byte> packet(header.size() + plaintext.size());
    std::ranges::copy(header, packet.begin());
    std::ranges::copy(plaintext, packet.begin() + header.size());
    return packet;
}

auto success_case(results& result, const suite& cipher, std::size_t count,
    std::size_t cid_size, std::uint64_t pn, std::uint32_t path,
    bool key_phase, bool force_relocation) -> void
{
    ++result.cases;
    ERR_clear_error();
    auto keys = make_keys(cipher);
    const auto header = make_header(cid_size, pn, key_phase);
    std::vector<std::byte> plaintext(count);
    for (std::size_t index{}; index < plaintext.size(); ++index)
        plaintext[index] = static_cast<std::byte>((index * 29U + count) & 0xffU);
    auto packet = joined(header, plaintext);
    const auto required = packet.size() + keys.tag_len;
    if (force_relocation)
    {
        packet.shrink_to_fit();
        // A nonbinding shrink_to_fit must not silently turn a relocation test
        // into an ordinary success case on a different standard library.
        if (!result.check(packet.capacity() < required, "relocation fixture not established"))
            return;
    }
    else
        packet.reserve(required);
    const auto previous_address = reinterpret_cast<std::uintptr_t>(packet.data());
    auto separate = header;
    const auto old = q::append_sealed_payload(keys, plaintext, separate, pn, path);
    if (!result.check(old.has_value(), "separate-input baseline seal"))
        return;
    const auto sealed = q::seal_packet_payload_in_place(keys, packet, header.size(), pn, path);
    if (!result.check(sealed.has_value(), "in-place seal"))
        return;
    result.check(packet == separate, "exact ciphertext/tag/header equivalence");
    result.check(packet.size() == required, "exact output length");
    result.check(std::equal(header.begin(), header.end(), packet.begin()), "AAD header unchanged");
    const auto relocated = reinterpret_cast<std::uintptr_t>(packet.data()) != previous_address;
    result.check(relocated == force_relocation, "resize relocation/capacity condition exercised");
    const auto opened = q::open_payload(keys,
        std::span<const std::byte>{packet}.subspan(header.size()),
        std::span<const std::byte>{packet}.first(header.size()), pn, path);
    result.check(opened.has_value() && *opened == plaintext, "independent open recovers plaintext");
    const auto wrong_path = q::open_payload(keys,
        std::span<const std::byte>{packet}.subspan(header.size()),
        std::span<const std::byte>{packet}.first(header.size()), pn, path ^ 1U);
    result.check(!wrong_path.has_value(), "path nonce authentication separation");
    ERR_clear_error();
    const auto before_hp = packet;
    const auto old_hp = q::protect_header(keys, separate, header.size() - 4U, false);
    const auto new_hp = q::protect_header(keys, packet, header.size() - 4U, false);
    result.check(old_hp.has_value() && new_hp.has_value() && packet == separate,
        "exact HP output equivalence including minimal tag sample");
    const auto restored = q::unprotect_header(keys, packet, header.size() - 4U, false);
    result.check(restored.has_value() && *restored == 4U && packet == before_hp,
        "HP round trip preserves sealed bytes");
}

auto failure_cases(results& result, const suite& cipher) -> void
{
    auto keys = make_keys(cipher);
    const auto header = make_header(8U, 7U, false);
    const std::vector<std::byte> plaintext(17U, std::byte{0x72});
    const auto original = joined(header, plaintext);
    auto invalid = [&](q::quic_level_keys bad, std::size_t h, std::uint64_t pn,
                       std::error_code expected, std::string_view label)
    {
        ++result.cases;
        auto packet = original;
        const auto sealed = q::seal_packet_payload_in_place(bad, packet, h, pn, 0U);
        result.check(!sealed.has_value() && sealed.error() == expected, label);
        result.check(packet == original, "pre-resize validation retains original buffer");
    };
    auto no_secret = keys; no_secret.secret.clear();
    invalid(std::move(no_secret), header.size(), 7U,
        std::make_error_code(std::errc::operation_not_permitted), "invalid key material");
    invalid(keys, original.size() + 1U, 7U,
        std::make_error_code(std::errc::invalid_argument), "header outside packet");
    invalid(keys, header.size(), std::uint64_t{1} << 62U,
        std::make_error_code(std::errc::invalid_argument), "packet number outside QUIC range");
    auto excessive_tag = keys; excessive_tag.tag_len = std::numeric_limits<std::size_t>::max();
    invalid(std::move(excessive_tag), header.size(), 7U,
        std::make_error_code(std::errc::message_size), "output-size overflow rejected before allocation");

    // A real EVP context-initialization failure: unsupported key length and
    // an explicitly uncached key. No fake error-returning implementation.
    ++result.cases;
    auto invalid_context = keys;
    invalid_context.aead_context.reset();
    invalid_context.aead_key.pop_back();
    auto packet = original;
    auto old_context_packet = header;
    ERR_clear_error();
    const auto old_context_result = q::append_sealed_payload(
        invalid_context, plaintext, old_context_packet, 7U, 0U);
    result.check(!old_context_result.has_value() && old_context_packet == header,
        "old context-init failure reference");
    ERR_clear_error();
    const auto context_result = q::seal_packet_payload_in_place(
        invalid_context, packet, header.size(), 7U, 0U);
    result.check(!context_result.has_value(), "real EVP context-init failure");
    result.check(packet == header, "context-init failure truncates logical packet to header");
    ERR_clear_error();

    // Warm a valid 16-byte-tag context, then provide zero output tag space.
    // The actual cached EVP_AEAD_CTX_seal must reject its output capacity.
    auto warm = header;
    const auto warmed = q::append_sealed_payload(keys, plaintext, warm, 7U, 0U);
    if (!result.check(warmed.has_value() && keys.aead_context != nullptr, "warm real AEAD context"))
        return;
    ++result.cases;
    auto insufficient = keys; insufficient.tag_len = 0U;
    packet = original;
    auto old_aead_packet = header;
    ERR_clear_error();
    const auto old_aead_result = q::append_sealed_payload(
        insufficient, plaintext, old_aead_packet, 7U, 0U);
    result.check(!old_aead_result.has_value() && old_aead_packet == header,
        "old real AEAD output-too-small reference");
    ERR_clear_error();
    const auto aead_result = q::seal_packet_payload_in_place(
        insufficient, packet, header.size(), 7U, 0U);
    result.check(!aead_result.has_value() && aead_result.error().value() != 0 &&
        std::string_view{aead_result.error().category().name()} == "openssl",
        "real cached AEAD output-too-small failure");
    result.check(packet == header, "AEAD failure truncates logical packet to header");
    // The truncated capacity is not read: the helper makes no secure-erasure
    // promise for retained storage. BoringSSL's failure zeroing is its contract.
    ERR_clear_error();

    ++result.cases;
    packet = original;
    const auto new_for_hp = q::seal_packet_payload_in_place(
        keys, packet, header.size(), 7U, 0U);
    if (!result.check(new_for_hp.has_value() && packet == warm,
            "HP-failure fixture encrypted through new helper"))
        return;
    const auto new_ciphertext = packet;
    auto bad_hp = keys; bad_hp.hp_key.clear();
    const auto hp_result = q::protect_header(bad_hp, packet, header.size() - 4U, false);
    result.check(!hp_result.has_value() && hp_result.error() ==
        std::make_error_code(std::errc::invalid_argument), "real HP invalid-key failure");
    result.check(packet == warm, "HP error before masking preserves ciphertext");
    ++result.cases;
    packet = new_ciphertext;
    packet.resize(header.size() + 15U);
    const auto too_short = packet;
    const auto sample_result = q::protect_header(keys, packet, header.size() - 4U, false);
    result.check(!sample_result.has_value() && sample_result.error() ==
        std::make_error_code(std::errc::message_size), "HP sample requires 16 bytes after PN+4");
    result.check(packet == too_short, "short HP sample leaves packet unchanged");
}
auto secret_replacement_case(results& result, const suite& cipher, bool reading) -> void
{
    ++result.cases;
    auto tls = cnetmod::ssl_context::quic_client();
    if (!result.check(tls.has_value(), "replacement TLS context"))
        return;
    auto session = q::quic_tls_session::client(*tls);
    auto reference = q::quic_tls_session::client(*tls);
    if (!result.check(session.has_value() && reference.has_value(), "replacement sessions"))
        return;
    const std::array<std::uint8_t, 32> first_secret{};
    auto next_secret = first_secret;
    next_secret.fill(0x91U);
    if (!result.check(q::quic_crypto_regression_access::install(**session,
            cipher.id, first_secret, reading), "first secret installed"))
        return;
    const auto keys = reading ? (*session)->read_keys(q::encryption_level::application)
                              : (*session)->write_keys(q::encryption_level::application);
    const auto header = make_header(8U, 7U, false);
    const std::vector<std::byte> body(64U, std::byte{0x31});
    auto warm = q::seal_payload(*keys, body, header, 7U);
    auto protected_header = joined(header, std::vector<std::byte>(64U));
    const auto hp = q::protect_header(*keys, protected_header, header.size() - 4U, false);
    if (!result.check(warm.has_value() && hp.has_value() && keys->aead_context_cached,
            "old AEAD and HP caches warmed"))
        return;
    // Retain the old owner to detect a stale pointer deterministically,
    // without making the regression depend on use-after-free behavior.
    const auto old_owner = keys->aead_context;
    if (!result.check(q::quic_crypto_regression_access::install(**session,
            cipher.id, next_secret, reading), "replacement secret installed"))
        return;
    if (!result.check(!keys->aead_context && !keys->aead_context_cached,
            "replacement invalidates both owner and cached pointer"))
        return;
    if (!result.check(q::quic_crypto_regression_access::install(**reference,
            cipher.id, next_secret, reading), "reference secret installed"))
        return;
    const auto reference_keys = reading
        ? (*reference)->read_keys(q::encryption_level::application)
        : (*reference)->write_keys(q::encryption_level::application);
    auto actual = q::seal_payload(*keys, body, header, 7U);
    auto expected = q::seal_payload(*reference_keys, body, header, 7U);
    result.check(actual && expected && *actual == *expected && *actual != *warm,
        "replacement uses new AEAD material");
    auto actual_hp = joined(header, std::vector<std::byte>(64U));
    auto expected_hp = actual_hp;
    const auto actual_protected = q::protect_header(*keys, actual_hp, header.size() - 4U, false);
    const auto expected_protected = q::protect_header(*reference_keys, expected_hp, header.size() - 4U, false);
    result.check(actual_protected && expected_protected && actual_hp == expected_hp &&
            actual_hp != protected_header && keys->aead_context != old_owner,
        "replacement uses new header-protection material and owner");
}
} // namespace

extern "C" auto cnetmod_test_in_place_crypto() -> int
{
    results result;
    const std::array suites{
        suite{SSL_CIPHER_AES_128_GCM_SHA256, EVP_aead_aes_128_gcm(), 16U},
        suite{SSL_CIPHER_AES_256_GCM_SHA384, EVP_aead_aes_256_gcm(), 32U},
        suite{SSL_CIPHER_CHACHA20_POLY1305_SHA256, EVP_aead_chacha20_poly1305(), 32U}};
    const std::array<std::size_t, 9> counts{0U, 1U, 15U, 16U, 17U, 1200U, 4067U, 8192U, 65478U};
    const std::array<std::size_t, 3> cid_sizes{0U, 8U, 20U};
    const std::array<std::uint64_t, 3> packet_numbers{0U, 255U, (std::uint64_t{1} << 62U) - 1U};
    const std::array<std::uint32_t, 3> paths{0U, 1U, 0x89abcdefU};
    for (const auto& cipher : suites)
    {
        for (const auto count : counts)
            for (const auto cid_size : cid_sizes)
                for (const auto pn : packet_numbers)
                    for (const auto path : paths)
                        for (const auto key_phase : {false, true})
                            for (const auto relocation : {false, true})
                                success_case(result, cipher, count, cid_size, pn, path,
                                    key_phase, relocation);
        failure_cases(result, cipher);
        secret_replacement_case(result, cipher, true);
        secret_replacement_case(result, cipher, false);
    }
    logger::info{"in-place crypto: cases={} checks={} failures={}",
        result.cases, result.checks, result.failures};
    return result.failures == 0U ? 0 : 1;
}
