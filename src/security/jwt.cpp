/**
 * @file jwt.cpp
 * @brief JWT sign/verify implementation — jwt-cpp backend, offloaded via blocking_invoke
 */
module;

// Select jwt-cpp's nlohmann::json traits before including its main header.
// Without this, jwt-cpp defaults to picojson even when nlohmann/json is the
// project's configured JSON provider.
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

module cnetmod.security.jwt;

import std;
import cnetmod.coro.bridge;

namespace cnetmod::security {
namespace {

    /// Known standard claim names — everything else is treated as custom
    constexpr std::string_view standard_claims[] = {
        "iss", "sub", "aud", "exp", "nbf", "iat", "jti", "scope"};

    auto is_standard_claim(std::string_view name) -> bool
    {
        for (auto sc : standard_claims)
            if (sc == name)
                return true;
        return false;
    }

    /// Synchronouse sign — runs on thread pool thread
    auto sign_jwt_sync(const jwt_sign_options& opts, std::string secret)
        -> std::expected<std::string, std::string>
    {
        try
        {
            auto now = std::chrono::system_clock::now();
            auto builder = jwt::create()
                               .set_type("JWT")
                               .set_issuer(opts.issuer)
                               .set_subject(opts.subject)
                               .set_issued_at(now)
                               .set_expires_at(now + opts.lifetime);

            // Encode scopes as space-separated string (OAuth2 convention)
            if (!opts.scopes.empty())
            {
                std::string scope_str;
                for (std::size_t i = 0; i < opts.scopes.size(); ++i)
                {
                    if (i > 0)
                        scope_str += ' ';
                    scope_str += opts.scopes[i];
                }
                builder.set_payload_claim("scope",
                    nlohmann::json(scope_str));
            }

            // Inject custom claims
            for (auto& [key, value] : opts.custom_claims)
                builder.set_payload_claim(key, nlohmann::json(value));

            std::string token;
            switch (opts.algorithm)
            {
            case jwt_algorithm::hs256:
                token = builder.sign(jwt::algorithm::hs256{secret});
                break;
            }
            return token;
        }
        catch (const std::exception& e)
        {
            return std::unexpected(std::string("jwt sign failed: ") + e.what());
        }
    }

    /// Synchronouse verify — runs on thread pool thread
    auto verify_jwt_sync(std::string token, std::string secret)
        -> std::expected<jwt_claims, std::string>
    {
        try
        {
            auto decoded = jwt::decode(token);

            auto verifier = jwt::verify()
                                .allow_algorithm(jwt::algorithm::hs256{secret});
            verifier.verify(decoded);

            jwt_claims claims{};

            if (decoded.has_issuer())
                claims.issuer = decoded.get_issuer();
            if (decoded.has_subject())
                claims.subject = decoded.get_subject();
            if (decoded.has_issued_at())
                claims.issued_at = decoded.get_issued_at();
            if (decoded.has_expires_at())
                claims.expires_at = decoded.get_expires_at();

            // Parse scope claim (space-separated string)
            if (decoded.has_payload_claim("scope"))
            {
                std::string scope_str = decoded.get_payload_claim("scope").as_string();
                std::string_view sv(scope_str);
                while (!sv.empty())
                {
                    auto pos = sv.find(' ');
                    if (pos == std::string_view::npos)
                    {
                        claims.scopes.emplace_back(sv);
                        break;
                    }
                    if (pos > 0)
                        claims.scopes.emplace_back(sv.substr(0, pos));
                    sv.remove_prefix(pos + 1);
                }
            }

            // Collect custom (non-standard) claims
            auto payload = decoded.get_payload_json();
            for (auto& [key, val] : payload)
            {
                if (!is_standard_claim(key))
                {
                    try
                    {
                        claims.custom[key] = val.get<std::string>();
                    }
                    catch (...)
                    {
                        // Skip non-string claims silently
                    }
                }
            }

            return claims;
        }
        catch (const std::exception& e)
        {
            return std::unexpected(std::string("jwt verify failed: ") + e.what());
        }
    }

} // namespace

auto sign_jwt(thread_pool& pool, io_context& io,
              const jwt_sign_options& opts, std::string_view secret)
    -> task<std::expected<std::string, std::string>>
{
    // Copy string_view data into owned strings before offloading
    std::string secret_owned(secret);
    co_return co_await blocking_invoke(pool, io,
        [opts, secret_owned = std::move(secret_owned)] {
            return sign_jwt_sync(opts, secret_owned);
        });
}

auto verify_jwt(thread_pool& pool, io_context& io,
                std::string_view token, std::string_view secret)
    -> task<std::expected<jwt_claims, std::string>>
{
    // Copy string_view data into owned strings before offloading
    std::string token_owned(token);
    std::string secret_owned(secret);
    co_return co_await blocking_invoke(pool, io,
        [token_owned = std::move(token_owned),
         secret_owned = std::move(secret_owned)] {
            return verify_jwt_sync(token_owned, secret_owned);
        });
}

} // namespace cnetmod::security
