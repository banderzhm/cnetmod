module cnetmod.protocol.redis;

#ifdef CNETMOD_HAS_SSL
import std;
import :client;
import cnetmod.core.ssl;

namespace cnetmod::redis {

auto client::configure_tls(const connect_options& options, bool require_default_trust)
    -> std::expected<void, tls_configuration_failure>
{
    auto context = ssl_context::client();
    if (!context)
        return std::unexpected(tls_configuration_failure{context.error(), "ssl context: "});
    ssl_ctx_ = std::make_unique<ssl_context>(std::move(*context));
    ssl_ctx_->set_verify_peer(options.tls_verify);
    if (!options.tls_ca_file.empty())
    {
        auto loaded = ssl_ctx_->load_ca_file(options.tls_ca_file);
        if (!loaded)
            return std::unexpected(tls_configuration_failure{loaded.error(), "ssl ca: "});
    }
    else if (options.tls_verify)
    {
        auto loaded = ssl_ctx_->set_default_ca();
        if (!loaded && require_default_trust)
            return std::unexpected(tls_configuration_failure{loaded.error(), "ssl ca: "});
    }
    if (!options.tls_cert_file.empty())
    {
        auto loaded = ssl_ctx_->load_cert_file(options.tls_cert_file);
        if (!loaded)
            return std::unexpected(tls_configuration_failure{loaded.error(), "ssl cert: "});
    }
    if (!options.tls_key_file.empty())
    {
        auto loaded = ssl_ctx_->load_key_file(options.tls_key_file);
        if (!loaded)
            return std::unexpected(tls_configuration_failure{loaded.error(), "ssl key: "});
    }
    ssl_ = std::make_unique<ssl_stream>(*ssl_ctx_, ctx_, sock_);
    ssl_->set_connect_state();
    ssl_->set_hostname(options.tls_sni.empty() ? options.host : options.tls_sni);
    return {};
}

} // namespace cnetmod::redis
#endif
