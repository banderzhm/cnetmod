#include <cnetmod/config.hpp>
#include <cstdio>

import std;
import cnetmod.core.net_init;
import cnetmod.core.log;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
import cnetmod.protocol.mqtt;

namespace cn = cnetmod;
namespace mqtt = cnetmod::mqtt;

namespace {

struct received_message {
    std::string topic;
    std::string payload;
    mqtt::qos qos_value{};
    bool retain = false;
};

auto parse_port(std::string_view text) -> std::optional<std::uint16_t> {
    unsigned value = 0;
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || value > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

auto make_options(std::string host, std::uint16_t port, std::string client_id,
                  bool clean_session = true) -> mqtt::connect_options {
    mqtt::connect_options opts;
    opts.host = std::move(host);
    opts.port = port;
    opts.client_id = std::move(client_id);
    opts.clean_session = clean_session;
    opts.keep_alive_sec = 10;
    opts.version = mqtt::protocol_version::v3_1_1;
    opts.connect_timeout = std::chrono::seconds{5};
    return opts;
}

auto fail(std::string& error, std::string message) -> bool {
    if (error.empty()) {
        error = std::move(message);
    }
    return false;
}

auto wait_for(cn::io_context& ctx, std::chrono::milliseconds timeout,
              std::function<bool()> predicate) -> cn::task<bool> {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            co_return true;
        }
        co_await cn::async_sleep(ctx, std::chrono::milliseconds{10});
    }
    co_return predicate();
}

auto connect_client(mqtt::client& client, mqtt::connect_options opts,
                    std::string& error) -> cn::task<bool> {
    auto r = co_await client.connect(std::move(opts));
    if (!r) {
        co_return fail(error, "connect failed: " + r.error());
    }
    co_return true;
}

auto subscribe_client(mqtt::client& client, std::string topic, mqtt::qos max_qos,
                      std::string& error) -> cn::task<bool> {
    auto r = co_await client.subscribe(std::move(topic), max_qos);
    if (!r) {
        co_return fail(error, "subscribe failed: " + r.error());
    }
    co_return true;
}

auto publish_client(mqtt::client& client, std::string_view topic, std::string_view payload,
                    mqtt::qos qos_value, bool retain, std::string& error) -> cn::task<bool> {
    auto r = co_await client.publish(topic, payload, qos_value, retain);
    if (!r) {
        co_return fail(error, "publish failed: " + r.error());
    }
    co_return true;
}

auto find_payload(const std::vector<received_message>& messages, std::string_view payload)
    -> std::optional<received_message> {
    auto it = std::ranges::find_if(messages, [&](const received_message& msg) {
        return msg.payload == payload;
    });
    if (it == messages.end()) {
        return std::nullopt;
    }
    return *it;
}

auto test_cnetmod_client_against_broker(cn::io_context& ctx, std::string host,
                                        std::uint16_t port, std::string prefix,
                                        std::string& error) -> cn::task<bool> {
    std::vector<received_message> qos_messages;
    mqtt::client sub(ctx);
    sub.on_message([&](const mqtt::publish_message& msg) {
        qos_messages.push_back(received_message{
            .topic = msg.topic,
            .payload = msg.payload,
            .qos_value = msg.qos_value,
            .retain = msg.retain,
        });
    });

    if (!co_await connect_client(sub, make_options(host, port, prefix + "-sub"), error)) {
        co_return false;
    }
    if (!co_await subscribe_client(sub, prefix + "/qos/#", mqtt::qos::exactly_once, error)) {
        co_return false;
    }

    mqtt::client pub(ctx);
    if (!co_await connect_client(pub, make_options(host, port, prefix + "-pub"), error)) {
        co_return false;
    }

    for (int i = 0; i <= 2; ++i) {
        const auto q = static_cast<mqtt::qos>(i);
        const auto payload = "qos-" + std::to_string(i);
        if (!co_await publish_client(pub, prefix + "/qos/" + std::to_string(i),
                                     payload, q, false, error)) {
            co_return false;
        }
        auto ok = co_await wait_for(ctx, std::chrono::seconds{4}, [&] {
            return find_payload(qos_messages, payload).has_value();
        });
        if (!ok) {
            co_return fail(error, "timeout waiting for " + payload);
        }
    }
    std::println("ok cnetmod client qos0/qos1/qos2");

    std::vector<received_message> wildcard_messages;
    mqtt::client wildcard_sub(ctx);
    wildcard_sub.on_message([&](const mqtt::publish_message& msg) {
        wildcard_messages.push_back(received_message{
            .topic = msg.topic,
            .payload = msg.payload,
            .qos_value = msg.qos_value,
            .retain = msg.retain,
        });
    });
    if (!co_await connect_client(wildcard_sub, make_options(host, port, prefix + "-wild-sub"), error)) {
        co_return false;
    }
    if (!co_await subscribe_client(wildcard_sub, prefix + "/wild/+/sensor/#",
                                   mqtt::qos::at_least_once, error)) {
        co_return false;
    }
    if (!co_await publish_client(pub, prefix + "/wild/a/sensor/temp", "wild-match",
                                 mqtt::qos::at_least_once, false, error)) {
        co_return false;
    }
    if (!co_await publish_client(pub, prefix + "/wild/b/other/temp", "wild-miss",
                                 mqtt::qos::at_least_once, false, error)) {
        co_return false;
    }
    auto wildcard_ok = co_await wait_for(ctx, std::chrono::seconds{4}, [&] {
        return find_payload(wildcard_messages, "wild-match").has_value();
    });
    if (!wildcard_ok) {
        co_return fail(error, "timeout waiting for wildcard match");
    }
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{200});
    if (find_payload(wildcard_messages, "wild-miss")) {
        co_return fail(error, "wildcard subscription received non-matching publish");
    }
    std::println("ok cnetmod client wildcard");

    const auto retained_topic = prefix + "/retain/item";
    if (!co_await publish_client(pub, retained_topic, "retained-value",
                                 mqtt::qos::at_least_once, true, error)) {
        co_return false;
    }

    std::vector<received_message> retained_messages;
    mqtt::client retained_sub(ctx);
    retained_sub.on_message([&](const mqtt::publish_message& msg) {
        retained_messages.push_back(received_message{
            .topic = msg.topic,
            .payload = msg.payload,
            .qos_value = msg.qos_value,
            .retain = msg.retain,
        });
    });
    if (!co_await connect_client(retained_sub, make_options(host, port, prefix + "-retained-sub"), error)) {
        co_return false;
    }
    if (!co_await subscribe_client(retained_sub, retained_topic, mqtt::qos::at_least_once, error)) {
        co_return false;
    }
    auto retained_ok = co_await wait_for(ctx, std::chrono::seconds{4}, [&] {
        return find_payload(retained_messages, "retained-value").has_value();
    });
    if (!retained_ok) {
        co_return fail(error, "timeout waiting for retained message");
    }
    auto retained = *find_payload(retained_messages, "retained-value");
    if (!retained.retain) {
        co_return fail(error, "retained delivery did not set retain flag");
    }
    if (!co_await publish_client(pub, retained_topic, "", mqtt::qos::at_least_once, true, error)) {
        co_return false;
    }
    std::println("ok cnetmod client retained");

    const auto unsub_topic = prefix + "/unsub/item";
    if (!co_await subscribe_client(sub, unsub_topic, mqtt::qos::at_least_once, error)) {
        co_return false;
    }
    auto unsub_r = co_await sub.unsubscribe({unsub_topic});
    if (!unsub_r) {
        co_return fail(error, "unsubscribe failed: " + unsub_r.error());
    }
    if (!co_await publish_client(pub, unsub_topic, "after-unsub",
                                 mqtt::qos::at_least_once, false, error)) {
        co_return false;
    }
    co_await cn::async_sleep(ctx, std::chrono::milliseconds{250});
    if (find_payload(qos_messages, "after-unsub")) {
        co_return fail(error, "message arrived after unsubscribe");
    }
    std::println("ok cnetmod client unsubscribe");

    const auto will_topic = prefix + "/will/client";
    std::vector<received_message> will_messages;
    mqtt::client will_sub(ctx);
    will_sub.on_message([&](const mqtt::publish_message& msg) {
        will_messages.push_back(received_message{
            .topic = msg.topic,
            .payload = msg.payload,
            .qos_value = msg.qos_value,
            .retain = msg.retain,
        });
    });
    if (!co_await connect_client(will_sub, make_options(host, port, prefix + "-will-sub"), error)) {
        co_return false;
    }
    if (!co_await subscribe_client(will_sub, will_topic, mqtt::qos::at_least_once, error)) {
        co_return false;
    }
    mqtt::client will_client(ctx);
    auto will_opts = make_options(host, port, prefix + "-will-client");
    will_opts.will_msg = mqtt::will{
        .topic = will_topic,
        .message = "will-fired",
        .qos_value = mqtt::qos::at_least_once,
    };
    if (!co_await connect_client(will_client, std::move(will_opts), error)) {
        co_return false;
    }
    will_client.close();
    auto will_ok = co_await wait_for(ctx, std::chrono::seconds{4}, [&] {
        return find_payload(will_messages, "will-fired").has_value();
    });
    if (!will_ok) {
        co_return fail(error, "timeout waiting for will message");
    }
    std::println("ok cnetmod client will");

    (void)co_await will_sub.disconnect();
    (void)co_await retained_sub.disconnect();
    (void)co_await wildcard_sub.disconnect();
    (void)co_await sub.disconnect();
    (void)co_await pub.disconnect();
    co_return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::println(stderr, "usage: mqtt_interop_client <host> <port> <topic-prefix>");
        return 2;
    }

    auto port = parse_port(argv[2]);
    if (!port || *port == 0) {
        std::println(stderr, "invalid port: {}", argv[2]);
        return 2;
    }

    cn::net_init net;
    auto ctx = cn::make_io_context();
    std::string error;
    int exit_code = 1;

    cn::spawn(*ctx, [&]() -> cn::task<void> {
        auto ok = co_await test_cnetmod_client_against_broker(
            *ctx, argv[1], *port, argv[3], error);
        exit_code = ok ? 0 : 1;
        ctx->stop();
    }());

    cn::spawn(*ctx, [&]() -> cn::task<void> {
        co_await cn::async_sleep(*ctx, std::chrono::seconds{20});
        if (exit_code != 0 && error.empty()) {
            error = "global timeout";
        }
        ctx->stop();
    }());

    ctx->run();
    if (exit_code != 0) {
        std::println(stderr, "FAIL {}", error.empty() ? "unknown error" : error);
    } else {
        std::println("PASS cnetmod mqtt client interop");
    }
    logger::shutdown();
    return exit_code;
}
