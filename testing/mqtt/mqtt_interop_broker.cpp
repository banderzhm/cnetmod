#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.coro.spawn;
import cnetmod.io.io_context;
import cnetmod.protocol.mqtt;

namespace cn = cnetmod;
namespace mqtt = cnetmod::mqtt;

auto parse_port(std::string_view text) -> std::optional<std::uint16_t> {
    unsigned value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || value > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 5) {
        std::println(stderr, "usage: mqtt_interop_broker <port> [--tls <cert.pem> <key.pem>]");
        return 2;
    }

    auto port = parse_port(argv[1]);
    if (!port || *port == 0) {
        std::println(stderr, "invalid port: {}", argv[1]);
        return 2;
    }

    cn::net_init net;
    auto ctx = cn::make_io_context();

    mqtt::broker broker(*ctx);
    mqtt::broker_options opts;
    opts.host = "127.0.0.1";
    opts.port = *port;
    opts.default_session_expiry = 3600;
    opts.delivery_channel_size = 4096;
    opts.topic_alias_maximum = 16;
    const bool use_tls = argc == 5;
    if (use_tls) {
        if (std::string_view{argv[2]} != "--tls") {
            std::println(stderr, "usage: mqtt_interop_broker <port> [--tls <cert.pem> <key.pem>]");
            return 2;
        }
        opts.tls_port = *port;
        opts.tls_cert_file = argv[3];
        opts.tls_key_file = argv[4];
    }
    broker.set_options(std::move(opts));

#ifdef CNETMOD_HAS_SSL
    auto lr = use_tls ? broker.listen_tls() : broker.listen();
#else
    if (use_tls) {
        std::println(stderr, "TLS requested but SSL support is disabled");
        return 77;
    }
    auto lr = broker.listen();
#endif
    if (!lr) {
        std::println(stderr, "listen failed: {}", lr.error().message());
        return 1;
    }

    std::println("READY {}", *port);
    std::fflush(stdout);

    cn::spawn(*ctx, broker.run());
    ctx->run();
    logger::shutdown();
    return 0;
}
