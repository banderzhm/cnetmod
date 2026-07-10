/// cnetmod benchmark - MQTT publish/subscribe throughput over local loopback TCP/TLS.
/// This is a throughput benchmark, not a weak-network or packet-loss test.

#include <cnetmod/config.hpp>

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mqtt;

#include "bench_framework.hpp"

namespace cn = cnetmod;
namespace mqtt = cnetmod::mqtt;

using cnetmod::bench::format_number;

namespace {

struct bench_case {
    const char* name;
    mqtt::qos qos_value;
    bool tls;
    std::uint16_t port_base;
};

constexpr bench_case cases[] = {
    {"MQTT QoS0",  mqtt::qos::at_most_once, false, 19680},
    {"MQTT QoS1",  mqtt::qos::at_least_once, false, 19780},
    {"MQTT QoS2",  mqtt::qos::exactly_once, false, 19880},
    {"MQTTS QoS0", mqtt::qos::at_most_once, true,  19980},
    {"MQTTS QoS1", mqtt::qos::at_least_once, true,  20080},
    {"MQTTS QoS2", mqtt::qos::exactly_once, true,  20180},
};

struct bench_state {
    std::atomic_size_t expected = 0;
    std::atomic_size_t received = 0;
    std::atomic_size_t publishers_done = 0;
    std::atomic_bool started = false;
    std::atomic_bool finished = false;
    std::chrono::steady_clock::time_point start{};
    std::chrono::steady_clock::time_point end{};
    std::string error;
    std::mutex error_mtx;
};

void set_error_once(bench_state& state, std::string error) {
    std::scoped_lock lock(state.error_mtx);
    if (state.error.empty()) {
        state.error = std::move(error);
    }
}

auto get_error(bench_state& state) -> std::string {
    std::scoped_lock lock(state.error_mtx);
    return state.error;
}

auto make_cert_path(std::string_view file) -> std::string {
    auto root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    return (root / "examples" / "test_ssl" / file).string();
}

auto env_u32(const char* name, unsigned fallback) -> unsigned {
    auto* value = std::getenv(name);
    if (value == nullptr) return fallback;
    unsigned out = 0;
    auto text = std::string_view(value);
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    if (ec != std::errc{} || ptr != text.data() + text.size() || out == 0) {
        return fallback;
    }
    return out;
}

auto default_worker_count() -> unsigned {
    auto hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    return std::max(1u, std::min(8u, hw / 2));
}

auto make_options(std::uint16_t port, std::string client_id, bool tls) -> mqtt::connect_options {
    mqtt::connect_options opts;
    opts.host = "127.0.0.1";
    opts.port = port;
    opts.client_id = std::move(client_id);
    opts.clean_session = true;
    opts.keep_alive_sec = 30;
    opts.version = mqtt::protocol_version::v3_1_1;
    opts.connect_timeout = std::chrono::seconds{5};
    opts.tls = tls;
    opts.tls_verify = false;
    return opts;
}

auto run_publisher(cn::io_context& ctx,
                   std::uint16_t port,
                   std::string client_id,
                   std::string topic,
                   mqtt::qos qos_value,
                   bool tls,
                   std::size_t messages,
                   bench_state& state) -> cn::task<void> {
    mqtt::client pub(ctx);
    auto cr = co_await pub.connect(make_options(port, std::move(client_id), tls));
    if (!cr) {
        set_error_once(state, "publisher connect failed: " + cr.error());
        state.publishers_done.fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    static constexpr std::string_view payload = "0123456789abcdef";
    for (std::size_t i = 0; i < messages; ++i) {
        auto pr = co_await pub.publish(topic, payload, qos_value, false);
        if (!pr) {
            set_error_once(state, "publish failed: " + pr.error());
            break;
        }
    }
    (void)co_await pub.disconnect();
    state.publishers_done.fetch_add(1, std::memory_order_relaxed);
}

auto run_case_task(cn::io_context& ctx,
                   cn::io_context& publisher_ctx,
                   mqtt::broker& broker,
                   bench_case cfg,
                   std::uint16_t port,
                   std::size_t messages_per_publisher,
                   std::size_t publisher_count,
                   bench_state& state,
                   std::function<void()> stop_all) -> cn::task<void> {
    const auto prefix = std::format("bench/{}/{}", port, std::chrono::steady_clock::now().time_since_epoch().count());
    state.expected.store(messages_per_publisher * publisher_count, std::memory_order_relaxed);

    mqtt::client sub(ctx);
    sub.on_message([&](const mqtt::publish_message&) {
        const auto received = state.received.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!state.finished.load(std::memory_order_relaxed) &&
            received >= state.expected.load(std::memory_order_relaxed)) {
            state.finished.store(true, std::memory_order_relaxed);
            state.end = std::chrono::steady_clock::now();
            broker.stop();
            stop_all();
        }
    });

    auto cr = co_await sub.connect(make_options(port, "bench-sub-" + std::to_string(port), cfg.tls));
    if (!cr) {
        set_error_once(state, "subscriber connect failed: " + cr.error());
        broker.stop();
        stop_all();
        co_return;
    }
    auto sr = co_await sub.subscribe(prefix + "/#", mqtt::qos::exactly_once);
    if (!sr) {
        set_error_once(state, "subscribe failed: " + sr.error());
        broker.stop();
        stop_all();
        co_return;
    }

    co_await cn::async_sleep(ctx, std::chrono::milliseconds{50});
    state.started.store(true, std::memory_order_relaxed);
    state.start = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < publisher_count; ++i) {
        cn::spawn(publisher_ctx, run_publisher(publisher_ctx,
                                               port,
                                               "bench-pub-" + std::to_string(port) + "-" + std::to_string(i),
                                               prefix + "/p/" + std::to_string(i),
                                               cfg.qos_value,
                                               cfg.tls,
                                               messages_per_publisher,
                                               state));
    }

    const auto timeout = std::chrono::seconds{30};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (state.finished.load(std::memory_order_relaxed)) co_return;
        if (state.publishers_done.load(std::memory_order_relaxed) >= publisher_count &&
            !get_error(state).empty()) break;
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{10});
    }

    if (!state.finished.load(std::memory_order_relaxed)) {
        state.end = std::chrono::steady_clock::now();
        state.finished.store(true, std::memory_order_relaxed);
        set_error_once(state, std::format("timeout: received {}/{}",
                                          state.received.load(std::memory_order_relaxed),
                                          state.expected.load(std::memory_order_relaxed)));
        broker.stop();
        stop_all();
    }
}

auto configure_broker(mqtt::broker& broker, const bench_case& cfg) -> bool {
    mqtt::broker_options opts;
    opts.host = "127.0.0.1";
    opts.delivery_channel_size = 65536;
    opts.receive_maximum = 65535;
    opts.write_batch_messages = env_u32("CNETMOD_MQTT_WRITE_BATCH_MESSAGES", 16);
    opts.write_batch_bytes = env_u32("CNETMOD_MQTT_WRITE_BATCH_BYTES", 8192);
#ifdef CNETMOD_HAS_SSL
    if (cfg.tls) {
        opts.tls_cert_file = make_cert_path("cert.pem");
        opts.tls_key_file = make_cert_path("key.pem");
    }
#else
    if (cfg.tls) {
        logger::info("  {:<10}: skipped, SSL disabled", cfg.name);
        return false;
    }
#endif
    broker.set_options(std::move(opts));
    return true;
}

auto listen_broker(mqtt::broker& broker, const bench_case& cfg) -> std::optional<std::uint16_t> {
    auto port = cfg.port_base;
    for (int attempt = 0; attempt < 100; ++attempt, ++port) {
#ifdef CNETMOD_HAS_SSL
        auto lr = cfg.tls ? broker.listen_tls("127.0.0.1", port)
                          : broker.listen("127.0.0.1", port);
#else
        auto lr = broker.listen("127.0.0.1", port);
#endif
        if (lr) {
            return port;
        }
    }
    logger::error("  {:<10}: listen failed", cfg.name);
    return std::nullopt;
}

void print_result(const bench_case& cfg,
                  std::size_t messages_per_publisher,
                  std::size_t publisher_count,
                  const char* mode,
                  const bench_state& state,
                  const mqtt::broker_metrics_snapshot& metrics) {
    const auto elapsed = std::chrono::duration<double>(state.end - state.start).count();
    const auto expected = state.expected.load(std::memory_order_relaxed);
    const auto ok = state.received.load(std::memory_order_relaxed);
    const auto failed = expected > ok ? expected - ok : 0;
    const double mps = elapsed > 0.0 ? static_cast<double>(ok) / elapsed : 0.0;

    const auto write_batch = env_u32("CNETMOD_MQTT_WRITE_BATCH_MESSAGES", 16);
    logger::info("  {:<10} {:<6} loopback:{} weaknet:no write_batch:{} pub:{:<3} {:>7} each {:>14} msg/sec  ({} ok, {} failed, {:.3f}s)",
                 cfg.name,
                 mode,
                 cfg.tls ? "tcp+tls" : "tcp",
                 write_batch,
                 publisher_count,
                 messages_per_publisher,
                 format_number(mps),
                 ok,
                 failed,
                 elapsed);
    std::string error;
    {
        auto& mutable_state = const_cast<bench_state&>(state);
        std::scoped_lock lock(mutable_state.error_mtx);
        error = mutable_state.error;
    }
    if (!error.empty()) {
        logger::warn("    {}", error);
    }
    const double avg_batch = metrics.batch_writes > 0
        ? static_cast<double>(metrics.batch_messages) /
              static_cast<double>(metrics.batch_writes)
        : 1.0;
    logger::info("    socket_writes:{} bytes:{} batch_writes:{} avg_batch:{:.2f}",
                 metrics.socket_writes,
                 metrics.socket_write_bytes,
                 metrics.batch_writes,
                 avg_batch);
    logger::info("    routed:{} delivered:{} accepted:{} active:{} write_failures:{} protocol_errors:{} offline:{}",
                 metrics.routed_messages,
                 metrics.delivered_messages,
                 metrics.accepted_connections,
                 metrics.active_connections,
                 metrics.write_failures,
                 metrics.protocol_errors,
                 metrics.offline_enqueued_messages);
}

void run_mqtt_bench_single(const bench_case& cfg,
                           std::size_t messages_per_publisher,
                           std::size_t publisher_count) {
    auto ctx = cn::make_io_context();
    mqtt::broker broker(*ctx);
    if (!configure_broker(broker, cfg)) return;
    auto port = listen_broker(broker, cfg);
    if (!port) {
        return;
    }

    bench_state state;
    cn::spawn(*ctx, broker.run());
    cn::spawn(*ctx, run_case_task(*ctx,
                                  *ctx,
                                  broker,
                                  cfg,
                                  *port,
                                  messages_per_publisher,
                                  publisher_count,
                                  state,
                                  [&] { ctx->stop(); }));

    ctx->run();
    print_result(cfg, messages_per_publisher, publisher_count, "single", state,
                 broker.metrics());
}

void run_mqtt_bench_multi(const bench_case& cfg,
                          std::size_t messages_per_publisher,
                          std::size_t publisher_count) {
    auto workers = env_u32("CNETMOD_BENCH_SERVER_WORKERS", default_worker_count());
    cn::server_context sctx(workers, workers);
    mqtt::broker broker(sctx);
    if (!configure_broker(broker, cfg)) return;
    auto port = listen_broker(broker, cfg);
    if (!port) {
        return;
    }

    auto subscriber_ctx = cn::make_io_context();
    auto publisher_ctx = cn::make_io_context();
    bench_state state;
    cn::spawn(sctx.accept_io(), broker.run());
    std::jthread server_thread([&] {
        sctx.run();
    });
    std::jthread publisher_thread([&] {
        publisher_ctx->run();
    });
    cn::spawn(*subscriber_ctx, run_case_task(*subscriber_ctx,
                                            *publisher_ctx,
                                            broker,
                                            cfg,
                                            *port,
                                            messages_per_publisher,
                                            publisher_count,
                                            state,
                                            [&] {
                                                subscriber_ctx->stop();
                                                publisher_ctx->stop();
                                                sctx.stop();
                                            }));
    subscriber_ctx->run();
    publisher_ctx->stop();
    if (publisher_thread.joinable()) publisher_thread.join();
    sctx.stop();
    if (server_thread.joinable()) server_thread.join();

    print_result(cfg, messages_per_publisher, publisher_count, "multi", state,
                 broker.metrics());
}

void run_mqtt_bench(const bench_case& cfg,
                    std::size_t messages_per_publisher,
                    std::size_t publisher_count,
                    std::string_view mode) {
    if (mode == "multi") {
        run_mqtt_bench_multi(cfg, messages_per_publisher, publisher_count);
    } else {
        run_mqtt_bench_single(cfg, messages_per_publisher, publisher_count);
    }
}

void run_matrix(std::size_t messages_per_publisher,
                std::size_t publisher_count,
                std::string_view mode) {
    for (auto cfg : cases) {
        run_mqtt_bench(cfg, messages_per_publisher, publisher_count, mode);
    }
}

void run_parser_bench(std::size_t frame_count) {
    std::string wire;
    wire.reserve(frame_count * 64);

    static constexpr std::string_view topic = "bench/parser/topic";
    static constexpr std::string_view payload = "0123456789abcdef";
    for (std::size_t i = 0; i < frame_count; ++i) {
        wire += mqtt::encode_publish(
            topic,
            payload,
            mqtt::qos::at_most_once,
            false,
            false,
            0,
            mqtt::protocol_version::v3_1_1);
    }

    mqtt::mqtt_parser parser;
    const auto start = std::chrono::steady_clock::now();
    parser.feed(wire);

    std::size_t decoded = 0;
    std::size_t payload_bytes = 0;
    std::string error;
    while (auto frame = parser.next()) {
        if (frame->type != mqtt::control_packet_type::publish) {
            error = "unexpected packet type";
            break;
        }
        auto msg = mqtt::decode_publish(
            frame->payload,
            frame->flags,
            mqtt::protocol_version::v3_1_1);
        if (!msg) {
            error = msg.error();
            break;
        }
        payload_bytes += msg->payload.size();
        ++decoded;
    }
    const auto end = std::chrono::steady_clock::now();

    const auto elapsed = std::chrono::duration<double>(end - start).count();
    const auto fps = elapsed > 0.0 ? static_cast<double>(decoded) / elapsed : 0.0;
    logger::info("  MQTT parser single-buffer weaknet:no frames:{} decoded:{} payload:{} bytes {:>14} frames/sec ({:.3f}s)",
                 frame_count,
                 decoded,
                 payload_bytes,
                 format_number(fps),
                 elapsed);
    if (parser.pending() != 0) {
        logger::warn("    pending bytes after parser drain: {}", parser.pending());
    }
    if (!error.empty()) {
        logger::warn("    parser error: {}", error);
    }
}

void run_selected(std::size_t messages_per_publisher,
                  std::size_t publisher_count,
                  std::string_view selector,
                  std::string_view mode) {
    if (selector == "parser") {
        run_parser_bench(messages_per_publisher);
        return;
    }
    if (selector == "clientburst") {
        for (auto cfg : cases) {
            if (!cfg.tls && cfg.qos_value == mqtt::qos::at_most_once) {
                logger::info("  clientburst: real TCP broker/client QoS0 burst consumer");
                run_mqtt_bench(cfg, messages_per_publisher, publisher_count, mode);
                return;
            }
        }
    }
    if (selector == "all") {
        run_matrix(messages_per_publisher, publisher_count, mode);
        return;
    }
    for (auto cfg : cases) {
        if ((selector == "qos0" && !cfg.tls && cfg.qos_value == mqtt::qos::at_most_once) ||
            (selector == "qos1" && !cfg.tls && cfg.qos_value == mqtt::qos::at_least_once) ||
            (selector == "qos2" && !cfg.tls && cfg.qos_value == mqtt::qos::exactly_once) ||
            (selector == "tls0" && cfg.tls && cfg.qos_value == mqtt::qos::at_most_once) ||
            (selector == "tls1" && cfg.tls && cfg.qos_value == mqtt::qos::at_least_once) ||
            (selector == "tls2" && cfg.tls && cfg.qos_value == mqtt::qos::exactly_once)) {
            run_mqtt_bench(cfg, messages_per_publisher, publisher_count, mode);
            return;
        }
    }
    logger::error("unknown selector '{}', expected all/qos0/qos1/qos2/tls0/tls1/tls2/parser/clientburst", selector);
}

} // namespace

int main(int argc, char** argv) {
    cn::net_init net;
    logger::set_level(logger::level::info);

    if (argc == 1) {
        logger::info("=== MQTT benchmark (Release, local loopback real TCP/TLS pub/sub, weaknet:no) ===");
        run_matrix(20'000, 1, "single");
        run_matrix(10'000, 8, "multi");
        logger::shutdown();
        return 0;
    }

    if (argc != 3 && argc != 4 && argc != 5) {
        std::println("usage: bench_mqtt [messages_per_publisher publisher_count [all|qos0|qos1|qos2|tls0|tls1|tls2|parser|clientburst] [single|multi]]");
        logger::shutdown();
        return 2;
    }

    std::size_t messages = 0;
    std::size_t publishers = 0;
    auto parse_size = [](const char* s, std::size_t& out) {
        auto text = std::string_view{s};
        auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
        return ec == std::errc{} && ptr == text.data() + text.size() && out > 0;
    };
    if (!parse_size(argv[1], messages) || !parse_size(argv[2], publishers)) {
        std::println("usage: bench_mqtt [messages_per_publisher publisher_count [all|qos0|qos1|qos2|tls0|tls1|tls2|parser|clientburst] [single|multi]]");
        logger::shutdown();
        return 2;
    }

    logger::info("=== MQTT benchmark (Release, local loopback real TCP/TLS pub/sub, weaknet:no) ===");
    auto selector = argc >= 4 ? std::string_view{argv[3]} : std::string_view{"all"};
    auto mode = argc >= 5 ? std::string_view{argv[4]} : std::string_view{"single"};
    if (mode != "single" && mode != "multi") {
        std::println("usage: bench_mqtt [messages_per_publisher publisher_count [all|qos0|qos1|qos2|tls0|tls1|tls2|parser|clientburst] [single|multi]]");
        logger::shutdown();
        return 2;
    }
    run_selected(messages, publishers, selector, mode);
    logger::shutdown();
    return 0;
}
